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
#include "ascii-chat/log/log.h"
#include "ascii-chat/options/options.h"
#include "ascii-chat/common.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

// Module-level static frame buffer (reused across renders to avoid malloc per frame)
static frame_buffer_t *g_frame_buf = NULL;

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
static uint64_t g_last_term_size_check_us = UINT64_MAX; // Force check on first call
#define TERM_SIZE_CHECK_INTERVAL_US US_PER_SEC_INT      // Check terminal size max once per second

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

static uint64_t g_render_start_time_ns = 0;

void terminal_screen_render(const terminal_screen_config_t *config) {
  // Validate config
  if (!config || !config->render_header) {
    return;
  }

  // Track startup timing (first 500ms)
  static bool first_render = true;
  if (first_render) {
    g_render_start_time_ns = platform_get_monotonic_time_us() * 1000;
    first_render = false;
  }

  uint64_t now_ns = platform_get_monotonic_time_us() * 1000;
  uint64_t elapsed_ms = (now_ns - g_render_start_time_ns) / 1000000;

  // Ensure cursor is visible for log-only UI (splash, status screens)
  (void)terminal_cursor_show();

  // Update terminal size (cached with 1-second refresh interval)
  // Always check on first call (when still at default 24x80) to get correct dimensions immediately
  uint64_t now_us = platform_get_monotonic_time_us();
  bool should_check = (g_cached_term_size.rows == 24 && g_cached_term_size.cols == 80) ||
                      (now_us - g_last_term_size_check_us >= TERM_SIZE_CHECK_INTERVAL_US);

  if (should_check) {
    terminal_size_t new_size = {0};

    // PRIORITY 1: COLUMNS and ROWS environment variables (most reliable, user-controlled)
    const char *cols_env = getenv("COLUMNS");
    const char *rows_env = getenv("ROWS");
    if (cols_env && rows_env) {
      int cols = atoi(cols_env);
      int rows = atoi(rows_env);
      if (cols > 0 && rows > 0) {
        new_size.cols = cols;
        new_size.rows = rows;
      }
    }

    // PRIORITY 2: Options-provided width/height (explicit user settings)
    if (new_size.cols == 0 || new_size.rows == 0) {
      const options_t *opts = options_get();
      if (opts && opts->width > 0 && opts->height > 0) {
        new_size.cols = opts->width;
        new_size.rows = opts->height;
      }
    }

    // PRIORITY 3: Terminal auto-detection
    if (new_size.cols == 0 || new_size.rows == 0) {
      if (terminal_get_size(&new_size) != ASCIICHAT_OK) {
        // If terminal detection fails, use defaults
        new_size.cols = 80;
        new_size.rows = 24;
      }
    }

    if (new_size.cols > 0 && new_size.rows > 0) {
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

  // Allocate or reuse frame buffer (module-level static to avoid malloc per frame)
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

  // Measure actual header height by pre-rendering and counting display lines
  int actual_header_height = config->fixed_header_lines;

  if (config->render_header) {
    // Pre-render header to a temporary buffer to measure its display height
    frame_buffer_t *temp_buf = frame_buffer_create(config->fixed_header_lines + 10, g_cached_term_size.cols);
    if (temp_buf) {
      config->render_header(temp_buf, g_cached_term_size, config->user_data);

      // Measure display height of the rendered header
      // The buffer contains ANSI codes + text. We count newlines and account for wrapped lines.
      int measured_height = 0;
      const char *content = frame_buffer_get_content(temp_buf);
      size_t buf_len = frame_buffer_get_length(temp_buf);
      const char *buf_end = content + buf_len;
      const char *line_start = content;

      while (content < buf_end) {
        if (*content == '\n') {
          // Found end of line - measure this line's display height
          size_t line_len = content - line_start;
          char line_buf[1024];
          if (line_len < sizeof(line_buf)) {
            strncpy(line_buf, line_start, line_len);
            line_buf[line_len] = '\0';

            int line_height = display_height(line_buf, g_cached_term_size.cols);
            if (line_height <= 0)
              line_height = 1;
            measured_height += line_height;
          }
          line_start = content + 1;
        }
        content++;
      }

      if (measured_height > 0) {
        actual_header_height = measured_height;
      }
      frame_buffer_destroy(temp_buf);
    }
  }

  // Calculate log area: total rows - actual header height - 1 (prevent scroll)
  int log_area_rows = g_cached_term_size.rows - actual_header_height - 1;

  if (log_area_rows <= 0) {
    // Terminal too small for logs, flush header and return
    frame_buffer_flush(g_frame_buf);
    if (grep_entering) {
      interactive_grep_render_input_line(g_cached_term_size.cols);
    }
    return;
  }

  // When grep input is active, reserve 1 row for the grep input line at bottom.
  // Subtract 1 from log_area_rows to prevent logs from overlapping the grep input.
  int renderable_log_rows = grep_entering ? (log_area_rows - 1) : log_area_rows;

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

  // Display logs - most recent at bottom, oldest scrolled off top.
  // Calculate a stable window: show logs that fit in log_area_rows, newest last.
  // This ensures smooth scrolling as new logs arrive (no jumping viewport).
  // CRITICAL: account for multi-line logs via display_height() in pre-selection.
  // Without this, logs overflow and push the header off screen.
  int first_log_to_display = log_count; // Start past the end (no logs selected)
  int total_lines_used = 0;

  // Work backwards from newest to oldest log, selecting which fit
  for (int i = (int)log_count - 1; i >= 0; i--) {
    int lines_for_msg = display_height(log_entries[i].message, g_cached_term_size.cols);
    if (lines_for_msg <= 0) {
      lines_for_msg = 1;
    }
    // Check if adding this log would exceed available space
    if (total_lines_used + lines_for_msg <= log_area_rows) {
      total_lines_used += lines_for_msg;
      first_log_to_display = i; // This is the first (oldest) log we'll display
    }
    // Continue checking older logs - they might be shorter and still fit
  }

  // If no logs fit, show nothing (just padding)
  if (first_log_to_display == (int)log_count) {
    first_log_to_display = log_count; // Will result in 0 logs to display
  }

  int logs_to_display = log_count - first_log_to_display;

  // Log detailed info about what logs are being displayed
  int logs_displayed_count = logs_to_display;
  if (elapsed_ms < 500) {
    log_dev_every(1 * NS_PER_SEC_INT, "[STARTUP_FRAME] t=%llu log_count=%zu first=%d display=%d area=%d",
                  (unsigned long long)elapsed_ms, log_count, first_log_to_display, logs_displayed_count, log_area_rows);
  } else {
    log_debug_every(1 * NS_PER_SEC_INT,
                    "[HEIGHT_CALC] log_count=%zu first_log_to_display=%d logs_displayed=%d "
                    "renderable_log_rows=%d",
                    log_count, first_log_to_display, logs_displayed_count, renderable_log_rows);
  }

  if (!grep_entering) {
    // Normal mode: accumulate everything (header + logs + blank lines) into frame buffer,
    // then flush atomically. This prevents real-time logs from appearing after the frame,
    // which would cause the display to scroll and push the header up.

    // Output logs to frame buffer, counting actual display lines (accounting for wrapping)
    int terminal_lines_used = 0;
    for (int i = first_log_to_display; i < (int)log_count && terminal_lines_used < log_area_rows; i++) {
      const char *msg = log_entries[i].message;

      // Calculate how many display lines this message will consume when wrapped
      // Uses display_height() which handles ANSI codes and UTF-8 properly
      int lines_for_this_msg = display_height(msg, g_cached_term_size.cols);
      if (lines_for_this_msg <= 0) {
        lines_for_this_msg = 1;
      }

      // Only output if it fits in remaining space
      if (terminal_lines_used + lines_for_this_msg <= log_area_rows) {
        frame_buffer_printf(g_frame_buf, "%s\n", msg);
        terminal_lines_used += lines_for_this_msg;
      }
    }

    // Pad remaining log area with blank lines to maintain fixed frame height.
    // This ensures every frame has exactly the same height on screen,
    // preventing flicker caused by dynamic frame height changes.
    while (terminal_lines_used < log_area_rows) {
      frame_buffer_printf(g_frame_buf, "\n");
      terminal_lines_used++;
    }

    // Flush entire frame (header + logs + blank lines) in one atomic write
    frame_buffer_flush(g_frame_buf);
  } else {
    // Grep mode: diff-based rendering. Only rewrite lines that changed.
    // Logs fill renderable_log_rows; the last row is the `/` input line.

    // Flush frame buffer (containing header) before rendering grep logs
    frame_buffer_flush(g_frame_buf);

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

    // Validate terminal size before using it in formatting
    if (g_cached_term_size.rows <= 0 || g_cached_term_size.rows > 9999) {
      // Invalid terminal size - skip grep rendering to avoid malformed output
      return;
    }

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
        int cursor_pos = interactive_grep_get_cursor_position();

        // Validate pattern_len and cursor_pos are within reasonable bounds
        if (pattern_len < 0 || pattern_len > 256) {
          pattern_len = 0;
          pattern = NULL;
        }
        if (cursor_pos < 0 || cursor_pos > pattern_len) {
          cursor_pos = pattern_len;
        }

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

        // Position cursor at the current edit position within the pattern
        // After writing "/{pattern}", cursor is at column = 1 + pattern_len + 1
        // We want it at column = 1 + cursor_pos + 1 (for "/" at column 1, then cursor_pos chars)
        // So move left by: pattern_len - cursor_pos columns
        int cursor_offset = pattern_len - cursor_pos;
        if (cursor_offset > 0) {
          int remaining =
              snprintf(grep_ui_buffer + pos, sizeof(grep_ui_buffer) - (size_t)pos, "\x1b[%dD", cursor_offset);
          if (remaining > 0 && remaining < (int)(sizeof(grep_ui_buffer) - (size_t)pos)) {
            pos += remaining;
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

void terminal_screen_cleanup(void) {
  if (g_frame_buf) {
    frame_buffer_destroy(g_frame_buf);
    g_frame_buf = NULL;
  }
}

void terminal_screen_log_init(void) {
  session_log_buffer_init();
}

void terminal_screen_log_destroy(void) {
  session_log_buffer_destroy();
}

void terminal_screen_log_clear(void) {
  session_log_buffer_clear();
}

void terminal_screen_log_append(const char *message) {
  session_log_buffer_append(message);
}
