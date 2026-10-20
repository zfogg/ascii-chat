#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/ui/sync.h>
#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/ui/notice.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/session.h>
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
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/video/ascii/rle.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/debug/named.h>

typedef struct {
  _Atomic unsigned references;
  int fd;
} screen_terminal_t;

typedef struct {
  _Atomic unsigned references;
  screen_terminal_t *terminal;
  uint64_t frame_id, submitted_ns, acquisition_started_ns;
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
static ui_session_t *g_session;
static _Thread_local frame_buffer_t *g_capture;
static _Thread_local const animation_sample_t *g_animation;
static lifecycle_t g_lifecycle = LIFECYCLE_INIT;
static mutex_t g_mutex;
static cond_t g_render_done;
static cond_t g_render_requested;
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
static _Thread_local bool g_volume_caps_valid;
static _Thread_local terminal_capabilities_t g_volume_caps;
static _Thread_local terminal_color_mode_t g_volume_color_mode;
static _Thread_local color_setting_t g_volume_color_setting;
static _Thread_local utf8_setting_t g_volume_utf8_setting;
static _Thread_local render_mode_t g_volume_render_mode;

static terminal_capabilities_t volume_capabilities(void) {
  terminal_color_mode_t mode = GET_OPTION(color_mode);
  color_setting_t color = GET_OPTION(color);
  utf8_setting_t utf8 = GET_OPTION(force_utf8);
  render_mode_t render = GET_OPTION(render_mode);
  if (!g_volume_caps_valid || mode != g_volume_color_mode || color != g_volume_color_setting ||
      utf8 != g_volume_utf8_setting || render != g_volume_render_mode) {
    g_volume_caps = apply_color_mode_override(detect_terminal_capabilities());
    g_volume_color_mode = mode;
    g_volume_color_setting = color;
    g_volume_utf8_setting = utf8;
    g_volume_render_mode = render;
    g_volume_caps_valid = true;
  }
  return g_volume_caps;
}

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
  terminal_capabilities_t caps = volume_capabilities();
  bool color = caps.color_level != TERM_COLOR_NONE && GET_OPTION(color) != COLOR_SETTING_FALSE;
  utf8_setting_t utf8_setting = GET_OPTION(force_utf8);
  bool utf8 = utf8_setting == UTF8_SETTING_TRUE || (utf8_setting == UTF8_SETTING_AUTO && caps.utf8_support);
  frame_buffer_append(buffer, "\0337\033[0m", 6);
  for (int row = 0; row < height; ++row) {
    int level = height - row;
    bool lit = level <= filled;
    log_color_t shade = level * 3 <= height       ? LOG_COLOR_ERROR
                        : level * 3 <= height * 2 ? LOG_COLOR_WARN
                                                  : LOG_COLOR_INFO;
    frame_buffer_printf(buffer, "\033[%d;%dH", top + row, col);
    if (color)
      frame_buffer_printf(buffer, "%s", log_level_color(lit ? shade : LOG_COLOR_GREY));
    const char *cells = utf8 ? (lit ? "██" : "░░") : (lit ? "##" : "..");
    frame_buffer_printf(buffer, " %s \033[0m", cells);
  }
  frame_buffer_printf(buffer, "\033[%d;%dH", top + height, col);
  if (volume <= 0.01)
    frame_buffer_append(buffer, "MUTE", 4);
  else
    frame_buffer_printf(buffer, "%3d%%", (int)(volume * 100.0 + 0.5));
  frame_buffer_append(buffer, "\033[0m\0338", 6);
}

int ui_controller_current_screen(void) {
  return atomic_load(&g_published_screen);
}

static int selected_screen_locked(void) {
  uint32_t visible = 0;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    if (g_screens[i].render)
      visible |= 1u << i;
  return ui_session_select_screen(visible);
}

static void publish_screen_locked(void) {
  atomic_store(&g_published_screen, selected_screen_locked());
  cond_signal(&g_render_requested);
}
static bool g_redraw;
static bool g_finished;
static uint64_t g_media_frame_id;
static _Thread_local uint64_t g_acquisition_started_ns;
static _Thread_local bool g_probe_frame;
static _Thread_local uint64_t g_probe_requested_ns;

