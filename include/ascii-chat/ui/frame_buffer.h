/**
 * @file ui/frame_buffer.h
 * @brief Buffered terminal frame rendering
 *
 * Accumulates terminal output (ANSI codes, text, colors) into a single buffer,
 * then flushes the entire frame in one atomic write. This eliminates the flicker
 * that occurs when clearing the screen and rendering line-by-line.
 *
 * Usage pattern:
 *   frame_buffer_t *buf = frame_buffer_create(rows, cols);
 *   frame_buffer_clear_screen(buf);
 *   frame_buffer_printf(buf, "Header line 1\n");
 *   frame_buffer_printf(buf, "Header line 2\n");
 *   frame_buffer_flush(buf);
 *   // Later, reuse the buffer:
 *   frame_buffer_reset(buf);
 *   frame_buffer_printf(buf, "New content\n");
 *   frame_buffer_flush(buf);
 *
 * @ingroup ui
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Opaque frame buffer handle
 */
typedef struct frame_buffer frame_buffer_t;

/**
 * @brief Create a new frame buffer
 *
 * @param rows Terminal row count (used for initial capacity estimate)
 * @param cols Terminal column count (used for initial capacity estimate)
 * @return Newly allocated frame buffer, or NULL on allocation failure
 *
 * Initial capacity is set to `rows * cols * 16` to account for ANSI escape overhead.
 */
frame_buffer_t *frame_buffer_create(int rows, int cols);

/**
 * @brief Destroy a frame buffer
 *
 * @param buf Frame buffer to destroy (may be NULL)
 */
void frame_buffer_destroy(frame_buffer_t *buf);

/**
 * @brief Clear buffer contents while keeping allocation
 *
 * Resets the buffer for a new frame without deallocating the underlying memory.
 * More efficient than destroy/create for repeated frame rendering.
 *
 * @param buf Frame buffer to reset
 */
void frame_buffer_reset(frame_buffer_t *buf);

/**
 * @brief Append raw bytes to the buffer
 *
 * @param buf Frame buffer
 * @param str Pointer to data (may contain ANSI codes, not NULL-terminated)
 * @param len Number of bytes to append
 *
 * Grows the buffer as needed. Does not add newlines or NULL terminators.
 */
void frame_buffer_append(frame_buffer_t *buf, const char *str, size_t len);

/**
 * @brief Printf-style append to the buffer
 *
 * @param buf Frame buffer
 * @param fmt Format string (standard printf syntax)
 * @param ... Arguments
 *
 * Formats the string into the buffer using snprintf. Grows buffer as needed.
 */
void frame_buffer_printf(frame_buffer_t *buf, const char *fmt, ...);

/**
 * @brief Append cursor home escape code (\033[H)
 *
 * @param buf Frame buffer
 */
void frame_buffer_cursor_home(frame_buffer_t *buf);

/**
 * @brief Append clear screen and home escape codes (\033[2J\033[H)
 *
 * @param buf Frame buffer
 *
 * Combines clear screen and cursor home into a single operation for atomicity.
 */
void frame_buffer_clear_screen(frame_buffer_t *buf);

/**
 * @brief Flush the entire buffer to stdout in one atomic write
 *
 * @param buf Frame buffer
 *
 * Writes all accumulated content to stdout via platform_write_all(),
 * bypassing libc buffering. Safe for high-frequency rendering.
 */
void frame_buffer_flush(frame_buffer_t *buf);

/**
 * @brief Get the current content length of the frame buffer
 *
 * @param buf Frame buffer
 * @return Number of bytes currently in the buffer
 *
 * Useful for debugging and verifying frame consistency.
 */
size_t frame_buffer_get_length(const frame_buffer_t *buf);

/**
 * @brief Get a pointer to the buffer content
 *
 * @param buf Frame buffer
 * @return Pointer to the buffer data (NULL if buffer is empty or NULL)
 *
 * Returns the raw buffer content for reading. The content is NOT null-terminated.
 * Use frame_buffer_get_length() to get the actual size.
 */
const char *frame_buffer_get_content(const frame_buffer_t *buf);

/**
 * @brief Render a horizontal border line (used by both splash and status screens)
 *
 * @param buf Frame buffer to write into
 * @param cols Terminal width (number of columns)
 * @param color ANSI color code (e.g., "\033[1;36m" for cyan) or NULL for no color
 */
void frame_buffer_render_border(frame_buffer_t *buf, int cols, const char *color);

/**
 * @brief Render centered text (used by both splash and status screens)
 *
 * @param buf Frame buffer to write into
 * @param text Text to center (may contain ANSI codes)
 * @param cols Terminal width
 * @return Padding added on left side (for manual padding if needed)
 *
 * Automatically centers the text accounting for ANSI escape codes.
 */
int frame_buffer_render_centered(frame_buffer_t *buf, const char *text, int cols);
