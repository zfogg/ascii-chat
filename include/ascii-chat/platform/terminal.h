#pragma once

/**
 * @file platform/terminal.h
 * @brief 🖥️ Cross-platform terminal interface for ascii-chat
 * @ingroup platform
 * @addtogroup platform
 * @{
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
#include "../log/types.h"

// ============================================================================
// Cross-platform CLI Utilities
// ============================================================================

/**
 * @brief Platform-specific getopt include
 *
 * Provides unified getopt functionality across platforms:
 * - Windows: Uses platform/windows/getopt.h
 * - POSIX: Uses system <getopt.h>
 *
 * @ingroup platform
 */
#ifdef _WIN32
#include "windows/getopt.h"
#else
#include <getopt.h>
#endif

// ============================================================================
// Platform-Specific Signal Definitions
// ============================================================================

#ifdef _WIN32
// Windows doesn't have these signals natively
#define SIGWINCH 28 // Window size change (not supported on Windows)
#define SIGTERM 15  // Termination signal (limited support on Windows)
#endif

// ============================================================================
// Theme-Aware Default Colors
// ============================================================================

/**
 * @brief Default text color for light theme (RGB)
 *
 * Used for text on light/white backgrounds. A subtle dark blue-grey that's
 * readable on light backgrounds and matches modern terminal color schemes.
 */
#define TERMINAL_COLOR_THEME_LIGHT_FG_R 65
#define TERMINAL_COLOR_THEME_LIGHT_FG_G 61
#define TERMINAL_COLOR_THEME_LIGHT_FG_B 61

/**
 * @brief Default text color for dark theme (RGB)
 *
 * Used for text on dark/black backgrounds. A light neutral color that's
 * readable on dark backgrounds and provides good contrast.
 */
#define TERMINAL_COLOR_THEME_DARK_FG_R 204
#define TERMINAL_COLOR_THEME_DARK_FG_G 204
#define TERMINAL_COLOR_THEME_DARK_FG_B 204

/**
 * @brief Default background color for light theme (RGB)
 *
 * Used for background in light/bright theme. White background for light terminals.
 */
#define TERMINAL_COLOR_THEME_LIGHT_BG_R 255
#define TERMINAL_COLOR_THEME_LIGHT_BG_G 255
#define TERMINAL_COLOR_THEME_LIGHT_BG_B 255

/**
 * @brief Default background color for dark theme (RGB)
 *
 * Used for background in dark/black theme. Black background for dark terminals.
 */
#define TERMINAL_COLOR_THEME_DARK_BG_R 0
#define TERMINAL_COLOR_THEME_DARK_BG_G 0
#define TERMINAL_COLOR_THEME_DARK_BG_B 0

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
  /** @brief Auto-detect color support from terminal capabilities */
  TERM_COLOR_AUTO = -1,
  /** @brief No color support (monochrome terminal) */
  TERM_COLOR_NONE = 0,
  /** @brief 16-color support (standard ANSI colors) */
  TERM_COLOR_16 = 1,
  /** @brief 256-color support (extended ANSI palette) */
  TERM_COLOR_256 = 2,
  /** @brief 24-bit truecolor support (RGB colors) */
  TERM_COLOR_TRUECOLOR = 3
} terminal_color_mode_t;

/**
 * @brief Monochromatic color filter enumeration
 *
 * Defines color filters for applying single-color tints to grayscale video.
 * Filters are applied server-side; clients see each user in their chosen color.
 *
 * @ingroup platform
 */
typedef enum {
  /** @brief No filtering (default) */
  COLOR_FILTER_NONE = 0,
  /** @brief Dark content on white background */
  COLOR_FILTER_BLACK = 1,
  /** @brief White content on black background */
  COLOR_FILTER_WHITE = 2,
  /** @brief Green (#00FF41) */
  COLOR_FILTER_GREEN = 3,
  /** @brief Magenta (#FF00FF) */
  COLOR_FILTER_MAGENTA = 4,
  /** @brief Fuchsia (#FF00AA) */
  COLOR_FILTER_FUCHSIA = 5,
  /** @brief Orange (#FF8800) */
  COLOR_FILTER_ORANGE = 6,
  /** @brief Teal (#00DDDD) */
  COLOR_FILTER_TEAL = 7,
  /** @brief Cyan (#00FFFF) */
  COLOR_FILTER_CYAN = 8,
  /** @brief Pink (#FFB6C1) */
  COLOR_FILTER_PINK = 9,
  /** @brief Red (#FF3333) */
  COLOR_FILTER_RED = 10,
  /** @brief Yellow (#FFEB99) */
  COLOR_FILTER_YELLOW = 11,
  /** @brief Rainbow (cycles through spectrum over 3.5s) */
  COLOR_FILTER_RAINBOW = 12,
  /** @brief Total count of filters (not a valid filter) */
  COLOR_FILTER_COUNT
} color_filter_t;

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
  /** @brief Detected color support level (terminal_color_mode_t) */
  terminal_color_mode_t color_level;
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
  /** @brief Monochromatic color filter enum value (color_filter_t) */
  color_filter_t color_filter;
  /** @brief Whether client wants frame padding (centering) - false for snapshot/piped modes */
  bool wants_padding;
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
 * @param level Color level enum value (terminal_color_mode_t)
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
const char *terminal_color_level_name(terminal_color_mode_t level);

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

