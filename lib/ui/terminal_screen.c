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
#include "ascii-chat/ui/frame_buffer.h"
#include "ascii-chat/log/interactive_grep.h"
#include "session/session_log_buffer.h"
#include "ascii-chat/util/display.h"
#include "ascii-chat/util/time.h"
#include "ascii-chat/platform/system.h"
#include "ascii-chat/platform/abstraction.h"
#include "ascii-chat/platform/mutex.h"
#include "ascii-chat/log/grep.h"
#include "ascii-chat/log/logging.h"
#include "ascii-chat/options/options.h"
#include "ascii-chat/common.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Strip ANSI escape codes from a string (matches map_plain_to_colored_pos logic)
static void strip_ansi_codes(const char *src, char *dst, size_t dst_size) {
  if (!src || !dst || dst_size == 0)
    return;

  size_t pos = 0;
  while (*src && pos < dst_size - 1) {
    if (*src == '\x1b') {
      src++;
      // Check if there's a next byte
      if (*src == '\0') {
        break;
      }
      unsigned char next = (unsigned char)*src;
      if (next == '[') {
        // CSI sequence: \x1b[...final_byte (where final byte is 0x40-0x7E)
        src++;
        while (*src != '\0') {
          unsigned char c = (unsigned char)*src;
          src++;
          if (c >= 0x40 && c <= 0x7E) {
            break;
          }
        }
      } else if (next >= 0x40 && next <= 0x7E) {
        // Fe sequence: \x1b + final_byte (e.g., \x1b7, \x1b8)
        src++;
      } else if (next == '(' || next == ')' || next == '*' || next == '+') {
        // Designate character set sequences: \x1b( + charset (3 bytes total)
        src++; // skip designator
        if (*src != '\0') {
          src++; // skip charset ID
        }
      } else {
        // Unknown escape sequence type, try to skip conservatively
        if (*src != '\0') {
          src++;
        }
      }
    } else {
      dst[pos++] = *src++;
    }
  }
  dst[pos] = '\0';
}

// Cached terminal size (to avoid flooding logs with terminal_get_size errors)
static terminal_size_t g_cached_term_size = {.rows = 24, .cols = 80};
static uint64_t g_last_term_size_check_us = 0;
#define TERM_SIZE_CHECK_INTERVAL_US US_PER_SEC_INT // Check terminal size max once per second

/**
 * @brief Calculate display lines needed for a log message, accounting for newlines and width wrapping
 *
 * Handles:
 * - Multiline messages with embedded newlines
 * - Width-based wrapping when a line exceeds terminal width
 * - ANSI escape sequences that don't count toward display width
 */
static int calculate_log_display_lines(const char *msg, int term_width) {
  if (!msg || term_width <= 0) {
    return 1;
  }

  int total_lines = 0;
  const char *line_start = msg;

  // Process message line by line (split by \n)
  while (*line_start != '\0') {
    // Find end of current line
    const char *line_end = strchr(line_start, '\n');
    if (!line_end) {
      line_end = line_start + strlen(line_start);
    }

    // Calculate display width of this line segment
    int line_length = (int)(line_end - line_start);
    char *line_segment = SAFE_MALLOC(line_length + 1, char *);
    if (!line_segment) {
      return 1; // Fallback
    }

    memcpy(line_segment, line_start, line_length);
    line_segment[line_length] = '\0';

    int segment_display_width = display_width(line_segment);
    if (segment_display_width <= 0) {
      // If display_width fails, assume the line is single-width as fallback
      segment_display_width = 1;
    }

    // Account for width wrapping
    int lines_in_segment = 1;
    if (term_width > 0 && segment_display_width > term_width) {
      lines_in_segment = (segment_display_width + term_width - 1) / term_width;
    }

    total_lines += lines_in_segment;
    SAFE_FREE(line_segment);

    // Move to next line
    if (*line_end == '\n') {
      line_start = line_end + 1;
    } else {
      break;
    }
  }

  return total_lines > 0 ? total_lines : 1;
}

// Cache of previously rendered log lines for diff-based rendering.
// Only rewrite lines whose content actually changed.
#define MAX_CACHED_LINES 256
static const char *g_prev_log_ptrs[MAX_CACHED_LINES];
static int g_prev_log_count = 0;
static int g_prev_total_lines = 0; // Total terminal lines used by logs last frame

