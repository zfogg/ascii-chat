#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/ui/sync.h>
#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/ui/notice.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/ui/too_small.h>
#include <ascii-chat/ui/fps_counter.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/shutdown.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/cond.h>
#include <stdatomic.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/options/options.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/debug/named.h>

typedef struct {
  _Atomic unsigned references;
  int fd;
  _Alignas(max_align_t) char data[];
} screen_snapshot_t;

typedef struct screen {
  struct screen *next;
  screen_snapshot_t *owned;
  void *snapshot;
  ui_render_fn render;
  terminal_size_t minimum;
  int fd;
  bool dirty;
} screen_t;

static screen_t g_screens[UI_SCREEN_COUNT];
static lifecycle_t g_lifecycle = LIFECYCLE_INIT;
static mutex_t g_mutex;
static cond_t g_render_done;
static int g_rendering_screen = -1;
static uint64_t g_render_started;
static uint64_t g_render_completed;
static asciichat_thread_t g_thread;
static atomic_t g_presentation_stop_requested = {0};
static _Thread_local bool g_owner;
static _Thread_local int g_render_fd = -1;
static _Thread_local screen_snapshot_t *g_render_snapshot;
static _Thread_local terminal_size_t g_render_size;
static terminal_size_t g_size = {.cols = 80, .rows = 24};
static int g_active = -1;
static _Atomic int g_published_screen = -1;
static atomic_t g_volume_until = {0};
static _Thread_local bool g_render_volume;

void ui_controller_show_volume(void) {
  if (!GET_OPTION(snapshot_mode))
    atomic_store_u64(&g_volume_until, time_get_ns() + 1500 * NS_PER_MS_INT);
}

static void append_volume(frame_buffer_t *buffer, terminal_size_t size) {
  if (size.cols < 8 || size.rows < 6)
    return;
  double volume = GET_OPTION(speakers_volume);
  if (volume < 0.0)
    volume = 0.0;
  if (volume > 1.0)
    volume = 1.0;
  int height = size.rows - 4;
  if (height > 10)
    height = 10;
  int filled = (int)(volume * height + 0.5);
  int top = (size.rows - height - 1) / 2 + 1;
  int col = size.cols - 5;
  bool color = terminal_get_effective_color_mode() != TERM_COLOR_NONE && GET_OPTION(color) != COLOR_SETTING_FALSE;
  bool utf8 = terminal_supports_utf8();
  frame_buffer_append(buffer, "\0337\033[0m", 6);
  for (int row = 0; row < height; ++row) {
    int level = height - row;
    bool lit = level <= filled;
    int shade = level * 3 <= height ? 31 : (level * 3 <= height * 2 ? 33 : 32);
    frame_buffer_printf(buffer, "\033[%d;%dH", top + row, col);
    if (color)
      frame_buffer_printf(buffer, "\033[%dm", lit ? shade : 90);
    const char *cells = utf8 ? (lit ? "██" : "░░") : (lit ? "##" : "..");
    frame_buffer_printf(buffer, " %s \033[0m", cells);
  }
  frame_buffer_printf(buffer, "\033[%d;%dH", top + height, col);
  if (volume == 0.0)
    frame_buffer_append(buffer, "MUTE", 4);
  else
    frame_buffer_printf(buffer, "%3d%%", (int)(volume * 100.0 + 0.5));
  frame_buffer_append(buffer, "\033[0m\0338", 6);
}

int ui_controller_current_screen(void) {
  return atomic_load(&g_published_screen);
}

