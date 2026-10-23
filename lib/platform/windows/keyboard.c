/**
 * @file platform/windows/keyboard.c
 * @brief Windows keyboard input implementation using Unicode console events
 * @ingroup platform
 */

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/lifecycle.h>
#include <ascii-chat/util/utf8.h>

#include <conio.h>
#include <windows.h>
#include <string.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static DWORD g_original_console_mode = 0;
static HANDLE g_console_input = NULL;
// Keyboard lifecycle (thread-safe init/cleanup)
static lifecycle_t g_keyboard_lc = LIFECYCLE_INIT;

/* ============================================================================
 * Keyboard Functions
 * ============================================================================ */

asciichat_error_t keyboard_init(void) {
  // If already initialized, return success
  if (lifecycle_is_initialized(&g_keyboard_lc)) {
    return ASCIICHAT_OK;
  }

  // CAS to claim initialization
  if (!lifecycle_init(&g_keyboard_lc, "keyboard")) {
    return ASCIICHAT_OK; // Already initialized
  }

  // We won the init race - do the actual work

  // Get handle to standard input
  g_console_input = GetStdHandle(STD_INPUT_HANDLE);
  if (g_console_input == INVALID_HANDLE_VALUE) {
    lifecycle_init_abort(&g_keyboard_lc);
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to get console input handle");
  }

  // Get current console mode
  if (!GetConsoleMode(g_console_input, &g_original_console_mode)) {
    lifecycle_init_abort(&g_keyboard_lc);
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to get console mode");
  }

  // Disable line input buffering mode
  // We want: raw mode (no ENABLE_LINE_INPUT)
  //          no echo (no ENABLE_ECHO_INPUT)
  //          but keep ENABLE_PROCESSED_INPUT to handle Ctrl+C
  DWORD new_mode = g_original_console_mode;
  new_mode &= ~(DWORD)ENABLE_LINE_INPUT; // Disable line buffering
  new_mode &= ~(DWORD)ENABLE_ECHO_INPUT; // Disable echo
  // Keep ENABLE_PROCESSED_INPUT for Ctrl+C signal handling

  if (!SetConsoleMode(g_console_input, new_mode)) {
    lifecycle_init_abort(&g_keyboard_lc);
    return SET_ERRNO(ERROR_PLATFORM_INIT, "Failed to set console mode");
  }

  keyboard_stream_reset();
  log_debug("Windows keyboard initialized: console=%p", g_console_input);
  platform_write_all(STDERR_FILENO, "\033[?2004h", 8);
  return ASCIICHAT_OK;
}

void keyboard_destroy(void) {
  if (!lifecycle_shutdown(&g_keyboard_lc)) {
    return; // Not initialized
  }

  platform_write_all(STDERR_FILENO, "\033[?2004l", 8);
  keyboard_stream_reset();
  // Restore console mode
  if (g_console_input != NULL && g_console_input != INVALID_HANDLE_VALUE) {
    if (!SetConsoleMode(g_console_input, g_original_console_mode)) {
      log_error("Failed to restore console mode");
    }
  }
}

static int keyboard_raw_read(void) {
  static unsigned char utf8[4];
  static int next, count;
  static wchar_t high;
  static KEY_EVENT_RECORD repeated;
  static INPUT_RECORD events[128];
  static DWORD event_next, event_count;
  if (next < count)
    return utf8[next++];
  if (!lifecycle_is_initialized(&g_keyboard_lc))
    return 0;
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    KEY_EVENT_RECORD key;
    if (repeated.wRepeatCount) {
      key = repeated;
      --repeated.wRepeatCount;
    } else {
      // A frontend reply arrives as a burst of key events. Drain that burst in
      // one console RPC instead of round-tripping once for every reply byte.
      if (event_next == event_count) {
        event_next = event_count = 0;
        if (WaitForSingleObject(g_console_input, 0) != WAIT_OBJECT_0 ||
            !ReadConsoleInputW(g_console_input, events, sizeof(events) / sizeof(events[0]), &event_count) ||
            !event_count)
          return 0;
      }
      INPUT_RECORD event = events[event_next++];
      if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
        continue;
      key = event.Event.KeyEvent;
      repeated = key;
      repeated.wRepeatCount = key.wRepeatCount > 0 ? (WORD)(key.wRepeatCount - 1) : 0;
    }
    if (key.wVirtualKeyCode == VK_RETURN && (key.dwControlKeyState & SHIFT_PRESSED))
      return KEY_SHIFT_ENTER;
    wchar_t ch = key.uChar.UnicodeChar;
    if (!ch) {
      switch (key.wVirtualKeyCode) {
      case VK_UP:
        return KEY_UP;
      case VK_DOWN:
        return KEY_DOWN;
      case VK_LEFT:
        return KEY_LEFT;
      case VK_RIGHT:
        return KEY_RIGHT;
      case VK_DELETE:
        return key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED) ? KEY_CTRL_DELETE : KEY_DELETE;
      case VK_HOME:
        return KEY_HOME;
      case VK_END:
        return KEY_END;
      case VK_PRIOR:
        return KEY_PAGEUP;
      case VK_NEXT:
        return KEY_PAGEDOWN;
      case VK_F12:
        return KEY_F12;
      default:
        continue;
      }
    }
    if (ch >= 0xd800 && ch <= 0xdbff) {
      high = ch;
      continue;
    }
    wchar_t wide[2] = {ch, 0};
    int length = 1;
    if (high) {
      wide[0] = high;
      wide[1] = ch;
      length = 2;
      high = 0;
    }
    count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, length, (char *)utf8, 4, NULL, NULL);
    next = 0;
    if (count > 0)
      return utf8[next++];
  }
  return 0;
}

bool keyboard_is_initialized(void) {
  return lifecycle_is_initialized(&g_keyboard_lc);
}

static bool send_background_query(void) {
  return platform_write_all(STDERR_FILENO, "\033]11;?\033\\", 8) == 8;
}

bool keyboard_query_background(uint8_t *r, uint8_t *g, uint8_t *b) {
  return keyboard_is_initialized() &&
         keyboard_stream_query_background(keyboard_raw_read, send_background_query, r, g, b);
}

bool keyboard_input_available(void) {
  return keyboard_stream_ready(keyboard_raw_read);
}

keyboard_key_t keyboard_read_nonblocking(void) {
  return keyboard_stream_read(keyboard_raw_read);
}

keyboard_key_t keyboard_read_with_timeout(uint32_t timeout_ms) {
  uint64_t deadline = time_get_ns() + (uint64_t)timeout_ms * NS_PER_MS_INT;
  do {
    keyboard_key_t key = keyboard_read_nonblocking();
    if (key != KEY_NONE || timeout_ms == 0)
      return key;
    platform_sleep_ns(NS_PER_MS_INT);
  } while (time_get_ns() < deadline);
  return KEY_NONE;
}

/* ============================================================================
 * Interactive Line Editing Implementation
 * ============================================================================ */

keyboard_line_edit_result_t keyboard_read_line_interactive(keyboard_line_edit_opts_t *opts) {
  if (opts && opts->key != KEY_NONE)
    return keyboard_apply_key(opts);
  // Validate options
  if (!opts || !opts->buffer || !opts->len || !opts->cursor) {
    return LINE_EDIT_NO_INPUT;
  }

  // Check if keyboard is initialized
  bool is_initialized = lifecycle_is_initialized(&g_keyboard_lc);

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
