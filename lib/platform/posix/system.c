/**
 * @file system.c
 * @brief POSIX system functions implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides POSIX system function wrappers for the platform abstraction layer,
 * enabling cross-platform system operations using a unified API.
 */

#ifndef _WIN32

#include "../abstraction.h"
#include "../internal.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>

/**
 * @brief Get username from environment variables
 * @return Username string or "unknown" if not found
 */
const char *get_username_env(void) {
  static char username[256];
  const char *user = getenv("USER");
  if (!user) {
    user = getenv("USERNAME");
  }
  if (user) {
    strncpy(username, user, sizeof(username) - 1);
    username[sizeof(username) - 1] = '\0';
    return username;
  }
  return "unknown";
}

/**
 * @brief Initialize platform-specific functionality
 * @return 0 on success, error code on failure
 * @note POSIX platforms don't need special initialization
 */
int platform_init(void) {
  // POSIX platforms don't need special initialization
  return 0;
}

/**
 * @brief Clean up platform-specific functionality
 * @note POSIX platforms don't need special cleanup
 */
void platform_cleanup(void) {
  // POSIX platforms don't need special cleanup
}

/**
 * @brief Sleep for specified milliseconds
 * @param ms Number of milliseconds to sleep
 */
void platform_sleep_ms(unsigned int ms) {
  usleep(ms * 1000);
}

/**
 * @brief Sleep for specified microseconds
 * @param us Number of microseconds to sleep
 */
void platform_sleep_us(unsigned int us) {
  usleep(us);
}

/**
 * @brief Get current process ID
 * @return Process ID as integer
 */
int platform_get_pid(void) {
  return (int)getpid();
}

/**
 * @brief Get current username
 * @return Username string or "unknown" if not found
 */
const char *platform_get_username(void) {
  return get_username_env();
}

/**
 * @brief Set signal handler
 * @param sig Signal number
 * @param handler Signal handler function
 * @return Previous signal handler, or SIG_ERR on error
 */
signal_handler_t platform_signal(int sig, signal_handler_t handler) {
  return signal(sig, handler);
}

/**
 * @brief Get environment variable value
 * @param name Environment variable name
 * @return Variable value or NULL if not found
 */
const char *platform_getenv(const char *name) {
  return getenv(name);
}

/**
 * @brief Set environment variable
 * @param name Environment variable name
 * @param value Environment variable value
 * @return 0 on success, error code on failure
 */
int platform_setenv(const char *name, const char *value) {
  return setenv(name, value, 1);
}

/**
 * @brief Check if file descriptor is a TTY
 * @param fd File descriptor to check
 * @return 1 if TTY, 0 if not
 */
int platform_isatty(int fd) {
  return isatty(fd);
}

/**
 * @brief Get TTY device path
 * @return Path to TTY device
 */
const char *platform_get_tty_path(void) {
  return get_tty_path();
}

/**
 * @brief Open TTY device
 * @param mode Open mode string ("r", "w", "rw")
 * @return File descriptor on success, -1 on failure
 */
int platform_open_tty(const char *mode) {
  int flags = O_RDWR;
  if (strchr(mode, 'r') && !strchr(mode, 'w')) {
    flags = O_RDONLY;
  } else if (strchr(mode, 'w') && !strchr(mode, 'r')) {
    flags = O_WRONLY;
  }
  return open("/dev/tty", flags);
}

#endif // !_WIN32