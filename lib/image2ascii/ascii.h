#pragma once

/**
 * @file image2ascii/ascii.h
 * @defgroup image2ascii Image to ASCII Conversion
 * @ingroup image2ascii
 * @brief ASCII Art Conversion and Output Interface
 *
 * This header provides comprehensive functions for converting images to ASCII art
 * and outputting frames to the terminal. Supports multiple color modes, aspect
 * ratio preservation, capability-aware conversion, and frame layout management.
 *
 * CORE FEATURES:
 * ==============
 * - Image-to-ASCII conversion with color support
 * - Terminal capability-aware conversion (auto-selects optimal color mode)
 * - Aspect ratio preservation for proper image proportions
 * - Terminal frame output and management
 * - Frame padding and layout utilities (horizontal and vertical)
 * - Grid layout for multiple frames (multi-user support)
 * - Buffer pool integration for efficient memory management
 * - SIMD-optimized conversion for performance
 *
 * CONVERSION MODES:
 * =================
 * The system supports multiple conversion modes:
 * - Standard conversion: Basic image-to-ASCII with optional color
 * - Capability-aware: Automatically selects best color mode for terminal
 * - Aspect ratio preservation: Maintains image proportions (terminal correction)
 * - Stretch mode: Fills terminal dimensions exactly (may distort proportions)
 *
 * FRAME MANAGEMENT:
 * ==================
 * Frame management includes:
 * - Terminal initialization and cleanup
 * - Frame output to terminal
 * - Frame padding (horizontal and vertical)
 * - Grid layout for multiple frames
 * - Frame source management
 *
 * @note All conversion functions return allocated strings that must be freed.
 * @note Terminal initialization functions must be matched with cleanup.
 * @note Capability-aware conversion is recommended for optimal results.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#ifdef _WIN32
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#endif

#include <stdio.h>
#include <time.h>
#include "simd/common.h"

// Include platform abstraction for write function mapping and deprecation suppression
#ifndef PLATFORM_ABSTRACTION_H
#include "platform/abstraction.h"
#endif

/* ============================================================================
 * Data Structures and Constants
 * ============================================================================
 */

/**
 * @brief Default ASCII palette characters
 *
 * Default character palette for ASCII art conversion. Contains
 * characters ordered by luminance (dark to light) for mapping
 * pixel brightness to ASCII characters.
 *
 * @note External variable (defined in ascii.c).
 * @note Default palette: " .:-=+*#%@$" (common ASCII art palette).
 *
 * @ingroup module_video
 */
extern char ascii_palette[];

/**
 * @brief Forward declaration of image structure
 *
 * Forward declaration for image_t structure used in conversion functions.
 * Full definition is in image.h.
 *
 * @ingroup module_video
 */
typedef struct image_t image_t;

/* ============================================================================
 * Subsystem Initialization Functions
 * @{
 */

/**
 * @brief Initialize ASCII read subsystem (e.g., webcam)
 * @param webcam_index Webcam device index (0 for default device)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes the ASCII read subsystem for image capture. Opens
 * webcam device and prepares it for frame capture. This is a
 * convenience wrapper around webcam initialization.
 *
 * @note This function initializes webcam capture for ASCII conversion.
 * @note Must call ascii_read_destroy() when done.
 * @note Use webcam_read() to capture frames after initialization.
 *
 * @warning On failure, use webcam_print_init_error_help() for diagnostics.
 *
 * @ingroup module_video
 */
asciichat_error_t ascii_read_init(unsigned short int webcam_index);

/**
 * @brief Initialize ASCII write subsystem
 * @param fd File descriptor to write to (must be valid file descriptor)
 * @param reset_terminal Whether to reset terminal on initialization (true to reset, false to preserve state)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes the ASCII write subsystem for terminal output. Configures
 * terminal for ASCII art display including cursor hiding, screen clearing,
 * and terminal state management.
 *
 * TERMINAL INITIALIZATION:
 * - Hides cursor (if reset_terminal is true)
 * - Clears screen (if reset_terminal is true)
 * - Configures terminal for raw output
 * - Prepares terminal for ASCII art display
 *
 * @note Must call ascii_write_destroy() when done.
 * @note Reset terminal clears screen and hides cursor for clean display.
 * @note File descriptor is used for terminal output (stdout, stderr, etc.).
 *
 * @ingroup module_video
 */
