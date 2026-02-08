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
#include <stdio.h>
#include <string.h>

// Cached terminal size (to avoid flooding logs with terminal_get_size errors)
static terminal_size_t g_cached_term_size = {.rows = 24, .cols = 80};
static uint64_t g_last_term_size_check_us = 0;
#define TERM_SIZE_CHECK_INTERVAL_US 1000000ULL // Check terminal size max once per second

// Cache of previously rendered log lines for diff-based rendering.
// Only rewrite lines whose content actually changed.
#define MAX_CACHED_LINES 256
static const char *g_prev_log_ptrs[MAX_CACHED_LINES];
static int g_prev_log_count = 0;
static int g_prev_total_lines = 0; // Total terminal lines used by logs last frame

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

  bool grep_entering = interactive_grep_is_entering();

  if (!grep_entering) {
    // Normal mode: clear and redraw (no flicker concern without grep)
    terminal_clear_screen();
    terminal_cursor_home(STDOUT_FILENO);
  } else {
    // Grep mode: overwrite in place, never clear whole screen
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

  // Reserve bottom line for grep input
  if (grep_entering) {
    log_area_rows -= 1;
  }

  if (log_area_rows <= 0) {
    if (grep_entering) {
      interactive_grep_render_input_line(g_cached_term_size.cols);
    }
    fflush(stdout);
    return;
  }

  // Fetch and filter logs
  session_log_entry_t *log_entries = NULL;
  size_t log_count = 0;

  if (interactive_grep_is_active()) {
    asciichat_error_t result = interactive_grep_gather_and_filter_logs(&log_entries, &log_count);
    if (result != ASCIICHAT_OK || !log_entries) {
      log_entries = SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(session_log_entry_t), session_log_entry_t *);
      if (log_entries) {
        log_count = session_log_buffer_get_recent(log_entries, SESSION_LOG_BUFFER_SIZE);
      }
    }
  } else {
    log_entries = SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(session_log_entry_t), session_log_entry_t *);
    if (log_entries) {
      log_count = session_log_buffer_get_recent(log_entries, SESSION_LOG_BUFFER_SIZE);
    }
  }

  // Calculate which logs fit (working backwards from most recent)
  int total_lines_needed = 0;
  int first_log_to_display = (log_count > 0) ? (int)log_count - 1 : 0;

  for (int i = (int)log_count - 1; i >= 0; i--) {
    const char *msg = log_entries[i].message;
    int msg_display_width = display_width(msg);
    if (msg_display_width < 0) {
      msg_display_width = (int)strlen(msg);
    }

    int lines_for_this_log = 1;
    if (msg_display_width > g_cached_term_size.cols) {
      lines_for_this_log = (msg_display_width + g_cached_term_size.cols - 1) / g_cached_term_size.cols;
    }

    if (total_lines_needed + lines_for_this_log > log_area_rows) {
      first_log_to_display = i + 1;
      break;
    }

    total_lines_needed += lines_for_this_log;
    first_log_to_display = i;
  }

  if (!grep_entering) {
    // Normal mode: just print everything (screen was cleared)
    for (int i = first_log_to_display; i < (int)log_count; i++) {
      fprintf(stdout, "%s\n", log_entries[i].message);
    }

    // Fill remaining lines
    int remaining = log_area_rows - total_lines_needed;
    for (int i = 0; i < remaining; i++) {
      fprintf(stdout, "\n");
    }
  } else {
    // Grep mode: diff-based rendering. Only rewrite lines that changed.
    // For unchanged lines, just move past them. For changed or new lines,
    // write content + \x1b[K. For blank lines, just \x1b[K.

    int log_idx = 0;
    int lines_used = 0;

    for (int i = first_log_to_display; i < (int)log_count; i++) {
      const char *msg = log_entries[i].message;
      int msg_display_width = display_width(msg);
      if (msg_display_width < 0) {
        msg_display_width = (int)strlen(msg);
      }
      int lines_for_this = 1;
      if (msg_display_width > g_cached_term_size.cols) {
        lines_for_this = (msg_display_width + g_cached_term_size.cols - 1) / g_cached_term_size.cols;
      }

      // Check if this log line is the same as what we rendered last frame
      bool same_as_before = (log_idx < g_prev_log_count && g_prev_log_ptrs[log_idx] == msg);

      if (same_as_before) {
        // Content unchanged - skip past it without rewriting.
        // Move cursor down by the number of terminal lines this log occupies.
        if (lines_for_this == 1) {
          fprintf(stdout, "\n");
        } else {
          fprintf(stdout, "\x1b[%dB", lines_for_this);
        }
      } else {
        // Content changed - overwrite and clear tail
        fprintf(stdout, "%s\x1b[K\n", msg);
      }

      // Cache this pointer for next frame
      if (log_idx < MAX_CACHED_LINES) {
        g_prev_log_ptrs[log_idx] = msg;
      }
      log_idx++;
      lines_used += lines_for_this;
    }

    // Clear remaining blank lines (stale content from previous render)
    int remaining = log_area_rows - lines_used;
    for (int i = 0; i < remaining; i++) {
      fprintf(stdout, "\x1b[K\n");
    }

    // If previous frame had more total terminal lines than now, clear those too.
    // (Already handled by the remaining loop above since we fill to log_area_rows)

    g_prev_log_count = log_idx;
    g_prev_total_lines = lines_used;

    // Render grep input on the bottom line
    int last_row = g_cached_term_size.rows;
    char pos_buf[32];
    int len = snprintf(pos_buf, sizeof(pos_buf), "\x1b[%d;1H\x1b[K", last_row);
    platform_write_all(STDOUT_FILENO, pos_buf, len);
    interactive_grep_render_input_line(g_cached_term_size.cols);
  }

  SAFE_FREE(log_entries);
  fflush(stdout);
}
