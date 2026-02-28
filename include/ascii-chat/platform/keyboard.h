#pragma once

/**
 * @file platform/keyboard.h
 * @brief 🎮 Cross-platform keyboard input interface for ascii-chat
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * This header provides unified keyboard input operations for interactive
 * controls during media playback and rendering. Supports both POSIX and
 * Windows platforms with consistent key code mappings.
 *
 * The interface provides:
 * - Non-blocking keyboard input detection
 * - Unified key code enumeration
 * - Platform-specific escape sequence handling
 * - Terminal raw mode setup/cleanup
 * - UTF-8 character support
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "../common/error_codes.h"

/* ============================================================================
 * Keyboard Key Enumeration
 * ============================================================================ */

/**
 * @brief Unified keyboard key code enumeration
 *
 * Maps keyboard input to unified key codes. Arrow keys and special keys
 * are abstracted across POSIX (escape sequences) and Windows (extended codes).
 *
 * **Return Value Ranges:**
 * - `KEY_NONE` (0): No key available
 * - `KEY_ESCAPE` (27): Escape key
 * - `KEY_SPACE` (32): Space bar
 * - Arrow keys: `KEY_UP`/`KEY_DOWN`/`KEY_LEFT`/`KEY_RIGHT` (256-259)
 * - Function keys: `KEY_DELETE` (260), `KEY_HOME` (261), `KEY_END` (262), `KEY_CTRL_DELETE` (263)
 * - ASCII/UTF-8 characters: Raw character code (1-26, 33-127)
 *
 * @note UTF-8 multibyte sequences are not currently supported in return values.
 *       Regular ASCII input (0-127) is returned as-is; all other bytes return KEY_NONE.
 *
 * @ingroup platform
 */
typedef enum {
  KEY_NONE = 0,          ///< No key pressed or no input available
  KEY_ESCAPE = 27,       ///< Escape key (ESC)
  KEY_SPACE = 32,        ///< Space bar
  KEY_UP = 256,          ///< Up arrow key
  KEY_DOWN = 257,        ///< Down arrow key
  KEY_LEFT = 258,        ///< Left arrow key
  KEY_RIGHT = 259,       ///< Right arrow key
  KEY_DELETE = 260,      ///< Delete key (forward delete)
  KEY_HOME = 261,        ///< Home key (move to start of line)
  KEY_END = 262,         ///< End key (move to end of line)
  KEY_CTRL_DELETE = 263, ///< Ctrl+Delete (delete word forward)
  KEY_0 = '0',           ///< '0' key - toggle matrix rain effect
  KEY_C = 'c',           ///< 'c' key - cycle color modes
  KEY_R = 'r',           ///< 'r' key - cycle render modes
  KEY_M = 'm',           ///< 'm' key - toggle mute
  KEY_F = 'f',           ///< 'f' key - flip webcam
  KEY_QUESTION = '?',    ///< '?' key - show help screen
  KEY_BACKTICK = '`',    ///< '`' key - print lock state (debug builds)
} keyboard_key_t;

/* ============================================================================
 * Keyboard Functions
 * @{
 */

/**
 * @brief Initialize keyboard input system
 * @return `ASCIICHAT_OK` on success, or error code on failure
 *
 * Sets up terminal for keyboard input by enabling raw mode (character-by-character
 * input without line buffering or echo). Must be called before keyboard_read_nonblocking()
 * and paired with keyboard_destroy() for proper terminal restoration.
 *
 * **Errors:**
 * - `ERROR_PLATFORM_INIT`: Terminal attribute query or configuration failed
 * - `ERROR_GENERAL`: System call failed (see asciichat_errno for details)
 *
 * **Platform behavior:**
 * - POSIX: Uses tcgetattr/tcsetattr to set raw mode (ICANON/ECHO disabled)
 * - POSIX: Sets stdin to non-blocking mode with fcntl(F_SETFL, O_NONBLOCK)
 * - Windows: Uses GetStdHandle(STD_INPUT_HANDLE) and SetConsoleMode()
 * - Windows: Disables ENABLE_LINE_INPUT and ENABLE_ECHO_INPUT modes
 *
 * **Error handling:**
 * - Automatically sets asciichat_errno on failure with context
 * - Call HAS_ERRNO() to check for error details after failure
 *
 * @note Must be paired with keyboard_destroy() before program exit
 * @note Calling multiple times is safe (reference-counted, idempotent)
 * @note Terminal state is restored by keyboard_destroy()
 * @note Safe to call before or after calling keyboard_read_nonblocking()
 *
 * @ingroup platform
 */