asciichat_error_t ascii_write_init(int fd, bool reset_terminal);

/** @} */

/* ============================================================================
 * Image-to-ASCII Conversion Functions
 * @{
 */

/**
 * @brief Convert image to ASCII art
 * @param original Source image (must not be NULL)
 * @param width Target width in characters (must be > 0)
 * @param height Target height in characters (must be > 0)
 * @param color Enable color output (true for color, false for monochrome)
 * @param aspect_ratio Preserve aspect ratio (true to maintain proportions, false to fill dimensions)
 * @param stretch Stretch to fit (if aspect_ratio is false, true to fill exactly, false to fit within)
 * @param palette_chars Character palette to use (or NULL for default ASCII palette)
 * @param luminance_palette Luminance-to-character mapping palette (must not be NULL, 256 elements)
 * @return Allocated ASCII frame string (caller must free), or NULL on error
 *
 * Converts an image to ASCII art with specified dimensions and color mode.
 * Uses character palette to map pixel luminance to ASCII characters and
 * optionally applies color output using ANSI escape sequences.
 *
 * CONVERSION PROCESS:
 * - Resizes image to target dimensions (with aspect ratio preservation if enabled)
 * - Maps each pixel's luminance to ASCII character using palette
 * - Optionally applies color output using ANSI escape sequences
 * - Returns null-terminated string containing ASCII frame
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note Aspect ratio preservation uses terminal character correction.
 * @note Color output requires terminal color support for proper display.
 *
 * @ingroup module_video
 */
char *ascii_convert(image_t *original, const ssize_t width, const ssize_t height, const bool color,
                    const bool aspect_ratio, const bool stretch, const char *palette_chars,
                    const char luminance_palette[256]);

/* ============================================================================
 * Capability-Aware Conversion Functions
 * @{
 */

// Capability-aware ASCII conversion using terminal detection
#include "platform/terminal.h"

/**
 * @brief Convert image to ASCII art with terminal capability awareness
 * @param original Source image (must not be NULL)
 * @param width Target width in characters (must be > 0)
 * @param height Target height in characters (must be > 0)
 * @param caps Terminal capabilities structure (must not be NULL)
 * @param use_aspect_ratio Preserve aspect ratio (true to maintain proportions, false to fill dimensions)
 * @param stretch Stretch to fit (if use_aspect_ratio is false, true to fill exactly, false to fit within)
 * @param palette_chars Character palette to use (or NULL for default ASCII palette)
 * @param luminance_palette Luminance-to-character mapping palette (must not be NULL, 256 elements)
 * @return Allocated ASCII frame string (caller must free), or NULL on error
 *
 * Converts an image to ASCII art with automatic color mode selection
 * based on terminal capabilities. Automatically chooses the best
 * color mode (16-color, 256-color, or truecolor) for optimal display.
 *
 * CAPABILITY-AWARE CONVERSION:
 * - Detects terminal color capabilities from structure
 * - Selects optimal color mode (truecolor > 256-color > 16-color > monochrome)
 * - Applies terminal character aspect ratio correction
 * - Uses capability-specific conversion algorithms
 *
 * @note Returns NULL on error (invalid image, memory allocation failure).
 * @note ASCII string must be freed by caller using free().
 * @note Color mode is selected automatically based on terminal capabilities.
 * @note This is the recommended function for capability-aware ASCII conversion.
 *
 * @ingroup module_video
 */
char *ascii_convert_with_capabilities(image_t *original, const ssize_t width, const ssize_t height,
                                      const terminal_capabilities_t *caps, const bool use_aspect_ratio,
                                      const bool stretch, const char *palette_chars, const char luminance_palette[256]);

/** @} */

/* ============================================================================
 * Frame Output Functions
 * @{
 */

/**
 * @brief Write ASCII frame to terminal
 * @param frame ASCII frame string to write (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Writes an ASCII frame string to the terminal output. Frame string
 * may contain ANSI escape sequences for color output and cursor
 * positioning. Output is written to the file descriptor configured
 * during ascii_write_init().
 *
 * @note Frame string should be null-terminated.
 * @note ANSI escape sequences in frame string are processed by terminal.
 * @note Output is written immediately (no buffering).
 *
 * @ingroup module_video
 */