static void publish_screen_locked(void) {
  int active = -1;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    if (g_screens[i].render)
      active = i;
  atomic_store(&g_published_screen, active);
}
static bool g_small;
static bool g_redraw;
static bool g_finished;
static atomic_t g_blocked = {0};
static atomic_t g_live = {0};
static terminal_size_t g_last_minimum;
static _Thread_local fps_counter_t *g_fps;
static _Thread_local bool g_stats_media, g_stats_failed;
static _Thread_local uint64_t g_stats_write_ns, g_stats_writes;
static void stats_media_begin(bool media) {
  g_stats_media = media;
  g_stats_failed = false;
  g_stats_write_ns = g_stats_writes = 0;
}
static void stats_media_end(void) {
  if (g_stats_media && g_stats_writes) {
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_TERMINAL_WRITE, g_stats_write_ns);
    stats_counter_add(stats_runtime_scope(),
                      g_stats_failed ? STATS_COUNTER_FRAMES_DROPPED : STATS_COUNTER_FRAMES_PRESENTED, 1);
  }
  g_stats_media = false;
}

asciichat_error_t ui_controller_write(int fd, const char *data, size_t len) {
  if (!data || !len)
    return ASCIICHAT_OK;
  // Logging may call this while holding subsystem locks; never wait for a
  // renderer callback here. Live screens already capture logs in their model.
  if (!g_owner && atomic_load_bool(&g_live) && platform_isatty(fd))
    return ASCIICHAT_OK;
  if (g_owner && g_render_fd >= 0)
    fd = g_render_fd;
  fps_counter_write_begin(g_fps);
  uint64_t stats_start = g_stats_media ? time_get_ns() : 0;
  bool complete = platform_write_all(fd, data, len) == len;
  if (g_stats_media) {
    g_stats_write_ns += time_get_ns() - stats_start;
    g_stats_writes++;
    g_stats_failed |= !complete;
  }
  fps_counter_write_end(g_fps, complete);
  return complete ? ASCIICHAT_OK : ERROR_FILE_OPERATION;
}

static void release_snapshot(screen_snapshot_t *snapshot) {
  if (snapshot && atomic_fetch_sub(&snapshot->references, 1) == 1) {
    platform_close(snapshot->fd);
    SAFE_FREE(snapshot);
  }
}

static void release_screen(screen_t *screen, bool flush_notices) {
  screen_t *current = screen;
  while (current) {
    screen_t *next = current->next;
    if (current->render) {
      if (flush_notices)
        ui_notice_flush_snapshot(current->fd, current->snapshot);
    }
    release_snapshot(current->owned);
    if (current != screen)
      SAFE_FREE(current);
    current = next;
  }
  memset(screen, 0, sizeof(*screen));
}

void ui_controller_finish(int fd, const char *data, size_t len) {
  screen_t retired[UI_SCREEN_COUNT] = {0};
  bool locked = lifecycle_is_initialized(&g_lifecycle);
  if (locked) {
    mutex_lock(&g_mutex);
    g_finished = true;
    memcpy(retired, g_screens, sizeof(retired));
    memset(g_screens, 0, sizeof(g_screens));
    atomic_store(&g_published_screen, -1);
    uint64_t pending = g_render_started;
    while (!g_render_snapshot && g_render_completed < pending)
      cond_wait(&g_render_done, &g_mutex);
    mutex_unlock(&g_mutex);
  }
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    release_screen(&retired[i], i == UI_SCREEN_NOTICE);
  platform_write_all(fd, data, len);
  atomic_store_bool(&g_live, false);
}

bool ui_controller_is_blocked(void) {
  return atomic_load_bool(&g_blocked);
}

bool ui_controller_is_presenting(void) {
  return atomic_load_bool(&g_live);
}

bool ui_controller_is_owner(void) {
  return g_owner;
}

terminal_size_t ui_controller_size(void) {
  if (g_owner)
    return g_render_size;
  terminal_size_t size = {.cols = 80, .rows = 24};
  if (lifecycle_is_initialized(&g_lifecycle)) {
    mutex_lock(&g_mutex);
    size = g_size;
    mutex_unlock(&g_mutex);
  } else {
    terminal_size_t detected = {0};
    if (terminal_get_size(&detected) == ASCIICHAT_OK && detected.cols > 0 && detected.rows > 0)
      size = detected;
  }
  return size;
}

