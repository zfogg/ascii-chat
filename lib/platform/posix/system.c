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
#include "../../common.h" // For log_error()
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdatomic.h>

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
  // Install crash handlers for automatic backtrace on crashes
  platform_install_crash_handler();
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
 * @brief Cross-platform high-precision sleep function
 * @param usec Number of microseconds to sleep
 *
 * POSIX implementation uses standard usleep() function.
 * Provides microsecond precision sleep capability.
 */
void platform_sleep_usec(unsigned int usec) {
  usleep(usec);
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
 * @brief Set signal handler using sigaction (thread-safe POSIX implementation)
 * @param sig Signal number
 * @param handler Signal handler function
 * @return Previous signal handler, or SIG_ERR on error
 */
signal_handler_t platform_signal(int sig, signal_handler_t handler) {
  struct sigaction sa, old_sa;

  // Set up new signal action
  sa.sa_handler = handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART; // Restart interrupted system calls

  // Install the signal handler
  if (sigaction(sig, &sa, &old_sa) == -1) {
    return SIG_ERR;
  }

  return old_sa.sa_handler;
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
 * @brief Get TTY name for a file descriptor
 * @param fd File descriptor
 * @return TTY name or NULL if not a TTY
 */
const char *platform_ttyname(int fd) {
  return ttyname(fd);
}

/**
 * @brief Synchronize file descriptor to disk
 * @param fd File descriptor to sync
 * @return 0 on success, -1 on failure
 */
int platform_fsync(int fd) {
  return fsync(fd);
}

// ============================================================================
// String Safety Functions
// ============================================================================

/**
 * @brief Platform-safe snprintf implementation
 * @param str Destination buffer
 * @param size Buffer size
 * @param format Format string
 * @return Number of characters written (excluding null terminator)
 */
int platform_snprintf(char *str, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsnprintf(str, size, format, args);
  va_end(args);
  return result;
}

/**
 * @brief Platform-safe vsnprintf implementation
 * @param str Destination buffer
 * @param size Buffer size
 * @param format Format string
 * @param ap Variable argument list
 * @return Number of characters written (excluding null terminator)
 */
int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap) {
  return vsnprintf(str, size, format, ap);
}

/**
 * @brief Duplicate a string
 * @param s Source string
 * @return Allocated copy of string, or NULL on failure
 */
char *platform_strdup(const char *s) {
  return strdup(s);
}

/**
 * @brief Duplicate up to n characters of a string
 * @param s Source string
 * @param n Maximum number of characters to copy
 * @return Allocated copy of string, or NULL on failure
 */
char *platform_strndup(const char *s, size_t n) {
#ifdef __APPLE__
  // macOS has strndup but it may not be declared in older SDKs
  size_t len = strnlen(s, n);
  char *result = (char *)malloc(len + 1);
  if (result) {
    memcpy(result, s, len);
    result[len] = '\0';
  }
  return result;
#else
  // Linux/BSD have strndup
  return strndup(s, n);
#endif
}

/**
 * @brief Case-insensitive string comparison
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int platform_strcasecmp(const char *s1, const char *s2) {
  return strcasecmp(s1, s2);
}

/**
 * @brief Case-insensitive string comparison with length limit
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int platform_strncasecmp(const char *s1, const char *s2, size_t n) {
  return strncasecmp(s1, s2, n);
}

/**
 * @brief Thread-safe string tokenization
 * @param str String to tokenize (NULL for continuation)
 * @param delim Delimiter string
 * @param saveptr Pointer to save state between calls
 * @return Pointer to next token, or NULL if no more tokens
 */
char *platform_strtok_r(char *str, const char *delim, char **saveptr) {
  return strtok_r(str, delim, saveptr);
}

/**
 * @brief Safe string copy with size limit
 * @param dst Destination buffer
 * @param src Source string
 * @param size Destination buffer size
 * @return Length of source string (excluding null terminator)
 */
size_t platform_strlcpy(char *dst, const char *src, size_t size) {
#ifdef __APPLE__
  // macOS has strlcpy
  return strlcpy(dst, src, size);
#else
  // Linux doesn't have strlcpy, implement it
  size_t src_len = strlen(src);
  if (size > 0) {
    size_t copy_len = (src_len >= size) ? size - 1 : src_len;
    memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
  }
  return src_len;
#endif
}

/**
 * @brief Safe string concatenation with size limit
 * @param dst Destination buffer
 * @param src Source string
 * @param size Destination buffer size
 * @return Total length of resulting string
 */
size_t platform_strlcat(char *dst, const char *src, size_t size) {
#ifdef __APPLE__
  // macOS has strlcat
  return strlcat(dst, src, size);
#else
  // Linux doesn't have strlcat, implement it
  size_t dst_len = strnlen(dst, size);
  size_t src_len = strlen(src);

  if (dst_len == size) {
    return size + src_len;
  }

  size_t remain = size - dst_len - 1;
  size_t copy_len = (src_len > remain) ? remain : src_len;

  memcpy(dst + dst_len, src, copy_len);
  dst[dst_len + copy_len] = '\0';

  return dst_len + src_len;
#endif
}

// ============================================================================
// Memory Operations
// ============================================================================

/**
 * @brief Allocate aligned memory
 * @param alignment Alignment requirement (must be power of 2)
 * @param size Size of memory block
 * @return Pointer to aligned memory, or NULL on failure
 */
void *platform_aligned_alloc(size_t alignment, size_t size) {
#ifdef __APPLE__
  // macOS uses posix_memalign
  void *ptr = NULL;
  if (posix_memalign(&ptr, alignment, size) != 0) {
    return NULL;
  }
  return ptr;
#else
  // Linux has aligned_alloc
  return aligned_alloc(alignment, size);
#endif
}

/**
 * @brief Free aligned memory
 * @param ptr Pointer to aligned memory block
 */
void platform_aligned_free(void *ptr) {
  // Regular free works for aligned memory on POSIX
  free(ptr);
}

/**
 * @brief Memory barrier for synchronization
 */
void platform_memory_barrier(void) {
  __sync_synchronize();
}

// ============================================================================
// Error Handling
// ============================================================================

// Thread-local storage for error strings
static __thread char error_buffer[256];

/**
 * @brief Get thread-safe error string
 * @param errnum Error number
 * @return Error string (thread-local storage)
 */
const char *platform_strerror(int errnum) {
  // Use strerror_r for thread safety
#ifdef __APPLE__
  // macOS uses XSI-compliant strerror_r
  if (strerror_r(errnum, error_buffer, sizeof(error_buffer)) != 0) {
    snprintf(error_buffer, sizeof(error_buffer), "Unknown error %d", errnum);
  }
#else
  // Linux uses GNU strerror_r which returns a char*
  char *result = strerror_r(errnum, error_buffer, sizeof(error_buffer));
  if (result != error_buffer) {
    // GNU strerror_r may return a static string
    strncpy(error_buffer, result, sizeof(error_buffer) - 1);
    error_buffer[sizeof(error_buffer) - 1] = '\0';
  }
#endif
  return error_buffer;
}

/**
 * @brief Get last error code
 * @return Last error code (errno on POSIX)
 */
int platform_get_last_error(void) {
  return errno;
}

/**
 * @brief Set last error code
 * @param error Error code to set
 */
void platform_set_last_error(int error) {
  errno = error;
}

// ============================================================================
// File Operations
// ============================================================================

/**
 * @brief Open file with platform-safe flags
 * @param pathname File path
 * @param flags Open flags
 * @param ... Mode (if O_CREAT is specified)
 * @return File descriptor on success, -1 on failure
 */
int platform_open(const char *pathname, int flags, ...) {
  int mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
    return open(pathname, flags, mode);
  }
  return open(pathname, flags);
}

