/**
 * @file platform/posix/keyboard.c
 * @brief POSIX keyboard input implementation using select() and termios
 * @ingroup platform
 */

#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/time.h>
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

  keyboard_stream_reset();
  platform_write_all(STDERR_FILENO, "\033[?2004h", 8);

  // Mark as initialized (still under lock-free CAS)
  log_debug("SUCCESS - using %s for keyboard input", use_stdin ? "stdin" : "/dev/tty");
  return ASCIICHAT_OK;
}

void keyboard_destroy(void) {
  if (!lifecycle_shutdown(&g_keyboard_lc)) {
    return; // Not initialized or already shutting down
  }

  platform_write_all(STDERR_FILENO, "\033[?2004l", 8);
  keyboard_stream_reset();

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

static int keyboard_raw_read(void) {
  int fd = g_tty_fd >= 0 ? g_tty_fd : STDIN_FILENO;
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  struct timeval timeout = {0};
  if (select(fd + 1, &fds, NULL, NULL, &timeout) <= 0)
    return 0;
  unsigned char byte;
  return read(fd, &byte, 1) == 1 ? byte : 0;
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
  return keyboard_apply_key(opts);
}
