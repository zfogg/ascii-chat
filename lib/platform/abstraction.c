
/**
 * @file platform/abstraction.c
 * @ingroup platform
 * @brief 🏗️ Common platform abstraction stubs (OS-specific code in posix/ and windows/ subdirectories)
 */

#include <errno.h>
#include "terminal.h"
#include "abstraction.h"
#include "../options/options.h"
#include "../common.h"

// ============================================================================
// Common Platform Functions
// ============================================================================
// This file is reserved for common platform functions that don't need
// OS-specific implementations. Currently all functions are OS-specific.
//
// The OS-specific implementations are in:
// - platform_windows.c (Windows)
// - platform_posix.c (POSIX/Unix/Linux/macOS)
//
// Socket-specific implementations are in:
// - platform/socket.c (Common socket utilities like socket_optimize_for_streaming)
// ============================================================================

/**
 * @brief Write all data to file descriptor with automatic retry on transient errors
 *
 * Handles incomplete writes and transient errors (EAGAIN, EWOULDBLOCK) by retrying.
 * This ensures complete writes even under high load or when piping to tools like tee.
 *
 * @param fd File descriptor to write to
 * @param buf Buffer containing data to write
 * @param count Number of bytes to write
 * @return Number of bytes written (should equal count if all data written), or -1 if failed
 */
size_t platform_write_all(int fd, const void *buf, size_t count) {
  if (!buf || count == 0) {
    return 0;
  }

  size_t written_total = 0;
  int attempts = 0;
  const int MAX_ATTEMPTS = 1000;

  while (written_total < count && attempts < MAX_ATTEMPTS) {
    ssize_t result = platform_write(fd, (const char *)buf + written_total, count - written_total);

    if (result > 0) {
      written_total += (size_t)result;
      attempts = 0; // Reset attempt counter on successful write
    } else if (result < 0) {
      // Write error - log it but keep retrying
      log_warn("platform_write_all: write() error on fd=%d (wrote %zu/%zu so far, errno=%d)", fd, written_total, count,
               errno);
      attempts++;
    } else {
      // result == 0: no bytes written, retry
      attempts++;
    }
  }

  if (attempts >= MAX_ATTEMPTS && written_total < count) {
    log_warn("platform_write_all: Hit retry limit on fd=%d: wrote %zu of %zu bytes", fd, written_total, count);
  }

  return written_total;
}

/**
 * @brief Check if terminal control sequences should be used for the given fd
 * @param fd File descriptor to check
 * @return true if terminal control sequences should be used, false otherwise
 *
 * This function determines whether to send terminal POSITIONING and CONTROL
 * sequences (cursor home, clear screen, hide cursor, etc.) which should only
 * be sent to TTY and never to pipes/redirected output.
 *
 * Note: This does NOT control ANSI COLOR CODES, which are controlled by
 * --color-mode option and may be sent to pipes if explicitly requested.
 */
bool terminal_should_use_control_sequences(int fd) {
  if (fd < 0) {
    return false;
  }
  if (GET_OPTION(snapshot_mode)) {
    return false;
  }
  const char *testing_env = SAFE_GETENV("TESTING");
  if (testing_env != NULL) {
    return false;
  }
  return platform_isatty(fd) != 0;
}