/**
 * @brief Read from file descriptor
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
ssize_t platform_read(int fd, void *buf, size_t count) {
  return read(fd, buf, count);
}

/**
 * @brief Write to file descriptor
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
ssize_t platform_write(int fd, const void *buf, size_t count) {
  return write(fd, buf, count);
}

/**
 * @brief Close file descriptor
 * @param fd File descriptor
 * @return 0 on success, -1 on failure
 */
int platform_close(int fd) {
  return close(fd);
}

// ============================================================================
// Debug/Stack Trace Functions
// ============================================================================

/**
 * @brief Get stack trace
 * @param buffer Array to store trace addresses
 * @param size Maximum number of addresses to retrieve
 * @return Number of addresses retrieved
 */
int platform_backtrace(void **buffer, int size) {
  return backtrace(buffer, size);
}

/**
 * @brief Convert stack trace addresses to symbols
 * @param buffer Array of addresses from platform_backtrace
 * @param size Number of addresses in buffer
 * @return Array of strings with symbol names (must be freed)
 */
char **platform_backtrace_symbols(void *const *buffer, int size) {
  return backtrace_symbols(buffer, size);
}

/**
 * @brief Free memory from platform_backtrace_symbols
 * @param strings Array returned by platform_backtrace_symbols
 */
void platform_backtrace_symbols_free(char **strings) {
  free(strings);
}

// ============================================================================
// Crash Handling
// ============================================================================

/**
 * @brief Print backtrace to stderr
 */
void platform_print_backtrace(void) {
  void *buffer[32];
  int size = platform_backtrace(buffer, 32);

  if (size > 0) {
    fprintf(stderr, "\n=== BACKTRACE ===\n");
    char **symbols = platform_backtrace_symbols(buffer, size);

    for (int i = 0; i < size; i++) {
      fprintf(stderr, "%2d: %s\n", i, symbols ? symbols[i] : "???");
    }

    platform_backtrace_symbols_free(symbols);
    fprintf(stderr, "================\n\n");
  }
}

/**
 * @brief Crash signal handler
 */
static void crash_handler(int sig, siginfo_t *info, void *context) {
  fprintf(stderr, "\n*** CRASH DETECTED ***\n");
  fprintf(stderr, "Signal: %d (%s)\n", sig,
          sig == SIGSEGV ? "SIGSEGV" :
          sig == SIGABRT ? "SIGABRT" :
          sig == SIGFPE ? "SIGFPE" :
          sig == SIGILL ? "SIGILL" :
          sig == SIGBUS ? "SIGBUS" : "UNKNOWN");

  if (info) {
    fprintf(stderr, "Signal Info: si_code=%d, si_addr=%p\n",
            info->si_code, info->si_addr);
  }

  platform_print_backtrace();

  // Restore default handler and re-raise signal
  struct sigaction sa;
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(sig, &sa, NULL);
  raise(sig);
}

/**
 * @brief Install crash handlers for common crash signals (thread-safe)
 */
void platform_install_crash_handler(void) {
  struct sigaction sa;
  sa.sa_sigaction = crash_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_SIGINFO | SA_RESTART;

  // Install handlers for all crash signals
  sigaction(SIGSEGV, &sa, NULL);  // Segmentation fault
  sigaction(SIGABRT, &sa, NULL);  // Abort
  sigaction(SIGFPE, &sa, NULL);   // Floating point exception
  sigaction(SIGILL, &sa, NULL);   // Illegal instruction
  sigaction(SIGBUS, &sa, NULL);   // Bus error
}

#endif // !_WIN32
