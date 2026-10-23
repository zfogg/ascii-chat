/**
 * @file tui/terminal_screen.c
 * @brief Reusable "fixed header + scrolling logs" terminal screen abstraction
 *
 * This module extracts the common rendering logic from splash.c and server_status.c
 * into a unified, testable abstraction.
 *
 * Design:
 * - Callback-based architecture for header content generation
 * - Automatic log scrolling with line wrapping
 * - Thread-safe log buffer management
 * - Terminal resize handling
 * - Color scheme integration
 */

#include <ascii-chat/tui/controller.h>
#include "ascii-chat/tui/terminal_screen.h"
#include "ascii-chat/ui/frame_buffer.h"
#include "ascii-chat/log/search.h"
#include <ascii-chat/session/session_log_buffer.h>
#include "ascii-chat/util/display.h"
#include "ascii-chat/util/time.h"
#include "ascii-chat/platform/system.h"
#include "ascii-chat/platform/abstraction.h"
#include "ascii-chat/platform/keyboard.h"
#include "ascii-chat/platform/mutex.h"
#include "ascii-chat/log/grep.h"
#include "ascii-chat/log/log.h"
#include "ascii-chat/options/options.h"
#include "ascii-chat/common.h"
#include <ascii-chat/atomic.h>
#include <ascii-chat/debug/named.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>

// Module-level static frame buffer (reused across renders to avoid malloc per frame)
static frame_buffer_t *g_frame_buf = NULL;

// Cleanup never waits for terminal I/O. An active renderer retains ownership
// until it returns, then services any deferred cleanup request.
enum { SCREEN_IDLE, SCREEN_RENDERING, SCREEN_CLEANUP_PENDING, SCREEN_CLEANING };
static atomic_t g_screen_state = {0};

static void terminal_screen_free_buffer(void) {
  frame_buffer_destroy(g_frame_buf);
  g_frame_buf = NULL;
}

// Shared process history, with an owned fallback for standalone screen users.
static session_log_buffer_t *g_session_log_buffer = NULL;
static bool g_owns_log_buffer;

/* Offset is expressed in rendered terminal rows from the live tail. */
static size_t g_log_scroll_rows;
static uint64_t g_log_hold_started_ns;
static uint64_t g_log_hold_last_ns;
static int g_log_hold_direction;
static bool g_log_help;
static bool g_log_help_too_small;
static uint64_t g_log_last_tail_sequence;
static mutex_t g_log_viewer_mutex;
enum {
  LOG_VIEWER_MUTEX_UNINITIALIZED,
  LOG_VIEWER_MUTEX_INITIALIZING,
  LOG_VIEWER_MUTEX_READY,
  LOG_VIEWER_MUTEX_DESTROYING
};
static atomic_t g_log_viewer_mutex_state = {0};

static bool terminal_screen_logs_mutex_init(void) {
  for (;;) {
    int state = atomic_load_int(&g_log_viewer_mutex_state);
    if (state == LOG_VIEWER_MUTEX_READY)
      return true;
    if (state == LOG_VIEWER_MUTEX_UNINITIALIZED) {
      int expected = LOG_VIEWER_MUTEX_UNINITIALIZED;
      if (!atomic_cas_int(&g_log_viewer_mutex_state, &expected, LOG_VIEWER_MUTEX_INITIALIZING))
        continue;
      if (mutex_init(&g_log_viewer_mutex, "log_viewer") != 0) {
        atomic_store_int(&g_log_viewer_mutex_state, LOG_VIEWER_MUTEX_UNINITIALIZED);
        return false;
      }
      atomic_store_int(&g_log_viewer_mutex_state, LOG_VIEWER_MUTEX_READY);
      return true;
    }
    platform_sleep_ms(1);
  }
}

static void terminal_screen_logs_reset_locked(void) {
  g_log_scroll_rows = 0;
  g_log_hold_started_ns = 0;
  g_log_hold_last_ns = 0;
  g_log_hold_direction = 0;
  g_log_help = false;
  g_log_help_too_small = false;
  g_log_last_tail_sequence = 0;
}

void terminal_screen_logs_reset(void) {
  if (!terminal_screen_logs_mutex_init()) {
    terminal_screen_logs_reset_locked();
    return;
  }
  mutex_lock(&g_log_viewer_mutex);
  terminal_screen_logs_reset_locked();
  mutex_unlock(&g_log_viewer_mutex);
}

size_t terminal_screen_logs_offset(void) {
  if (!terminal_screen_logs_mutex_init())
    return g_log_scroll_rows;
  mutex_lock(&g_log_viewer_mutex);
  size_t offset = g_log_scroll_rows;
  mutex_unlock(&g_log_viewer_mutex);
  return offset;
}

