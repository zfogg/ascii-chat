/**
 * @file ui/terminal_screen.c
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

#include <ascii-chat/ui/controller.h>
#include "ascii-chat/ui/terminal_screen.h"
#include "ascii-chat/ui/frame_buffer.h"
#include "ascii-chat/log/search.h"
#include <ascii-chat/session/session_log_buffer.h>
#include "ascii-chat/util/display.h"
#include "ascii-chat/util/time.h"
#include "ascii-chat/platform/system.h"
#include "ascii-chat/platform/abstraction.h"
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