asciichat_error_t keyboard_init(void);

/**
 * @brief Cleanup keyboard input system and restore terminal
 *
 * Restores terminal to original state (canonical mode with echo enabled).
 * Must be called after keyboard_init() to prevent terminal corruption
 * on program exit.
 *
 * **Platform behavior:**
 * - POSIX: Restores original termios settings via tcsetattr
 * - Windows: Restores original console mode
 *
 * @note Safe to call multiple times (no-op if not initialized)
 * @note Safe to call even if keyboard_init() failed
 *
 * @ingroup platform
 */
void keyboard_destroy(void);

/**
 * @brief Read next keyboard input without blocking
 * @return Keyboard key code (keyboard_key_t) or KEY_NONE if no input
 *
 * Checks for available keyboard input and returns immediately. Returns
 * KEY_NONE if no input is currently available. This is a non-blocking
 * operation suitable for integration into render loops.
 *
 * **Supported input:**
 * - Arrow keys: KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT
 * - Special keys: KEY_ESCAPE, KEY_SPACE
 * - ASCII characters: Returned as raw character codes (a-z, A-Z, 0-9, etc.)
 * - Control keys: Ctrl+C, Ctrl+Z, and other control sequences (codes 1-31)
 * - UTF-8: Not currently supported (non-ASCII bytes return KEY_NONE)
 *
 * **Platform behavior:**
 * - POSIX: Uses select() with zero timeout on stdin for non-blocking check
 * - POSIX: Parses ESC escape sequences for arrow keys (ESC [ A/B/C/D)
 * - POSIX: 50ms timeout per escape sequence byte to distinguish ESC key from sequences
 * - Windows: Uses _kbhit() and _getch() for non-blocking input
 * - Windows: Handles 0xE0 and 0x00 extended key prefixes for arrow keys
 * - Windows: Arrow key mappings: 72/up, 80/down, 75/left, 77/right
 *
 * **Return behavior:**
 * - Returns immediately with KEY_NONE if no input available (non-blocking)
 * - Returns KEY_NONE if keyboard_init() was never called (safe, graceful)
 * - Returns ASCII character code unchanged (e.g., 'c'=99, 'm'=109, '5'=53)
 * - Arrow key sequences are fully consumed before returning
 *
 * @note Safe to call without prior keyboard_init() (returns KEY_NONE)
 * @note Thread-safe; uses mutex to check initialization state
 * @note Not suitable for high-frequency polling (CPU overhead); use in 60 FPS loops
 * @note Assumes terminal is in raw mode (set by keyboard_init())
 *
 * @ingroup platform
 */
keyboard_key_t keyboard_read_nonblocking(void);

/**
 * @brief Read keyboard input with timeout
 * @param timeout_ms Timeout in milliseconds (0 for non-blocking)
 * @return Keyboard key code or KEY_NONE if timeout
 *
 * Waits up to timeout_ms for keyboard input. Returns immediately if input
 * is available, or after timeout if no input. Use this for event-driven
 * input handling where you want to wait for keypresses.
 */
keyboard_key_t keyboard_read_with_timeout(uint32_t timeout_ms);

/* ============================================================================
 * Interactive Line Editing
 * @{
 */

/**
 * @brief Options for interactive line editing
 *
 * Configuration structure for keyboard_read_line_interactive(). Provides
 * control over echo behavior, masking, prefix display, and validation.
 *
 * @ingroup platform
 */
typedef struct {
  char *buffer;                    ///< Input buffer (modified in-place)
  size_t max_len;                  ///< Maximum buffer size (including null terminator)
  size_t *len;                     ///< Current length in bytes (in/out parameter)
  size_t *cursor;                  ///< Cursor position in bytes (in/out parameter)
  bool echo;                       ///< Echo characters to terminal
  char mask_char;                  ///< Mask character (0 for no masking, '*' for passwords)
  const char *prefix;              ///< Prefix to display (e.g., "/" for grep), NULL for none
  bool (*validator)(const char *); ///< Optional validator callback (for live feedback)
  keyboard_key_t key;              ///< Pre-read key (use this instead of reading if not KEY_NONE)
} keyboard_line_edit_opts_t;

