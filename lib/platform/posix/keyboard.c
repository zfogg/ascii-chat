/**
 * @file platform/posix/keyboard.c
 * @brief POSIX keyboard input implementation using select() and termios
 * @ingroup platform
 */

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/init.h>
// utf8.h no longer needed - keyboard thread handles raw bytes

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
// fcntl.h no longer needed - O_NONBLOCK removed
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

// Timeout for escape sequence detection (in nanoseconds)
#define KEYBOARD_ESCAPE_TIMEOUT_NS (50LL * NS_PER_MS_INT)

/* ============================================================================
 * Static State
 * ============================================================================ */

static struct termios g_original_termios;
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

  // Hold lock for entire initialization sequence to prevent TOCTOU race.
  // Multiple threads must not call tcgetattr/tcsetattr concurrently.

  struct termios new_termios;

  // Get current terminal settings
  if (tcgetattr(STDIN_FILENO, &g_original_termios) < 0) {
    static_mutex_unlock(&g_keyboard_init_mutex);
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to get terminal attributes");
  }

  // Save original settings and create raw mode version
  new_termios = g_original_termios;

  // Disable canonical mode (line buffering) and echo
  new_termios.c_lflag &= ~((tcflag_t)(ICANON | ECHO));

  // VMIN=1: read() blocks until at least 1 byte is available.
  // VTIME=0: no inter-byte timeout.
  // The keyboard thread relies on this to do true blocking reads.
  // Other callers (client, splash) use select() before read() so they're unaffected.
  new_termios.c_cc[VMIN] = 1;
  new_termios.c_cc[VTIME] = 0;

  // Apply new settings
  if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) < 0) {
    static_mutex_unlock(&g_keyboard_init_mutex);
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to set terminal attributes");
  }

  // Do NOT set O_NONBLOCK. The keyboard thread owns stdin reads via
  // blocking select(). Non-blocking mode is unnecessary and can cause
  // read() to return EAGAIN spuriously.

  // Mark as initialized with reference counting (still under lock)
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

  g_keyboard_init_refcount = 0;

  // Restore original terminal settings to prevent corrupting subsequent shell commands
  // This is safe to call at process exit time after all output is complete
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original_termios) < 0) {
    // Silently ignore errors during cleanup
  }

  static_mutex_unlock(&g_keyboard_init_mutex);
}

keyboard_key_t keyboard_read_nonblocking(void) {
  // Check if keyboard is initialized with reference counting
  static_mutex_lock(&g_keyboard_init_mutex);
  bool is_initialized = (g_keyboard_init_refcount > 0);
  static_mutex_unlock(&g_keyboard_init_mutex);

  if (!is_initialized) {
    return KEY_NONE;
  }

  // Check if input is available using select with zero timeout
  fd_set readfds;
  struct timeval timeout;

  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  int select_result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
  if (select_result <= 0) {
    return KEY_NONE; // No input available or error
  }

  // Input is available, read one byte
  unsigned char ch;
  ssize_t n = read(STDIN_FILENO, &ch, 1);
  if (n <= 0) {
    return KEY_NONE;
  }

  // Handle special characters first
  if (ch == ' ') {
    return KEY_SPACE;
  }
  if (ch == 27) { // ESC - might be start of escape sequence
    // Try to read escape sequence for arrow keys
    // Sequences: ESC[A (up), ESC[B (down), ESC[C (right), ESC[D (left)

    // Check if there's more data available
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec =
        (suseconds_t)((KEYBOARD_ESCAPE_TIMEOUT_NS % NS_PER_SEC_INT) / 1000); // 50ms escape sequence timeout

    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
      unsigned char ch2;
      if (read(STDIN_FILENO, &ch2, 1) > 0 && ch2 == '[') {
        // Might be an arrow key sequence
        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
          unsigned char ch3;
          if (read(STDIN_FILENO, &ch3, 1) > 0) {
            switch (ch3) {
            case 'A':
              return KEY_UP;
            case 'B':
              return KEY_DOWN;
            case 'C':
              return KEY_RIGHT;
            case 'D':
              return KEY_LEFT;
            default:
              // Unknown escape sequence, return ESC
              return KEY_ESCAPE;
            }
          }
        }
      }
    }

    return KEY_ESCAPE;
  }

  // Return regular ASCII character (including control characters 0-31, printable 32-126)
  return (keyboard_key_t)ch;
}

