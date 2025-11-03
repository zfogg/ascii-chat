#pragma once

/**
 * @file platform/terminal.h
 * @ingroup platform
 * @brief Cross-platform terminal interface for ascii-chat
 *
 * This header provides unified terminal I/O operations including
 * ANSI escape sequences, cursor control, and terminal configuration.
 *
 * The interface provides:
 * - Terminal size detection and management
 * - Cursor control and positioning
 * - Screen clearing and scrolling
 * - Terminal mode configuration (raw mode, echo, buffering)
 * - Terminal capability detection (color, unicode, UTF-8)
 * - Terminal title and bell control
 * - Windows console resize detection
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>
#include <stdint.h>
#include "../common.h"

// ============================================================================
// Platform-Specific Signal Definitions
// ============================================================================

#ifdef _WIN32
// Windows doesn't have these signals natively
#define SIGWINCH 28 // Window size change (not supported on Windows)
#define SIGTERM 15  // Termination signal (limited support on Windows)
#endif

/* ============================================================================
 * Terminal Data Structures
 * ============================================================================
 */

/**
 * @brief Terminal size structure
 *
 * Contains terminal dimensions in rows and columns.
 *
 * @note Rows represent height (vertical dimension).
 * @note Columns represent width (horizontal dimension).
 *
 * @ingroup platform
 */
typedef struct {
  int rows; ///< Number of rows (height) in terminal
  int cols; ///< Number of columns (width) in terminal
} terminal_size_t;

/* ============================================================================
 * Terminal Control Functions
 * @{
 */

/**
 * @brief Get terminal size
 * @param size Pointer to store terminal size (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Queries the terminal for its current dimensions (rows and columns).
 * Uses platform-specific methods (ioctl on Unix, Windows Console API).
 *
 * @note On failure, size structure is not modified.
 * @note Terminal size may change if terminal is resized.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_get_size(terminal_size_t *size);

/**
 * @brief Set terminal to raw mode
 * @param enable true to enable raw mode, false to disable
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Controls terminal raw mode. In raw mode, terminal input is not processed:
 * - No line buffering (character-by-character input)
 * - No echo (characters not printed)
 * - No canonical mode (no line editing)
 * - Immediate character availability
 *
 * Raw mode is useful for real-time input processing (keyboard events, etc.).
 *
 * @note Disabling raw mode restores normal terminal behavior.
 * @note Raw mode affects only the current terminal session.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_set_raw_mode(bool enable);

/**
 * @brief Set terminal echo mode
 * @param enable true to enable echo, false to disable
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Controls whether terminal input is echoed back to the display.
 * When echo is disabled, input characters are not displayed (useful
 * for password input or silent key capture).
 *
 * @note Echo mode works independently of raw mode.
 * @note Disabling echo is useful for password prompts.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_set_echo(bool enable);

/**
 * @brief Check if terminal supports color
 * @return true if terminal supports color, false otherwise
 *
 * Determines whether the terminal supports color output. Checks
 * environment variables ($TERM, $COLORTERM) and terminal type
 * to detect color capabilities.
 *
 * @note Returns true if ANY color support is detected (16, 256, or truecolor).
 * @note Use detect_terminal_capabilities() for detailed color level detection.
 *
 * @ingroup platform
 */
bool terminal_supports_color(void);

/**
 * @brief Check if terminal supports unicode
 * @return true if terminal supports unicode, false otherwise
 *
 * Determines whether the terminal supports Unicode character output.
 * Checks locale settings and terminal type for Unicode support.
 *
 * @note Unicode support is broader than UTF-8 (includes UTF-16, etc.).
 * @note Use terminal_supports_utf8() for UTF-8 specific detection.
 *
 * @ingroup platform
 */
bool terminal_supports_unicode(void);

/**
 * @brief Check if terminal supports UTF-8
 * @return true if terminal supports UTF-8, false otherwise
 *
 * Determines whether the terminal supports UTF-8 encoding. Checks
 * locale settings ($LC_ALL, $LANG) and terminal type for UTF-8 support.
 *
 * @note UTF-8 support is required for Unicode palette characters.
 * @note Use detect_terminal_capabilities() for comprehensive capability detection.
 *
 * @ingroup platform
 */
bool terminal_supports_utf8(void);

