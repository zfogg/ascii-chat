#pragma once

/**
 * @file image2ascii/output_buffer.h
 * @ingroup module_video
 * @brief Dynamic Output Buffer with ANSI Sequence Support
 *
 * This header provides a dynamic output buffer system with automatic expansion
 * and optimized ANSI escape sequence emission functions. The buffer is
 * designed for efficient ASCII art frame construction and terminal output.
 *
 * CORE FEATURES:
 * ==============
 * - Auto-expanding output buffer (grows as needed)
 * - Efficient buffer management operations
 * - Optimized ANSI escape sequence emission
 * - Run-length encoding (RLE) support for compression
 * - Color mode auto-selection (16-color, 256-color, truecolor)
 * - Integer-to-string conversion utilities
 *
 * BUFFER MANAGEMENT:
 * ==================
 * The output buffer automatically expands when space is needed:
 * - Initial capacity is allocated on first reservation
 * - Buffer doubles in size when capacity is exceeded
 * - Memory is allocated on-demand (no upfront allocation)
 * - Buffer ownership: caller must free buffer when done
 *
 * ANSI ESCAPE SEQUENCES:
 * =====================
 * The system emits ANSI escape sequences for:
 * - Color output (foreground and background)
 * - Cursor control and positioning
 * - Reset and formatting codes
 * - Run-length encoding for repeated characters
 *
 * COLOR MODE AUTO-SELECTION:
 * ==========================
 * Color emission functions automatically select optimal mode:
 * - 16-color: Uses standard ANSI color codes (0-15)
 * - 256-color: Uses extended palette (16-255)
 * - Truecolor: Uses 24-bit RGB escape sequences
 *
 * @note All buffer operations ensure null-terminated strings.
 * @note Buffer memory must be freed by caller using free() on buf pointer.
 * @note ANSI sequences are optimized for minimal terminal I/O.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Data Structures
 * ============================================================================
 */

/**
 * @brief Dynamic output buffer (auto-expanding)
 *
 * Represents a dynamic output buffer that automatically expands when more
 * space is needed. Buffer grows exponentially (doubles in size) to minimize
 * reallocation overhead.
 *
 * MEMORY MANAGEMENT:
 * - buf: Points to allocated buffer (NULL if not yet allocated)
 * - len: Current number of bytes used in buffer
 * - cap: Current buffer capacity in bytes
 * - Buffer must be freed by caller using free(buf) when done
 *
 * @note Buffer is null-terminated after each append operation.
 * @note Buffer grows automatically when space is needed.
 *
 * @ingroup module_video
 */
typedef struct {
  char *buf;  ///< Buffer pointer (allocated, owned by caller, must be freed)
  size_t len; ///< Current length in bytes (excluding null terminator)
  size_t cap; ///< Buffer capacity in bytes (maximum length before reallocation)
} outbuf_t;

/* ============================================================================
 * Buffer Management Functions
 * @{
 */

/**
 * @brief Reserve buffer space for upcoming writes
 * @param ob Output buffer structure (must not be NULL)
 * @param need Minimum bytes needed (must be > 0)
 *
 * Ensures the buffer has at least 'need' bytes of available space.
 * Automatically expands the buffer if necessary. Buffer grows exponentially
 * (doubles in size) to minimize reallocation overhead.
 *
 * @note Buffer is automatically allocated on first reservation.
 * @note Buffer doubles in size when capacity is exceeded.
 * @note This function does not modify buffer length (only capacity).
 *
 * @ingroup module_video
 */
void ob_reserve(outbuf_t *ob, size_t need);

/**
 * @brief Append a character to buffer
 * @param ob Output buffer structure (must not be NULL)
 * @param c Character to append
 *
 * Appends a single character to the output buffer. Automatically expands
 * the buffer if necessary. Buffer remains null-terminated after append.
 *
 * @note Buffer is automatically expanded if needed.
 * @note Length is incremented by 1 after append.
 * @note Null terminator is updated automatically.
 *
 * @ingroup module_video
 */
void ob_putc(outbuf_t *ob, char c);

/**
 * @brief Append a string to buffer
 * @param ob Output buffer structure (must not be NULL)
 * @param s String to append (must not be NULL)
 * @param n Number of bytes to append (must be > 0)
 *
 * Appends n bytes from string s to the output buffer. Automatically expands
 * the buffer if necessary. Buffer remains null-terminated after append.
 *
 * @note Buffer is automatically expanded if needed.
 * @note Length is incremented by n after append.
 * @note Null terminator is updated automatically.
 * @note This function copies n bytes (may include embedded nulls).
 *
 * @ingroup module_video
 */
void ob_write(outbuf_t *ob, const char *s, size_t n);