keyboard_key_t keyboard_read_with_timeout(uint32_t timeout_ms) {
  // Check if keyboard is initialized
  static_mutex_lock(&g_keyboard_init_mutex);
  bool is_initialized = (g_keyboard_init_refcount > 0);
  static_mutex_unlock(&g_keyboard_init_mutex);

  if (!is_initialized) {
    return KEY_NONE;
  }

  // Wait for input with timeout
  fd_set readfds;
  struct timeval timeout;

  FD_ZERO(&readfds);
  FD_SET(STDIN_FILENO, &readfds);

  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  int select_result = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
  if (select_result <= 0) {
    return KEY_NONE; // Timeout or error
  }

  // Input is available, read it (reuse the same logic as nonblocking)
  unsigned char ch;
  ssize_t n = read(STDIN_FILENO, &ch, 1);
  if (n <= 0) {
    return KEY_NONE;
  }

  // Handle special characters
  if (ch == ' ') {
    return KEY_SPACE;
  }
  if (ch == 27) { // ESC
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = (KEYBOARD_ESCAPE_TIMEOUT_NS % NS_PER_SEC_INT) / 1000;

    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
      unsigned char ch2;
      if (read(STDIN_FILENO, &ch2, 1) > 0 && ch2 == '[') {
        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
          unsigned char ch3;
          if (read(STDIN_FILENO, &ch3, 1) > 0) {
            switch (ch3) {
            case 'A':
              return KEY_UP;
            case 'B':
              return KEY_DOWN;
            case 'C':
              return KEY_RIGHT;
            case 'D':
              return KEY_LEFT;
            default:
              return KEY_ESCAPE;
            }
          }
        }
      }
    }
    return KEY_ESCAPE;
  }

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

  // This function MUST receive a pre-read key. It never reads from stdin.
  // The keyboard thread is the sole reader of stdin.
  if (opts->key == KEY_NONE) {
    return LINE_EDIT_NO_INPUT;
  }

  int c = opts->key;
  size_t len = *opts->len;
  size_t cursor = *opts->cursor;
  char *buffer = opts->buffer;
  size_t max_len = opts->max_len;

  // Arrow keys (already resolved by keyboard thread)
  if (c == KEY_LEFT) {
    if (cursor > 0) {
      cursor--;
      *opts->cursor = cursor;
    }
    return LINE_EDIT_CONTINUE;
  }
  if (c == KEY_RIGHT) {
    if (cursor < len) {
      cursor++;
      *opts->cursor = cursor;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Enter - accept input
  if (c == '\n' || c == '\r') {
    return LINE_EDIT_ACCEPTED;
  }

  // Ctrl+C - cancel
  if (c == 3) {
    return LINE_EDIT_CANCELLED;
  }

  // Escape (already resolved by keyboard thread - standalone ESC)
  if (c == KEY_ESCAPE) {
    return LINE_EDIT_CANCELLED;
  }

  // Backspace (BS = 8 or DEL = 127)
  if (c == 8 || c == 127) {
    if (cursor > 0) {
      memmove(&buffer[cursor - 1], &buffer[cursor], len - cursor);
      cursor--;
      len--;
      *opts->len = len;
      *opts->cursor = cursor;
    } else if (len == 0) {
      // Backspace with empty buffer - cancel (like vim)
      return LINE_EDIT_CANCELLED;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Ignore control characters (except tab)
  if (c < 32 && c != '\t') {
    return LINE_EDIT_CONTINUE;
  }

  // Ignore non-ASCII (keyboard thread doesn't handle multi-byte UTF-8 yet)
  if (c > 127) {
    return LINE_EDIT_CONTINUE;
  }

  // Insert printable ASCII character at cursor position
  if (len < max_len - 1) {
    memmove(&buffer[cursor + 1], &buffer[cursor], len - cursor);
    buffer[cursor] = (char)c;
    len++;
    cursor++;

    *opts->len = len;
    *opts->cursor = cursor;
    buffer[len] = '\0';
  }

  return LINE_EDIT_CONTINUE;
}