/**
 * @brief Clear the terminal screen
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Clears the terminal screen using ANSI escape sequences (Unix) or
 * Windows Console API. Removes all visible characters and resets
 * cursor position to top-left.
 *
 * @note Uses ANSI escape sequence ESC[2J on Unix systems.
 * @note On Windows, uses Console API ClearScreen() function.
 * @note Screen clearing does not affect scrollback buffer.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_clear_screen(void);

/**
 * @brief Move cursor to specified position
 * @param row Row position (1-based, top is row 1)
 * @param col Column position (1-based, left is column 1)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Moves the terminal cursor to the specified row and column position.
 * Uses ANSI escape sequences (Unix) or Windows Console API. Positions
 * are 1-based (top-left is row 1, column 1).
 *
 * @note Row 1 is the top row of the terminal.
 * @note Column 1 is the leftmost column of the terminal.
 * @note Cursor position may be clamped to terminal bounds.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_move_cursor(int row, int col);

/**
 * @brief Enable ANSI escape sequences
 *
 * On Windows, enables ANSI escape sequence processing in the console.
 * This allows Windows console to interpret ANSI escape codes (colors,
 * cursor movement, etc.) that are normally only available on Unix terminals.
 *
 * @note This function is a no-op on Unix systems (ANSI already supported).
 * @note On Windows, requires Windows 10 build 1511 or later.
 * @note ANSI support is enabled for the current console session.
 *
 * @ingroup platform
 */
void terminal_enable_ansi(void);

/**
 * @brief Set terminal buffering mode
 * @param line_buffered true for line buffering, false for unbuffered
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Controls terminal output buffering mode:
 * - Line buffering: Output is buffered until newline is written
 * - Unbuffered: Output is written immediately (real-time)
 *
 * Unbuffered mode is useful for real-time ASCII art rendering where
 * immediate output is desired. Line buffering is more efficient for
 * line-based output.
 *
 * @note Buffering mode affects stdout/stderr behavior.
 * @note Unbuffered mode may reduce performance for large outputs.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_set_buffering(bool line_buffered);

/**
 * @brief Flush terminal output
 * @param fd File descriptor to flush (must be valid file descriptor)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Forces all buffered output to be written to the terminal immediately.
 * Ensures that all pending terminal output is displayed before continuing.
 *
 * @note This function calls fsync/flush operations on the file descriptor.
 * @note Useful for ensuring output is visible before blocking operations.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_flush(int fd);

/**
 * @brief Get current cursor position
 * @param row Pointer to store row position (must not be NULL, 1-based)
 * @param col Pointer to store column position (must not be NULL, 1-based)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Queries the terminal for the current cursor position. Uses platform-specific
 * methods to detect cursor location. Positions are returned in 1-based
 * coordinates (row 1, column 1 is top-left).
 *
 * @note On failure, output parameters are not modified.
 * @note Cursor position detection may not be available on all terminals.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_get_cursor_position(int *row, int *col);

/**
 * @brief Save cursor position
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Saves the current cursor position for later restoration. Uses ANSI
 * escape sequence ESC[s to save cursor position. Restore with
 * terminal_restore_cursor().
 *
 * @note Saved position is terminal-specific and not stored in application.
 * @note Some terminals may not support cursor position save/restore.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_save_cursor(void);

/**
 * @brief Restore saved cursor position
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Restores a previously saved cursor position. Uses ANSI escape sequence
 * ESC[u to restore cursor position. Must be preceded by terminal_save_cursor().
 *
 * @note Only restores the most recently saved cursor position.
 * @note Some terminals may not support cursor position save/restore.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_restore_cursor(void);

/**
 * @brief Set terminal window title
 * @param title Title string to set (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Sets the terminal window title to the specified string. Uses ANSI
 * escape sequence ESC]0;titleBEL or platform-specific API. Title
 * appears in window title bar or terminal tab.
 *
 * @note Title is truncated to terminal-specific maximum length.
 * @note Some terminals may not support title setting.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_set_title(const char *title);

/**
 * @brief Ring terminal bell
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Rings the terminal bell (beep sound). Uses ANSI escape sequence BEL
 * or platform-specific API to trigger audible notification.
 *
 * @note Bell sound depends on terminal/system sound settings.
 * @note Some terminals may have bell disabled or silent.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_ring_bell(void);

/**
 * @brief Hide or show cursor
 * @param fd File descriptor for terminal (must be valid)
 * @param hide true to hide cursor, false to show cursor
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Controls terminal cursor visibility. Hiding the cursor is useful for
 * full-screen ASCII art rendering where cursor flicker is distracting.
 * Uses ANSI escape sequences (ESC[?25l to hide, ESC[?25h to show).
 *
 * @note Hidden cursor should be restored before program exit.
 * @note Cursor visibility change is immediate.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_hide_cursor(int fd, bool hide);

/**
 * @brief Set scroll region
 * @param top Top row of scroll region (1-based, must be > 0)
 * @param bottom Bottom row of scroll region (1-based, must be >= top)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Defines a scroll region within the terminal. Only the specified
 * row range will scroll when text exceeds the bottom. Uses ANSI
 * escape sequence ESC[top;bottomr. Useful for preserving header/footer
 * regions while allowing content area to scroll.
 *
 * @note Scroll region must have top <= bottom.
 * @note Setting scroll region to entire terminal clears the restriction.
 * @note Some terminals may not support scroll regions.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_set_scroll_region(int top, int bottom);

/**
 * @brief Reset terminal to default state
 * @param fd File descriptor for terminal (must be valid)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Resets terminal to default state including:
 * - Default colors (foreground/background)
 * - Default cursor visibility
 * - Default attributes (bold, underline, etc.)
 * - Cleared scroll regions
 *
 * Useful for cleanup before program exit or when resetting terminal state.
 *
 * @note This function sends ANSI reset sequence (ESC[0m).
 * @note Terminal state is reset immediately.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_reset(int fd);

/**
 * @brief Move cursor to home position (top-left)
 * @param fd File descriptor for terminal (must be valid)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Moves cursor to home position (row 1, column 1 - top-left corner).
 * Equivalent to terminal_move_cursor(1, 1) but more efficient.
 * Uses ANSI escape sequence ESC[H.
 *
 * @note Home position is always row 1, column 1.
 * @note Useful for starting new frame rendering at top-left.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_cursor_home(int fd);

/**
 * @brief Clear terminal scrollback buffer
 * @param fd File descriptor for terminal (must be valid)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Clears the terminal scrollback buffer (history of previous output).
 * This removes all previous terminal output that can be scrolled back
 * to view. Useful for starting with a clean terminal state.
 *
 * @note Scrollback clearing is terminal-dependent.
 * @note Some terminals may not support scrollback clearing.
 * @note On Windows, clears console screen buffer.
 *
 * @ingroup platform
 */
