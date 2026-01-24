/**
 * @file platform/posix/keyboard.c
 * @brief POSIX keyboard input implementation using select() and termios
 * @ingroup platform
 */

#include "../keyboard.h"
#include "../../common.h"

#include <termios.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>

/* ============================================================================
 * Static State
 * ============================================================================ */

static struct termios g_original_termios;
static bool g_keyboard_initialized = false;

/* ============================================================================
 * Keyboard Functions
 * ============================================================================ */

int keyboard_init(void) {
  if (g_keyboard_initialized) {
    return 0; // Already initialized
  }

  struct termios new_termios;

  // Get current terminal settings
  if (tcgetattr(STDIN_FILENO, &g_original_termios) < 0) {
    log_error("Failed to get terminal attributes");
    return -1;
  }

  // Save original settings and create raw mode version
  new_termios = g_original_termios;

  // Disable canonical mode (line buffering) and echo
  new_termios.c_lflag &= ~(ICANON | ECHO);

  // Set minimum characters to read and timeout
  new_termios.c_cc[VMIN] = 0;  // Non-blocking: return immediately if no input
  new_termios.c_cc[VTIME] = 0; // No timeout

  // Apply new settings
  if (tcsetattr(STDIN_FILENO, TCSANOW, &new_termios) < 0) {
    log_error("Failed to set terminal attributes");
    return -1;
  }

  // Make stdin non-blocking just in case
  int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
  if (flags < 0) {
    // Restore original settings on error
    tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    return -1;
  }

  if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) {
    // Restore original settings on error
    tcsetattr(STDIN_FILENO, TCSANOW, &g_original_termios);
    return -1;
  }

  g_keyboard_initialized = true;
  return 0;
}

void keyboard_cleanup(void) {
  if (!g_keyboard_initialized) {
    return;
  }

  // Restore original terminal settings
  // Use TCSADRAIN to avoid clearing the display when restoring from raw mode
  // TCSADRAIN waits for pending output to be sent before changing terminal attributes
  if (tcsetattr(STDIN_FILENO, TCSADRAIN, &g_original_termios) < 0) {
    log_error("Failed to restore terminal attributes");
  }

  g_keyboard_initialized = false;
}

int keyboard_read_nonblocking(void) {
  if (!g_keyboard_initialized) {
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

  // Handle regular ASCII characters and spacebar
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
    timeout.tv_usec = 50000; // 50ms timeout for escape sequence

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

  // Return regular ASCII character
  return (int)ch;
}
