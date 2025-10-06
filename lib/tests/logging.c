#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "logging.h"

#ifndef _WIN32
// POSIX-only test utilities for redirecting stdout/stderr

#include "platform/file.h"
#include "platform/internal.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

// Global variables to store original file descriptors for restoration
static int original_stdout_fd = -1;
static int original_stderr_fd = -1;
static int dev_null_fd = -1;
static bool logging_disabled = false;

int test_logging_disable(bool disable_stdout, bool disable_stderr) {
  // If already disabled, return success
  if (logging_disabled) {
    return 0;
  }

  // Open /dev/null for writing
  dev_null_fd = platform_open("/dev/null", PLATFORM_O_WRONLY);
  if (dev_null_fd == -1) {
    return -1;
  }

  // Save original file descriptors if we need to redirect them
  if (disable_stdout) {
    original_stdout_fd = dup(STDOUT_FILENO);
    if (original_stdout_fd == -1) {
      close(dev_null_fd);
      dev_null_fd = -1;
      return -1;
    }
    dup2(dev_null_fd, STDOUT_FILENO);
    // Reopen stdout to use the redirected file descriptor
    (void)freopen("/dev/null", "w", stdout);
    // Make stdout unbuffered to ensure immediate redirection
    (void)setvbuf(stdout, NULL, _IONBF, 0);
  }

  if (disable_stderr) {
    original_stderr_fd = dup(STDERR_FILENO);
    if (original_stderr_fd == -1) {
      // Clean up stdout if it was redirected
      if (disable_stdout && original_stdout_fd != -1) {
        dup2(original_stdout_fd, STDOUT_FILENO);
        close(original_stdout_fd);
        original_stdout_fd = -1;
      }
      close(dev_null_fd);
      dev_null_fd = -1;
      return -1;
    }
    dup2(dev_null_fd, STDERR_FILENO);
    // Reopen stderr to use the redirected file descriptor
    (void)freopen("/dev/null", "w", stderr);
    // Make stderr unbuffered to ensure immediate redirection
    (void)setvbuf(stderr, NULL, _IONBF, 0);
  }

  logging_disabled = true;
  return 0;
}

int test_logging_restore(void) {
  // If not disabled, return success
  if (!logging_disabled) {
    return 0;
  }

  // Restore original stdout
  if (original_stdout_fd != -1) {
    dup2(original_stdout_fd, STDOUT_FILENO);
    // Reopen stdout to use the restored file descriptor
    (void)freopen("/dev/stdout", "w", stdout);
    // Restore line buffering for stdout
    (void)setvbuf(stdout, NULL, _IOLBF, 0);
    close(original_stdout_fd);
    original_stdout_fd = -1;
  }

  // Restore original stderr
  if (original_stderr_fd != -1) {
    dup2(original_stderr_fd, STDERR_FILENO);
    // Reopen stderr to use the restored file descriptor
    (void)freopen("/dev/stderr", "w", stderr);
    // Restore unbuffered mode for stderr
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    close(original_stderr_fd);
    original_stderr_fd = -1;
  }

  // Close /dev/null file descriptor
  if (dev_null_fd != -1) {
    close(dev_null_fd);
    dev_null_fd = -1;
  }

  logging_disabled = false;
  return 0;
}

bool test_logging_is_disabled(void) {
  return logging_disabled;
}

#else
// Windows stub implementations
int test_logging_disable(bool disable_stdout, bool disable_stderr) {
  (void)disable_stdout;
  (void)disable_stderr;
  return 0; // Not implemented on Windows
}

int test_logging_restore(void) {
  return 0; // Not implemented on Windows
}

bool test_logging_is_disabled(void) {
  return false;
}
#endif // !_WIN32
