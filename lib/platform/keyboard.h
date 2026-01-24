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
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stdbool.h>

/* ============================================================================
 * Keyboard Key Enumeration
 * ============================================================================ */

/**
 * @brief Unified keyboard key code enumeration
 *
 * Maps keyboard input to unified key codes. Arrow keys and special keys
 * are abstracted across POSIX (escape sequences) and Windows (extended codes).
 *
 * @ingroup platform
 */
typedef enum {
  KEY_NONE = 0,         ///< No key pressed
  KEY_ESCAPE = 27,      ///< Escape key
  KEY_SPACE = 32,       ///< Space bar
  KEY_UP = 256,         ///< Up arrow key
  KEY_DOWN = 257,       ///< Down arrow key
  KEY_LEFT = 258,       ///< Left arrow key
  KEY_RIGHT = 259,      ///< Right arrow key
  KEY_CHAR_BASE = 1000, ///< Base for regular ASCII characters
} keyboard_key_t;

/* ============================================================================
 * Keyboard Functions
 * @{
 */

/**
 * @brief Initialize keyboard input system
 * @return 0 on success, -1 on failure
 *
 * Sets up terminal for keyboard input by enabling raw mode (character-by-character
 * input without line buffering or echo). Must be called before keyboard_read_nonblocking()
 * and paired with keyboard_cleanup() for proper terminal restoration.
 *
 * **Platform behavior:**
 * - POSIX: Uses tcgetattr/tcsetattr to set raw mode (ICANON/ECHO disabled)
 * - Windows: Uses SetConsoleMode to disable line input buffering
 *
 * @note Must be paired with keyboard_cleanup() before program exit
 * @note Calling multiple times is safe (no-op if already initialized)
 * @note Terminal state is restored by keyboard_cleanup()
 *
 * @ingroup platform
 */
int keyboard_init(void);

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
 * **Arrow keys:**
 * - Up arrow → KEY_UP
 * - Down arrow → KEY_DOWN
 * - Left arrow → KEY_LEFT
 * - Right arrow → KEY_RIGHT
 *
 * **Special keys:**
 * - Escape → KEY_ESCAPE
 * - Space → KEY_SPACE
 * - Regular ASCII → Returned as-is ('c', 'm', 'f', etc.)
 *
 * **Platform behavior:**
 * - POSIX: Uses select() with zero timeout on stdin
 * - POSIX: Parses ESC escape sequences for arrow keys
 * - Windows: Uses _kbhit() and _getch() for non-blocking input
 * - Windows: Handles 0xE0 extended key prefix for arrows
 *
 * @note Requires prior call to keyboard_init()
 * @note Returns KEY_NONE if no input available (non-blocking)
 * @note Returns regular ASCII characters unchanged (e.g., 'c', 'm', 'f')
 * @note Arrow keys are parsed from multi-byte escape sequences
 *
 * @ingroup platform
 */
int keyboard_read_nonblocking(void);

/** @} */
