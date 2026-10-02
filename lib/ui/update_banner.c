/**
 * @file ui/update_banner.c
 * @brief Update available prompt screen implementation
 * @ingroup session
 *
 * Renders a centered box-drawing prompt after splash ends, showing version
 * info and upgrade instructions. Blocks for user input (Y/Enter or N/Esc).
 */

#include <ascii-chat/ui/update_banner.h>
#include <ascii-chat/ui/splash.h>
#include "session/display.h"
#include <ascii-chat/common.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/lifecycle.h>

#include <stdio.h>
#include <string.h>

// Thread-safe storage for the update check result
static update_check_result_t g_update_result;
static mutex_t g_update_result_mutex;
static bool g_update_result_set = false;
static lifecycle_t g_update_result_lifecycle = LIFECYCLE_INIT_MUTEX(&g_update_result_mutex);

// Background update check thread
static asciichat_thread_t g_update_thread;
static bool g_update_thread_started = false;

void update_banner_set_result(const update_check_result_t *result) {
  if (!result || !result->update_available) {
    return;
  }

  if (lifecycle_init_once(&g_update_result_lifecycle)) {
    mutex_init(&g_update_result_mutex, "update_banner_result");
    lifecycle_init_commit(&g_update_result_lifecycle);
  }

  mutex_lock(&g_update_result_mutex);
  memcpy(&g_update_result, result, sizeof(g_update_result));
  g_update_result_set = true;
  mutex_unlock(&g_update_result_mutex);
}

bool update_banner_has_update(void) {
  if (!g_update_result_set) {
    return false;
  }

  if (lifecycle_init_once(&g_update_result_lifecycle)) {
    mutex_init(&g_update_result_mutex, "update_banner_result");
    lifecycle_init_commit(&g_update_result_lifecycle);
  }

  mutex_lock(&g_update_result_mutex);
  bool available = g_update_result_set && g_update_result.update_available;
  mutex_unlock(&g_update_result_mutex);
  return available;
}

// Build a box line: "║  <content><padding>║"
// Content may contain ANSI escape sequences — truncate_with_ellipsis handles them.
static void build_box_line(char *output, size_t output_size, const char *content, int box_width) {
  if (!output || output_size < 256 || !content || box_width < 6) {
    return;
  }

  int content_available = box_width - 4; // "║  " + "║"
  if (content_available < 1) {
    content_available = 1;
  }

  char truncated[512];
  truncate_with_ellipsis(content, truncated, sizeof(truncated), content_available);

  int content_width = display_width(truncated);
  int padding = content_available - content_width;
  if (padding < 0) {
    padding = 0;
  }

  char *pos = output;
  int remaining = (int)output_size;

  int n = snprintf(pos, remaining, "║  %s", truncated);
  if (n > 0) {
    pos += n;
    remaining -= n;
  }

  for (int i = 0; i < padding && remaining > 1; i++) {
    *pos++ = ' ';
    remaining--;
  }

  if (remaining > 3) {
    snprintf(pos, remaining, "║");
  }
}

// Append a positioned box line to the buffer
static void append_line(char *buffer, size_t *buf_pos, size_t buf_size, int start_row, int *current_row, int start_col,
                         int box_width, const char *content) {
  if (!buffer || *buf_pos >= buf_size || !content) {
    return;
  }

  char line_buf[256];
  int written =
      snprintf(buffer + *buf_pos, buf_size - *buf_pos, "\033[%d;%dH", start_row + *current_row, start_col + 1);
  if (written > 0) {
    *buf_pos += (size_t)written;
  }

  build_box_line(line_buf, sizeof(line_buf), content, box_width);
  written = snprintf(buffer + *buf_pos, buf_size - *buf_pos, "%s", line_buf);
  if (written > 0) {
    *buf_pos += (size_t)written;
  }

  (*current_row)++;
}

// Build a horizontal border line (top: ╔═╗, middle: ╠═╣, bottom: ╚═╝)
static void append_border(char *buffer, size_t *buf_pos, size_t buf_size, int start_row, int *current_row,
                           int start_col, int box_width, const char *left, const char *right) {
  if (!buffer || *buf_pos >= buf_size) {
    return;
  }

  int written =
      snprintf(buffer + *buf_pos, buf_size - *buf_pos, "\033[%d;%dH", start_row + *current_row, start_col + 1);
  if (written > 0) {
    *buf_pos += (size_t)written;
  }

  char border[256];
  int bpos = 0;
  int r = snprintf(border + bpos, sizeof(border) - bpos, "%s", left);
  if (r > 0) {
    bpos += r;
  }
  for (int i = 1; i < box_width - 1; i++) {
    r = snprintf(border + bpos, sizeof(border) - bpos, "═");
    if (r > 0) {
      bpos += r;
    }
  }
  snprintf(border + bpos, sizeof(border) - bpos, "%s", right);

  written = snprintf(buffer + *buf_pos, buf_size - *buf_pos, "%s", border);
  if (written > 0) {
    *buf_pos += (size_t)written;
  }

  (*current_row)++;
}