/**
 * @brief Determine if color output should be used
 *
 * Priority order:
 * 1. If --color flag is set → ALWAYS use colors (force override)
 * 2. If CLAUDECODE env var is set → NEVER use colors (LLM automation)
 * 3. If output is not a TTY (piping) → NO colors
 * 4. If --color-mode=none → NO colors (user choice)
 * 5. Otherwise → Use colors
 *
 * @param fd File descriptor to check (STDOUT_FILENO or STDERR_FILENO)
 * @return true if colors should be used, false otherwise
 *
 * @ingroup platform
 */
bool terminal_should_color_output(int fd);

/**
 * @brief Get current color mode considering all overrides
 *
 * Determines effective color mode by checking:
 * 1. --color flag (force enable)
 * 2. --color-mode option (none/16/256/truecolor)
 * 3. Terminal capability detection
 *
 * @return Effective terminal_color_mode_t to use
 *
 * @ingroup platform
 */
terminal_color_mode_t terminal_get_effective_color_mode(void);

/**
 * @brief Detect if terminal theme is dark
 * @return true if terminal has a dark theme, false if light theme or unknown
 *
 * Attempts to detect terminal's color theme (dark or light background) using:
 * 1. OSC 11 escape sequence query with luminance calculation (modern terminals)
 * 2. Common environment variables (COLORFGBG, TERM_PROGRAM)
 * 3. Terminal-specific hints (iTerm2, VS Code, Konsole, etc.)
 * 4. Defaults to dark theme (most common for developer terminals)
 *
 * Used by the theme system to select appropriate colors throughout the UI:
 * - Color schemes adapt based on detected theme
 * - Highlight colors choose better contrast for the detected theme
 * - Text colors are selected to work with the background theme
 *
 * @note This is a best-effort heuristic and may not be 100% accurate
 * @note Result is cached for performance (theme doesn't change during session)
 * @note User can override via TERM_BACKGROUND environment variable
 *
 * @ingroup platform
 */
bool terminal_has_dark_background(void);

/**
 * @brief Query terminal background color using OSC 11 escape sequence
 * @param bg_r Pointer to store red component (0-255)
 * @param bg_g Pointer to store green component (0-255)
 * @param bg_b Pointer to store blue component (0-255)
 * @return true if successful, false if query failed or timed out
 *
 * Sends OSC 11 query to terminal and parses RGB response.
 * Works with modern terminals (iTerm2, kitty, Konsole, etc.)
 * Returns false if terminal doesn't support OSC 11 or times out.
 *
 * @note Requires raw terminal mode to read response
 * @note Has 100ms timeout to prevent hanging
 * @note Only works if stdout is a TTY
 *
 * @ingroup platform
 */
bool terminal_query_background_color(uint8_t *bg_r, uint8_t *bg_g, uint8_t *bg_b);

/** @} */

/* ============================================================================
 * Windows Console Resize Detection
 * @{
 *
 * Windows-specific functionality for detecting console window resize events.
 * Unix systems use SIGWINCH signal, but Windows requires polling or event
 * monitoring, so a background thread is used.
 */

/**
 * @brief Check if terminal control sequences should be used for the given fd
 * @param fd File descriptor to check (must be valid or -1)
 * @return true if terminal control sequences should be used, false otherwise
 *
 * Determines whether terminal control sequences (cursor home, clear screen, etc.)
 * should be sent to the given file descriptor. Checks:
 * 1. File descriptor is valid (>= 0)
 * 2. Not in snapshot mode
 * 3. Not in TESTING environment
 * 4. File descriptor is connected to a TTY (not piped/redirected)
 *
 * This is useful for deciding whether to send ANSI escape sequences to
 * output. When piped or redirected, escape sequences should not be sent.
 *
 * @note Returns false if fd is invalid (-1) to be safe.
 * @note Returns false in snapshot mode to provide clean output.
 * @note Checks TESTING environment variable for test/CI environments.
 * @note Primary use case: determining if output is going to a TTY.
 *
 * @ingroup platform
 */
