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

/**
 * @brief Calculate how many display lines text will consume when wrapped
 *
 * Splits text by newlines and calculates how many terminal rows it will
 * occupy when wrapped to the given terminal width. Properly handles ANSI
 * escape codes (ignored in width calculation) and UTF-8 multi-byte characters.
 *
 * @param text Input text which may contain ANSI codes and newlines (NULL-safe)
 * @param terminal_width Width available for wrapping (e.g., 80)
 * @return Number of display lines consumed, or 0 if text is empty
 *
 * @par Example
 * @code
 * // "Hello\nWorld" in 80-char terminal = 2 lines
 * int height = display_height("Hello\nWorld", 80);  // Returns 2
 *
 * // "This is a very long line..." wrapped to 40 chars
 * int height = display_height("This is a very long line that exceeds...", 40);
 * // Returns 2-3 depending on exact length
 *
 * // With ANSI codes: "\033[1;36m" is not counted in width
 * int height = display_height("\033[1;36m50-char line\033[0m", 40);
 * // Returns 2 (50 visible chars / 40 width)
 * @endcode
 *
 * @ingroup util
 */
int display_height(const char *text, int terminal_width);
