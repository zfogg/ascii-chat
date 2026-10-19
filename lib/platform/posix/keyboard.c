/**
 * @file platform/posix/keyboard.c
 * @brief POSIX keyboard input implementation using select() and termios
 * @ingroup platform
 */

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/util/lifecycle.h>
// utf8.h no longer needed - keyboard thread handles raw bytes

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <ascii-chat/util/time.h>

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
// Fallback TTY device fd (used when stdin is not a TTY)
static int g_tty_fd = -1;
// Track whether we opened /dev/tty so we can close it in cleanup
static bool g_tty_fd_owned = false;

/* ============================================================================
 * Keyboard Functions
 * ============================================================================ */

asciichat_error_t keyboard_init(void) {
  // If already initialized, return success
  if (lifecycle_is_initialized(&g_keyboard_lc)) {
    log_debug("Already initialized");
    return ASCIICHAT_OK;
  }

  // Stderr can host interactive overlays while stdout carries redirected output.
  // Avoid configuring input when neither output is a terminal.
  if (terminal_is_piped_output() && !platform_isatty(STDERR_FILENO)) {
    log_debug("Skipping (output is piped, non-interactive mode)");
    // LOG_IO may redirect stdout temporarily. Retry initialization on the
    // next poll instead of permanently committing an unconfigured keyboard.
    return ASCIICHAT_OK;
  }

  // CAS to claim initialization
  if (!lifecycle_init(&g_keyboard_lc, "keyboard")) {
    log_debug("Lost init race, another thread initialized");
    return ASCIICHAT_OK; // Already initialized
  }

  // We won the init race - do the actual work
  // Multiple threads must not call tcgetattr/tcsetattr concurrently.

  struct termios new_termios;
  int config_fd = STDIN_FILENO;
  bool use_stdin = true;

  // Check if stdin is a TTY - if so, use it directly.
  // This is important for tmux sessions where stdin receives input from tmux send-keys.
  // Uses platform_isatty() so non-desktop platforms (iOS, WASM) can override.
  if (platform_isatty(STDIN_FILENO)) {
    log_debug("stdin is a TTY - using stdin for keyboard input");
    if (tcgetattr(STDIN_FILENO, &g_original_termios) < 0) {
      log_error("FAILED - tcgetattr(stdin) failed");
      lifecycle_init_abort(&g_keyboard_lc);
      return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to get terminal attributes from stdin");
    }
    config_fd = STDIN_FILENO;
    use_stdin = true;
    log_debug("Successfully using stdin for keyboard input");
  } else {
    // stdin is not a TTY - try /dev/tty as fallback
    log_debug("stdin is not a TTY - attempting to open /dev/tty as fallback");
    g_tty_fd = open("/dev/tty", O_RDWR);
    if (g_tty_fd >= 0) {
      // Try to get terminal settings from /dev/tty
      if (tcgetattr(g_tty_fd, &g_original_termios) >= 0) {
        log_debug("Successfully using /dev/tty for keyboard input");
        config_fd = g_tty_fd;
        g_tty_fd_owned = true;
        use_stdin = false;
      } else {
        // tcgetattr failed on /dev/tty - close it
        log_warn("tcgetattr(/dev/tty) failed");
        close(g_tty_fd);
        g_tty_fd = -1;
        g_tty_fd_owned = false;
        log_error("FAILED - neither stdin nor /dev/tty available for keyboard input");
        lifecycle_init_abort(&g_keyboard_lc);
        return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to get terminal attributes from both stdin and /dev/tty");
      }
    } else {
      log_error("FAILED - /dev/tty not available (errno=%d)", errno);
      lifecycle_init_abort(&g_keyboard_lc);
      return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to open /dev/tty and stdin is not a TTY");
    }
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
  if (tcsetattr(config_fd, TCSANOW, &new_termios) < 0) {
    if (!use_stdin && g_tty_fd >= 0) {
      close(g_tty_fd);
      g_tty_fd = -1;
      g_tty_fd_owned = false;
    }
    lifecycle_init_abort(&g_keyboard_lc);
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to set terminal attributes");
  }

  // Do NOT set O_NONBLOCK. The keyboard thread owns stdin reads via
  // blocking select(). Non-blocking mode is unnecessary and can cause
  // read() to return EAGAIN spuriously.

  // Mark as initialized (still under lock-free CAS)
  log_debug("SUCCESS - using %s for keyboard input", use_stdin ? "stdin" : "/dev/tty");
  return ASCIICHAT_OK;
}

void keyboard_destroy(void) {
  if (!lifecycle_shutdown(&g_keyboard_lc)) {
    return; // Not initialized or already shutting down
  }

  // Restore original terminal settings to prevent corrupting subsequent shell commands
  // This is safe to call at process exit time after all output is complete
  if (g_tty_fd_owned && g_tty_fd >= 0) {
    // We were using /dev/tty as fallback - restore and close it
    if (tcsetattr(g_tty_fd, TCSAFLUSH, &g_original_termios) < 0) {
      // Silently ignore errors during cleanup
    }
    close(g_tty_fd);
    g_tty_fd = -1;
    g_tty_fd_owned = false;
  } else {
    // We were using stdin - restore it
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_original_termios) < 0) {
      // Silently ignore errors during cleanup
    }
  }
}

