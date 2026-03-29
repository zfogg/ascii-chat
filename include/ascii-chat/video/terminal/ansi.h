/**
 * @file video/terminal/ansi.h
 * @ingroup video
 * @brief ANSI escape sequence utilities and fast color code generation
 *
 * Provides functions for:
 * - Stripping and detecting ANSI escape sequences
 * - Fast ANSI color code generation with SIMD-accelerated terminal output
 * - Support for truecolor, 256-color, and 16-color modes
 * - Run-length encoding for efficient terminal output
 */

#ifndef ASCIICHAT_VIDEO_TERMINAL_ANSI_H
#define ASCIICHAT_VIDEO_TERMINAL_ANSI_H

#include <ascii-chat/common.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/util/math.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/options/options.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup ansi ANSI Utilities
 * @{
 */

/* ===== ANSI Mode Enumeration ===== */
typedef enum {
  ANSI_MODE_FOREGROUND = 0,
  ANSI_MODE_BACKGROUND,
  ANSI_MODE_FOREGROUND_BACKGROUND,
} ansi_color_mode_t;

/* ===== Run-Length Encoding Context ===== */
typedef struct {
  char *buffer;
  size_t capacity;
  size_t length;
  ansi_color_mode_t mode;
  bool first_pixel;
  uint8_t last_r;
  uint8_t last_g;
  uint8_t last_b;
} ansi_rle_context_t;

/* ===== ANSI Timing ===== */
typedef struct {
  uint64_t start_ns;
  uint64_t end_ns;
} ansi_timing_t;

/* ===== RGB Error for Dithering ===== */
typedef struct {
  int r;
  int g;
  int b;
} rgb_error_t;

/* ===== Basic ANSI Utilities ===== */

/**
 * @brief Strip all ANSI escape sequences from a string
 *
 * Removes all ANSI CSI sequences (ESC [ ... final_byte) from the input,
 * leaving only printable text. Useful for creating plain text output
 * from colorized ASCII art.
 *
 * @param input Input string containing ANSI escape sequences
 * @param input_len Length of input string
 * @return Newly allocated string with escapes removed (caller must free),
 *         or NULL on error
 */
char *ansi_strip_escapes(const char *input, size_t input_len);

/**
 * @brief Find the end of an ANSI CSI escape sequence
 *
 * Given a pointer to an ESC character (\x1b) that starts a CSI sequence,
 * returns a pointer past the final byte of the sequence. If the pointer
 * does not point to a valid CSI sequence start, returns ptr + 1.
 *
 * @param ptr Pointer to the ESC (\x1b) character
 * @param end Pointer past the end of the string
 * @return Pointer to the first byte after the escape sequence
 */
const char *ansi_skip_escape(const char *ptr, const char *end);

/**
 * @brief Check if a position in text is already colored
 *
 * Scans from the start of the message to the given position, tracking ANSI
 * escape codes. Returns true if there is an active color code (not in reset state).
 *
 * Detects reset codes: \x1b[0m and \x1b[m
 *
 * @param message The full message
 * @param pos Position to check
 * @return true if already colored (not in reset state), false if in reset state
 */
bool ansi_is_already_colorized(const char *message, size_t pos);

/* ===== Truecolor Functions ===== */

/**
 * Append truecolor foreground SGR sequence
 * Maximum output: 19 bytes (\033[38;2;255;255;255m)
 * @param dst Destination buffer
 * @param r Red component
 * @param g Green component
 * @param b Blue component
 * @return Pointer to end of written data
 */
char *append_truecolor_fg(char *dst, uint8_t r, uint8_t g, uint8_t b);

/**
 * Append truecolor background SGR sequence
 * Maximum output: 19 bytes (\033[48;2;255;255;255m)
 * @param dst Destination buffer
 * @param r Red component
 * @param g Green component
 * @param b Blue component
 * @return Pointer to end of written data
 */
char *append_truecolor_bg(char *dst, uint8_t r, uint8_t g, uint8_t b);

/**
 * Append combined foreground + background truecolor SGR
 * Maximum output: 38 bytes (\033[38;2;255;255;255;48;2;255;255;255m)
 * @param dst Destination buffer
 * @param fg_r Foreground red component
 * @param fg_g Foreground green component
 * @param fg_b Foreground blue component
 * @param bg_r Background red component
 * @param bg_g Background green component
 * @param bg_b Background blue component
 * @return Pointer to end of written data
 */
char *append_truecolor_fg_bg(char *dst, uint8_t fg_r, uint8_t fg_g, uint8_t fg_b, uint8_t bg_r, uint8_t bg_g,
                             uint8_t bg_b);

/* ===== Run-Length Encoding Functions ===== */

/**
 * Initialize RLE context for efficient ANSI generation
 * @param ctx Context to initialize
 * @param buffer Output buffer
 * @param capacity Buffer capacity
 * @param mode Color mode (foreground/background/both)
 */
void ansi_rle_init(ansi_rle_context_t *ctx, char *buffer, size_t capacity, ansi_color_mode_t mode);

/**
 * Add pixel with run-length encoding (only emits SGR when color changes)
 * @param ctx RLE context
 * @param r Red component
 * @param g Green component
 * @param b Blue component
 * @param ascii_char ASCII character to append
 */
void ansi_rle_add_pixel(ansi_rle_context_t *ctx, uint8_t r, uint8_t g, uint8_t b, char ascii_char);

/**
 * Finish RLE sequence with reset and null terminator
 * @param ctx RLE context
 */
void ansi_rle_finish(ansi_rle_context_t *ctx);

/* ===== Initialization Functions ===== */

/**
 * Initialize fast ANSI generation (call once at startup)
 */
void ansi_fast_init(void);

/**
 * Initialize 256-color mode (thread-safe, called on first use)
 */
void ansi_fast_init_256color(void);

/**
 * Initialize 16-color mode (thread-safe, called on first use)
 */
void ansi_fast_init_16color(void);

/* ===== 256-Color Functions ===== */

/**
 * Append 256-color foreground SGR sequence
 * @param dst Destination buffer
 * @param color_index 256-color palette index (0-255)
 * @return Pointer to end of written data
 */
char *append_256color_fg(char *dst, uint8_t color_index);

/**
 * Append 256-color background SGR sequence
 * @param dst Destination buffer
 * @param color_index 256-color palette index (0-255)
 * @return Pointer to end of written data
 */
char *append_256color_bg(char *dst, uint8_t color_index);

/**
 * Convert RGB to closest 256-color palette index
 * Uses 6x6x6 color cube + grayscale ramp
 * @param r Red component
 * @param g Green component
 * @param b Blue component
 * @return 256-color palette index (0-255)
 */
uint8_t rgb_to_256color(uint8_t r, uint8_t g, uint8_t b);

/* ===== 16-Color Functions ===== */

/**
 * Append 16-color ANSI foreground SGR sequence
 * @param dst Destination buffer
 * @param color_index 16-color index (0-15)
 * @return Pointer to end of written data
 */
char *append_16color_fg(char *dst, uint8_t color_index);

/**
 * Append 16-color ANSI background SGR sequence
 * @param dst Destination buffer
 * @param color_index 16-color index (0-15)
 * @return Pointer to end of written data
 */
char *append_16color_bg(char *dst, uint8_t color_index);

/**
 * Convert RGB to closest 16-color ANSI index
 * Uses distance-based matching against standard ANSI colors
 * @param r Red component
 * @param g Green component
 * @param b Blue component
 * @return 16-color index (0-15)
 */
uint8_t rgb_to_16color(uint8_t r, uint8_t g, uint8_t b);

/**
 * Get RGB values for a 16-color ANSI index
 * @param color_index 16-color index (0-15)
 * @param r Output red component
 * @param g Output green component
 * @param b Output blue component
 */
void get_16color_rgb(uint8_t color_index, uint8_t *r, uint8_t *g, uint8_t *b);

/**
 * Convert RGB to 16-color with Floyd-Steinberg dithering
 * @param r Red component
 * @param g Green component
 * @param b Blue component
 * @param x Current pixel X coordinate
 * @param y Current pixel Y coordinate
 * @param width Image width
 * @param height Image height
 * @param error_buffer Dithering error buffer (width*height elements)
 * @return 16-color index with dithering applied
 */
uint8_t rgb_to_16color_dithered(int r, int g, int b, int x, int y, int width, int height, rgb_error_t *error_buffer);

/* ===== Mode-Aware Color Functions ===== */

/**
 * Append color SGR sequence for the appropriate terminal color mode
 * @param dst Destination buffer
 * @param r Red component
 * @param g Green component
 * @param b Blue component
 * @param mode Terminal color mode capability
 * @return Pointer to end of written data
 */
char *append_color_fg_for_mode(char *dst, uint8_t r, uint8_t g, uint8_t b, terminal_color_mode_t mode);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif // ASCIICHAT_VIDEO_TERMINAL_ANSI_H
