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
#include <ascii-chat/platform/keyboard_text.h>

#include <windows.h>
#include <string.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static uint16_t g_pending_high;
static keyboard_key_t g_repeat_key;
static WORD g_repeat_count;
static INPUT_RECORD g_events[128];
static DWORD g_event_next, g_event_count;

static void reset_console_input(void) {
  g_pending_high = 0;
  g_repeat_key = KEY_NONE;
  g_repeat_count = 0;
  g_event_next = g_event_count = 0;
}

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
  reset_console_input();
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
  reset_console_input();
  // Restore console mode
  if (g_console_input != NULL && g_console_input != INVALID_HANDLE_VALUE) {
    if (!SetConsoleMode(g_console_input, g_original_console_mode)) {
      log_error("Failed to restore console mode");
    }
  }
}

static keyboard_key_t console_key(KEY_EVENT_RECORD key) {
  keyboard_key_t special = KEY_NONE;
  if (key.wVirtualKeyCode == VK_RETURN && (key.dwControlKeyState & SHIFT_PRESSED))
    special = KEY_SHIFT_ENTER;
  else if (key.uChar.UnicodeChar)
    return keyboard_text_utf16(&g_pending_high, (uint16_t)key.uChar.UnicodeChar);
  else {
    switch (key.wVirtualKeyCode) {
    case VK_UP:
      special = KEY_UP;
      break;
    case VK_DOWN:
      special = KEY_DOWN;
      break;
    case VK_LEFT:
      special = KEY_LEFT;
      break;
    case VK_RIGHT:
      special = KEY_RIGHT;
      break;
    case VK_DELETE:
      special = key.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED) ? KEY_CTRL_DELETE : KEY_DELETE;
      break;
    case VK_HOME:
      special = KEY_HOME;
      break;
    case VK_END:
      special = KEY_END;
      break;
    case VK_PRIOR:
      special = KEY_PAGEUP;
      break;
    case VK_NEXT:
      special = KEY_PAGEDOWN;
      break;
    case VK_F12:
      special = KEY_F12;
      break;
    default:
      return KEY_NONE;
    }
  }
  g_pending_high = 0;
  return special;
}

static int keyboard_raw_read(void) {
  if (!lifecycle_is_initialized(&g_keyboard_lc))
    return 0;
  if (g_repeat_count) {
    --g_repeat_count;
    return g_repeat_key;
  }
  for (unsigned attempt = 0; attempt < 64; ++attempt) {
    // Drain frontend replies in one console RPC rather than one per byte.
    if (g_event_next == g_event_count) {
      g_event_next = g_event_count = 0;
      if (WaitForSingleObject(g_console_input, 0) != WAIT_OBJECT_0 ||
          !ReadConsoleInputW(g_console_input, g_events, sizeof(g_events) / sizeof(g_events[0]), &g_event_count) ||
          !g_event_count)
        return 0;
    }
    INPUT_RECORD event = g_events[g_event_next++];
    if (event.EventType != KEY_EVENT || !event.Event.KeyEvent.bKeyDown)
      continue;
    KEY_EVENT_RECORD native = event.Event.KeyEvent;
    keyboard_key_t key = console_key(native);
    if (key != KEY_NONE) {
      // Repeat decoded characters, so surrogate halves cannot consume repeats.
      g_repeat_key = key;
      g_repeat_count = native.wRepeatCount > 0 ? (WORD)(native.wRepeatCount - 1) : 0;
      return key;
    }
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