asciichat_error_t ascii_write(const char *frame);

/** @} */

/* ============================================================================
 * Subsystem Cleanup Functions
 * @{
 */

/**
 * @brief Destroy ASCII read subsystem
 *
 * Cleans up the ASCII read subsystem and releases resources. Closes
 * webcam device and frees associated memory. Should be called when
 * done with image capture.
 *
 * @note Safe to call multiple times (no-op after first call).
 * @note After cleanup, webcam capture will fail until re-initialized.
 *
 * @ingroup module_video
 */
void ascii_read_destroy(void);

/**
 * @brief Destroy ASCII write subsystem
 * @param fd File descriptor that was used for writing (must be valid)
 * @param reset_terminal Whether to reset terminal on cleanup (true to restore state, false to preserve)
 *
 * Cleans up the ASCII write subsystem and restores terminal state.
 * Restores cursor visibility, clears screen, and resets terminal
 * attributes if reset_terminal is true.
 *
 * TERMINAL CLEANUP:
 * - Restores cursor visibility (if reset_terminal is true)
 * - Resets terminal attributes (if reset_terminal is true)
 * - Clears screen (if reset_terminal is true)
 * - Restores terminal to default state
 *
 * @note Safe to call multiple times (no-op after first call).
 * @note Reset terminal restores terminal to default state.
 * @note After cleanup, terminal output functions may fail until re-initialized.
 *
 * @ingroup module_video
 */
void ascii_write_destroy(int fd, bool reset_terminal);

/** @} */

/* ============================================================================
 * Frame Layout and Padding Functions
 * @{
 */

/**
 * @brief Add leading spaces (left-padding) to each line of a frame
 * @param frame ASCII frame string (must not be NULL)
 * @param pad Number of spaces to pad on left (must be >= 0)
 * @return Allocated padded frame string (caller must free with free()), or NULL on error
 *
 * Adds leading spaces (left-padding) to each line of an ASCII frame.
 * Useful for centering frames horizontally or creating margins.
 * Each line in the frame is padded with the specified number of spaces.
 *
 * @note Returns NULL on error (invalid frame, memory allocation failure).
 * @note Padded frame string must be freed by caller using free().
 * @note Frame height is preserved (number of lines unchanged).
 * @note Frame width is increased by 'pad' characters.
 *
 * @ingroup module_video
 */
char *ascii_pad_frame_width(const char *frame, size_t pad);

/**
 * @brief Add blank lines (vertical padding) to center a frame vertically
 * @param frame ASCII frame string (must not be NULL)
 * @param pad_top Number of blank lines to add at top (must be >= 0)
 * @return Allocated padded frame string (caller must free with free()), or NULL on error
 *
 * Adds blank lines (vertical padding) to the top of an ASCII frame.
 * Useful for centering frames vertically or creating margins.
 * Blank lines are added at the top of the frame.
 *
 * @note Returns NULL on error (invalid frame, memory allocation failure).
 * @note Padded frame string must be freed by caller using free().
 * @note Frame width is preserved (characters per line unchanged).
 * @note Frame height is increased by 'pad_top' lines.
 *
 * @ingroup module_video
 */
char *ascii_pad_frame_height(const char *frame, size_t pad_top);

/**
 * @brief Frame source structure for grid layout
 *
 * Contains frame data and size for grid layout operations.
 * Used to combine multiple ASCII frames into a single grid layout.
 *
 * @note frame_data points to ASCII frame string (not owned by structure).
 * @note frame_size is the size of frame_data in bytes.
 * @note Frame data must remain valid during grid creation.
 *
 * @ingroup module_video
 */
typedef struct {
  const char *frame_data; ///< Frame data pointer (ASCII frame string, not owned)
  size_t frame_size;      ///< Frame data size in bytes (length of frame string)
} ascii_frame_source_t;

