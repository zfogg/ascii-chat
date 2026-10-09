#include <ascii-chat/ui/controller.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/ui/too_small.h>
#include <ascii-chat/ui/fps_counter.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/shutdown.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/options/options.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/debug/named.h>

typedef struct {
  void *snapshot;
  ui_render_fn render;
  terminal_size_t minimum;
  int fd;
  bool dirty;
} screen_t;

static screen_t g_screens[UI_SCREEN_COUNT];
static lifecycle_t g_lifecycle = LIFECYCLE_INIT;
static mutex_t g_mutex;
static asciichat_thread_t g_thread;
static atomic_t g_stop = {0};
static _Thread_local bool g_owner;
static _Thread_local int g_render_fd = -1;
static _Thread_local terminal_size_t g_render_size;
static terminal_size_t g_size = {.cols = 80, .rows = 24};
static int g_active = -1;
static bool g_small;
static bool g_redraw;
static bool g_finished;
static atomic_t g_blocked = {0};
static atomic_t g_live = {0};
static terminal_size_t g_last_minimum;
static _Thread_local fps_counter_t *g_fps;

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
  bool complete = platform_write_all(fd, data, len) == len;
  fps_counter_write_end(g_fps, complete);
  return complete ? ASCIICHAT_OK : ERROR_FILE_OPERATION;
}

void ui_controller_finish(int fd, const char *data, size_t len) {
  bool locked = lifecycle_is_initialized(&g_lifecycle);
  if (locked) {
    mutex_lock(&g_mutex);
    g_finished = true;
    for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
      if (g_screens[i].render)
        platform_close(g_screens[i].fd);
      SAFE_FREE(g_screens[i].snapshot);
      memset(&g_screens[i], 0, sizeof(screen_t));
    }
  }
  platform_write_all(fd, data, len);
  atomic_store_bool(&g_live, false);
  if (locked)
    mutex_unlock(&g_mutex);
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
  g_fps = fps_counter_create();
  while (!atomic_load_bool(&g_stop)) {
    mutex_lock(&g_mutex);
    int active = -1;
    for (int i = 0; i < UI_SCREEN_COUNT; ++i)
      if (g_screens[i].render)
        active = i;
    bool changed = active != g_active;
    g_active = active;
    if (active < 0)
      atomic_store_bool(&g_blocked, false);
    if (active >= 0 && !shutdown_is_requested()) {
      screen_t *screen = &g_screens[active];
      terminal_size_t size = {0};
      // Measure the physical output device, never --width/--height or environment overrides.
      if (terminal_get_size_fd(screen->fd, &size) != ASCIICHAT_OK || size.cols <= 0 || size.rows <= 0) {
        // A minimized or detached terminal has no usable drawing area.
        atomic_store_bool(&g_blocked, true);
        g_small = true;
        mutex_unlock(&g_mutex);
        platform_sleep_ns(16 * NS_PER_MS_INT);
        continue;
      }
      bool resized = size.cols != g_size.cols || size.rows != g_size.rows;
      g_size = size;
      g_render_size = size;
      bool small = ui_too_small(size, screen->minimum);
      atomic_store_bool(&g_blocked, small);
      bool requirement_changed =
          screen->minimum.cols != g_last_minimum.cols || screen->minimum.rows != g_last_minimum.rows;
      g_last_minimum = screen->minimum;
      bool transition = changed || resized || requirement_changed || small != g_small || g_redraw;
      bool show_fps = active == UI_SCREEN_HELP || (active == UI_SCREEN_MEDIA && GET_OPTION(fps_counter));
      transition |= fps_counter_set_visible(g_fps, show_fps);
      if (changed || small || g_small)
        fps_counter_reset(g_fps);
      g_redraw = false;
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
        bool rendered = screen->dirty || transition || active != UI_SCREEN_MEDIA;
        if (rendered) {
          fps_counter_frame_begin(g_fps, active == UI_SCREEN_MEDIA || active == UI_SCREEN_HELP);
          screen->render(size, screen->snapshot);
          fps_counter_frame_end(g_fps, time_get_ns());
        }
        fps_counter_render(g_fps, screen->fd, size.cols, rendered);
      }
      g_small = small;
      screen->dirty = false;
    }
    g_render_fd = -1;
    mutex_unlock(&g_mutex);
    platform_sleep_ns(16 * NS_PER_MS_INT);
  }
  fps_counter_destroy(g_fps);
  g_fps = NULL;
  g_owner = false;
  return NULL;
}