bool update_banner_show_prompt(session_display_ctx_t *ctx) {
  if (!ctx) {
    return false;
  }

  // Get a copy of the result under mutex
  update_check_result_t result;
  mutex_lock(&g_update_result_mutex);
  memcpy(&result, &g_update_result, sizeof(result));
  mutex_unlock(&g_update_result_mutex);

  // Get upgrade suggestion
  install_method_t method = update_check_detect_install_method();
  char suggestion[512];
  update_check_get_upgrade_suggestion(method, result.latest_version, suggestion, sizeof(suggestion));

  // Terminal dimensions
  int term_width = (int)terminal_get_effective_width();
  int term_height = (int)terminal_get_effective_height();

  // Box sizing
  int box_width = 52;
  if (box_width > term_width - 2) {
    box_width = term_width - 2;
  }
  if (box_width < 30) {
    box_width = 30;
  }

  int box_height = 16;
  int start_col = (term_width - box_width) / 2;
  if (start_col < 0) {
    start_col = 0;
  }
  int start_row = (term_height - box_height) / 2;
  if (start_row < 0) {
    start_row = 0;
  }

  // Build the screen
  const size_t BUF_SIZE = 4096;
  char *buffer = SAFE_MALLOC(BUF_SIZE, char *);
  size_t buf_pos = 0;

#define APPEND(fmt, ...)                                                                                               \
  do {                                                                                                                 \
    int _w = snprintf(buffer + buf_pos, BUF_SIZE - buf_pos, fmt, ##__VA_ARGS__);                                       \
    if (_w > 0) {                                                                                                      \
      buf_pos += (size_t)_w;                                                                                           \
    }                                                                                                                  \
  } while (0)

  // Clear screen
  APPEND("\033[2J\033[H");

  int row = 1;

  // Top border
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╔", "╗");

  // Title (bold yellow)
  {
    char title_content[256];
    snprintf(title_content, sizeof(title_content), "\033[1;33mUpdate Available\033[0m");
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, title_content);
  }

  // Separator
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╠", "╣");

  // Blank line
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "");

  // Current version
  {
    char line[256];
    snprintf(line, sizeof(line), "Current : %s (%.8s)", result.current_version, result.current_sha);
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Latest version (green)
  {
    char line[256];
    snprintf(line, sizeof(line), "Latest  : \033[32m%s\033[0m", result.latest_version);
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Blank line
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "");

  // Separator
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╠", "╣");

  // Upgrade instructions
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "To upgrade:");
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "");

  // Upgrade command (bold white)
  {
    char line[256];
    snprintf(line, sizeof(line), "  \033[1m%s\033[0m", suggestion);
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Blank line
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "");

  // Separator
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╠", "╣");

  // Prompt (grey)
  {
    char line[256];
    snprintf(line, sizeof(line), "\033[90m[Y/Enter] exit to update  [N/Esc] continue\033[0m");
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Bottom border
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╚", "╝");

#undef APPEND

  // Write to terminal
  session_display_write_raw(ctx, buffer, buf_pos);
  terminal_flush(STDOUT_FILENO);
  SAFE_FREE(buffer);

  // Block for user input
  while (true) {
    keyboard_key_t key = keyboard_read_with_timeout(60000); // 60s timeout, re-loop
    if (key == 'y' || key == 'Y' || key == '\r' || key == '\n') {
      return true;
    }
    if (key == 'n' || key == 'N' || key == KEY_ESCAPE) {
      return false;
    }
    // Ignore other keys (including KEY_NONE on timeout — just re-render/wait)
  }
}

void update_banner_print_instructions(void) {
  mutex_lock(&g_update_result_mutex);
  update_check_result_t result;
  memcpy(&result, &g_update_result, sizeof(result));
  mutex_unlock(&g_update_result_mutex);

  install_method_t method = update_check_detect_install_method();
  char suggestion[512];
  update_check_get_upgrade_suggestion(method, result.latest_version, suggestion, sizeof(suggestion));

  terminal_clear_screen();

  fprintf(stdout, "\nUpdate available: %s → %s\n\n", result.current_version, result.latest_version);

  if (method == INSTALL_METHOD_GITHUB || method == INSTALL_METHOD_UNKNOWN) {
    fprintf(stdout, "Download the latest release:\n\n    %s\n\n", suggestion);
  } else {
    fprintf(stdout, "To upgrade, run:\n\n    %s\n\n", suggestion);
  }

  if (result.release_url[0] != '\0') {
    fprintf(stdout, "Release notes: %s\n\n", result.release_url);
  }

  fflush(stdout);
}

static void *update_check_thread_func(void *arg) {
  (void)arg;
  update_check_result_t result;
  asciichat_error_t err = update_check_startup(&result);
  if (err == ASCIICHAT_OK && result.update_available) {
    char notification[1024];
    update_check_format_notification(&result, notification, sizeof(notification));
    log_info("%s", notification);

    // Set notification for splash screen (thread-safe)
    splash_set_update_notification(notification);

    // Store full result for the update prompt screen
    update_banner_set_result(&result);
  }
  return NULL;
}

void update_banner_start_check(void) {
  if (asciichat_thread_create(&g_update_thread, "update_check", update_check_thread_func, NULL) == 0) {
    g_update_thread_started = true;
  }
}

void update_banner_wait_for_check(void) {
  if (!g_update_thread_started) {
    return;
  }
  int ret = asciichat_thread_join_timeout(&g_update_thread, NULL, 5LL * NS_PER_SEC_INT);
  if (ret != 0) {
    // Timeout — thread is still alive. Do a blocking join to avoid orphaning it.
    // The thread has bounded runtime (DNS + HTTP both have timeouts) so this won't hang.
    log_warn("Update check thread did not finish within 5s, waiting for completion");
    asciichat_thread_join(&g_update_thread, NULL);
  }
  g_update_thread_started = false;
}