keyboard_key_t keyboard_read_nonblocking(void) {
  // Note: This function works with or without keyboard_init() having been called.
  // If init failed or wasn't called, we still try to read from stdin or /dev/tty.
  // The select() and read() calls below are safe to use regardless of init state.

  // Determine which fd to read from: use /dev/tty if available (keyboard_init fallback),
  // otherwise use stdin
  int read_fd = (g_tty_fd >= 0) ? g_tty_fd : STDIN_FILENO;

  // Check if input is available using select with zero timeout
  fd_set readfds;
  struct timeval timeout;

  FD_ZERO(&readfds);
  FD_SET(read_fd, &readfds);

  timeout.tv_sec = 0;
  timeout.tv_usec = 0;

  int select_result = select(read_fd + 1, &readfds, NULL, NULL, &timeout);
  log_dev_every(5 * NS_PER_SEC_INT, "select fd=%d result=%d, tty_fd=%d", read_fd, select_result, g_tty_fd);
  if (select_result <= 0) {
    return KEY_NONE; // No input available or error
  }

  // Input is available, read one byte
  unsigned char ch;
  ssize_t n = read(read_fd, &ch, 1);
  if (n <= 0) {
    // EOF is normal for redirected stdin. A paused session can poll this path
    // every millisecond; do not turn absent input into a stream of warnings.
    if (n < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
      log_warn_every(US_PER_SEC_INT, "read() failed, errno=%d", errno);
    }
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
    FD_SET(read_fd, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec =
        (suseconds_t)((KEYBOARD_ESCAPE_TIMEOUT_NS % NS_PER_SEC_INT) / 1000); // 50ms escape sequence timeout

    if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
      unsigned char ch2;
      if (read(read_fd, &ch2, 1) > 0) {
        if (ch2 == '[') {
          // Might be an arrow key or function key sequence
          if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
            unsigned char ch3;
            if (read(read_fd, &ch3, 1) > 0) {
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
                if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(read_fd, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 261; // KEY_HOME
              case '2':
                // Insert/Ctrl+Delete sends ESC [ 2 ~
                if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(read_fd, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 263; // KEY_CTRL_DELETE
              case '3':
                // Delete key sends ESC [ 3 ~
                if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(read_fd, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 260; // KEY_DELETE
              case '4':
                // End key sends ESC [ 4 ~
                if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(read_fd, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 262; // KEY_END
              default:
                // Unknown escape sequence - consume any trailing ~ to avoid leaving it in buffer
                if (ch3 >= '0' && ch3 <= '9') {
                  // Other function key sequences - just consume the trailing ~
                  if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                    unsigned char ch4;
                    read(read_fd, &ch4, 1); // Just consume it
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
  // If init failed or wasn't called, we still try to read from stdin or /dev/tty.
  // The select() and read() calls below are safe to use regardless of init state.

  // Determine which fd to read from: use /dev/tty if available (keyboard_init fallback),
  // otherwise use stdin
  int read_fd = (g_tty_fd >= 0) ? g_tty_fd : STDIN_FILENO;

  // Wait for input with timeout
  fd_set readfds;
  struct timeval timeout;

  FD_ZERO(&readfds);
  FD_SET(read_fd, &readfds);

  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;

  int select_result = select(read_fd + 1, &readfds, NULL, NULL, &timeout);
  if (select_result <= 0) {
    return KEY_NONE; // Timeout or error
  }

  // Input is available, read it (reuse the same logic as nonblocking)
  unsigned char ch;
  ssize_t n = read(read_fd, &ch, 1);
  if (n <= 0) {
    return KEY_NONE;
  }

  // Handle special characters
  if (ch == ' ') {
    return KEY_SPACE;
  }
  if (ch == 27) { // ESC
    FD_ZERO(&readfds);
    FD_SET(read_fd, &readfds);
    timeout.tv_sec = 0;
    timeout.tv_usec = (KEYBOARD_ESCAPE_TIMEOUT_NS % NS_PER_SEC_INT) / 1000;

    if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
      unsigned char ch2;
      if (read(read_fd, &ch2, 1) > 0) {
        if (ch2 == '[') {
          if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
            unsigned char ch3;
            if (read(read_fd, &ch3, 1) > 0) {
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
                if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(read_fd, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 261; // KEY_HOME
              case '2':
                // Insert/Ctrl+Delete sends ESC [ 2 ~
                if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(read_fd, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 263; // KEY_CTRL_DELETE
              case '3':
                // Delete key sends ESC [ 3 ~
                if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(read_fd, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 260; // KEY_DELETE
              case '4':
                // End key sends ESC [ 4 ~
                if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                  unsigned char ch4;
                  if (read(read_fd, &ch4, 1) > 0) {
                    // ch4 should be '~', consume it
                  }
                }
                return 262; // KEY_END
              default:
                // Unknown escape sequence - consume any trailing ~ to avoid leaving it in buffer
                if (ch3 >= '0' && ch3 <= '9') {
                  // Other function key sequences - just consume the trailing ~
                  if (select(read_fd + 1, &readfds, NULL, NULL, &timeout) > 0) {
                    unsigned char ch4;
                    read(read_fd, &ch4, 1); // Just consume it
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
  return keyboard_apply_key(opts);
}
