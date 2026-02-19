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
#include "ascii-chat/log/interactive_grep.h"
#include "ascii-chat/session/session_log_buffer.h"
#include "ascii-chat/log/file_parser.h"
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

  if (log_area_rows <= 0) {
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

      // Try to tail log file if specified to merge with memory buffer
      session_log_entry_t *file_entries = NULL;
      size_t file_count = 0;
      const char *log_file = GET_OPTION(log_file);
      if (log_file && log_file[0] != '\0') {
        // Tail last 100KB of log file
        file_count = log_file_parser_tail(log_file, 100 * 1024, &file_entries, SESSION_LOG_BUFFER_SIZE / 2);
      }

      // Merge and deduplicate if we have file entries
      if (file_count > 0) {
        // Ensure colors are initialized before recoloring file logs
        log_init_colors();

        session_log_entry_t *merged_entries = NULL;
        size_t merged_count =
            log_file_parser_merge_and_dedupe(buffer_entries, buffer_count, file_entries, file_count, &merged_entries);
        SAFE_FREE(buffer_entries);
        SAFE_FREE(file_entries);

        // Cap merged entries at SESSION_LOG_BUFFER_SIZE to prevent buffer overflow
        if (merged_count > SESSION_LOG_BUFFER_SIZE) {
          merged_count = SESSION_LOG_BUFFER_SIZE;
        }

        log_entries = merged_entries;
        log_count = merged_count;
      } else {
        SAFE_FREE(file_entries);
        log_entries = buffer_entries;
        log_count = buffer_count;
      }
    }
  }

  // Calculate which logs fit (working backwards from most recent).
  // Use renderable_log_rows so that entering grep mode doesn't change
  // which logs are selected - only the bottom line changes from log to input.
  int total_lines_needed = 0;
  int first_log_to_display = (log_count > 0) ? (int)log_count - 1 : 0;

  for (int i = (int)log_count - 1; i >= 0; i--) {
    const char *msg = log_entries[i].message;
    int msg_display_width = display_width(msg);
    if (msg_display_width < 0) {
      msg_display_width = (int)strlen(msg);
    }

    int lines_for_this_log = 1;
    if (g_cached_term_size.cols > 0 && msg_display_width > g_cached_term_size.cols) {
      lines_for_this_log = (msg_display_width + g_cached_term_size.cols - 1) / g_cached_term_size.cols;
    }

    if (total_lines_needed + lines_for_this_log > renderable_log_rows) {
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

    // Fill remaining lines (use full log_area_rows)
    int remaining = log_area_rows - total_lines_needed;
    for (int i = 0; i < remaining; i++) {
      fprintf(stdout, "\n");
    }
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
    int pos = snprintf(grep_ui_buffer, sizeof(grep_ui_buffer), "\x1b[%d;1H\x1b[0m\x1b[K",
                       g_cached_term_size.rows);

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
          int remaining = snprintf(grep_ui_buffer + pos, sizeof(grep_ui_buffer) - (size_t)pos,
                                   "/%.*s", pattern_len, pattern);
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