/**
 * @brief Append null terminator to buffer
 * @param ob Output buffer structure (must not be NULL)
 *
 * Ensures the buffer is null-terminated by appending a null character.
 * Does not increment length counter (null terminator is not counted).
 * Useful for ensuring buffer is a valid C string.
 *
 * @note This function ensures buffer[len] == '\0'.
 * @note Length counter is not modified (null terminator excluded from length).
 *
 * @ingroup module_video
 */
void ob_term(outbuf_t *ob);

/** @} */

/* ============================================================================
 * Integer Formatting Functions
 * @{
 */

/**
 * @brief Append unsigned 8-bit integer as decimal string
 * @param ob Output buffer structure (must not be NULL)
 * @param v Value to append (0-255)
 *
 * Converts an unsigned 8-bit integer to decimal string representation
 * and appends it to the buffer. Useful for formatting small numeric
 * values (color indices, counts, etc.).
 *
 * @note Buffer is automatically expanded if needed.
 * @note String representation is appended (1-3 characters for 0-255).
 *
 * @ingroup module_video
 */
void ob_u8(outbuf_t *ob, uint8_t v);

/**
 * @brief Append unsigned 32-bit integer as decimal string
 * @param ob Output buffer structure (must not be NULL)
 * @param v Value to append (0 to 4294967295)
 *
 * Converts an unsigned 32-bit integer to decimal string representation
 * and appends it to the buffer. Useful for formatting numeric values
 * (dimensions, counts, timestamps, etc.).
 *
 * @note Buffer is automatically expanded if needed.
 * @note String representation is appended (1-10 characters for 0-4294967295).
 *
 * @ingroup module_video
 */
void ob_u32(outbuf_t *ob, uint32_t v);

/** @} */

/* ============================================================================
 * ANSI Escape Sequence Emission Functions
 * @{
 */

/**
 * @brief Emit truecolor foreground ANSI sequence
 * @param ob Output buffer structure (must not be NULL)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 *
 * Emits ANSI escape sequence for 24-bit truecolor foreground color.
 * Format: ESC[38;2;r;g;bm (where r, g, b are RGB values).
 * Uses 24-bit RGB color values for optimal color accuracy.
 *
 * @note Requires terminal truecolor support for proper display.
 * @note Sequence length: ~15 bytes (ESC[38;2;255;255;255m).
 *
 * @ingroup module_video
 */
