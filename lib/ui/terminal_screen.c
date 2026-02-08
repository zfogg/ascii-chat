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

#include "ascii-chat/ui/terminal_screen.h"
#include "ascii-chat/ui/interactive_grep.h"
#include "ascii-chat/session/session_log_buffer.h"
#include "ascii-chat/util/display.h"
#include "ascii-chat/platform/system.h"
#include "ascii-chat/asciichat_errno.h"
#include <stdio.h>
#include <string.h>

// Cached terminal size (to avoid flooding logs with terminal_get_size errors)
static terminal_size_t g_cached_term_size = {.rows = 24, .cols = 80};
static uint64_t g_last_term_size_check_us = 0;
#define TERM_SIZE_CHECK_INTERVAL_US 1000000ULL // Check terminal size max once per second

void terminal_screen_render(const terminal_screen_config_t *config) {
  // Validate config
  if (!config || !config->render_header) {
    return;
  }

  // Update terminal size (cached with 1-second refresh interval)
  uint64_t now_us = platform_get_monotonic_time_us();
  if (now_us - g_last_term_size_check_us >= TERM_SIZE_CHECK_INTERVAL_US) {
    terminal_size_t new_size;
    if (terminal_get_size(&new_size) == ASCIICHAT_OK) {
      g_cached_term_size = new_size;
    }
    g_last_term_size_check_us = now_us;
  }

  // Check if grep is entering (we'll handle screen clearing differently)
  bool grep_entering = interactive_grep_is_entering();

  if (!grep_entering) {
    // Normal mode: clear entire screen
    terminal_clear_screen();
    terminal_cursor_home(STDOUT_FILENO);
  } else {
    // Grep mode: just move to home, don't clear
    terminal_cursor_home(STDOUT_FILENO);
  }

  // Render fixed header via callback
  config->render_header(g_cached_term_size, config->user_data);

  // If logs are disabled, we're done
  if (!config->show_logs) {
    fflush(stdout);
    return;
  }

  // Calculate log area: total rows - header lines - 1 (prevent scroll)
  int log_area_rows = g_cached_term_size.rows - config->fixed_header_lines - 1;

  // Reserve bottom line for grep input if entering (already defined above)
  if (grep_entering) {
    log_area_rows -= 1; // One less row for logs
  }

  if (log_area_rows <= 0) {
    // Still render grep input line even if no space for logs
    if (grep_entering) {
      interactive_grep_render_input_line(g_cached_term_size.cols);
    }
    fflush(stdout);
    return; // No space for logs
  }

  // Fetch and filter logs
  session_log_entry_t *log_entries = NULL;
  size_t log_count = 0;

  if (interactive_grep_is_active()) {
    // Grep is active - use filtered logs
    asciichat_error_t result = interactive_grep_gather_and_filter_logs(&log_entries, &log_count);
    if (result != ASCIICHAT_OK || !log_entries) {
      // Fall back to unfiltered logs
      log_entries = SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(session_log_entry_t), session_log_entry_t *);
      if (log_entries) {
        log_count = session_log_buffer_get_recent(log_entries, SESSION_LOG_BUFFER_SIZE);
      }
    }
  } else {
    // Normal mode - use unfiltered logs from buffer
    log_entries = SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(session_log_entry_t), session_log_entry_t *);
    if (log_entries) {
      log_count = session_log_buffer_get_recent(log_entries, SESSION_LOG_BUFFER_SIZE);
    }
  }

  if (log_count == 0) {
    // No logs to display, fill remaining lines to prevent scroll
    for (int i = 0; i < log_area_rows; i++) {
      fprintf(stdout, "\n");
    }

    // Still need to render grep input if entering
    if (grep_entering) {
      int last_row = g_cached_term_size.rows;
      char pos_buf[32];
      int len = snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;1H\x1b[K", last_row);
      platform_write_all(STDOUT_FILENO, pos_buf, len);
      interactive_grep_render_input_line(g_cached_term_size.cols);
    }

    fflush(stdout);
    return;
  }

  // Calculate which logs fit (working backwards from most recent)
  // We need to account for line wrapping (ANSI-aware)
  int total_lines_needed = 0;
  int first_log_to_display = (int)log_count - 1; // Start from most recent

  // Work backwards to find how many logs fit
  for (int i = (int)log_count - 1; i >= 0; i--) {
    const char *msg = log_entries[i].message;
    int msg_display_width = display_width(msg);

    // Handle invalid display width (shouldn't happen, but be safe)
    if (msg_display_width < 0) {
      msg_display_width = (int)strlen(msg);
    }

    // Calculate how many terminal lines this log needs
    int lines_for_this_log = 1;
    if (msg_display_width > g_cached_term_size.cols) {
      // Line wrapping: ceil(width / cols)
      lines_for_this_log = (msg_display_width + g_cached_term_size.cols - 1) / g_cached_term_size.cols;
    }

    // Check if adding this log would exceed available space
    if (total_lines_needed + lines_for_this_log > log_area_rows) {
      // This log doesn't fit, stop here
      first_log_to_display = i + 1;
      break;
    }

    total_lines_needed += lines_for_this_log;
    first_log_to_display = i;
  }

  // Display logs chronologically (oldest to newest, latest at bottom)
  int lines_printed = 0;
  for (int i = first_log_to_display; i < (int)log_count; i++) {
    const char *msg = log_entries[i].message;
    int msg_display_width = display_width(msg);

    if (msg_display_width < 0) {
      msg_display_width = (int)strlen(msg);
    }

    // Calculate how many lines this log will take
    int lines_for_this_log = 1;
    if (msg_display_width > g_cached_term_size.cols) {
      lines_for_this_log = (msg_display_width + g_cached_term_size.cols - 1) / g_cached_term_size.cols;
    }

    // Print the log message (terminal will handle wrapping)
    fprintf(stdout, "%s\n", msg);
    lines_printed += lines_for_this_log;
  }

  // Fill remaining lines to prevent terminal scroll
  int remaining_lines = log_area_rows - lines_printed;
  for (int i = 0; i < remaining_lines; i++) {
    fprintf(stdout, "\n");
  }

  // Position cursor to absolute bottom line and render grep input
  if (grep_entering) {
    // Move to the very last row using platform_write_all() to bypass any filtering
    int last_row = g_cached_term_size.rows;
    char pos_buf[32];
    int len = snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;1H\x1b[K", last_row);
    platform_write_all(STDOUT_FILENO, pos_buf, len);
    interactive_grep_render_input_line(g_cached_term_size.cols);
  }

  // Free log entries (allocated by interactive_grep or SAFE_MALLOC)
  SAFE_FREE(log_entries);

  // Flush output
  fflush(stdout);
}