/**
 * @brief Result codes for interactive line editing
 *
 * Return values for keyboard_read_line_interactive() indicating the current
 * state of the editing session after processing one keystroke.
 *
 * @ingroup platform
 */
typedef enum {
  LINE_EDIT_CONTINUE,  ///< Keep editing (more input needed)
  LINE_EDIT_ACCEPTED,  ///< User pressed Enter (accept input)
  LINE_EDIT_CANCELLED, ///< User pressed Escape/Ctrl+C (cancel input)
  LINE_EDIT_NO_INPUT   ///< No key available (non-blocking mode)
} keyboard_line_edit_result_t;

/**
 * @brief Process one keystroke for interactive line editing
 * @param opts Line editing options (must not be NULL)
 * @return Result code indicating editing status
 *
 * Non-blocking line editor that processes one keystroke per call. Supports
 * full text editing with cursor movement, character insertion/deletion,
 * and UTF-8 multi-byte sequences.
 *
 * **Supported editing operations:**
 * - **Backspace** (8/127): Delete character before cursor
 * - **Delete** (ESC[3~): Delete character at cursor
 * - **Left/Right arrows**: Move cursor
 * - **Home/End**: Jump to start/end of line
 * - **Enter**: Accept input (return LINE_EDIT_ACCEPTED)
 * - **Escape/Ctrl+C**: Cancel input (return LINE_EDIT_CANCELLED)
 * - **Printable characters**: Insert at cursor position
 * - **UTF-8 multi-byte**: Full support for non-ASCII characters
 *
 * **Display behavior:**
 * - If `opts.echo` is true, characters are displayed as typed
 * - If `opts.mask_char` is non-zero, characters are masked (e.g., '*' for passwords)
 * - If `opts.prefix` is non-NULL, it's displayed before the input (e.g., "/" for grep)
 * - If `opts.validator` is non-NULL, it's called on every change (for live validation)
 *
 * **Non-blocking design:**
 * - Returns immediately if no input available (LINE_EDIT_NO_INPUT)
 * - Suitable for integration with render loops
 * - Call repeatedly in a loop until LINE_EDIT_ACCEPTED or LINE_EDIT_CANCELLED
 *
 * **Errors:**
 * - Returns LINE_EDIT_NO_INPUT if keyboard not initialized
 * - Returns LINE_EDIT_NO_INPUT if opts is NULL or buffer is NULL
 *
 * @note Terminal must be in raw mode (call keyboard_init() first)
 * @note The buffer is modified in-place as user types
 * @note len and cursor are updated to reflect current state
 * @note Thread-safe (but only one editing session should be active at a time)
 *
 * @par Example
 * @code
 * // Interactive grep input
 * char pattern[256] = {0};
 * size_t len = 0;
 * size_t cursor = 0;
 *
 * keyboard_line_edit_opts_t opts = {
 *     .buffer = pattern,
 *     .max_len = sizeof(pattern),
 *     .len = &len,
 *     .cursor = &cursor,
 *     .echo = false,       // We render ourselves
 *     .mask_char = 0,      // No masking
 *     .prefix = "/",       // Show "/" prefix
 *     .validator = validate_pattern  // Optional validator
 * };
 *
 * while (true) {
 *     keyboard_line_edit_result_t result = keyboard_read_line_interactive(&opts);
 *     switch (result) {
 *         case LINE_EDIT_ACCEPTED:
 *             // User pressed Enter - pattern is in buffer
 *             apply_pattern(pattern);
 *             return;
 *         case LINE_EDIT_CANCELLED:
 *             // User pressed Escape - restore previous state
 *             restore_previous();
 *             return;
 *         case LINE_EDIT_CONTINUE:
 *             // Still editing - re-render display
 *             render_input_line(pattern, cursor);
 *             break;
 *         case LINE_EDIT_NO_INPUT:
 *             // No input - continue loop
 *             break;
 *     }
 * }
 * @endcode
 *
 * @ingroup platform
 */
keyboard_line_edit_result_t keyboard_read_line_interactive(keyboard_line_edit_opts_t *opts);

/** @} */