/**
 * @brief Create a grid layout from multiple ASCII frames
 * @param sources Array of frame sources (must not be NULL, must have source_count elements)
 * @param source_count Number of frame sources (must be > 0)
 * @param width Grid width in characters per frame (must be > 0)
 * @param height Grid height in characters per frame (must be > 0)
 * @param out_size Pointer to store output size in bytes (can be NULL)
 * @return Allocated grid frame string (caller must free), or NULL on error
 *
 * Combines multiple ASCII frames into a single grid layout. Arranges
 * frames in a grid pattern suitable for multi-user display. Each frame
 * is positioned in the grid according to its index in the sources array.
 *
 * GRID LAYOUT:
 * - Frames are arranged in grid pattern (rows and columns)
 * - Each frame has specified width and height in characters
 * - Grid dimensions are calculated based on source_count
 * - Output frame contains all frames arranged in grid
 *
 * @note Returns NULL on error (invalid sources, memory allocation failure).
 * @note Grid frame string must be freed by caller using free().
 * @note Frame sources must have valid frame_data and frame_size.
 * @note Grid layout is useful for multi-user video display.
 *
 * @par Example
 * @code
 * ascii_frame_source_t sources[4];
 * // Initialize sources...
 * char *grid = ascii_create_grid(sources, 4, 80, 40, NULL);
 * if (grid) {
 *     ascii_write(grid);
 *     free(grid);
 * }
 * @endcode
 *
 * @ingroup module_video
 */
char *ascii_create_grid(ascii_frame_source_t *sources, int source_count, int width, int height, size_t *out_size);

/** @} */

/* ============================================================================
 * Palette Utility Functions
 * @{
 */

/**
 * @brief Get luminance palette for character mapping
 * @return Pointer to luminance palette array (256 elements, do not free)
 *
 * Returns the default luminance-to-character mapping palette. The palette
 * contains 256 characters ordered by luminance (dark to light) for
 * mapping pixel brightness values (0-255) to ASCII characters.
 *
 * @note Returns pointer to static array (do not free).
 * @note Palette has 256 elements (one for each luminance level).
 * @note Palette is used for monochrome ASCII conversion.
 *
 * @ingroup module_video
 */
char *get_lum_palette(void);

/** @} */

/* ============================================================================
 * ASCII Conversion Constants
 * ============================================================================
 */

/**
 * @brief Number of luminance levels supported (256)
 *
 * Number of luminance levels used for character mapping. Each pixel's
 * brightness value (0-255) maps to a character in the luminance palette.
 *
 * @note 256 levels correspond to 8-bit pixel brightness values.
 * @note Luminance palette must have 256 elements.
 *
 * @ingroup module_video
 */
#define ASCII_LUMINANCE_LEVELS 256

/**
 * @brief Sleep duration in nanoseconds between frames (50000 ns = 50 μs)
 *
 * Sleep duration between frame outputs for frame rate limiting.
 * Used to prevent excessive terminal I/O and control frame rate.
 *
 * @note 50000 nanoseconds = 50 microseconds = 0.05 milliseconds
 * @note Used for frame rate limiting in video playback.
 *
 * @ingroup module_video
 */
#define ASCII_SLEEP_NS 50000L

/* ============================================================================
 * ANSI Escape Sequence Constants
 * ============================================================================
 */

/**
 * @brief ANSI foreground color prefix (truecolor mode)
 *
 * ANSI escape sequence prefix for 24-bit truecolor foreground colors.
 * Format: ESC[38;2;r;g;bm (where r, g, b are RGB values).
 *
 * @note Use with ANSI_COLOR_SUFFIX to form complete escape sequence.
 * @note Example: ANSI_FG_PREFIX "255;0;0" ANSI_COLOR_SUFFIX (red foreground).
 *
 * @ingroup module_video
 */
#define ANSI_FG_PREFIX "\033[38;2;"

/**
 * @brief ANSI background color prefix (truecolor mode)
 *
 * ANSI escape sequence prefix for 24-bit truecolor background colors.
 * Format: ESC[48;2;r;g;bm (where r, g, b are RGB values).
 *
 * @note Use with ANSI_COLOR_SUFFIX to form complete escape sequence.
 * @note Example: ANSI_BG_PREFIX "255;0;0" ANSI_COLOR_SUFFIX (red background).
 *
 * @ingroup module_video
 */