asciichat_error_t terminal_clear_scrollback(int fd);

/** @} */

/* ============================================================================
 * Terminal Detection and Capabilities
 * @{
 */

/**
 * @brief Terminal color support levels
 *
 * Enumeration of terminal color capability levels from no color support
 * to full 24-bit truecolor support. Used for capability detection and
 * rendering mode selection.
 *
 * @ingroup platform
 */
typedef enum {
  /** @brief No color support (monochrome terminal) */
  TERM_COLOR_NONE = 0,
  /** @brief 16-color support (standard ANSI colors) */
  TERM_COLOR_16 = 1,
  /** @brief 256-color support (extended ANSI palette) */
  TERM_COLOR_256 = 2,
  /** @brief 24-bit truecolor support (RGB colors) */
  TERM_COLOR_TRUECOLOR = 3
} terminal_color_level_t;

/**
 * @brief Terminal capability flags (bitmask)
 *
 * Bitmask enumeration for terminal capabilities. Multiple flags can be
 * combined to indicate support for various features. Used in terminal
 * capability detection and rendering optimization.
 *
 * @ingroup platform
 */
typedef enum {
  /** @brief 16-color support (TERM_CAP_COLOR_16) */
  TERM_CAP_COLOR_16 = 0x0001,
  /** @brief 256-color support (TERM_CAP_COLOR_256) */
  TERM_CAP_COLOR_256 = 0x0002,
  /** @brief Truecolor support (TERM_CAP_COLOR_TRUE) */
  TERM_CAP_COLOR_TRUE = 0x0004,
  /** @brief UTF-8 encoding support (TERM_CAP_UTF8) */
  TERM_CAP_UTF8 = 0x0008,
  /** @brief Background color support (TERM_CAP_BACKGROUND) */
  TERM_CAP_BACKGROUND = 0x0010
} terminal_capability_flags_t;

/**
 * @brief Render mode preferences
 *
 * Enumeration of rendering modes for ASCII art output. Different modes
 * provide different visual effects and require different terminal capabilities.
 *
 * @ingroup platform
 */
typedef enum {
  /** @brief Foreground colors only (text color) */
  RENDER_MODE_FOREGROUND = 0,
  /** @brief Background colors (block colors) */
  RENDER_MODE_BACKGROUND = 1,
  /** @brief Unicode half-block characters (mixed foreground/background) */
  RENDER_MODE_HALF_BLOCK = 2
} render_mode_t;

/**
 * @brief Complete terminal capabilities structure
 *
 * Comprehensive terminal capabilities structure containing all detected
 * terminal features, color support, encoding capabilities, and rendering
 * preferences. Used for optimal ASCII art rendering configuration.
 *
 * @ingroup platform
 */
