/**
 * @file platform/posix/keyboard.c
 * @brief POSIX keyboard input implementation using select() and termios
 * @ingroup platform
 */

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/lifecycle.h>
// utf8.h no longer needed - keyboard thread handles raw bytes

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
// fcntl.h no longer needed - O_NONBLOCK removed
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

/* ============================================================================
 * Constants
 * ============================================================================ */

// Timeout for escape sequence detection (in nanoseconds)
#define KEYBOARD_ESCAPE_TIMEOUT_NS (50LL * NS_PER_MS_INT)

/* ============================================================================
 * Static State
 * ============================================================================ */

static struct termios g_original_termios;
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
  // Multiple threads must not call tcgetattr/tcsetattr concurrently.

  struct termios new_termios;

  // Check if terminal is interactive before attempting tcgetattr
  // If terminal is not interactive, we skip raw mode configuration but allow
  // keyboard reading to continue via fallback with buffered input
  if (!terminal_is_interactive()) {
    log_debug("keyboard_init: terminal is not interactive, skipping raw mode configuration");
    // Mark as initialized but don't actually configure terminal mode
    // The keyboard_read functions will still work using regular read() calls,
    // but keyboard input will need to be terminated with Enter in non-raw mode
    return ASCIICHAT_OK;
  }

  // Get current terminal settings
  if (tcgetattr(STDIN_FILENO, &g_original_termios) < 0) {
    lifecycle_init_abort(&g_keyboard_lc);
    int errno_val = errno;
    log_debug("keyboard_init: tcgetattr failed with errno=%d (%s)", errno_val, strerror(errno_val));
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to get terminal attributes");
  }

  // Save original settings and create raw mode version
  new_termios = g_original_termios;

  // Disable canonical mode (line buffering) and echo.
  // ISIG stays enabled so Ctrl+C generates SIGINT, which properly
  // interrupts blocking syscalls (select/accept) in other threads.
  // The SIGINT handler checks an atomic flag to decide whether to
  // cancel grep mode or shut down the server.
  new_termios.c_lflag &= ~((tcflag_t)(ICANON | ECHO));

  // VMIN=1: read() blocks until at least 1 byte is available.
  // VTIME=0: no inter-byte timeout.
  // The keyboard thread relies on this to do true blocking reads.
  // Other callers (client, splash) use select() before read() so they're unaffected.
  new_termios.c_cc[VMIN] = 1;
  new_termios.c_cc[VTIME] = 0;

  // Apply new settings
  if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) < 0) {
    lifecycle_init_abort(&g_keyboard_lc);
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to set terminal attributes");
  }

  // Do NOT set O_NONBLOCK. The keyboard thread owns stdin reads via
  // blocking select(). Non-blocking mode is unnecessary and can cause
  // read() to return EAGAIN spuriously.

  // Mark as initialized (still under lock-free CAS)
  return ASCIICHAT_OK;
}

void keyboard_destroy(void) {
  if (!lifecycle_shutdown(&g_keyboard_lc)) {
    return; // Not initialized or already shutting down
  }

  // Restore original terminal settings to prevent corrupting subsequent shell commands
  // This is safe to call at process exit time after all output is complete
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original_termios) < 0) {
    // Silently ignore errors during cleanup
  }
}

keyboard_key_t keyboard_read_nonblocking(void) {
  // Note: This function works with or without keyboard_init() having been called.
  // If init failed or wasn't called, we still try to read from stdin.
  // The select() and read() calls below are safe to use regardless of init state.

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
      if (read(STDIN_FILENO, &ch2, 1) > 0) {
        if (ch2 == '[') {
          // Might be an arrow key or function key sequence
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
              case 'H':
                // Home key (some terminals send ESC [ H instead of ESC [ 1 ~)
                return 261; // KEY_HOME
              case 'F':
                // End key (some terminals send ESC [ F instead of ESC [ 4 ~)
                return 262; // KEY_END
              case '1':
                // Home key sends ESC [ 1 ~
                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(STDIN_FILENO, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 261; // KEY_HOME
              case '2':
                // Insert/Ctrl+Delete sends ESC [ 2 ~
                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(STDIN_FILENO, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 263; // KEY_CTRL_DELETE
              case '3':
                // Delete key sends ESC [ 3 ~
                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(STDIN_FILENO, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 260; // KEY_DELETE
              case '4':
                // End key sends ESC [ 4 ~
                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(STDIN_FILENO, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 262; // KEY_END
              default:
                // Unknown escape sequence - consume any trailing ~ to avoid leaving it in buffer
                if (ch3 >= '0' && ch3 <= '9') {
                  // Other function key sequences - just consume the trailing ~
                  if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                    unsigned char ch4;
                    read(STDIN_FILENO, &ch4, 1); // Just consume it
                  }
                }
                return KEY_NONE;
              }
            }
          }
        } else {
          // ESC followed by something other than [ (like Ctrl+number sending ESC+digit)
          // Ignore the sequence and return KEY_NONE
          return KEY_NONE;
        }
      }
    }

    return KEY_ESCAPE;
  }

  // Return regular ASCII character (including control characters 0-31, printable 32-126)
  return (keyboard_key_t)ch;
}

