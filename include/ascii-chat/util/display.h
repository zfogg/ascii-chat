#pragma once

/**
 * @file util/display.h
 * @brief 🖥️ Display Width and Terminal Centering Utilities
 * @ingroup util
 *
 * Utilities for calculating display width of text with ANSI escape codes
 * and Unicode characters, plus helpers for centering text in terminals.
 *
 * These functions properly handle:
 * - ANSI escape codes (stripped before width calculation)
 * - UTF-8 multi-byte characters and wide characters
 * - Unicode display width (CJK, emoji, combining marks)
 */

#include <stddef.h>

/**
 * @brief Calculate the visible display width of text with ANSI escape codes
 *
 * Strips ANSI escape sequences and calculates the actual terminal display width.
 * Properly handles UTF-8 multi-byte characters and wide characters.
 *
 * @param text Input text which may contain ANSI escape codes (must not be NULL)
 * @return Display width in terminal columns, or -1 on error
 *
 * @par Example
 * @code
 * int width = display_width("\033[1;36mHello\033[0m");  // Returns 5
 * @endcode
 *
 * @ingroup util
 */
int display_width(const char *text);

/**
 * @brief Calculate left padding needed to center text horizontally
 *
 * Calculates how many spaces are needed before text to center it within
 * a given terminal width. Properly handles ANSI escape codes and UTF-8.
 *
 * @param text Text to center (may contain ANSI codes, must not be NULL)
 * @param terminal_width Total width available for centering (e.g., 80)
 * @return Number of spaces to print before text, or 0 if text is wider than terminal
 *
 * @par Example
 * @code
 * // Center "Hello" in 80-char line
 * int padding = display_center_horizontal("\033[1;36mHello\033[0m", 80);
 * // Returns 38 ((80-5)/2 = 37.5, rounds to 38)
 * @endcode
 *
 * @ingroup util
 */
int display_center_horizontal(const char *text, int terminal_width);

/**
 * @brief Calculate top padding needed to vertically center content
 *
 * Calculates how many newlines are needed before content to center it
 * vertically within a terminal of given height.
 *
 * @param content_height Height of content in lines
 * @param terminal_height Total height available (typical: 24-30 lines)
 * @return Number of newlines to print before content
 *
 * @par Example
 * @code
 * // Center 8-line status screen in 24-line terminal
 * int padding = display_center_vertical(8, 24);
 * // Returns 8 ((24-8)/2 = 8 lines)
 * @endcode
 *
 * @ingroup util
 */
int display_center_vertical(int content_height, int terminal_height);