typedef struct {
  /** @brief Detected color support level (terminal_color_level_t) */
  terminal_color_level_t color_level;
  /** @brief Capability flags bitmask (terminal_capability_flags_t) */
  uint32_t capabilities;
  /** @brief Maximum number of colors (16, 256, or 16777216) */
  uint32_t color_count;
  /** @brief True if terminal supports UTF-8 encoding */
  bool utf8_support;
  /** @brief True if detection is confident (reliable detection) */
  bool detection_reliable;
  /** @brief Preferred rendering mode (render_mode_t) */
  render_mode_t render_mode;
  /** @brief $TERM environment variable value (for debugging) */
  char term_type[64];
  /** @brief $COLORTERM environment variable value (for debugging) */
  char colorterm[64];
  /** @brief True if background colors are preferred */
  bool wants_background;
  /** @brief Palette type enum value (palette_type_t) */
  int palette_type;
  /** @brief Custom palette characters (if palette_type == PALETTE_CUSTOM) */
  char palette_custom[64];
  /** @brief Client's desired frame rate (1-144 FPS) */
  uint8_t desired_fps;
} terminal_capabilities_t;

/**
 * @brief TTY detection and management structure
 *
 * Contains information about the current TTY (terminal) including file
 * descriptor, device path, and ownership information for proper cleanup.
 *
 * @note owns_fd indicates whether the file descriptor was opened by the
 *       function and should be closed when done.
 *
 * @ingroup platform
 */
typedef struct {
  /** @brief File descriptor for TTY access */
  int fd;
  /** @brief Path to TTY device (e.g., "/dev/tty", "CON", etc.) */
  const char *path;
  /** @brief True if we opened the FD and should close it, false otherwise */
  bool owns_fd;
} tty_info_t;

/* ============================================================================
 * Terminal Capability Detection Functions
 * @{
 */

/**
 * @brief Detect terminal capabilities
 * @return Terminal capabilities structure with all detected features
 *
 * Comprehensively detects terminal capabilities including:
 * - Color support level (none, 16, 256, truecolor)
 * - UTF-8 encoding support
 * - Terminal type and environment variables
 * - Render mode preferences
 * - Detection reliability
 *
 * Detection uses multiple methods:
 * - Environment variable analysis ($TERM, $COLORTERM, $LC_ALL, $LANG)
 * - Terminal type database lookups
 * - Runtime capability queries (where available)
 *
 * @note Returns a structure with all detected capabilities.
 * @note Detection reliability is indicated by detection_reliable field.
 * @note Use apply_color_mode_override() to apply command-line overrides.
 *
 * @ingroup platform
 */
terminal_capabilities_t detect_terminal_capabilities(void);

/**
 * @brief Get current TTY information
 * @return TTY information structure with file descriptor and path
 *
 * Retrieves information about the current TTY (terminal). Returns file
 * descriptor for TTY access, device path, and ownership information.
 * Useful for advanced terminal operations that require direct TTY access.
 *
 * @note File descriptor may need to be closed if owns_fd is true.
 * @note TTY path is platform-specific (Unix: /dev/tty, Windows: CON, etc.).
 *
 * @ingroup platform
 */
tty_info_t get_current_tty(void);

/**
 * @brief Check if a TTY path is valid
 * @param path Path to check (must not be NULL)
 * @return true if path is valid TTY device, false otherwise
 *
 * Validates that a path points to a valid TTY (terminal) device.
 * Checks device file existence and type on Unix systems.
 *
 * @note Returns false for non-TTY devices or invalid paths.
 * @note Useful for validating TTY paths before use.
 *
 * @ingroup platform
 */
bool is_valid_tty_path(const char *path);

/**
 * @brief Get terminal size with multiple fallback methods
 * @param width Pointer to store width in columns (must not be NULL)
 * @param height Pointer to store height in rows (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Detects terminal size using multiple fallback methods for reliability:
 * 1. Terminal size query (ioctl TIOCGWINSZ on Unix, Console API on Windows)
 * 2. Environment variable fallback ($COLUMNS, $LINES)
 * 3. Default size fallback (80x24) if all methods fail
 *
 * @note On failure, output parameters are not modified.
 * @note Terminal size may change if terminal is resized.
 * @note This function is more reliable than terminal_get_size() due to fallbacks.
 *
 * @ingroup platform
 */
asciichat_error_t get_terminal_size(unsigned short int *width, unsigned short int *height);