keyboard_key_t keyboard_read_with_timeout(uint32_t timeout_ms) {
  // Note: This function works with or without keyboard_init() having been called.
  // If init failed or wasn't called, we still try to read from stdin.
  // The select() and read() calls below are safe to use regardless of init state.

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
      if (read(STDIN_FILENO, &ch2, 1) > 0) {
        if (ch2 == '[') {
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
              case 'H':
                // Home key (some terminals send ESC [ H instead of ESC [ 1 ~)
                return 261; // KEY_HOME
              case 'F':
                // End key (some terminals send ESC [ F instead of ESC [ 4 ~)
                return 262; // KEY_END
              case '1':
                // Home key sends ESC [ 1 ~
                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(STDIN_FILENO, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 261; // KEY_HOME
              case '2':
                // Insert/Ctrl+Delete sends ESC [ 2 ~
                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(STDIN_FILENO, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 263; // KEY_CTRL_DELETE
              case '3':
                // Delete key sends ESC [ 3 ~
                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(STDIN_FILENO, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 260; // KEY_DELETE
              case '4':
                // End key sends ESC [ 4 ~
                if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(STDIN_FILENO, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 262; // KEY_END
              default:
                // Unknown escape sequence - consume any trailing ~ to avoid leaving it in buffer
                if (ch3 >= '0' && ch3 <= '9') {
                  // Other function key sequences - just consume the trailing ~
                  if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
                    unsigned char ch4;
                    read(STDIN_FILENO, &ch4, 1); // Just consume it
                  }
                }
                return KEY_NONE;
              }
            }
          }
        } else {
          // ESC followed by something other than [
          // Ignore the sequence and return KEY_NONE
          return KEY_NONE;
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

  // Delete key (forward delete - delete character at cursor)
  if (c == 260) { // KEY_DELETE
    if (cursor < len) {
      memmove(&buffer[cursor], &buffer[cursor + 1], len - cursor - 1);
      len--;
      buffer[len] = '\0';
      *opts->len = len;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Home key (move to beginning of line)
  if (c == 261) { // KEY_HOME
    cursor = 0;
    *opts->cursor = cursor;
    return LINE_EDIT_CONTINUE;
  }

  // End key (move to end of line)
  if (c == 262) { // KEY_END
    cursor = len;
    *opts->cursor = cursor;
    return LINE_EDIT_CONTINUE;
  }

  // Ctrl+Delete - delete word forward (opposite of Ctrl+W)
  if (c == 263) { // KEY_CTRL_DELETE
    if (cursor < len) {
      size_t word_end = cursor;

      // Skip non-whitespace (word characters)
      while (word_end < len && !isspace((unsigned char)buffer[word_end])) {
        word_end++;
      }

      // Skip whitespace
      while (word_end < len && isspace((unsigned char)buffer[word_end])) {
        word_end++;
      }

      // Delete from cursor to word_end
      if (word_end > cursor) {
        memmove(&buffer[cursor], &buffer[word_end], len - word_end);
        len -= (word_end - cursor);
        buffer[len] = '\0';
        *opts->len = len;
      }
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
      buffer[len] = '\0';
      *opts->len = len;
      *opts->cursor = cursor;
    } else if (len == 0) {
      // Backspace with empty buffer - cancel (like vim)
      return LINE_EDIT_CANCELLED;
    }
    return LINE_EDIT_CONTINUE;
  }

  // Ctrl+W - delete word (zsh/readline style)
  if (c == 23) {
    if (cursor > 0) {
      size_t word_start = cursor;

      // Skip whitespace backwards
      while (word_start > 0 && isspace((unsigned char)buffer[word_start - 1])) {
        word_start--;
      }

      // Skip word characters backwards
      while (word_start > 0 && !isspace((unsigned char)buffer[word_start - 1])) {
        word_start--;
      }

      // Delete from word_start to cursor
      memmove(&buffer[word_start], &buffer[cursor], len - cursor);
      len -= (cursor - word_start);
      cursor = word_start;
      buffer[len] = '\0';
      *opts->len = len;
      *opts->cursor = cursor;
    } else if (len == 0) {
      // Ctrl+W with empty buffer - cancel
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