static void *presentation_main(void *unused) {
  (void)unused;
  g_owner = true;
  uint64_t progress_render_ns = 0;
  g_fps = fps_counter_create();
  bool was_sync = false;
  uint64_t last_volume_until = 0;
  bool volume_was_visible = false;
  while (!atomic_load_bool(&g_presentation_stop_requested)) {
    // This path must remain independent of producers, including one stuck
    // holding the controller mutex. It reads only the collector's mailbox.
    if (ui_sync_is_visible() && ui_controller_current_screen() >= 0 &&
        ui_controller_current_screen() <= UI_SCREEN_SYNC && !shutdown_is_requested()) {
      int fd = ui_sync_output_fd();
      terminal_size_t size = {0};
      g_render_fd = fd;
      if (terminal_get_size_fd(fd, &size) == ASCIICHAT_OK && size.cols > 0 && size.rows > 0) {
        g_render_size = size;
        atomic_store_bool(&g_blocked, false);
        ui_sync_render(size);
      }
      g_render_fd = -1;
      was_sync = true;
      platform_sleep_ns(50 * NS_PER_MS_INT);
      continue;
    }
    if (was_sync) {
      g_active = -1;
      was_sync = false;
    }
    mutex_lock(&g_mutex);
    int active = -1;
    for (int i = 0; i < UI_SCREEN_COUNT; ++i)
      if (g_screens[i].render)
        active = i;
    bool changed = active != g_active;
    g_active = active;
    bool redraw = g_redraw;
    g_redraw = false;
    terminal_size_t previous_size = g_size;
    screen_t frame = {0};
    if (active >= 0 && !g_finished && (!shutdown_is_requested() || active == UI_SCREEN_RENDER_PROGRESS)) {
      frame = g_screens[active];
      atomic_fetch_add(&frame.owned->references, 1);
      g_screens[active].dirty = false;
      g_rendering_screen = active;
      ++g_render_started;
    }
    mutex_unlock(&g_mutex);
    if (active < 0)
      atomic_store_bool(&g_blocked, false);
    if (frame.render) {
      g_render_snapshot = frame.owned;
      screen_t *screen = &frame;
      terminal_size_t size = {0};
      // Measure the physical output device, never --width/--height or environment overrides.
      if (!platform_isatty(screen->fd) || terminal_get_size_fd(screen->fd, &size) != ASCIICHAT_OK || size.cols <= 0 ||
          size.rows <= 0) {
        // A minimized or detached terminal has no usable drawing area.
        atomic_store_bool(&g_blocked, true);
        g_small = true;
        goto frame_complete;
      }
      bool resized = size.cols != previous_size.cols || size.rows != previous_size.rows;
      mutex_lock(&g_mutex);
      g_size = size;
      mutex_unlock(&g_mutex);
      g_render_size = size;
      if (active == UI_SCREEN_PROMPT)
        screen->minimum.rows = ui_prompt_required_rows(screen->snapshot, size.cols);
      bool small = ui_too_small(size, screen->minimum);
      atomic_store_bool(&g_blocked, small);
      bool requirement_changed =
          screen->minimum.cols != g_last_minimum.cols || screen->minimum.rows != g_last_minimum.rows;
      g_last_minimum = screen->minimum;
      bool transition = changed || resized || requirement_changed || small != g_small || redraw;
      uint64_t volume_until = atomic_load_u64(&g_volume_until);
      bool volume_visible = active == UI_SCREEN_MEDIA && !GET_OPTION(snapshot_mode) && !GET_OPTION(strip_ansi) &&
                            time_get_ns() < volume_until;
      // Repaint the retained clean frame on both edges, even when playback is paused.
      // Clearing also restores overlay cells outside explicitly sized media frames.
      bool volume_changed =
          volume_visible != volume_was_visible || (volume_visible && volume_until != last_volume_until);
      transition |= volume_was_visible && !volume_visible;
      volume_was_visible = volume_visible;
      last_volume_until = volume_until;
      bool show_fps = active == UI_SCREEN_HELP || (active == UI_SCREEN_MEDIA && GET_OPTION(fps_counter));
      transition |= fps_counter_set_visible(g_fps, show_fps);
      if (changed || small || g_small)
        fps_counter_reset(g_fps);
      g_render_fd = screen->fd;
      frame_buffer_set_screen_output_fd(screen->fd);
      if (small) {
        if (transition || !g_small) {
          frame_buffer_t *buffer = frame_buffer_create(3, 96);
          if (buffer) {
            ui_too_small_render(buffer, size, screen->minimum);
            ui_controller_write(screen->fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
            frame_buffer_destroy(buffer);
          }
        }
      } else {
        if (transition) {
          const char reset[] = "\033[0m\033[2J\033[H";
          ui_controller_write(screen->fd, reset, sizeof(reset) - 1);
        }
        if ((resized || changed) && active == UI_SCREEN_MEDIA) {
          if (GET_OPTION(auto_width))
            options_set_int("width", size.cols);
          if (GET_OPTION(auto_height))
            options_set_int("height", size.rows);
        }
        uint64_t now = time_get_ns();
        bool animate = active != UI_SCREEN_MEDIA && active != UI_SCREEN_STATS;
        if (active == UI_SCREEN_RENDER_PROGRESS)
          animate = now - progress_render_ns >= 125 * NS_PER_MS_INT;
        bool rendered = screen->dirty || transition || animate || volume_changed;
        if (rendered) {
          fps_counter_frame_begin(g_fps, active == UI_SCREEN_MEDIA || active == UI_SCREEN_HELP);
          stats_media_begin(active == UI_SCREEN_MEDIA);
          g_render_volume = volume_visible;
          screen->render(size, screen->snapshot);
          g_render_volume = false;
          stats_media_end();
          if (active == UI_SCREEN_RENDER_PROGRESS)
            progress_render_ns = now;
          fps_counter_frame_end(g_fps, time_get_ns());
        }
        fps_counter_render(g_fps, screen->fd, size.cols, rendered);
      }
      g_small = small;
    }
  frame_complete:
    g_render_fd = -1;
    g_render_snapshot = NULL;
    if (frame.render) {
      release_snapshot(frame.owned);
      mutex_lock(&g_mutex);
      g_rendering_screen = -1;
      g_render_completed = g_render_started;
      bool live = false;
      for (int i = 0; i < UI_SCREEN_COUNT; ++i)
        live |= g_screens[i].render != NULL;
      atomic_store_bool(&g_live, live);
      cond_broadcast(&g_render_done);
      mutex_unlock(&g_mutex);
    }
    platform_sleep_ns(16 * NS_PER_MS_INT);
  }
  fps_counter_destroy(g_fps);
  g_fps = NULL;
  g_owner = false;
  return NULL;
}

static asciichat_error_t controller_start(void) {
  if (lifecycle_init_once(&g_lifecycle)) {
    atomic_store_bool(&g_presentation_stop_requested, false);
    atomic_store_bool(&g_blocked, false);
    g_active = -1;
    g_small = false;
    g_redraw = false;
    g_finished = false;
    g_rendering_screen = -1;
    g_render_started = g_render_completed = 0;
    if (mutex_init(&g_mutex, "ui_controller") != 0) {
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot initialize UI mutex");
    }
    if (cond_init(&g_render_done, "ui_render_done") != 0) {
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot initialize UI render completion condition");
    }
    NAMED_REGISTER_ATOMIC(&g_live, "ui_controller_live", NULL);
    NAMED_REGISTER_ATOMIC(&g_presentation_stop_requested, "ui_presentation_stop_requested", NULL);
    NAMED_REGISTER_ATOMIC(&g_blocked, "ui_controller_blocked", NULL);
    NAMED_REGISTER_ATOMIC(&g_lifecycle.state, "ui_controller_lifecycle", NULL);
    if (asciichat_thread_create(&g_thread, "ui_controller", presentation_main, NULL) != ASCIICHAT_OK) {
      NAMED_UNREGISTER(&g_live);
      NAMED_UNREGISTER(&g_presentation_stop_requested);
      NAMED_UNREGISTER(&g_blocked);
      NAMED_UNREGISTER(&g_lifecycle.state);
      cond_destroy(&g_render_done);
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot start UI controller");
    }
    lifecycle_init_commit(&g_lifecycle);
  }
  if (!lifecycle_is_initialized(&g_lifecycle))
    return SET_ERRNO(ERROR_INVALID_STATE, "UI controller initialization is not complete");
  return ASCIICHAT_OK;
}

asciichat_error_t ui_controller_submit(ui_screen_t screen, int fd, terminal_size_t minimum, ui_render_fn render,
                                       const void *snapshot, size_t bytes) {
  if (screen < 0 || screen >= UI_SCREEN_COUNT || !render || !snapshot || !bytes || minimum.cols <= 0 ||
      minimum.rows <= 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid UI screen snapshot");
  // A live slot retains its terminal even while LOG_IO redirects process stdio.
  bool live = false;
  if (lifecycle_is_initialized(&g_lifecycle)) {
    mutex_lock(&g_mutex);
    live = g_screens[screen].render != NULL;
    mutex_unlock(&g_mutex);
  }
  // Batch output and finite snapshots retain their synchronous output semantics.
  if ((!live && !platform_isatty(fd)) || (GET_OPTION(snapshot_mode) && screen != UI_SCREEN_RENDER_PROGRESS)) {
    bool previous_owner = g_owner;
    int previous_fd = g_render_fd;
    terminal_size_t detected = {0};
    // Redirected output has no terminal geometry. Avoid raising a terminal
    // error for every frame when rendering to a file, pipe, or null device.
    if (!platform_isatty(fd) || terminal_get_size_fd(fd, &detected) != ASCIICHAT_OK || detected.cols <= 0 ||
        detected.rows <= 0)
      detected = (terminal_size_t){.cols = GET_OPTION(width), .rows = GET_OPTION(height)};
    g_render_size = detected;
    g_owner = true;
    g_render_fd = fd;
    frame_buffer_set_screen_output_fd(fd);
    stats_media_begin(screen == UI_SCREEN_MEDIA);
    render(g_render_size, snapshot);
    stats_media_end();
    g_owner = previous_owner;
    g_render_fd = previous_fd;
    return ASCIICHAT_OK;
  }
  asciichat_error_t err = controller_start();
  if (err != ASCIICHAT_OK)
    return err;
  if (bytes > SIZE_MAX - sizeof(screen_snapshot_t))
    return SET_ERRNO(ERROR_MEMORY, "UI snapshot is too large");
  screen_snapshot_t *copy = SAFE_MALLOC(sizeof(*copy) + bytes, screen_snapshot_t *);
  if (!copy)
    return SET_ERRNO(ERROR_MEMORY, "Cannot copy UI snapshot");
  atomic_init(&copy->references, 1);
  memcpy(copy->data, snapshot, bytes);
  mutex_lock(&g_mutex);
  if (g_finished) {
    mutex_unlock(&g_mutex);
    SAFE_FREE(copy);
    return ASCIICHAT_OK;
  }
  screen_t *slot = &g_screens[screen];
  bool queue_notice = screen == UI_SCREEN_NOTICE && slot->render;
  int output_fd = platform_dup(slot->render && !queue_notice ? slot->fd : fd);
  if (output_fd < 0) {
    mutex_unlock(&g_mutex);
    SAFE_FREE(copy);
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot retain UI output descriptor");
  }
  copy->fd = output_fd;
  if (queue_notice) {
    size_t pending = 1;
    screen_t *tail = slot;
    while (tail->next) {
      tail = tail->next;
      ++pending;
    }
    if (pending >= 64) {
      platform_close(output_fd);
      SAFE_FREE(copy);
      mutex_unlock(&g_mutex);
      return SET_ERRNO(ERROR_BUFFER_FULL, "UI notice queue is full");
    }
    screen_t *queued = SAFE_CALLOC(1, sizeof(*queued), screen_t *);
    if (!queued) {
      platform_close(output_fd);
      SAFE_FREE(copy);
      mutex_unlock(&g_mutex);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate queued UI notice");
    }
    *queued = (screen_t){.owned = copy, .snapshot = copy->data, .render = render, .minimum = minimum,
                         .fd = output_fd, .dirty = true};
    tail->next = queued;
    mutex_unlock(&g_mutex);
    return ASCIICHAT_OK;
  }
  screen_snapshot_t *retired = slot->owned;
  atomic_store_bool(&g_live, true);
  *slot = (screen_t){.owned = copy, .snapshot = copy->data, .render = render, .minimum = minimum,
                     .fd = output_fd, .dirty = true};
  publish_screen_locked();
  mutex_unlock(&g_mutex);
  release_snapshot(retired);
  return ui_sync_start(fd);
}

void ui_controller_remove(ui_screen_t screen) {
  if (screen < 0 || screen >= UI_SCREEN_COUNT || !lifecycle_is_initialized(&g_lifecycle))
    return;
  mutex_lock(&g_mutex);
  // An old callback must not remove a replacement submitted while it rendered.
  if (g_render_snapshot && g_rendering_screen == (int)screen && g_screens[screen].owned != g_render_snapshot) {
    mutex_unlock(&g_mutex);
    return;
  }
  screen_snapshot_t *retired = g_screens[screen].owned;
  uint64_t pending = g_rendering_screen == (int)screen ? g_render_started : 0;
  screen_t *next = g_screens[screen].next;
  if (next) {
    g_screens[screen] = *next;
    SAFE_FREE(next);
    g_redraw = true;
  } else {
    memset(&g_screens[screen], 0, sizeof(screen_t));
  }
  publish_screen_locked();
  bool live = g_rendering_screen >= 0;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    live |= g_screens[i].render != NULL;
  atomic_store_bool(&g_live, live);
  // Borrowed pointers inside snapshots may be freed as soon as remove returns.
  // A callback removing itself retains its owned bytes until the frame ends.
  while (!g_render_snapshot && g_render_completed < pending)
    cond_wait(&g_render_done, &g_mutex);
  mutex_unlock(&g_mutex);
  release_snapshot(retired);
}

ui_presentation_state_t ui_controller_state(void) {
  ui_presentation_state_t state = {.screen = -1};
  if (!lifecycle_is_initialized(&g_lifecycle))
    return state;
  mutex_lock(&g_mutex);
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    if (g_screens[i].render)
      state.screen = i;
  if (state.screen >= 0) {
    screen_t *screen = &g_screens[state.screen];
    terminal_size_t size = {0};
    if (state.screen == UI_SCREEN_PROMPT && terminal_get_size_fd(screen->fd, &size) == ASCIICHAT_OK)
      screen->minimum.rows = ui_prompt_required_rows(screen->snapshot, size.cols);
    state.covered = !platform_isatty(screen->fd) || terminal_get_size_fd(screen->fd, &size) != ASCIICHAT_OK ||
                    size.cols <= 0 || size.rows <= 0 || ui_too_small(size, screen->minimum);
  }
  mutex_unlock(&g_mutex);
  return state;
}

typedef struct {
  int fd;
  bool media;
  terminal_size_t dimensions;
  size_t len;
  char text[];
} text_snapshot_t;

static void render_text(terminal_size_t size, const void *data) {
  const text_snapshot_t *text = data;
  if (!text->media || GET_OPTION(snapshot_mode) || !platform_isatty(g_render_fd >= 0 ? g_render_fd : text->fd) ||
      (text->dimensions.cols <= size.cols && text->dimensions.rows <= size.rows)) {
    if (text->media && g_render_volume) {
      frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
      if (buffer) {
        frame_buffer_append(buffer, text->text, text->len);
        append_volume(buffer, size);
        ui_controller_write(text->fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
        frame_buffer_destroy(buffer);
      }
    } else {
      ui_controller_write(text->fd, text->text, text->len);
    }
    return;
  }
  // A paused frame can outlive its original dimensions. Clip its rows instead
  // of letting old-sized content wrap and scroll after a terminal resize.
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  char *line = SAFE_MALLOC(text->len + 1, char *);
  char *clipped = SAFE_MALLOC(text->len + 32, char *);
  if (buffer && line && clipped) {
    const char *cursor = text->text;
    const char *end = cursor + text->len;
    for (int row = 1; row <= size.rows && cursor < end; ++row) {
      const char *newline = memchr(cursor, '\n', (size_t)(end - cursor));
      size_t length = (size_t)((newline ? newline : end) - cursor);
      memcpy(line, cursor, length);
      line[length] = '\0';
      truncate_with_ellipsis(line, clipped, text->len + 32, size.cols - 1);
      frame_buffer_printf(buffer, "\033[%d;1H\033[2K", row);
      frame_buffer_append(buffer, clipped, strlen(clipped));
      cursor += length + (newline ? 1 : 0);
    }
    if (g_render_volume)
      append_volume(buffer, size);
    ui_controller_write(text->fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
  }
  if (buffer)
    frame_buffer_destroy(buffer);
  SAFE_FREE(line);
  SAFE_FREE(clipped);
}

void ui_controller_redraw(void) {
  if (!lifecycle_is_initialized(&g_lifecycle))
    return;
  mutex_lock(&g_mutex);
  g_redraw = true;
  mutex_unlock(&g_mutex);
}

asciichat_error_t ui_controller_present(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data,
                                        size_t len) {
  if (!data || !len || len > 16 * 1024 * 1024)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid UI frame");
  text_snapshot_t *text = SAFE_MALLOC(sizeof(*text) + len, text_snapshot_t *);
  if (!text)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate UI frame");
  text->fd = fd;
  text->media = screen == UI_SCREEN_MEDIA;
  text->dimensions = (terminal_size_t){.cols = GET_OPTION(width), .rows = GET_OPTION(height)};
  text->len = len;
  memcpy(text->text, data, len);
  asciichat_error_t err = ui_controller_submit(screen, fd, minimum, render_text, text, sizeof(*text) + len);
  SAFE_FREE(text);
  return err;
}

void ui_controller_shutdown(void) {
  if (!lifecycle_destroy_once(&g_lifecycle))
    return;
  atomic_store_bool(&g_presentation_stop_requested, true);
  asciichat_thread_join(&g_thread, NULL);
  atomic_store_u64(&g_volume_until, 0);
  ui_sync_stop();
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    release_screen(&g_screens[i], i == UI_SCREEN_NOTICE);
  }
  cond_destroy(&g_render_done);
  mutex_destroy(&g_mutex);
  atomic_store_bool(&g_live, false);
  NAMED_UNREGISTER(&g_live);
  NAMED_UNREGISTER(&g_presentation_stop_requested);
  NAMED_UNREGISTER(&g_blocked);
  NAMED_UNREGISTER(&g_lifecycle.state);
  lifecycle_destroy_commit(&g_lifecycle);
}

asciichat_error_t ui_controller_printf(int fd, const char *format, ...) {
  char buffer[16384];
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (len < 0)
    return ERROR_INVALID_PARAM;
  size_t bytes = (size_t)len < sizeof(buffer) ? (size_t)len : sizeof(buffer) - 1;
  return ui_controller_write(fd, buffer, bytes);
}

void ui_controller_restore_terminal(void) {
  ui_controller_shutdown();
  if (platform_isatty(STDOUT_FILENO))
    ui_controller_write(STDOUT_FILENO, "\033[0m\033[?25h", 10);
}
