/**
 * @file system.c
 * @brief Windows system functions implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides Windows system function wrappers for the platform abstraction layer,
 * enabling cross-platform system operations using a unified API.
 */

#ifdef _WIN32

#include "../abstraction.h"
#include "../internal.h"
#include "../../common.h" // For log_error()
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <stdatomic.h>

/**
 * @brief Get username from environment variables
 * @return Username string or "unknown" if not found
 */
const char *get_username_env(void) {
  static char username[256];
  const char *user = getenv("USERNAME");
  if (!user) {
    user = getenv("USER");
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
 */
int platform_init(void) {
  // Set binary mode for stdin/stdout to handle raw data
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);

  // Initialize Winsock will be done in socket_windows.c
  return 0;
}

/**
 * @brief Clean up platform-specific functionality
 */
void platform_cleanup(void) {
  // Cleanup will be done in socket_windows.c for Winsock
}

/**
 * @brief Sleep for specified milliseconds
 * @param ms Number of milliseconds to sleep
 */
void platform_sleep_ms(unsigned int ms) {
  Sleep(ms);
}

/**
 * @brief Sleep for specified microseconds
 * @param us Number of microseconds to sleep
 * @note Windows Sleep only supports milliseconds, so we convert
 */
void platform_sleep_us(unsigned int us) {
  // Windows Sleep only supports milliseconds, so convert
  Sleep((us + 999) / 1000);
}

/**
 * @brief POSIX-compatible usleep function for Windows
 * @param usec Number of microseconds to sleep
 * @return 0 on success
 */
int usleep(unsigned int usec) {
  // Use the platform function
  platform_sleep_us(usec);
  return 0;
}

/**
 * @brief Cross-platform high-precision sleep function with shutdown support
 * @param usec Number of microseconds to sleep
 *
 * On Windows, uses Sleep() with millisecond precision.
 * Simple implementation due to Windows timer characteristics.
 * Sleep(1) may sleep up to 15.6ms due to timer resolution.
 */
void platform_sleep_usec(unsigned int usec) {
  // Convert microseconds to milliseconds, minimum 1ms
  int timeout_ms = (int)(usec / 1000);
  if (timeout_ms < 1)
    timeout_ms = 1;

  Sleep(timeout_ms);
}

/**
 * @brief Get current process ID
 * @return Process ID as integer
 */
int platform_get_pid(void) {
  return (int)GetCurrentProcessId();
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
  return _putenv_s(name, value);
}

/**
 * @brief Check if file descriptor is a TTY
 * @param fd File descriptor to check
 * @return 1 if TTY, 0 if not
 */
int platform_isatty(int fd) {
  return _isatty(fd);
}

/**
 * @brief Get TTY name for a file descriptor
 * @param fd File descriptor
 * @return TTY name or NULL if not a TTY
 */
const char *platform_ttyname(int fd) {
  // Windows doesn't have ttyname, return "CON" for console
  if (_isatty(fd)) {
    return "CON";
  }
  return NULL;
}

/**
 * @brief Synchronize file descriptor to disk
 * @param fd File descriptor to sync
 * @return 0 on success, -1 on failure
 */
int platform_fsync(int fd) {
  // Windows uses _commit for file sync
  return _commit(fd);
}

// ============================================================================
// Debug/Stack Trace Functions
// ============================================================================

/**
 * @brief Get stack trace (stub on Windows)
 * @param buffer Array to store trace addresses
 * @param size Maximum number of addresses to retrieve
 * @return Number of addresses retrieved (always 0 on Windows)
 */
int platform_backtrace(void **buffer, int size) {
  (void)buffer;
  (void)size;
  // Windows doesn't have a simple backtrace function
  // Could use StackWalk64 API but it's complex
  return 0;
}

/**
 * @brief Convert stack trace addresses to symbols (stub on Windows)
 * @param buffer Array of addresses from platform_backtrace
 * @param size Number of addresses in buffer
 * @return NULL on Windows
 */
char **platform_backtrace_symbols(void *const *buffer, int size) {
  (void)buffer;
  (void)size;
  return NULL;
}

/**
 * @brief Free memory from platform_backtrace_symbols (no-op on Windows)
 * @param strings Array returned by platform_backtrace_symbols
 */
void platform_backtrace_symbols_free(char **strings) {
  (void)strings;
  // No-op on Windows since we return NULL
}

/**
 * @brief clock_gettime implementation for Windows
 * @param clk_id Clock ID (unused)
 * @param tp Pointer to timespec structure to fill
 * @return 0 on success, -1 on failure
 */
int clock_gettime(int clk_id, struct timespec *tp) {
  LARGE_INTEGER freq, counter;
  (void)clk_id; // Unused parameter

  if (!QueryPerformanceFrequency(&freq) || !QueryPerformanceCounter(&counter)) {
    return -1;
  }

  // Convert to seconds and nanoseconds
  tp->tv_sec = counter.QuadPart / freq.QuadPart;
  tp->tv_nsec = ((counter.QuadPart % freq.QuadPart) * 1000000000) / freq.QuadPart;

  return 0;
}

/**
 * @brief aligned_alloc implementation for Windows
 * @param alignment Memory alignment requirement
 * @param size Size of memory block to allocate
 * @return Pointer to aligned memory block, or NULL on failure
 */
void *aligned_alloc(size_t alignment, size_t size) {
  return _aligned_malloc(size, alignment);
}

/**
 * @brief gmtime_r implementation for Windows (thread-safe gmtime)
 * @param timep Pointer to time_t value
 * @param result Pointer to struct tm to fill
 * @return Pointer to result on success, NULL on failure
 */
struct tm *gmtime_r(const time_t *timep, struct tm *result) {
  errno_t err = gmtime_s(result, timep);
  if (err != 0) {
    return NULL;
  }
  return result;
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
  int result = vsnprintf_s(str, size, _TRUNCATE, format, args);
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
  return vsnprintf_s(str, size, _TRUNCATE, format, ap);
}

/**
 * @brief Duplicate a string
 * @param s Source string
 * @return Allocated copy of string, or NULL on failure
 */
char *platform_strdup(const char *s) {
  return _strdup(s);
}

/**
 * @brief Duplicate up to n characters of a string
 * @param s Source string
 * @param n Maximum number of characters to copy
 * @return Allocated copy of string, or NULL on failure
 */
char *platform_strndup(const char *s, size_t n) {
  size_t len = strnlen(s, n);
  char *result = (char *)malloc(len + 1);
  if (result) {
    memcpy(result, s, len);
    result[len] = '\0';
  }
  return result;
}

/**
 * @brief Case-insensitive string comparison
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int platform_strcasecmp(const char *s1, const char *s2) {
  return _stricmp(s1, s2);
}

/**
 * @brief Case-insensitive string comparison with length limit
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int platform_strncasecmp(const char *s1, const char *s2, size_t n) {
  return _strnicmp(s1, s2, n);
}

/**
 * @brief Thread-safe string tokenization
 * @param str String to tokenize (NULL for continuation)
 * @param delim Delimiter string
 * @param saveptr Pointer to save state between calls
 * @return Pointer to next token, or NULL if no more tokens
 */
char *platform_strtok_r(char *str, const char *delim, char **saveptr) {
  return strtok_s(str, delim, saveptr);
}

/**
 * @brief Safe string copy with size limit
 * @param dst Destination buffer
 * @param src Source string
 * @param size Destination buffer size
 * @return Length of source string (excluding null terminator)
 */
size_t platform_strlcpy(char *dst, const char *src, size_t size) {
  if (size == 0)
    return strlen(src);

  strncpy_s(dst, size, src, _TRUNCATE);
  return strlen(src);
}

/**
 * @brief Safe string concatenation with size limit
 * @param dst Destination buffer
 * @param src Source string
 * @param size Destination buffer size
 * @return Total length of resulting string
 */
size_t platform_strlcat(char *dst, const char *src, size_t size) {
  size_t dst_len = strnlen(dst, size);
  if (dst_len == size)
    return size + strlen(src);

  strncat_s(dst, size, src, _TRUNCATE);
  return dst_len + strlen(src);
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
  return _aligned_malloc(size, alignment);
}

/**
 * @brief Free aligned memory
 * @param ptr Pointer to aligned memory block
 */
void platform_aligned_free(void *ptr) {
  _aligned_free(ptr);
}

/**
 * @brief Memory barrier for synchronization
 */
void platform_memory_barrier(void) {
  MemoryBarrier();
}

// ============================================================================
// Error Handling
// ============================================================================

// Thread-local storage for error strings
static __declspec(thread) char error_buffer[256];

/**
 * @brief Get thread-safe error string
 * @param errnum Error number
 * @return Error string (thread-local storage)
 */
const char *platform_strerror(int errnum) {
  strerror_s(error_buffer, sizeof(error_buffer), errnum);
  return error_buffer;
}

/**
 * @brief Get last error code
 * @return Last error code (GetLastError on Windows)
 */
int platform_get_last_error(void) {
  return (int)GetLastError();
}

/**
 * @brief Set last error code
 * @param error Error code to set
 */
void platform_set_last_error(int error) {
  SetLastError((DWORD)error);
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
  if (flags & _O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
  }

  // Windows requires _O_BINARY for binary mode
  if (!(flags & _O_TEXT)) {
    flags |= _O_BINARY;
  }

  return _open(pathname, flags, mode);
}

/**
 * @brief Read from file descriptor
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
ssize_t platform_read(int fd, void *buf, size_t count) {
  // Windows _read returns int, convert to ssize_t
  return (ssize_t)_read(fd, buf, (unsigned int)count);
}

/**
 * @brief Write to file descriptor
 * @param fd File descriptor
 * @param buf Buffer to write from
 * @param count Number of bytes to write
 * @return Number of bytes written, or -1 on error
 */
ssize_t platform_write(int fd, const void *buf, size_t count) {
  // Windows _write returns int, convert to ssize_t
  return (ssize_t)_write(fd, buf, (unsigned int)count);
}

/**
 * @brief Close file descriptor
 * @param fd File descriptor
 * @return 0 on success, -1 on failure
 */
int platform_close(int fd) {
  return _close(fd);
}

#endif // _WIN32