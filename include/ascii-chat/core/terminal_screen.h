/**
 * @file core/terminal_screen.h
 * @brief Reusable "fixed header + scrolling logs" terminal screen abstraction
 *
 * Provides a common pattern for rendering terminal screens with:
 * - Fixed header area (caller-defined via callback)
 * - Scrolling log feed below header (automatically managed)
 * - Terminal size caching to avoid error log spam
 * - ANSI-aware line wrapping using display_width()
 * - Latest log at bottom (standard terminal behavior)
 */

#pragma once

#include <ascii-chat/platform/terminal.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Callback to render the fixed header portion of the screen
 *
 * @param term_size Current terminal dimensions (cached, refreshed every 1 second)
 * @param user_data Caller-provided context data
 *
 * The callback should:
 * - Print exactly the number of lines specified in terminal_screen_config_t.fixed_header_lines
 * - Use display_width() to ensure lines don't exceed term_size.cols
 * - NOT clear the screen (terminal_screen_render does that)
 * - NOT print the final newline if it would be line N+1 (causes scroll)
 */
typedef void (*terminal_screen_header_fn)(terminal_size_t term_size, void *user_data);

/**
 * @brief Configuration for terminal screen rendering
 */
typedef struct {
  int fixed_header_lines;                  ///< How many lines the header takes (e.g., 4 for status, 8 for splash)
  terminal_screen_header_fn render_header; ///< Callback to draw header content
  void *user_data;                         ///< Passed to render_header callback
  bool show_logs;                          ///< Whether to show log feed below header
} terminal_screen_config_t;

/**
 * @brief Render a terminal screen with fixed header and scrolling logs
 *
 * Renders a screen following the pattern:
 * 1. Clear screen and move cursor to home (both stdout and stderr)
 * 2. Call render_header() callback to draw fixed header
 * 3. Calculate log area: rows - fixed_header_lines - 1 (prevent scroll)
 * 4. Fetch recent logs from session_log_buffer
 * 5. Calculate which logs fit (working backwards, accounting for wrapping)
 * 6. Display logs chronologically (oldest to newest, latest at bottom)
 * 7. Fill remaining lines to prevent terminal scroll
 * 8. Flush stdout
 *
 * @param config Screen configuration (header callback, line count, etc.)
 *
 * Terminal size is cached internally with 1-second refresh interval to avoid
 * flooding error logs if terminal size checks fail repeatedly.
 */
void terminal_screen_render(const terminal_screen_config_t *config);