bool terminal_should_use_control_sequences(int fd);

/* ============================================================================
 * Interactive Mode and TTY State Detection
 * @{
 *
 * Centralized functions for detecting TTY status and interactive mode.
 * These functions consolidate scattered isatty() checks throughout the codebase.
 */

/**
 * @brief Check if stdin is connected to a TTY
 * @return true if stdin is a TTY (can read interactive input), false otherwise
 *
 * Determines whether standard input is connected to a terminal device.
 * Returns false when stdin is piped or redirected from a file.
 *
 * Use this to decide whether interactive input (prompts, keyboard events) is possible.
 *
 * @note Wrapper around platform_isatty(STDIN_FILENO) for clarity
 * @note Returns false in non-interactive environments (CI, scripts, pipes)
 *
 * @ingroup platform
 */
bool terminal_is_stdin_tty(void);

/**
 * @brief Check if stdout is connected to a TTY
 * @return true if stdout is a TTY (not piped/redirected), false otherwise
 *
 * Determines whether standard output is connected to a terminal device.
 * Returns false when stdout is piped or redirected to a file.
 *
 * Use this to decide whether:
 * - Terminal control sequences (colors, cursor movement) should be used
 * - Output padding/formatting should be applied
 * - Progress bars or animations should be shown
 *
 * @note Wrapper around platform_isatty(STDOUT_FILENO) for clarity
 * @note Returns false in piped contexts (e.g., ascii-chat client | less)
 *
 * @ingroup platform
 */
bool terminal_is_stdout_tty(void);

/**
 * @brief Check if stderr is connected to a TTY
 * @return true if stderr is a TTY, false otherwise
 *
 * Determines whether standard error is connected to a terminal device.
 * Returns false when stderr is piped or redirected to a file.
 *
 * Use this to decide whether diagnostic messages should include colors.
 *
 * @note Wrapper around platform_isatty(STDERR_FILENO) for clarity
 * @note stderr is often a TTY even when stdout is piped
 *
 * @ingroup platform
 */
bool terminal_is_stderr_tty(void);

/**
 * @brief Check if the session is fully interactive
 * @return true if BOTH stdin AND stdout are TTYs, false otherwise
 *
 * Determines whether the session is fully interactive (user at terminal).
 * Returns true only when BOTH stdin and stdout are connected to TTYs.
 *
 * Use this to decide whether:
 * - User prompts and interactive input should be shown
 * - Splash screens and animations should be displayed
 * - Frame padding should be enabled
 * - Password prompts are appropriate
 *
 * Examples:
 * - `ascii-chat mirror` in terminal → true (interactive)
 * - `ascii-chat mirror | less` → false (stdout piped)
 * - `cat file | ascii-chat client` → false (stdin piped)
 * - `ascii-chat client < input.txt > output.txt` → false (both piped)
 *
 * @note Equivalent to: terminal_is_stdin_tty() && terminal_is_stdout_tty()
 * @note Used for enabling interactive features (prompts, padding, splash)
 * @note Snapshot mode is checked separately via GET_OPTION(snapshot_mode)
 *
 * @ingroup platform
 */
bool terminal_is_interactive(void);

/**
 * @brief Check if stdout is piped or redirected
 * @return true if stdout is piped/redirected, false if connected to TTY
 *
 * Determines whether standard output is being piped or redirected to a file.
 * This is the logical inverse of terminal_is_stdout_tty().
 *
 * Use this to decide whether:
 * - Logs should be forced to stderr (to avoid corrupting piped output)
 * - Frame padding should be disabled
 * - Clean, parseable output should be preferred
 *
 * Examples:
 * - `ascii-chat mirror > output.txt` → true (redirected)
 * - `ascii-chat client | grep pattern` → true (piped)
 * - `ascii-chat mirror` in terminal → false (TTY)
 *
 * @note Equivalent to: !terminal_is_stdout_tty()
 * @note Common pattern: force stderr when piped to prevent data corruption
 *
 * @ingroup platform
 */
bool terminal_is_piped_output(void);