/**
 * @brief Get name of color level
 * @param level Color level enum value (terminal_color_level_t)
 * @return Human-readable color level name (e.g., "16-color", "truecolor")
 *
 * Converts a terminal color level enum value to a human-readable string
 * name. Useful for logging and debugging terminal capability detection.
 *
 * @note Returns static string (do not free).
 * @note Returns "unknown" for invalid level values.
 *
 * @ingroup platform
 */
const char *terminal_color_level_name(terminal_color_level_t level);

/**
 * @brief Get summary string of terminal capabilities
 * @param caps Terminal capabilities structure (must not be NULL)
 * @return Summary string describing capabilities (may be static, do not free)
 *
 * Generates a human-readable summary string describing the terminal's
 * capabilities including color level, UTF-8 support, and render mode.
 * Useful for logging and debugging terminal configuration.
 *
 * @note Returns static string (do not free).
 * @note Summary format: "16-color, UTF-8, foreground mode" (example).
 *
 * @ingroup platform
 */
const char *terminal_capabilities_summary(const terminal_capabilities_t *caps);

/**
 * @brief Print terminal capabilities to stdout
 * @param caps Terminal capabilities structure (must not be NULL)
 *
 * Prints a detailed report of terminal capabilities to stdout including:
 * - Color support level and count
 * - UTF-8 encoding support
 * - Render mode preferences
 * - Terminal type and environment variables
 * - Detection reliability
 *
 * @note Useful for debugging terminal capability detection.
 * @note Output is formatted for human readability.
 *
 * @ingroup platform
 */
void print_terminal_capabilities(const terminal_capabilities_t *caps);

/**
 * @brief Test terminal output modes
 *
 * Tests various terminal output modes to verify capabilities. Outputs
 * test patterns for different color modes (16-color, 256-color, truecolor)
 * and rendering modes to verify terminal behavior.
 *
 * @note This function outputs test patterns to stdout.
 * @note Useful for verifying terminal capability detection accuracy.
 *
 * @ingroup platform
 */
void test_terminal_output_modes(void);

/**
 * @brief Apply command-line overrides to detected capabilities
 * @param caps Terminal capabilities structure to modify
 * @return Modified terminal capabilities structure
 *
 * Applies any command-line option overrides to the detected terminal
 * capabilities. Overrides may include:
 * - Force color mode (--color, --no-color, --256, --truecolor)
 * - Force UTF-8 mode (--utf8)
 * - Render mode selection (--bg, --fg, --half-block)
 * - Palette selection (--palette)
 *
 * @note Returns a modified copy of the input structure.
 * @note Overrides take precedence over detected capabilities.
 *
 * @ingroup platform
 */
terminal_capabilities_t apply_color_mode_override(terminal_capabilities_t caps);

/** @} */

/* ============================================================================
 * Windows Console Resize Detection
 * @{
 *
 * Windows-specific functionality for detecting console window resize events.
 * Unix systems use SIGWINCH signal, but Windows requires polling or event
 * monitoring, so a background thread is used.
 */

#ifdef _WIN32
/**
 * @brief Callback function type for terminal resize events
 * @param cols New terminal width in columns
 * @param rows New terminal height in rows
 *
 * Callback function called when terminal resize is detected. Receives
 * the new terminal dimensions (columns and rows). Called from resize
 * detection thread.
 *
 * @note Callback is called asynchronously from resize detection thread.
 * @note Callback should perform operations quickly to avoid blocking.
 *
 * @ingroup platform
 */
typedef void (*terminal_resize_callback_t)(int cols, int rows);

/**
 * @brief Start Windows console resize detection thread
 * @param callback Function to call when resize is detected (must not be NULL)
 * @return 0 on success, -1 on failure
 *
 * Starts a background thread that monitors Windows console window for
 * resize events. When resize is detected, calls the provided callback
 * function with new dimensions. Windows-specific because Unix systems
 * use SIGWINCH signal instead.
 *
 * @note Thread continues running until terminal_stop_resize_detection() is called.
 * @note Only one resize detection thread can be active at a time.
 * @note Thread must be stopped before program exit to avoid leaks.
 *
 * @warning Callback is called from a separate thread - ensure thread-safety.
 *
 * @ingroup platform
 */
int terminal_start_resize_detection(terminal_resize_callback_t callback);

/**
 * @brief Stop Windows console resize detection thread
 *
 * Stops the background resize detection thread and cleans up resources.
 * Should be called before program exit to ensure proper cleanup.
 *
 * @note Safe to call multiple times (no-op after first call).
 * @note After stopping, resize events are no longer detected.
 *
 * @ingroup platform
 */
void terminal_stop_resize_detection(void);

/** @} */
#endif
