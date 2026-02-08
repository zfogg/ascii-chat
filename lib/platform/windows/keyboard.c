/**
 * @file platform/windows/keyboard.c
 * @brief Windows keyboard input implementation using _kbhit() and _getch()
 * @ingroup platform
 */

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/utf8.h>

#include <conio.h>
#include <windows.h>
#include <string.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static DWORD g_original_console_mode = 0;
static HANDLE g_console_input = NULL;
// Keyboard initialization reference counting (supports multiple init/cleanup pairs)
static unsigned int g_keyboard_init_refcount = 0;
static static_mutex_t g_keyboard_init_mutex = STATIC_MUTEX_INIT;

/* ============================================================================
 * Keyboard Functions
 * ============================================================================ */

asciichat_error_t keyboard_init(void) {
  static_mutex_lock(&g_keyboard_init_mutex);

  // If already initialized, just increment refcount
  if (g_keyboard_init_refcount > 0) {
    g_keyboard_init_refcount++;
    static_mutex_unlock(&g_keyboard_init_mutex);
    return ASCIICHAT_OK;
  }

  static_mutex_unlock(&g_keyboard_init_mutex);

  // Get handle to standard input
  g_console_input = GetStdHandle(STD_INPUT_HANDLE);
  if (g_console_input == INVALID_HANDLE_VALUE) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to get console input handle");
  }

  // Get current console mode
  if (!GetConsoleMode(g_console_input, &g_original_console_mode)) {
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to get console mode");
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
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to set console mode");
  }

  // Mark as initialized with reference counting
  static_mutex_lock(&g_keyboard_init_mutex);
  g_keyboard_init_refcount = 1;
  static_mutex_unlock(&g_keyboard_init_mutex);

  return ASCIICHAT_OK;
}

void keyboard_destroy(void) {
  static_mutex_lock(&g_keyboard_init_mutex);
  if (g_keyboard_init_refcount == 0) {
    static_mutex_unlock(&g_keyboard_init_mutex);
    return;
  }

  g_keyboard_init_refcount--;
  static_mutex_unlock(&g_keyboard_init_mutex);

  // Restore original console mode
  if (g_console_input != NULL && g_console_input != INVALID_HANDLE_VALUE) {
    if (!SetConsoleMode(g_console_input, g_original_console_mode)) {
      log_error("Failed to restore console mode");
    }
  }
}

keyboard_key_t keyboard_read_nonblocking(void) {
  // Check if keyboard is initialized with reference counting
  static_mutex_lock(&g_keyboard_init_mutex);
  bool is_initialized = (g_keyboard_init_refcount > 0);
  static_mutex_unlock(&g_keyboard_init_mutex);

  if (!is_initialized) {
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

  // Handle special characters
  if (ch == ' ') {
    return KEY_SPACE;
  }
  if (ch == 27) {
    return KEY_ESCAPE;
  }

  // Handle Windows extended key codes (0xE0 or 0x00 prefix)
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

  // Return regular ASCII character (including control characters 1-31, printable 32-126)
  return (keyboard_key_t)ch;
}

/* ============================================================================
 * Interactive Line Editing Implementation
 * ============================================================================ */

keyboard_line_edit_result_t keyboard_read_line_interactive(keyboard_line_edit_opts_t *opts) {
  // Validate options
  if (!opts || !opts->buffer || !opts->len || !opts->cursor) {
    return LINE_EDIT_NO_INPUT;
  }

  // Check if keyboard is initialized
  static_mutex_lock(&g_keyboard_init_mutex);
  bool is_initialized = (g_keyboard_init_refcount > 0);
  static_mutex_unlock(&g_keyboard_init_mutex);

  if (!is_initialized) {
    return LINE_EDIT_NO_INPUT;
  }

  int c;

  // Use pre-read key if provided, otherwise read from console
  if (opts->key != KEY_NONE) {
    c = opts->key;
  } else {
    // Check if input is available (non-blocking)
    if (!_kbhit()) {
      return LINE_EDIT_NO_INPUT;
    }

    // Read one character
    c = _getch();
    if (c < 0) {
      return LINE_EDIT_NO_INPUT;
    }
  }

  size_t len = *opts->len;
  size_t cursor = *opts->cursor;
  char *buffer = opts->buffer;
  size_t max_len = opts->max_len;

  // Handle newline/carriage return (Enter key) - accept input
  if (c == '\r' || c == '\n') {
    return LINE_EDIT_ACCEPTED;
  }

  // Handle Ctrl+C (interrupt) - cancel
  if (c == 3) {
    return LINE_EDIT_CANCELLED;
  }

  // Handle escape key - cancel
  if (c == 27) {
    return LINE_EDIT_CANCELLED;
  }

  // Handle Windows extended key codes (0xE0 or 0x00 prefix)
  if (c == 0xE0 || c == 0x00) {
    // Extended key code - read the actual key
    if (!_kbhit()) {
      return LINE_EDIT_CONTINUE; // Not enough data yet
    }

    int extended = _getch();
    if (extended < 0) {
      return LINE_EDIT_CONTINUE;
    }

    // Map extended key codes
    switch (extended) {
    case 75: // Left arrow
      if (cursor > 0) {
        cursor--;
        *opts->cursor = cursor;
      }
      return LINE_EDIT_CONTINUE;
    case 77: // Right arrow
      if (cursor < len) {
        cursor++;
        *opts->cursor = cursor;
      }
      return LINE_EDIT_CONTINUE;
    case 83: // Delete key
      if (cursor < len) {
        // Shift characters left
        memmove(&buffer[cursor], &buffer[cursor + 1], len - cursor - 1);
        len--;
        *opts->len = len;
      }
      return LINE_EDIT_CONTINUE;
    case 71: // Home
      cursor = 0;
      *opts->cursor = cursor;
      return LINE_EDIT_CONTINUE;
    case 79: // End
      cursor = len;
      *opts->cursor = cursor;
      return LINE_EDIT_CONTINUE;
    default:
      // Unknown extended key
      return LINE_EDIT_CONTINUE;
    }
  }

  // Handle backspace (BS = 8)
  if (c == 8) {
    if (cursor > 0) {
      // Shift characters left
      memmove(&buffer[cursor - 1], &buffer[cursor], len - cursor);
      cursor--;
      len--;
      *opts->len = len;
      *opts->cursor = cursor;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Ignore other control characters (except tab)
  if (c < 32 && c != '\t') {
    return LINE_EDIT_CONTINUE;
  }

  // Determine how many continuation bytes are needed for this character
  int continuation_bytes = utf8_continuation_bytes_needed((unsigned char)c);
  if (continuation_bytes < 0) {
    // Invalid UTF-8 start byte, skip it
    return LINE_EDIT_CONTINUE;
  }

  // Insert first byte (or ASCII character) at cursor position
  if (len < max_len - 1) {
    // Shift characters right to make room for this byte
    memmove(&buffer[cursor + 1], &buffer[cursor], len - cursor);
    buffer[cursor] = (char)c;
    len++;
    cursor++;

    // Read continuation bytes for multi-byte UTF-8 if needed
    if (continuation_bytes > 0) {
      if (utf8_read_and_insert_continuation_bytes(buffer, &cursor, &len, max_len, continuation_bytes, _getch) < 0) {
        // EOF or buffer overflow during continuation bytes
        // Roll back the first byte
        memmove(&buffer[cursor - 1], &buffer[cursor], len - cursor);
        len--;
        cursor--;
      }
    }

    *opts->len = len;
    *opts->cursor = cursor;

    // Null-terminate
    buffer[len] = '\0';
  }

  return LINE_EDIT_CONTINUE;
}