void ui_controller_frame_acquired_at(uint64_t started_ns) {
  g_acquisition_started_ns = started_ns;
}
static atomic_t g_blocked = {0};
static atomic_t g_live = {0};
static _Thread_local fps_counter_t *g_fps;
static _Thread_local bool g_stats_media, g_stats_failed;
static _Thread_local uint64_t g_stats_write_ns, g_stats_writes, g_stats_max_chunk_ns, g_stats_chunks;
static void stats_media_begin(bool media) {
  g_stats_media = media;
  g_stats_failed = false;
  g_stats_write_ns = g_stats_writes = g_stats_max_chunk_ns = g_stats_chunks = 0;
  g_probe_frame = false;
  g_probe_requested_ns = 0;
}
static void stats_media_end(void) {
  if (g_stats_media && g_stats_writes) {
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_TERMINAL_WRITE, g_stats_write_ns);
    stats_counter_add(stats_runtime_scope(),
                      g_stats_failed ? STATS_COUNTER_FRAMES_DROPPED : STATS_COUNTER_FRAMES_PRESENTED, 1);
  }
  g_stats_media = false;
}

static size_t write_output_chunk(int fd, const void *data, size_t length) {
  uint64_t started_ns = g_stats_media ? time_get_ns() : 0;
  size_t written = platform_write_all(fd, data, length);
  if (g_stats_media) {
    uint64_t elapsed_ns = time_get_ns() - started_ns;
    if (elapsed_ns > g_stats_max_chunk_ns)
      g_stats_max_chunk_ns = elapsed_ns;
    ++g_stats_chunks;
  }
  return written;
}

asciichat_error_t ui_controller_write(int fd, const char *data, size_t len) {
  if (!data || !len)
    return ASCIICHAT_OK;
  if (g_capture) {
    frame_buffer_append(g_capture, data, len);
    return ASCIICHAT_OK;
  }
  // Logging may call this while holding subsystem locks; never wait for a
  // renderer callback here. Live screens already capture logs in their model.
  if (!g_owner && atomic_load_bool(&g_live))
    return ASCIICHAT_OK;
  if (g_owner && g_render_fd >= 0)
    fd = g_render_fd;
  fps_counter_write_begin(g_fps);
  uint64_t stats_start = g_stats_media ? time_get_ns() : 0;
  // Terminals supporting synchronized output present the art and its overlays
  // together, even when a large UTF-8 frame requires several writes.
  bool synchronized = g_stats_media && g_render_snapshot;
  bool started = !synchronized || write_output_chunk(fd, "\033[?2026h", 8) == 8;
  size_t written = 0;
  while (started && written < len) {
    // A pending editor or overlay supersedes the remainder of a media frame.
    // Bound each console write so a large frame cannot delay that transition.
    if (g_stats_media && ui_controller_current_screen() != UI_SCREEN_MEDIA)
      break;
    size_t count = len - written;
    if (g_stats_media && count > 65536) {
      count = 65536;
      while (count && ((unsigned char)data[written + count] & 0xc0) == 0x80)
        --count;
    }
    size_t sent = write_output_chunk(fd, data + written, count);
    written += sent;
    if (sent != count)
      break;
  }
  // End the update even when input superseded the frame halfway through.
  static const char end_with_query[] = "\033[?2026l\033]11;?\033\\";
  bool probe = synchronized && g_probe_frame && written == len && started;
  if (probe)
    g_probe_requested_ns = time_get_ns();
  size_t end_length = probe ? sizeof(end_with_query) - 1 : 8;
  bool ended = !synchronized || write_output_chunk(fd, end_with_query, end_length) == end_length;
  if (!ended)
    g_probe_requested_ns = 0;
  bool complete = written == len && started && ended;
  if (g_stats_media) {
    g_stats_write_ns += time_get_ns() - stats_start;
    g_stats_writes++;
    g_stats_failed |= !complete;
  }
  fps_counter_write_end(g_fps, complete);
  return complete ? ASCIICHAT_OK : ERROR_FILE_OPERATION;
}

static void release_terminal(screen_terminal_t *terminal) {
  if (terminal && atomic_fetch_sub(&terminal->references, 1) == 1) {
    platform_close(terminal->fd);
    SAFE_FREE(terminal);
  }
}