/**
 * @brief Determine if logs should be forced to stderr
 * @return true if logs should go to stderr only, false if stdout is acceptable
 *
 * Determines whether logging output should be forced to stderr instead of stdout.
 * Returns true when stdout is piped/redirected (to avoid corrupting output data).
 *
 * Use this to decide whether to call log_set_force_stderr(true).
 *
 * Logic:
 * 1. If stdout is piped/redirected → force stderr (true)
 * 2. If in TESTING environment → allow stdout (false)
 * 3. Otherwise → allow stdout (false)
 *
 * Examples:
 * - `ascii-chat mirror > frames.txt` → true (don't corrupt frame data)
 * - `ascii-chat mirror` in terminal → false (stdout is fine)
 * - `TESTING=1 ascii-chat mirror` → false (tests may capture stdout)
 *
 * @note This prevents logs from corrupting piped ASCII frame data
 * @note TESTING environment variable bypasses this for test environments
 * @note Typical usage: if (terminal_should_force_stderr()) log_set_force_stderr(true);
 *
 * @ingroup platform
 */
bool terminal_should_force_stderr(void);

/**
 * @brief Choose output file descriptor for logging based on level and interactivity
 * @param level Log level (determines default routing: WARN+ to stderr, others to stdout)
 * @return STDERR_FILENO or STDOUT_FILENO based on log level and terminal state
 *
 * Routes logs appropriately:
 * - When terminal is NOT interactive (piped): ALL logs to stderr
 * - When force_stderr enabled: ALL logs to stderr
 * - Otherwise: WARN/ERROR/FATAL to stderr, others to stdout
 *
 * This consolidates log routing logic used throughout the codebase.
 *
 * @note Use: int fd = terminal_choose_log_fd(LOG_INFO);
 * @note Common usage: platform_write_all(terminal_choose_log_fd(level), data, len);
 *
 * @ingroup platform
 */
int terminal_choose_log_fd(log_level_t level);

/**
 * @brief Determine if interactive user prompts are appropriate
 * @return true if prompts can be shown, false if non-interactive/automated
 *
 * Determines whether interactive user prompts (yes/no, passwords, confirmations)
 * should be displayed. Returns false in non-interactive or automated contexts.
 *
 * Use this to decide whether:
 * - Password prompts should be shown (or auto-cancel)
 * - Yes/No confirmations should wait for input (or use defaults)
 * - Known hosts prompts should be interactive (or auto-deny)
 *
 * Logic:
 * 1. If not fully interactive (stdin or stdout not TTY) → false
 * 2. If in snapshot mode (--snapshot) → false
 * 3. If ASCII_CHAT_QUESTION_PROMPT_RESPONSE set → false (automated responses)
 * 4. Otherwise → true (interactive prompts OK)
 *
 * Examples:
 * - `ascii-chat client` in terminal → true (can prompt)
 * - `ascii-chat client --snapshot` → false (non-interactive mode)
 * - `echo data | ascii-chat client` → false (stdin piped)
 * - `ASCII_CHAT_QUESTION_PROMPT_RESPONSE='y' ascii-chat client` → false (automated)
 * - `ascii-chat client > output.txt` → false (stdout redirected)
 *
 * @note Combines checks for: TTY status, snapshot mode, automation env vars
 * @note Used by password prompts, known_hosts verification, user confirmations
 * @note When false, password prompts should auto-cancel with error
 * @note When false, yes/no prompts should use default value or deny
 *
 * @ingroup platform
 */
bool terminal_can_prompt_user(void);

/* ============================================================================
 * Renderer Color Selection (Cross-Platform Abstraction)
 * ============================================================================ */

/**
 * @brief Get theme-aware default foreground color for pixel renderers
 * @param theme Terminal theme (0=dark, 1=light, 2=auto)
 * @param out_r Pointer to store red component (0-255)
 * @param out_g Pointer to store green component (0-255)
 * @param out_b Pointer to store blue component (0-255)
 *
 * Used by both Linux and macOS renderers for consistent color selection.
 * Returns appropriate text color based on terminal theme.
 *
 * @ingroup platform
 */
void terminal_get_default_foreground_color(int theme, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b);

/**
 * @brief Get theme-aware default background color for pixel renderers
 * @param theme Terminal theme (0=dark, 1=light, 2=auto)
 * @param out_r Pointer to store red component (0-255)
 * @param out_g Pointer to store green component (0-255)
 * @param out_b Pointer to store blue component (0-255)
 *
 * Used by both Linux and macOS renderers for consistent color selection.
 * Returns appropriate background color based on terminal theme.
 *
 * @ingroup platform
 */
void terminal_get_default_background_color(int theme, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b);

/** @} */

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