#define ANSI_BG_PREFIX "\033[48;2;"

/**
 * @brief ANSI color suffix
 *
 * ANSI escape sequence suffix for color escape sequences.
 * Used with ANSI_FG_PREFIX or ANSI_BG_PREFIX to form complete
 * escape sequences.
 *
 * @note Format: prefix + RGB + suffix = complete ANSI sequence.
 * @note Example: "\033[38;2;255;0;0m" (red foreground).
 *
 * @ingroup module_video
 */
#define ANSI_COLOR_SUFFIX "m"

/* ============================================================================
 * Utility Macros
 * ============================================================================
 */

/**
 * @brief Print string to stdout
 * @param s String literal to print
 *
 * Utility macro for printing string literals to stdout.
 * Uses fwrite() for efficient output without null terminator requirement.
 *
 * @note This is a macro that expands to fwrite() call.
 * @note Only works with string literals (compile-time known size).
 * @note Use fputs() or printf() for runtime strings.
 *
 * @ingroup module_video
 */
#define print(s) fwrite(s, 1, sizeof(s) / sizeof((s)[0]), stdout)

/* ============================================================================
 * Terminal Operation Macros
 * ============================================================================
 *
 * Convenience macros for terminal operations using platform abstraction
 * layer functions. These macros simplify common terminal operations.
 */

/**
 * @brief Clear console and move cursor to home position
 * @param fd File descriptor for terminal
 *
 * Clears terminal screen and moves cursor to home position (top-left).
 * Equivalent to terminal_clear_screen() followed by terminal_cursor_home().
 *
 * @ingroup module_video
 */
#define console_clear(fd) (terminal_clear_screen(), terminal_cursor_home(fd))

/**
 * @brief Reset cursor to home position
 * @param fd File descriptor for terminal
 *
 * Moves cursor to home position (top-left, row 1, column 1).
 * Equivalent to terminal_cursor_home().
 *
 * @ingroup module_video
 */
#define cursor_reset(fd) terminal_cursor_home(fd)

/**
 * @brief Clear terminal screen
 * @param fd File descriptor for terminal
 *
 * Clears terminal screen without moving cursor.
 * Equivalent to terminal_clear_screen().
 *
 * @ingroup module_video
 */
#define ascii_clear_screen(fd) terminal_clear_screen()

/**
 * @brief Hide terminal cursor
 * @param fd File descriptor for terminal
 *
 * Hides terminal cursor for clean ASCII art display.
 * Equivalent to terminal_hide_cursor(fd, true).
 *
 * @ingroup module_video
 */
#define cursor_hide(fd) terminal_hide_cursor(fd, true)

/**
 * @brief Show terminal cursor
 * @param fd File descriptor for terminal
 *
 * Shows terminal cursor.
 * Equivalent to terminal_hide_cursor(fd, false).
 *
 * @ingroup module_video
 */
#define cursor_show(fd) terminal_hide_cursor(fd, false)

/* ============================================================================
 * Frame Rate Limiting
 * ============================================================================
 */

/**
 * @brief Sleep duration structure for frame rate limiting
 *
 * Timespec structure for frame rate limiting sleep duration.
 * Used with ascii_zzz() macro for frame rate control.
 *
 * @note Sleep duration: 500 nanoseconds (0.5 microseconds)
 * @note Used to prevent excessive terminal I/O.
 *
 * @ingroup module_video
 */
static const struct timespec ASCII_SLEEP_START = {.tv_sec = 0, .tv_nsec = 500},
                             ASCII_SLEEP_STOP = {.tv_sec = 0, .tv_nsec = 0};

/**
 * @brief Sleep for frame rate limiting
 *
 * Sleeps for a short duration (500 nanoseconds) to limit frame rate
 * and prevent excessive terminal I/O. Uses nanosleep() for precise
 * timing control.
 *
 * @note Sleep duration: 500 nanoseconds (very short, minimal overhead)
 * @note Used between frame outputs for rate limiting.
 * @note May not be needed on all systems (depends on terminal I/O speed).
 *
 * @ingroup module_video
 */
#define ascii_zzz() nanosleep((struct timespec *)&ASCII_SLEEP_START, (struct timespec *)&ASCII_SLEEP_STOP)