void emit_set_truecolor_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Emit truecolor background ANSI sequence
 * @param ob Output buffer structure (must not be NULL)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 *
 * Emits ANSI escape sequence for 24-bit truecolor background color.
 * Format: ESC[48;2;r;g;bm (where r, g, b are RGB values).
 * Uses 24-bit RGB color values for optimal color accuracy.
 *
 * @note Requires terminal truecolor support for proper display.
 * @note Sequence length: ~15 bytes (ESC[48;2;255;255;255m).
 *
 * @ingroup module_video
 */
void emit_set_truecolor_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Emit 256-color foreground ANSI sequence
 * @param ob Output buffer structure (must not be NULL)
 * @param color_idx 256-color palette index (0-255)
 *
 * Emits ANSI escape sequence for 256-color foreground color.
 * Format: ESC[38;5;idxm (where idx is palette index 0-255).
 * Uses extended ANSI color palette for 256-color mode.
 *
 * @note Requires terminal 256-color support for proper display.
 * @note Sequence length: ~9 bytes (ESC[38;5;255m).
 * @note Palette indices 0-15 are standard ANSI colors.
 * @note Palette indices 16-255 are extended palette.
 *
 * @ingroup module_video
 */
void emit_set_256_color_fg(outbuf_t *ob, uint8_t color_idx);

/**
 * @brief Emit 256-color background ANSI sequence
 * @param ob Output buffer structure (must not be NULL)
 * @param color_idx 256-color palette index (0-255)
 *
 * Emits ANSI escape sequence for 256-color background color.
 * Format: ESC[48;5;idxm (where idx is palette index 0-255).
 * Uses extended ANSI color palette for 256-color mode.
 *
 * @note Requires terminal 256-color support for proper display.
 * @note Sequence length: ~9 bytes (ESC[48;5;255m).
 * @note Palette indices 0-15 are standard ANSI colors.
 * @note Palette indices 16-255 are extended palette.
 *
 * @ingroup module_video
 */
void emit_set_256_color_bg(outbuf_t *ob, uint8_t color_idx);

/**
 * @brief Emit ANSI reset sequence
 * @param ob Output buffer structure (must not be NULL)
 *
 * Emits ANSI escape sequence to reset all terminal attributes to defaults.
 * Format: ESC[0m or ESC[m. Resets colors, bold, underline, and other
 * formatting attributes.
 *
 * @note This resets both foreground and background colors to default.
 * @note Sequence length: 3 bytes (ESC[0m).
 * @note Useful for resetting terminal state between frames.
 *
 * @ingroup module_video
 */
void emit_reset(outbuf_t *ob);

/** @} */

/* ============================================================================
 * Run-Length Encoding (RLE) Functions
 * @{
 */

/**
 * @brief Check if run-length encoding is profitable
 * @param runlen Run length to check (number of repeated characters)
 * @return true if RLE is profitable, false otherwise
 *
 * Determines whether run-length encoding (RLE) would reduce output size
 * compared to emitting individual characters. RLE is profitable when the
 * overhead of the repeat sequence is less than the characters it replaces.
 *
 * RLE PROFITABILITY:
 * - RLE uses format: ESC[runs (e.g., ESC[100X repeats 'X' 100 times)
 * - Overhead: ~6-8 bytes for repeat sequence
 * - Profit: runlen - overhead bytes saved
 * - Profitable when runlen > ~10-15 characters
 *
 * @note Returns true if runlen exceeds RLE overhead threshold.
 * @note Threshold is tuned for typical ASCII art character runs.
 *
 * @ingroup module_video
 */
bool rep_is_profitable(uint32_t runlen);

/**
 * @brief Emit run-length encoded sequence
 * @param ob Output buffer structure (must not be NULL)
 * @param extra Additional characters to repeat (must be > 0)
 *
 * Emits an ANSI run-length encoding sequence that repeats the last
 * character in the buffer 'extra' times. Format: ESC[extram (where
 * 'extra' is the repeat count and the last character is repeated).
 *
 * @note This function repeats the character at buffer[len-1].
 * @note Useful for compressing repeated characters (spaces, blocks, etc.).
 * @note Only profitable for runs exceeding threshold (see rep_is_profitable()).
 *
 * @par Example
 * @code
 * ob_putc(ob, ' ');  // Append space
 * if (rep_is_profitable(50)) {
 *     emit_rep(ob, 49);  // Repeat space 49 more times (total 50)
 * }
 * @endcode
 *
 * @ingroup module_video
 */
void emit_rep(outbuf_t *ob, uint32_t extra);

/** @} */

/* ============================================================================
 * Auto-Select Color Mode Functions
 * @{
 */

/**
 * @brief Emit foreground color sequence (auto-select mode)
 * @param ob Output buffer structure (must not be NULL)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 *
 * Emits foreground color ANSI sequence using the optimal color mode
 * based on terminal capabilities. Automatically selects:
 * - Truecolor mode (if supported): 24-bit RGB
 * - 256-color mode (if supported): Quantized to 256-color palette
 * - 16-color mode (fallback): Quantized to standard ANSI colors
 *
 * @note Color mode is selected based on terminal_capabilities_t configuration.
 * @note This function uses the best available color mode for the terminal.
 * @note Color quantization is applied when truecolor is not available.
 *
 * @ingroup module_video
 */
void emit_set_fg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Emit background color sequence (auto-select mode)
 * @param ob Output buffer structure (must not be NULL)
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 *
 * Emits background color ANSI sequence using the optimal color mode
 * based on terminal capabilities. Automatically selects:
 * - Truecolor mode (if supported): 24-bit RGB
 * - 256-color mode (if supported): Quantized to 256-color palette
 * - 16-color mode (fallback): Quantized to standard ANSI colors
 *
 * @note Color mode is selected based on terminal_capabilities_t configuration.
 * @note This function uses the best available color mode for the terminal.
 * @note Color quantization is applied when truecolor is not available.
 *
 * @ingroup module_video
 */
void emit_set_bg(outbuf_t *ob, uint8_t r, uint8_t g, uint8_t b);

/** @} */

/* ============================================================================
 * Utility Functions
 * ============================================================================
 */

/**
 * @brief Calculate number of decimal digits for a 32-bit unsigned integer
 * @param v Value to calculate digits for (0 to 4294967295)
 * @return Number of decimal digits (1-10)
 *
 * Calculates the number of decimal digits required to represent a 32-bit
 * unsigned integer as a string. Optimized for REP (repeat) count formatting
 * where digit count is needed for buffer reservation.
 *
 * @note This is an inline function for performance (inlined at compile time).
 * @note Returns 1 for values 0-9, 10 for value 4294967295.
 * @note Used internally for efficient RLE sequence formatting.
 *
 * @par Example
 * @code
 * int digits = digits_u32(12345);  // Returns 5
 * int digits = digits_u32(0);       // Returns 1
 * @endcode
 *
 * @ingroup module_video
 */
static inline int digits_u32(uint32_t v) {
  if (v >= 1000000000u)
    return 10;
  if (v >= 100000000u)
    return 9;
  if (v >= 10000000u)
    return 8;
  if (v >= 1000000u)
    return 7;
  if (v >= 100000u)
    return 6;
  if (v >= 10000u)
    return 5;
  if (v >= 1000u)
    return 4;
  if (v >= 100u)
    return 3;
  if (v >= 10u)
    return 2;
  return 1;
}
