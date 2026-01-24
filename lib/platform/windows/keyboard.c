/**
 * @file platform/windows/keyboard.c
 * @brief Windows keyboard input implementation using _kbhit() and _getch()
 * @ingroup platform
 */

#include "../keyboard.h"
#include "../../common.h"

#include <conio.h>
#include <windows.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static DWORD g_original_console_mode = 0;
static HANDLE g_console_input = NULL;
static bool g_keyboard_initialized = false;

/* ============================================================================
 * Keyboard Functions
 * ============================================================================ */

int keyboard_init(void) {
  if (g_keyboard_initialized) {
    return 0; // Already initialized
  }

  // Get handle to standard input
  g_console_input = GetStdHandle(STD_INPUT_HANDLE);
  if (g_console_input == INVALID_HANDLE_VALUE) {
    log_error("Failed to get console input handle");
    return -1;
  }

  // Get current console mode
  if (!GetConsoleMode(g_console_input, &g_original_console_mode)) {
    log_error("Failed to get console mode");
    return -1;
  }

  // Disable line input buffering mode
  // We want: raw mode (no ENABLE_LINE_INPUT)
  //          no echo (no ENABLE_ECHO_INPUT)
  //          but keep ENABLE_PROCESSED_INPUT to handle Ctrl+C
  DWORD new_mode = g_original_console_mode;
  new_mode &= ~ENABLE_LINE_INPUT; // Disable line buffering
  new_mode &= ~ENABLE_ECHO_INPUT; // Disable echo
  // Keep ENABLE_PROCESSED_INPUT for signal handling

  if (!SetConsoleMode(g_console_input, new_mode)) {
    log_error("Failed to set console mode");
    return -1;
  }

  g_keyboard_initialized = true;
  return 0;
}

void keyboard_cleanup(void) {
  if (!g_keyboard_initialized) {
    return;
  }

  // Restore original console mode
  if (g_console_input != NULL && g_console_input != INVALID_HANDLE_VALUE) {
    if (!SetConsoleMode(g_console_input, g_original_console_mode)) {
      log_error("Failed to restore console mode");
    }
  }

  g_keyboard_initialized = false;
}

int keyboard_read_nonblocking(void) {
  if (!g_keyboard_initialized) {
    return KEY_NONE;
  }

  // Check if input is available using _kbhit
  if (!_kbhit()) {
    return KEY_NONE;
  }

  // Input is available, read it
  int ch = _getch();
  if (ch < 0) {
    return KEY_NONE;
  }

  // Handle regular characters
  if (ch == ' ') {
    return KEY_SPACE;
  }
  if (ch == 27) {
    return KEY_ESCAPE;
  }

  // Handle Windows extended key codes (0xE0 prefix)
  if (ch == 0xE0 || ch == 0x00) {
    // Extended key code - read the actual key
    if (!_kbhit()) {
      return KEY_NONE; // Not enough data
    }

    int extended = _getch();
    if (extended < 0) {
      return KEY_NONE;
    }

    // Map extended key codes to arrow keys
    switch (extended) {
    case 72: // Up arrow
      return KEY_UP;
    case 80: // Down arrow
      return KEY_DOWN;
    case 75: // Left arrow
      return KEY_LEFT;
    case 77: // Right arrow
      return KEY_RIGHT;
    default:
      // Unknown extended key
      return KEY_NONE;
    }
  }

  // Return regular ASCII character
  return ch;
}