void terminal_screen_logs_set_offset(size_t rows) {
  if (!terminal_screen_logs_mutex_init()) {
    g_log_scroll_rows = rows;
    return;
  }
  mutex_lock(&g_log_viewer_mutex);
  g_log_scroll_rows = rows;
  mutex_unlock(&g_log_viewer_mutex);
}

size_t terminal_screen_logs_retain_position(uint64_t tail_sequence, size_t appended_rows, size_t maximum_offset) {
  if (!terminal_screen_logs_mutex_init())
    return 0;
  mutex_lock(&g_log_viewer_mutex);
  if (g_log_scroll_rows && g_log_last_tail_sequence && tail_sequence > g_log_last_tail_sequence) {
    if (SIZE_MAX - g_log_scroll_rows < appended_rows)
      g_log_scroll_rows = SIZE_MAX;
    else
      g_log_scroll_rows += appended_rows;
  }
  g_log_last_tail_sequence = tail_sequence;
  if (g_log_scroll_rows > maximum_offset)
    g_log_scroll_rows = maximum_offset;
  size_t offset = g_log_scroll_rows;
  mutex_unlock(&g_log_viewer_mutex);
  return offset;
}

uint64_t terminal_screen_logs_last_tail_sequence(void) {
  if (!terminal_screen_logs_mutex_init())
    return 0;
  mutex_lock(&g_log_viewer_mutex);
  uint64_t sequence = g_log_last_tail_sequence;
  mutex_unlock(&g_log_viewer_mutex);
  return sequence;
}

bool terminal_screen_logs_handle_key(int key) {
  if (!terminal_screen_logs_mutex_init())
    return false;
  bool redraw = false;
  mutex_lock(&g_log_viewer_mutex);
  if (g_log_help) {
    if (key == KEY_ESCAPE || key == '?') {
      g_log_help = false;
      redraw = true;
    }
    mutex_unlock(&g_log_viewer_mutex);
    if (redraw)
      ui_controller_redraw();
    return true;
  }
  if (key == '?') {
    terminal_size_t size = ui_controller_size();
    g_log_help_too_small = size.rows < 12 || size.cols < 8;
    g_log_help = !g_log_help_too_small;
    redraw = true;
    mutex_unlock(&g_log_viewer_mutex);
    ui_controller_redraw();
    return true;
  }
  int direction = 0;
  if (key == KEY_UP || key == 'k')
    direction = 1;
  else if (key == KEY_DOWN || key == 'j')
    direction = -1;
  else if (key == KEY_LEFT || key == 21)
    direction = 1000000;
  else if (key == KEY_RIGHT || key == 4)
    direction = -1000000;
  else {
    g_log_help_too_small = false;
    mutex_unlock(&g_log_viewer_mutex);
    return false;
  }

  uint64_t now = time_get_ns();
  size_t step = 1;
  if (direction == 1 || direction == -1) {
    if (g_log_hold_direction != direction || now - g_log_hold_last_ns > 250 * NS_PER_MS_INT)
      g_log_hold_started_ns = now;
    uint64_t held_ms = (now - g_log_hold_started_ns) / NS_PER_MS_INT;
    /* A gentle quadratic curve: single taps stay precise, held keys travel. */
    step += (size_t)((held_ms * held_ms) / 180000);
    if (step > 64)
      step = 64;
  } else {
    int rows = ui_controller_size().rows;
    step = (size_t)(rows > 6 ? (rows - 5) / 2 : 1);
  }
  g_log_hold_direction = direction;
  g_log_hold_last_ns = now;
  if (direction > 0) {
    if (SIZE_MAX - g_log_scroll_rows < step)
      g_log_scroll_rows = SIZE_MAX;
    else
      g_log_scroll_rows += step;
  } else {
    g_log_scroll_rows = g_log_scroll_rows > step ? g_log_scroll_rows - step : 0;
  }
  g_log_help_too_small = false;
  mutex_unlock(&g_log_viewer_mutex);
  ui_controller_redraw();
  return true;
}

bool terminal_screen_logs_help_open(void) {
  if (!terminal_screen_logs_mutex_init())
    return false;
  mutex_lock(&g_log_viewer_mutex);
  bool help_open = g_log_help;
  mutex_unlock(&g_log_viewer_mutex);
  return help_open;
}

bool terminal_screen_logs_help_too_small(void) {
  if (!terminal_screen_logs_mutex_init())
    return false;
  mutex_lock(&g_log_viewer_mutex);
  bool too_small = g_log_help_too_small;
  mutex_unlock(&g_log_viewer_mutex);
  return too_small;
}

