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
 * - ASCII/UTF-8 characters: Raw character code (1-26, 33-127)
 *
 * @note UTF-8 multibyte sequences are not currently supported in return values.
 *       Regular ASCII input (0-127) is returned as-is; all other bytes return KEY_NONE.
 *
 * @ingroup platform
 */
typedef enum {
  KEY_NONE = 0,       ///< No key pressed or no input available
  KEY_ESCAPE = 27,    ///< Escape key (ESC)
  KEY_SPACE = 32,     ///< Space bar
  KEY_UP = 256,       ///< Up arrow key
  KEY_DOWN = 257,     ///< Down arrow key
  KEY_LEFT = 258,     ///< Left arrow key
  KEY_RIGHT = 259,    ///< Right arrow key
  KEY_C = 'c',        ///< 'c' key - cycle color modes
  KEY_M = 'm',        ///< 'm' key - toggle mute
  KEY_F = 'f',        ///< 'f' key - flip webcam
  KEY_QUESTION = '?', ///< '?' key - show help screen
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
 * and paired with keyboard_cleanup() for proper terminal restoration.
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
 * @note Must be paired with keyboard_cleanup() before program exit
 * @note Calling multiple times is safe (reference-counted, idempotent)
 * @note Terminal state is restored by keyboard_cleanup()
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
void keyboard_cleanup(void);

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

/** @} */