static asciichat_error_t controller_start(void) {
  if (lifecycle_init_once(&g_lifecycle)) {
    atomic_store_bool(&g_stop, false);
    atomic_store_bool(&g_blocked, false);
    g_active = -1;
    g_small = false;
    g_redraw = false;
    g_finished = false;
    if (mutex_init(&g_mutex, "ui_controller") != 0) {
      lifecycle_init_abort(&g_lifecycle);
      return SET_ERRNO(ERROR_THREAD, "Cannot initialize UI mutex");
    }
    NAMED_REGISTER_ATOMIC(&g_live, "ui_controller_live", NULL);
    NAMED_REGISTER_ATOMIC(&g_stop, "ui_controller_stop", NULL);
    NAMED_REGISTER_ATOMIC(&g_blocked, "ui_controller_blocked", NULL);
    NAMED_REGISTER_ATOMIC(&g_lifecycle.state, "ui_controller_lifecycle", NULL);
    if (asciichat_thread_create(&g_thread, "ui_controller", presentation_main, NULL) != ASCIICHAT_OK) {
      NAMED_UNREGISTER(&g_live);
      NAMED_UNREGISTER(&g_stop);
      NAMED_UNREGISTER(&g_blocked);
      NAMED_UNREGISTER(&g_lifecycle.state);
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
  if ((!live && !platform_isatty(fd)) || GET_OPTION(snapshot_mode)) {
    bool previous_owner = g_owner;
    int previous_fd = g_render_fd;
    terminal_size_t detected = {0};
    if (terminal_get_size_fd(fd, &detected) != ASCIICHAT_OK || detected.cols <= 0 || detected.rows <= 0)
      detected = (terminal_size_t){.cols = GET_OPTION(width), .rows = GET_OPTION(height)};
    g_render_size = detected;
    g_owner = true;
    g_render_fd = fd;
    frame_buffer_set_screen_output_fd(fd);
    render(g_render_size, snapshot);
    g_owner = previous_owner;
    g_render_fd = previous_fd;
    return ASCIICHAT_OK;
  }
  asciichat_error_t err = controller_start();
  if (err != ASCIICHAT_OK)
    return err;
  void *copy = SAFE_MALLOC(bytes, void *);
  if (!copy)
    return SET_ERRNO(ERROR_MEMORY, "Cannot copy UI snapshot");
  memcpy(copy, snapshot, bytes);
  mutex_lock(&g_mutex);
  if (g_finished) {
    mutex_unlock(&g_mutex);
    SAFE_FREE(copy);
    return ASCIICHAT_OK;
  }
  screen_t *slot = &g_screens[screen];
  int output_fd = slot->render ? slot->fd : platform_dup(fd);
  if (output_fd < 0) {
    mutex_unlock(&g_mutex);
    SAFE_FREE(copy);
    return SET_ERRNO_SYS(ERROR_FILE_OPERATION, "Cannot retain UI output descriptor");
  }
  SAFE_FREE(slot->snapshot);
  atomic_store_bool(&g_live, true);
  *slot = (screen_t){.snapshot = copy, .render = render, .minimum = minimum, .fd = output_fd, .dirty = true};
  mutex_unlock(&g_mutex);
  return ASCIICHAT_OK;
}

void ui_controller_remove(ui_screen_t screen) {
  if (screen < 0 || screen >= UI_SCREEN_COUNT || !lifecycle_is_initialized(&g_lifecycle))
    return;
  if (!g_owner)
    mutex_lock(&g_mutex);
  if (g_screens[screen].render)
    platform_close(g_screens[screen].fd);
  SAFE_FREE(g_screens[screen].snapshot);
  memset(&g_screens[screen], 0, sizeof(screen_t));
  bool live = false;
  for (int i = 0; i < UI_SCREEN_COUNT; ++i)
    live |= g_screens[i].render != NULL;
  atomic_store_bool(&g_live, live);
  if (!g_owner)
    mutex_unlock(&g_mutex);
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
    state.covered = terminal_get_size_fd(screen->fd, &size) != ASCIICHAT_OK || size.cols <= 0 || size.rows <= 0 ||
                    ui_too_small(size, screen->minimum);
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
    ui_controller_write(text->fd, text->text, text->len);
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
  atomic_store_bool(&g_stop, true);
  asciichat_thread_join(&g_thread, NULL);
  for (int i = 0; i < UI_SCREEN_COUNT; ++i) {
    if (g_screens[i].render)
      platform_close(g_screens[i].fd);
    SAFE_FREE(g_screens[i].snapshot);
    memset(&g_screens[i], 0, sizeof(screen_t));
  }
  mutex_destroy(&g_mutex);
  atomic_store_bool(&g_live, false);
  NAMED_UNREGISTER(&g_live);
  NAMED_UNREGISTER(&g_stop);
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