static void terminal_screen_clear_cache(void) {
  memset(g_prev_log_ptrs, 0, sizeof(g_prev_log_ptrs));
  g_prev_log_count = 0;
  g_prev_total_lines = 0;
}

void terminal_screen_render(const terminal_screen_config_t *config) {
  // Validate config
  if (!config || !config->render_header) {
    return;
  }

  fprintf(stderr, "[RENDER_CALLED] show_logs=%d fixed_header_lines=%d\n", config->show_logs,
          config->fixed_header_lines);

  // Ensure cursor is visible for log-only UI (splash, status screens)
  (void)terminal_cursor_show();

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

  // When transitioning out of grep mode, clear the log cache so all logs are redrawn
  // (grep mode may have shown filtered logs, need to refresh for normal view)
  static bool last_grep_state = false;
  if (last_grep_state && !grep_entering) {
    terminal_screen_clear_cache();
  }

  // Also clear cache when pattern changes while still in grep mode (e.g., user backspaces to empty)
  if (grep_entering && interactive_grep_needs_rerender()) {
    terminal_screen_clear_cache();
  }

  last_grep_state = grep_entering;

  // Allocate or reuse frame buffer (static to avoid malloc per frame)
  static frame_buffer_t *g_frame_buf = NULL;
  if (!g_frame_buf) {
    g_frame_buf = frame_buffer_create(g_cached_term_size.rows, g_cached_term_size.cols);
    if (!g_frame_buf) {
      return; // Allocation failed
    }
  }

  frame_buffer_reset(g_frame_buf);

  if (!grep_entering) {
    // Normal mode: clear and redraw (all output goes into frame buffer)
    frame_buffer_clear_screen(g_frame_buf);
  } else {
    // Grep mode: overwrite in place, never clear whole screen
    frame_buffer_cursor_home(g_frame_buf);
  }

  // Render fixed header via callback (writes into frame buffer)
  config->render_header(g_frame_buf, g_cached_term_size, config->user_data);

  // If logs are disabled, flush and return
  if (!config->show_logs) {
    frame_buffer_flush(g_frame_buf);
    return;
  }

  // Calculate log area: total rows - header lines - 1 (prevent scroll)
  int log_area_rows = g_cached_term_size.rows - config->fixed_header_lines - 1;
  fprintf(stderr, "[LOG_AREA_CALC] rows=%d header=%d log_area_rows=%d\n", g_cached_term_size.rows,
          config->fixed_header_lines, log_area_rows);

  if (log_area_rows <= 0) {
    fprintf(stderr, "[EARLY_RETURN_LOG_AREA] log_area_rows=%d\n", log_area_rows);
    if (grep_entering) {
      interactive_grep_render_input_line(g_cached_term_size.cols);
    }
    fflush(stdout);
    return;
  }

  // When grep input is active, use full log area for logs since grep input
  // will be rendered on the last row (which is normally reserved for preventing scroll).
  // This maximizes vertical space usage while keeping grep input at the bottom.
  int renderable_log_rows = log_area_rows;

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
    // Get logs from in-memory buffer
    session_log_entry_t *buffer_entries =
        SAFE_MALLOC(SESSION_LOG_BUFFER_SIZE * sizeof(session_log_entry_t), session_log_entry_t *);
    if (buffer_entries) {
      size_t buffer_count = session_log_buffer_get_recent(buffer_entries, SESSION_LOG_BUFFER_SIZE);
      log_entries = buffer_entries;
      log_count = buffer_count;
    }
  }

  // Calculate which logs fit (working backwards from most recent).
  // Use renderable_log_rows so that entering grep mode doesn't change
  // which logs are selected - only the bottom line changes from log to input.
  fprintf(stderr, "[BEFORE_CALC] log_count=%zu log_area_rows=%d\n", log_count, log_area_rows);
  int total_lines_needed = 0;
  int first_log_to_display = (log_count > 0) ? (int)log_count - 1 : 0;

  for (int i = (int)log_count - 1; i >= 0; i--) {
    const char *msg = log_entries[i].message;

    // Calculate display lines properly handling multiline messages and width wrapping
    int lines_for_this_log = calculate_log_display_lines(msg, g_cached_term_size.cols);

    if (total_lines_needed + lines_for_this_log > renderable_log_rows) {
      first_log_to_display = i + 1;
      break;
    }

    total_lines_needed += lines_for_this_log;
    first_log_to_display = i;
  }

  // Log calculated height after computing which logs fit
  int calculated_total_height = total_lines_needed + (log_area_rows - total_lines_needed);
  fprintf(stderr,
          "[CALC_HEIGHT] terminal=%dx%d header=%d log_area_rows=%d total_lines_needed=%d calculated_height=%d\n",
          g_cached_term_size.cols, g_cached_term_size.rows, config->fixed_header_lines, log_area_rows,
          total_lines_needed, calculated_total_height);

  if (!grep_entering) {
    // Normal mode: flush header from frame buffer first
    frame_buffer_flush(g_frame_buf);

    // Output only the logs that fit (don't add extra newlines for wrapping)
    int logs_output = 0;
    for (int i = first_log_to_display; i < (int)log_count; i++) {
      const char *msg = log_entries[i].message;
      log_info("%s", msg);
      logs_output++;
    }

    // Fill remaining lines with blank lines
    int remaining = log_area_rows - total_lines_needed;
    for (int i = 0; i < remaining; i++) {
      log_info("");
    }

    // Log actual height verification for debugging
    int actual_height = logs_output + remaining;
    fprintf(stderr,
            "[ACTUAL_HEIGHT] terminal=%dx%d logs_output=%d remaining=%d actual_height=%d log_area_rows=%d | "
            "MATCH_CALC=%s MATCH_LOG_AREA=%s CONSTANT=%s\n",
            g_cached_term_size.cols, g_cached_term_size.rows, logs_output, remaining, actual_height, log_area_rows,
            (actual_height == calculated_total_height ? "YES" : "NO"), (actual_height == log_area_rows ? "YES" : "NO"),
            (actual_height == calculated_total_height && actual_height == log_area_rows ? "YES" : "NO"));
  } else {
    // Grep mode: diff-based rendering. Only rewrite lines that changed.
    // Logs fill renderable_log_rows; the last row is the `/` input line.

    int log_idx = 0;
    int lines_used = 0;

    for (int i = first_log_to_display; i < (int)log_count; i++) {
      const char *original_msg = log_entries[i].message;
      const char *msg = original_msg;

      // Strip ANSI codes first to match against plain text
      char plain_text[SESSION_LOG_LINE_MAX] = {0};
      strip_ansi_codes(original_msg, plain_text, sizeof(plain_text));

      // Apply highlighting if match found in plain text
      size_t match_start = 0, match_len = 0;
      if (interactive_grep_get_match_info(plain_text, &match_start, &match_len) && match_len > 0) {
        // Validate match is within bounds
        size_t plain_len = strlen(plain_text);
        if (match_start < plain_len && (match_start + match_len) <= plain_len) {
          const char *highlighted = grep_highlight_colored(original_msg, plain_text, match_start, match_len);
          if (highlighted && highlighted[0] != '\0') {
            msg = highlighted;
          }
        }
      }

      int msg_display_width = display_width(msg);
      if (msg_display_width < 0) {
        msg_display_width = (int)strlen(msg);
      }
      int lines_for_this = 1;
      if (g_cached_term_size.cols > 0 && msg_display_width > g_cached_term_size.cols) {
        lines_for_this = (msg_display_width + g_cached_term_size.cols - 1) / g_cached_term_size.cols;
      }

      // When logs are filtered (grep active), they're in a different order than before.
      // Only use cached pointers when NOT filtering to avoid cache misses on every frame.
      // This prevents the "spam" effect where all logs redraw constantly during grep searches.
      bool same_as_before = false;
      if (!interactive_grep_is_active()) {
        same_as_before = (log_idx < g_prev_log_count && g_prev_log_ptrs[log_idx] == original_msg);
      }

      if (same_as_before) {
        // Content unchanged - skip past it without rewriting.
        // Still need to reset colors to prevent them from leaking to next output.
        if (lines_for_this == 1) {
          fprintf(stdout, "\x1b[0m\n");
        } else {
          fprintf(stdout, "\x1b[0m\x1b[%dB", lines_for_this);
        }
      } else {
        // Content changed - overwrite and clear tail.
        // Reset both foreground and background colors to prevent color bleeding
        // to the next line (important for grep UI which follows).
        fprintf(stdout, "%s\x1b[0m\x1b[K\n", msg);
      }

      if (log_idx < MAX_CACHED_LINES) {
        g_prev_log_ptrs[log_idx] = original_msg;
      }
      log_idx++;
      lines_used += lines_for_this;
    }

    // Fill blank lines up to renderable_log_rows. If there's one line of space left,
    // render one more partial log (truncated to fit terminal width) instead of blank line.
    int remaining = renderable_log_rows - lines_used;

    if (remaining >= 1 && first_log_to_display > 0) {
      // Render one more log line above the displayed ones, truncated to fit terminal width
      int prev_idx = first_log_to_display - 1;
      const char *prev_msg = log_entries[prev_idx].message;

      int prev_width = display_width(prev_msg);
      if (prev_width < 0) {
        prev_width = (int)strlen(prev_msg);
      }

      if (prev_width > g_cached_term_size.cols) {
        // Truncate to fit: progressively test shorter substrings until one fits
        int target_width = g_cached_term_size.cols - 3; // Reserve space for ellipsis
        if (target_width <= 0) {
          fprintf(stdout, "...\x1b[K\n");
        } else {
          size_t src_len = strlen(prev_msg);
          bool found = false;

          for (size_t truncate_at = src_len; truncate_at > 0; truncate_at--) {
            char test_buf[SESSION_LOG_LINE_MAX];
            SAFE_STRNCPY(test_buf, prev_msg, truncate_at);
            test_buf[truncate_at] = '\0';

            int test_width = display_width(test_buf);
            if (test_width < 0) {
              test_width = (int)strlen(test_buf);
            }

            if (test_width <= target_width) {
              // Found a length that fits
              fprintf(stdout, "%s...\x1b[K\n", test_buf);
              found = true;
              break;
            }
          }

          if (!found) {
            fprintf(stdout, "...\x1b[K\n");
          }
        }
      } else {
        // Fits without truncation
        fprintf(stdout, "%s\x1b[K\n", prev_msg);
      }

      remaining--;
    }

    // Fill remaining blank lines with color reset to prevent color bleed
    for (int i = 0; i < remaining; i++) {
      fprintf(stdout, "\x1b[0m\x1b[K\n");
    }

    g_prev_log_count = log_idx;
    g_prev_total_lines = lines_used;

    // Flush buffered output before rendering grep UI to ensure correct order
    fflush(stdout);

    // Atomic grep UI rendering: combine cursor positioning and input line into
    // a single write to prevent log output from interrupting the escape sequences.
    // This prevents the race condition where logs appear between the cursor
    // positioning command and the grep input line rendering.
    char grep_ui_buffer[512];
    int pos = snprintf(grep_ui_buffer, sizeof(grep_ui_buffer), "\x1b[%d;1H\x1b[0m\x1b[K", g_cached_term_size.rows);

    // Validate snprintf succeeded and produced expected output
    if (pos > 0 && pos < (int)sizeof(grep_ui_buffer) - 256) {
      // Lock while reading grep state to ensure atomic render
      // Get the search pattern under mutex protection
      mutex_t *grep_mutex = interactive_grep_get_mutex();
      if (grep_mutex) {
        mutex_lock(grep_mutex);
        int pattern_len = interactive_grep_get_input_len();
        const char *pattern = interactive_grep_get_input_buffer();

        if (pattern_len > 0 && pattern) {
          int remaining =
              snprintf(grep_ui_buffer + pos, sizeof(grep_ui_buffer) - (size_t)pos, "/%.*s", pattern_len, pattern);
          if (remaining > 0 && remaining < (int)sizeof(grep_ui_buffer) - (int)pos) {
            pos += remaining;
          }
        } else {
          // No pattern yet - just output the slash
          if (pos + 1 < (int)sizeof(grep_ui_buffer)) {
            grep_ui_buffer[pos++] = '/';
          }
        }
        mutex_unlock(grep_mutex);
      }
    }

    // Write entire grep UI (cursor positioning + input) in single operation
    if (pos > 0 && pos <= (int)sizeof(grep_ui_buffer)) {
      platform_write_all(STDOUT_FILENO, grep_ui_buffer, (size_t)pos);
    }
  }

  SAFE_FREE(log_entries);
}