static void release_snapshot(screen_snapshot_t *snapshot) {
  if (snapshot && atomic_fetch_sub(&snapshot->references, 1) == 1) {
    release_terminal(snapshot->terminal);
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
    cond_signal(&g_render_requested);
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

const animation_sample_t *ui_controller_animation_sample(void) {
  return g_animation;
}

typedef struct {
  screen_t *screen;
  int active;
  bool volume;
  bool drew;
  bool fresh;
} native_view_t;

static void draw_native_view(frame_buffer_t *output, terminal_size_t size, const animation_sample_t *animation,
                             void *context) {
  native_view_t *view = context;
  g_capture = output;
  g_animation = animation;
  g_render_volume = view->volume;
  view->fresh = fps_counter_frame_begin_id(g_fps, view->active == UI_SCREEN_MEDIA ? view->screen->owned->frame_id : 0);
  // Paint the reserved cells before the art, which never writes over them.
  // This remains visible on terminals without synchronized-output support.
  fps_counter_render(g_fps, view->screen->fd, size.cols, true);
  view->screen->render(size, view->screen->snapshot);
  // Compose overlays before publishing the frame, so replacing the media cannot
  // expose an intermediate frame with its counter erased.
  fps_counter_render(g_fps, view->screen->fd, size.cols, true);
  g_render_volume = false;
  g_animation = NULL;
  g_capture = NULL;
  view->drew = true;
}

static void *presentation_main(void *unused) {
  (void)unused;
  g_owner = true;
  g_fps = fps_counter_create();
  bool was_sync = false;
  uint64_t last_volume_until = 0;
  bool volume_was_visible = false;
  uint64_t reply_requested_ns = 0, reply_frame_ns = 0, reply_frame_id = 0, reply_written_ns = 0;
  int reply_support = 0;
  bool awaiting_reply = false;
  while (!atomic_load_bool(&g_presentation_stop_requested)) {
    uint64_t iteration_started_ns = time_get_ns();
    uint64_t refresh_deadline = iteration_started_ns + 16 * NS_PER_MS_INT;
    uint64_t replied_ns = keyboard_stream_terminal_reply_ns();
    if (reply_requested_ns && replied_ns >= reply_requested_ns) {
      reply_support = 1;
      if (awaiting_reply) {
        fps_counter_frame_end(g_fps, replied_ns);
        log_debug("TERMINAL_ACK: id=%llu response_ms=%.2f capture_to_terminal_ms=%.2f",
                  (unsigned long long)reply_frame_id, (double)(replied_ns - reply_requested_ns) / NS_PER_MS_INT,
                  reply_frame_ns ? (double)(replied_ns - reply_frame_ns) / NS_PER_MS_INT : 0);
      }
      awaiting_reply = false;
    }
    if (awaiting_reply && reply_support == 0 && iteration_started_ns - reply_requested_ns > 500 * NS_PER_MS_INT) {
      // Classic consoles need no remote acknowledgement and may not answer OSC.
      reply_support = -1;
      awaiting_reply = false;
      fps_counter_frame_end(g_fps, reply_written_ns);
    }
    if (awaiting_reply && ui_controller_current_screen() == UI_SCREEN_MEDIA && !shutdown_is_requested()) {
      // Keep only the latest mailbox frame while the frontend parses output.
      // Input and modal screens remain independent of this backpressure.
      platform_sleep_ms(2);
      continue;
    }
    // This path must remain independent of producers, including one stuck
    // holding the controller mutex. It reads only the collector's mailbox.
    if (ui_sync_is_visible() && ui_controller_current_screen() >= 0 &&
        ui_controller_current_screen() <= UI_SCREEN_SYNC && !shutdown_is_requested()) {
      if (!was_sync) {
        const ui_session_view_t hidden = {.screen = -1};
        ui_session_render(g_session, &hidden, time_get_ns());
      }
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
      ui_session_redraw(g_session);
      was_sync = false;
    }
    mutex_lock(&g_mutex);
    int active = selected_screen_locked();
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
    if (active < 0) {
      const ui_session_view_t hidden = {.screen = -1};
      ui_session_render(g_session, &hidden, time_get_ns());
      atomic_store_bool(&g_blocked, false);
    }
    if (frame.render) {
      g_render_snapshot = frame.owned;
      screen_t *screen = &frame;
      terminal_resize_watcher_t watcher = {.size = previous_size};
      bool resized = false;
      // Observe physical changes once for all modes; consumers use the published dimensions.
      if (terminal_resize_poll(&watcher, screen->fd, &resized) != ASCIICHAT_OK) {
        // A minimized or detached terminal has no usable drawing area.
        atomic_store_bool(&g_blocked, true);
        const ui_session_view_t hidden = {.screen = -1};
        ui_session_render(g_session, &hidden, time_get_ns());
        goto frame_complete;
      }
      uint64_t geometry_done_ns = time_get_ns();
      terminal_size_t size = watcher.size;
      mutex_lock(&g_mutex);
      g_size = size;
      mutex_unlock(&g_mutex);
      g_render_size = size;
      if (active == UI_SCREEN_PROMPT)
        screen->minimum.rows = ui_prompt_required_rows(screen->snapshot, size.cols);
      uint64_t now = time_get_ns();
      uint64_t volume_until = atomic_load_u64(&g_volume_until);
      bool volume_visible =
          active == UI_SCREEN_MEDIA && !GET_OPTION(snapshot_mode) && !GET_OPTION(strip_ansi) && now < volume_until;
      bool volume_changed =
          volume_visible != volume_was_visible || (volume_visible && volume_until != last_volume_until);
      if (volume_was_visible && !volume_visible)
        redraw = true;
      volume_was_visible = volume_visible;
      last_volume_until = volume_until;
      bool show_fps = active == UI_SCREEN_HELP || (active == UI_SCREEN_MEDIA && GET_OPTION(fps_counter));
      redraw |= fps_counter_set_visible(g_fps, show_fps);
      bool was_covered = ui_session_is_covered(g_session);
      if (changed || was_covered)
        fps_counter_reset(g_fps);
      g_render_fd = screen->fd;
      frame_buffer_set_screen_output_fd(screen->fd);
      if ((resized || changed) && active == UI_SCREEN_MEDIA) {
        if (GET_OPTION(auto_width))
          options_set_int("width", size.cols);
        if (GET_OPTION(auto_height))
          options_set_int("height", size.rows);
      }
      ui_session_resize(g_session, size.cols, size.rows);
      if (redraw)
        ui_session_redraw(g_session);
      if (active == UI_SCREEN_MEDIA) {
        // Console geometry queries can block while capture continues. Take the
        // latest mailbox snapshot after those queries instead of drawing the
        // image retained before them.
        screen_snapshot_t *retired = NULL;
        mutex_lock(&g_mutex);
        if (g_screens[active].owned && g_screens[active].owned != frame.owned) {
          retired = frame.owned;
          frame = g_screens[active];
          atomic_fetch_add(&frame.owned->references, 1);
          g_screens[active].dirty = false;
          g_render_snapshot = frame.owned;
        }
        mutex_unlock(&g_mutex);
        release_snapshot(retired);
      }
      native_view_t context = {.screen = screen, .active = active, .volume = volume_visible, .fresh = false};
      ui_session_view_t view = {
          .screen = active,
          .minimum = screen->minimum,
          .dirty = screen->dirty || volume_changed,
          .refresh_ns = active == UI_SCREEN_PROMPT ? 50 * NS_PER_MS_INT
                        : active == UI_SCREEN_LOGS ? 100 * NS_PER_MS_INT
                        : active == UI_SCREEN_RENDER_PROGRESS
                            ? 125 * NS_PER_MS_INT
                            : (active != UI_SCREEN_MEDIA && active != UI_SCREEN_STATS ? 16 * NS_PER_MS_INT : 0),
          .draw = draw_native_view,
          .context = &context};
      uint64_t compose_started_ns = time_get_ns();
      const char *output = ui_session_render(g_session, &view, now);
      uint64_t composed_ns = time_get_ns();
      bool covered = ui_session_is_covered(g_session);
      atomic_store_bool(&g_blocked, covered);
      if (covered && !was_covered)
        fps_counter_reset(g_fps);
      if (output) {
        stats_media_begin(context.fresh && context.drew);
        g_probe_frame = context.fresh && context.drew && reply_support >= 0;
        asciichat_error_t written = ui_controller_write(screen->fd, output, ui_session_frame_length(g_session));
        uint64_t write_done_ns = time_get_ns();
        if (context.fresh && context.drew) {
          fps_counter_write_begin(g_fps);
          // A frontend reply drains the complete update. A local console query
          // adds an RPC without establishing any stronger completion boundary.
          asciichat_error_t drained = g_probe_requested_ns ? ASCIICHAT_OK : terminal_flush(screen->fd);
          fps_counter_write_end(g_fps, drained == ASCIICHAT_OK);
          if (written != ASCIICHAT_OK || drained != ASCIICHAT_OK)
            CLEAR_ERRNO();
          else {
            uint64_t completed_ns = time_get_ns();
            log_debug(
                "FRAME_PRESENT: id=%llu submit_to_stdout_ms=%.2f acquired_ns=%llu submitted_ns=%llu "
                "completed_ns=%llu capture_to_stdout_ms=%.2f mailbox_ms=%.2f geometry_ms=%.2f "
                "compose_ms=%.2f write_ms=%.2f drain_ms=%.2f max_chunk_ms=%.2f chunks=%llu bytes=%zu cols=%d rows=%d",
                (unsigned long long)screen->owned->frame_id,
                (double)(completed_ns - screen->owned->submitted_ns) / NS_PER_MS_INT,
                (unsigned long long)screen->owned->acquisition_started_ns,
                (unsigned long long)screen->owned->submitted_ns, (unsigned long long)completed_ns,
                screen->owned->acquisition_started_ns
                    ? (double)(completed_ns - screen->owned->acquisition_started_ns) / NS_PER_MS_INT
                    : 0,
                (double)(compose_started_ns - screen->owned->submitted_ns) / NS_PER_MS_INT,
                (double)(geometry_done_ns - iteration_started_ns) / NS_PER_MS_INT,
                (double)(composed_ns - compose_started_ns) / NS_PER_MS_INT,
                (double)(write_done_ns - composed_ns) / NS_PER_MS_INT,
                (double)(completed_ns - write_done_ns) / NS_PER_MS_INT, (double)g_stats_max_chunk_ns / NS_PER_MS_INT,
                (unsigned long long)g_stats_chunks, ui_session_frame_length(g_session), size.cols, size.rows);
          }
        }
        stats_media_end();
        if (g_probe_frame) {
          awaiting_reply = written == ASCIICHAT_OK && g_probe_requested_ns != 0;
          if (awaiting_reply) {
            reply_requested_ns = g_probe_requested_ns;
            reply_frame_ns = screen->owned->acquisition_started_ns;
            reply_frame_id = screen->owned->frame_id;
            reply_written_ns = time_get_ns();
          }
        }
        if (context.drew && !awaiting_reply)
          fps_counter_frame_end(g_fps, time_get_ns());
      }
      if (!covered && !context.drew)
        fps_counter_render(g_fps, screen->fd, size.cols, context.drew);
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
    // New snapshots wake presentation immediately. The deadline only refreshes
    // animations, terminal geometry and idle FPS counters; rendering time is
    // part of the interval rather than an additional delay after each frame.
    mutex_lock(&g_mutex);
    int pending_screen = atomic_load(&g_published_screen);
    bool pending = g_redraw || pending_screen != g_active || (pending_screen >= 0 && g_screens[pending_screen].dirty);
    uint64_t now = time_get_ns();
    if (!pending && !atomic_load_bool(&g_presentation_stop_requested) && now < refresh_deadline)
      cond_timedwait(&g_render_requested, &g_mutex, refresh_deadline - now);
    mutex_unlock(&g_mutex);
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
    if (cond_init(&g_render_requested, "ui_render_requested") != 0) {
      cond_destroy(&g_render_done);
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot initialize UI presentation condition");
    }
    g_session = ui_session_create();
    if (!g_session) {
      cond_destroy(&g_render_requested);
      cond_destroy(&g_render_done);
      mutex_destroy(&g_mutex);
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_MEMORY, "Cannot create session UI runtime");
    }
    NAMED_REGISTER_ATOMIC(&g_live, "ui_controller_live", NULL);
    NAMED_REGISTER_ATOMIC(&g_presentation_stop_requested, "ui_presentation_stop_requested", NULL);
    NAMED_REGISTER_ATOMIC(&g_blocked, "ui_controller_blocked", NULL);
    NAMED_REGISTER_ATOMIC(&g_lifecycle.state, "ui_controller_lifecycle", NULL);
    if (asciichat_thread_create(&g_thread, "ui_controller", presentation_main, NULL) != ASCIICHAT_OK) {
      ui_session_destroy(g_session);
      g_session = NULL;
      NAMED_UNREGISTER(&g_live);
      NAMED_UNREGISTER(&g_presentation_stop_requested);
      NAMED_UNREGISTER(&g_blocked);
      NAMED_UNREGISTER(&g_lifecycle.state);
      cond_destroy(&g_render_requested);
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

static asciichat_error_t submit_screen(ui_screen_t screen, int fd, terminal_size_t minimum, ui_render_fn render,
                                       const void *snapshot, size_t bytes, screen_terminal_t *retained_terminal,
                                       bool fresh_frame) {
  uint64_t entered_ns = time_get_ns();
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
  // New modal slots inherit the live terminal instead of process stdio, which
  // an audio/decoder worker can temporarily redirect through LOG_IO.
  if (!live && !retained_terminal && lifecycle_is_initialized(&g_lifecycle) &&
      (screen == UI_SCREEN_PROMPT || screen == UI_SCREEN_RENDER_PROGRESS || screen == UI_SCREEN_NOTICE)) {
    screen_terminal_t *inherited = NULL;
    mutex_lock(&g_mutex);
    int active = selected_screen_locked();
    if (active >= 0) {
      inherited = g_screens[active].owned->terminal;
      atomic_fetch_add(&inherited->references, 1);
    }
    mutex_unlock(&g_mutex);
    if (inherited) {
      asciichat_error_t result =
          submit_screen(screen, inherited->fd, minimum, render, snapshot, bytes, inherited, fresh_frame);
      release_terminal(inherited);
      return result;
    }
  }
  // Batch output and finite snapshots retain their synchronous output semantics.
  if ((!live && !retained_terminal && !platform_isatty(fd)) ||
      (GET_OPTION(snapshot_mode) && screen != UI_SCREEN_RENDER_PROGRESS)) {
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
  uint64_t prepared_ns = time_get_ns();
  mutex_lock(&g_mutex);
  uint64_t locked_ns = time_get_ns();
  if (g_finished) {
    mutex_unlock(&g_mutex);
    SAFE_FREE(copy);
    return ASCIICHAT_OK;
  }
  copy->frame_id = screen == UI_SCREEN_MEDIA && fresh_frame ? ++g_media_frame_id : 0;
  copy->submitted_ns = time_get_ns();
  copy->acquisition_started_ns = screen == UI_SCREEN_MEDIA ? g_acquisition_started_ns : 0;
  screen_t *slot = &g_screens[screen];
  bool queue_notice = screen == UI_SCREEN_NOTICE && slot->render;
  if (slot->render && !queue_notice) {
    // The terminal belongs to the live slot, not to each incoming image.
    // Duplicating/closing a Windows console handle per frame introduces console
    // RPCs on producers and can serialize them behind ongoing presentation.
    copy->terminal = slot->owned->terminal;
    atomic_fetch_add(&copy->terminal->references, 1);
  } else if (retained_terminal) {
    copy->terminal = retained_terminal;
    atomic_fetch_add(&copy->terminal->references, 1);
  } else {
    int retained_fd = platform_dup(fd);
    if (retained_fd < 0) {
      mutex_unlock(&g_mutex);
      SAFE_FREE(copy);
      return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot retain UI output descriptor");
    }
    copy->terminal = SAFE_MALLOC(sizeof(*copy->terminal), screen_terminal_t *);
    if (!copy->terminal) {
      platform_close(retained_fd);
      mutex_unlock(&g_mutex);
      SAFE_FREE(copy);
      return SET_ERRNO(ERROR_MEMORY, "Cannot retain UI terminal ownership");
    }
    atomic_init(&copy->terminal->references, 1);
    copy->terminal->fd = retained_fd;
  }
  int output_fd = copy->terminal->fd;
  if (queue_notice) {
    size_t pending = 1;
    screen_t *tail = slot;
    while (tail->next) {
      tail = tail->next;
      ++pending;
    }
    if (pending >= 64) {
      release_snapshot(copy);
      mutex_unlock(&g_mutex);
      return SET_ERRNO(ERROR_BUFFER_FULL, "UI notice queue is full");
    }
    screen_t *queued = SAFE_CALLOC(1, sizeof(*queued), screen_t *);
    if (!queued) {
      release_snapshot(copy);
      mutex_unlock(&g_mutex);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate queued UI notice");
    }
    *queued = (screen_t){
        .owned = copy, .snapshot = copy->data, .render = render, .minimum = minimum, .fd = output_fd, .dirty = true};
    tail->next = queued;
    mutex_unlock(&g_mutex);
    return ASCIICHAT_OK;
  }
  screen_snapshot_t *retired = slot->owned;
  atomic_store_bool(&g_live, true);
  *slot = (screen_t){
      .owned = copy, .snapshot = copy->data, .render = render, .minimum = minimum, .fd = output_fd, .dirty = true};
  publish_screen_locked();
  mutex_unlock(&g_mutex);
  uint64_t published_ns = time_get_ns();
  release_snapshot(retired);
  uint64_t retired_ns = time_get_ns();
  asciichat_error_t sync_result = ui_sync_start(fd);
  uint64_t finished_ns = time_get_ns();
  if (screen == UI_SCREEN_MEDIA && finished_ns - entered_ns > 50 * NS_PER_MS_INT)
    log_debug_every(
        US_PER_SEC_INT, "UI_SUBMIT_STAGE: build_ms=%.2f lock_ms=%.2f publish_ms=%.2f retire_ms=%.2f sync_ms=%.2f",
        (double)(prepared_ns - entered_ns) / NS_PER_MS_INT, (double)(locked_ns - prepared_ns) / NS_PER_MS_INT,
        (double)(published_ns - locked_ns) / NS_PER_MS_INT, (double)(retired_ns - published_ns) / NS_PER_MS_INT,
        (double)(finished_ns - retired_ns) / NS_PER_MS_INT);
  return sync_result;
}

asciichat_error_t ui_controller_submit(ui_screen_t screen, int fd, terminal_size_t minimum, ui_render_fn render,
                                       const void *snapshot, size_t bytes) {
  return submit_screen(screen, fd, minimum, render, snapshot, bytes, NULL, true);
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
  state.screen = selected_screen_locked();
  if (state.screen >= 0) {
    screen_t *screen = &g_screens[state.screen];
    // Geometry belongs to the presentation owner. A keyboard poll must never
    // wait for a console RPC behind pending terminal output while holding the
    // controller mutex.
    terminal_size_t size = g_size;
    if (state.screen == UI_SCREEN_PROMPT)
      screen->minimum.rows = ui_prompt_required_rows(screen->snapshot, size.cols);
    state.covered = size.cols <= 0 || size.rows <= 0 || ui_too_small(size, screen->minimum);
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

// Bound REP expansion to one terminal row before allocating its expanded text.
static char *expand_media_row(const char *row, size_t length, int columns) {
  size_t repeats = 0;
  for (const char *p = row, *end = row + length; p < end;) {
    if (*p != '\033') {
      ++p;
      continue;
    }
    const char *next = ansi_skip_escape(p, end);
    if (next <= p)
      return NULL;
    if (next - p >= 3 && p[1] == '[' && next[-1] == 'b') {
      size_t count = 0;
      for (const char *digit = p + 2; digit < next - 1; ++digit) {
        if (*digit < '0' || *digit > '9' || count > (size_t)columns / 10)
          return NULL;
        count = count * 10 + (size_t)(*digit - '0');
        if (count > (size_t)columns)
          return NULL;
      }
      if (!count || count > (size_t)columns - repeats)
        return NULL;
      repeats += count;
    }
    p = next;
  }
  return ansi_expand_rle(row, length);
}

static void render_text(terminal_size_t size, const void *data) {
  const text_snapshot_t *text = data;
  if (text->media && !GET_OPTION(snapshot_mode) && GET_OPTION(fps_counter) && size.cols >= 7 &&
      text->dimensions.cols <= size.cols && text->dimensions.rows <= size.rows) {
    const char *newline = memchr(text->text, '\n', text->len);
    size_t first_bytes = newline ? (size_t)(newline - text->text) : text->len;
    // Expand REP before clipping, so a repeated glyph cannot cross the counter.
    char *first = expand_media_row(text->text, first_bytes, size.cols);
    if (first) {
      size_t prefix = utf8_prefix_bytes_for_width(first, strlen(first), size.cols - 7);
      ui_controller_write(text->fd, first, prefix);
      SAFE_FREE(first);
    } else {
      ui_controller_printf(text->fd, "\033[1;1H\033[%dX", size.cols - 7);
    }
    if (newline) {
      ui_controller_write(text->fd, "\033[0m\033[2;1H", 10);
      ui_controller_write(text->fd, newline + 1, text->len - first_bytes - 1);
    }
    if (g_render_volume)
      append_volume(g_capture, size);
    return;
  }
  if (!text->media || GET_OPTION(snapshot_mode) ||
      (text->dimensions.cols <= size.cols && text->dimensions.rows <= size.rows) ||
      !platform_isatty(g_render_fd >= 0 ? g_render_fd : text->fd)) {
    if (text->media && g_render_volume) {
      frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
      if (buffer) {
        frame_buffer_append(buffer, text->text, text->len);
        append_volume(buffer, size);
        ui_controller_write(text->fd, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
        frame_buffer_destroy(buffer);
      } else {
        ui_controller_write(text->fd, text->text, text->len);
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
      bool reserved_counter = row == 1 && GET_OPTION(fps_counter) && size.cols >= 7;
      char *expanded = expand_media_row(line, length, text->dimensions.cols);
      truncate_with_ellipsis(expanded ? expanded : "", clipped, text->len + 32, size.cols - (reserved_counter ? 7 : 1));
      SAFE_FREE(expanded);
      frame_buffer_printf(buffer, reserved_counter ? "\033[%d;1H" : "\033[%d;1H\033[2K", row);
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
  cond_signal(&g_render_requested);
  mutex_unlock(&g_mutex);
}

static asciichat_error_t present_text(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data, size_t len,
                                      bool fresh) {
  uint64_t entered_ns = time_get_ns();
  if (!data || !len || len > UI_CONTROLLER_MAX_FRAME_BYTES)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid UI frame");
  text_snapshot_t *text = SAFE_MALLOC(sizeof(*text) + len, text_snapshot_t *);
  if (!text)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate UI frame");
  text->fd = fd;
  text->media = screen == UI_SCREEN_MEDIA;
  text->dimensions = (terminal_size_t){.cols = GET_OPTION(width), .rows = GET_OPTION(height)};
  text->len = len;
  memcpy(text->text, data, len);
  uint64_t prepared_ns = time_get_ns();
  asciichat_error_t err = submit_screen(screen, fd, minimum, render_text, text, sizeof(*text) + len, NULL, fresh);
  uint64_t submitted_ns = time_get_ns();
  SAFE_FREE(text);
  uint64_t finished_ns = time_get_ns();
  if (screen == UI_SCREEN_MEDIA && finished_ns - entered_ns > 50 * NS_PER_MS_INT)
    log_debug_every(US_PER_SEC_INT, "UI_TEXT_STAGE: prepare_ms=%.2f submit_ms=%.2f free_ms=%.2f",
                    (double)(prepared_ns - entered_ns) / NS_PER_MS_INT,
                    (double)(submitted_ns - prepared_ns) / NS_PER_MS_INT,
                    (double)(finished_ns - submitted_ns) / NS_PER_MS_INT);
  return err;
}

void ui_controller_shutdown(void) {
  if (!lifecycle_destroy_once(&g_lifecycle))
    return;
  mutex_lock(&g_mutex);
  atomic_store_bool(&g_presentation_stop_requested, true);
  cond_signal(&g_render_requested);
  mutex_unlock(&g_mutex);
  asciichat_thread_join(&g_thread, NULL);
  ui_session_destroy(g_session);
  g_session = NULL;
  atomic_store_u64(&g_volume_until, 0);
  ui_sync_stop();
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    release_screen(&g_screens[i], i == UI_SCREEN_NOTICE);
  }
  cond_destroy(&g_render_requested);
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

static void render_logs(terminal_size_t size, const void *snapshot) {
  (void)snapshot;
  ui_session_logs_render(g_capture, size, g_session);
}

void ui_controller_hide_logs(void) {
  ui_controller_remove(UI_SCREEN_LOGS);
}

void ui_controller_toggle_logs(void) {
  if (ui_controller_current_screen() == UI_SCREEN_LOGS) {
    ui_controller_hide_logs();
    return;
  }
  if (!platform_isatty(STDOUT_FILENO) || GET_OPTION(snapshot_mode))
    return;
  const bool snapshot = true;
  asciichat_error_t result =
      ui_controller_submit(UI_SCREEN_LOGS, STDOUT_FILENO, (terminal_size_t){.cols = 40, .rows = 6}, render_logs,
                           &snapshot, sizeof(snapshot));
  if (result != ASCIICHAT_OK)
    log_warn("Cannot show application logs: %d", result);
}

asciichat_error_t ui_controller_present(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data,
                                        size_t len) {
  return present_text(screen, fd, minimum, data, len, true);
}
asciichat_error_t ui_controller_present_retained(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data,
                                                 size_t len) {
  return present_text(screen, fd, minimum, data, len, false);
}