static void terminal_screen_render_impl(const terminal_screen_config_t *config) {
  if (!config || !config->render_header)
    return;
  terminal_size_t size = ui_controller_size();
  if (!g_frame_buf)
    g_frame_buf = frame_buffer_create(size.rows, size.cols);
  if (!g_frame_buf)
    return;
  frame_buffer_reset(g_frame_buf);
  terminal_screen_compose(g_frame_buf, size, config, g_session_log_buffer);
  ui_controller_write(frame_buffer_get_screen_output_fd(), frame_buffer_get_content(g_frame_buf),
                      frame_buffer_get_length(g_frame_buf));
}

static void terminal_screen_render_owned(const terminal_screen_config_t *config) {
  int expected = SCREEN_IDLE;
  if (!atomic_cas_int(&g_screen_state, &expected, SCREEN_RENDERING))
    return;

  terminal_screen_render_impl(config);

  expected = SCREEN_RENDERING;
  if (!atomic_cas_int(&g_screen_state, &expected, SCREEN_IDLE)) {
    // Cleanup requested ownership while we were rendering or blocked in write.
    // Keep other renderers out until the buffer is released.
    terminal_screen_free_buffer();
    atomic_store_int(&g_screen_state, SCREEN_IDLE);
  }
}

void terminal_screen_cleanup(void) {
  for (;;) {
    int state = atomic_load_int(&g_screen_state);
    if (state == SCREEN_CLEANUP_PENDING || state == SCREEN_CLEANING)
      return;
    int next = state == SCREEN_RENDERING ? SCREEN_CLEANUP_PENDING : SCREEN_CLEANING;
    if (!atomic_cas_int(&g_screen_state, &state, next))
      continue;
    if (next == SCREEN_CLEANING) {
      terminal_screen_free_buffer();
      atomic_store_int(&g_screen_state, SCREEN_IDLE);
    }
    return;
  }
}

session_log_buffer_t *terminal_screen_log_init(void) {
  log_search_init();
  if (!terminal_screen_logs_mutex_init())
    return NULL;
  if (g_session_log_buffer) {
    return g_session_log_buffer; // Already initialized
  }

  NAMED_REGISTER_ATOMIC(&g_screen_state, "terminal_screen_state", NULL);
  g_session_log_buffer = log_get_history_buffer();
  g_owns_log_buffer = !g_session_log_buffer;
  if (g_owns_log_buffer)
    g_session_log_buffer = session_log_buffer_create();
  return g_session_log_buffer;
}

void terminal_screen_log_destroy(void) {
  ui_controller_remove(UI_SCREEN_STATUS);
  ui_controller_remove(UI_SCREEN_SPLASH);
  NAMED_UNREGISTER(&g_screen_state);
  if (g_session_log_buffer) {
    if (g_owns_log_buffer)
      session_log_buffer_destroy(g_session_log_buffer);
    g_session_log_buffer = NULL;
  }
  terminal_screen_logs_reset();
  int expected = LOG_VIEWER_MUTEX_READY;
  if (atomic_cas_int(&g_log_viewer_mutex_state, &expected, LOG_VIEWER_MUTEX_DESTROYING)) {
    mutex_destroy(&g_log_viewer_mutex);
    atomic_store_int(&g_log_viewer_mutex_state, LOG_VIEWER_MUTEX_UNINITIALIZED);
  }
}

void terminal_screen_log_clear(void) {
  // Process history survives splash/status transitions.
  if (g_session_log_buffer && g_owns_log_buffer) {
    session_log_buffer_clear(g_session_log_buffer);
  }
}

session_log_buffer_t *terminal_screen_get_log_buffer(void) {
  return g_session_log_buffer;
}

void terminal_screen_set_output_fd(int fd) {
  frame_buffer_set_screen_output_fd(fd);
}

typedef struct {
  terminal_screen_config_t config;
  unsigned char header[];
} screen_snapshot_t;
static void render_screen_snapshot(terminal_size_t size, const void *data) {
  (void)size;
  const screen_snapshot_t *snapshot = data;
  terminal_screen_config_t config = snapshot->config;
  config.user_data = (void *)snapshot->header;
  terminal_screen_render_owned(&config);
}
void terminal_screen_render(const terminal_screen_config_t *config) {
  if (!config || !config->render_header || !config->user_data || !config->user_data_size)
    return;
  size_t bytes = sizeof(screen_snapshot_t) + config->user_data_size;
  screen_snapshot_t *snapshot = SAFE_MALLOC(bytes, screen_snapshot_t *);
  if (!snapshot)
    return;
  snapshot->config = *config;
  memcpy(snapshot->header, config->user_data, config->user_data_size);
  terminal_size_t minimum = {.cols = config->minimum_cols,
                             .rows = config->fixed_header_lines + (log_search_is_entering() ? 4 : 2)};
  (void)ui_controller_submit(config->screen, config->output_fd, minimum, render_screen_snapshot, snapshot, bytes);
  SAFE_FREE(snapshot);
}
