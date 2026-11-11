
/* Storage capacities in elements; text buffers include the terminating NUL. */
#define PLATFORM_POSIX_SYSTEM_CHUNK_BUFFER_SIZE 4096

#include <ascii-chat/util/url.h>
/**
 * @file platform/posix/system.c
 * @ingroup platform
 * @brief 🖥️ POSIX system utilities: process management, file I/O, memory mapping, and signal handling
 */

#ifndef _WIN32

#include "terminal.h"
#include <ascii-chat/otel.h>
#include <ascii-chat/platform/api.h>
#if defined(PLATFORM_IOS) && defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
#include <ascii-chat/platform/ios/render_bridge.h>
#include <ascii-chat/platform/ios/stdin_bridge.h>
#endif
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/internal.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/debug/backtrace.h>
#include <ascii-chat/common.h> // For log_error()
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/platform/symbols.h> // For symbol cache
#include <ascii-chat/platform/backtrace.h>
#include <ascii-chat/options/options.h> // For options_get()
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <termios.h>
// execinfo.h provides backtrace functions - available in glibc
// For musl, libexecinfo provides it but may not be available during compile
#ifndef USE_MUSL
#include <execinfo.h>
#else
// Forward declarations for backtrace functions when execinfo.h is not available
// These functions will either be provided by libexecinfo at link time, or we'll use fallbacks
extern int backtrace(void **buffer, int size) __attribute__((weak));
extern char **backtrace_symbols(void *const *buffer, int size) __attribute__((weak));
#endif
#include <pthread.h>
#include <stdarg.h>
#include <ascii-chat/atomic.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <netdb.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

/**
 * @brief Get username from environment variables
 * @return Username string or "unknown" if not found
 */
const char *get_username_env(void) {
  static char username[BUFFER_SIZE_SMALL];
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
asciichat_error_t platform_init(void) {
  // Initialize symbol cache for backtrace resolution
  if (symbol_cache_init() != ASCIICHAT_OK) {
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Symbol cache initialization failed");
  }

  // Initialize Winsock (required before getaddrinfo and socket operations)
  if (socket_init() != ASCIICHAT_OK) {
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Network operation failed");
  }

  // Install crash handlers for automatic backtrace on crashes
  platform_install_crash_handler();
  return ASCIICHAT_OK;
}

/**
 * @brief Clean up platform-specific functionality
 * @note POSIX platforms don't need special cleanup
 */
void platform_destroy(void) {
  // Cleanup binary PATH cache
  platform_cleanup_binary_path_cache();

  // Print symbol cache statistics before cleanup
  symbol_cache_print_stats();

  // Clean up symbol cache
  log_debug("Platform cleanup: calling symbol_cache_destroy()");
  symbol_cache_destroy();
}

/**
 * @brief Sleep for specified milliseconds
 * @param ms Number of milliseconds to sleep
 *
 * Uses a loop with 100ms intervals instead of a single usleep() call.
 * This allows signals to be processed more frequently without requiring
 * signal handlers to interrupt usleep(). Makes SIGTERM shutdown responsive
 * during reconnection delays.
 */
void platform_sleep_ms(unsigned int ms) {
  // Sleep in 100ms chunks so signals can wake us up more frequently
  unsigned int chunk_ms = 100;

  while (ms > 0) {
    unsigned int sleep_ms = (ms < chunk_ms) ? ms : chunk_ms;
    usleep(sleep_ms * 1000);
    ms -= sleep_ms;
  }
}

/**
 * @brief Get monotonic time in microseconds
 * @return Current monotonic time in microseconds
 *
 * Uses CLOCK_MONOTONIC for a monotonically increasing time value
 * that is not affected by system clock changes.
 */
uint64_t platform_get_monotonic_time_us(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    return 0; // Fallback on error (shouldn't happen)
  }
  return time_ns_to_us(time_timespec_to_ns(&ts));
}

/**
 * @brief Cross-platform high-precision sleep function
 * @param usec Number of microseconds to sleep
 *
 * POSIX implementation uses standard usleep() function.
 * Provides microsecond precision sleep capability.
 */
void platform_sleep_us(unsigned int usec) {
  usleep(usec);
}

/**
 * @brief Platform-safe sleep function with nanosecond precision (POSIX)
 * @param ns Sleep duration in nanoseconds
 */
void platform_sleep_ns(uint64_t ns) {
  struct timespec ts;
  ts.tv_sec = (time_t)(ns / NS_PER_SEC_INT);
  ts.tv_nsec = (long)(ns % NS_PER_SEC_INT);
  nanosleep(&ts, NULL);
}

/**
 * @brief Convert time_t to local time
 * @param timer Pointer to time_t value
 * @param result Pointer to struct tm to receive result
 * @return 0 on success, non-zero on error
 */
asciichat_error_t platform_localtime(const time_t *timer, struct tm *result) {
  if (!timer || !result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for localtime");
  }
  struct tm *tm_result = localtime_r(timer, result);
  if (!tm_result) {
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to convert time to local time");
  }
  return ASCIICHAT_OK;
}

asciichat_error_t platform_gtime(const time_t *timer, struct tm *result) {
  if (!timer || !result) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for gmtime");
  }

  struct tm *tm_result = gmtime_r(timer, result);
  if (!tm_result) {
    return SET_ERRNO_SYS(ERROR_PLATFORM_INIT, "Failed to convert time to UTC");
  }

  return ASCIICHAT_OK;
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
  sa.sa_flags = 0; // No SA_RESTART - allow system calls to return EINTR for signal handling

  // Install the signal handler
  if (sigaction(sig, &sa, &old_sa) == -1) {
    return SIG_ERR;
  }

  return old_sa.sa_handler;
}

// Global console control handler for POSIX
static console_ctrl_handler_t g_console_ctrl_handler = NULL;

/**
 * @brief POSIX signal handler that routes to console control handler
 * @param sig Signal number received
 */
static void posix_console_ctrl_signal_handler(int sig) {
  if (!g_console_ctrl_handler) {
    SET_ERRNO(ERROR_INVALID_STATE, "No console control handler registered");
    return;
  }

  console_ctrl_event_t event;
  switch (sig) {
  case SIGINT:
    event = CONSOLE_CTRL_C;
    break;
  case SIGTERM:
    event = CONSOLE_CLOSE;
    break;
  default:
    return;
  }

  // Call user's handler (in signal context - limited operations allowed!)
  (void)g_console_ctrl_handler(event);
}

/**
 * @brief Set console control handler (POSIX implementation)
 * @param handler Handler function to register, or NULL to unregister
 * @return true on success, false on failure
 *
 * Uses sigaction() for SIGINT and SIGTERM handling on POSIX systems.
 */
bool platform_set_console_ctrl_handler(console_ctrl_handler_t handler) {
  struct sigaction sa;

  if (handler != NULL) {
    g_console_ctrl_handler = handler;

    // Set up signal action
    sa.sa_handler = posix_console_ctrl_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // Don't use SA_RESTART - let the handler control flow

    // Install handlers for SIGINT and SIGTERM
    if (sigaction(SIGINT, &sa, NULL) == -1) {
      g_console_ctrl_handler = NULL;
      return false;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
      // Restore default SIGINT handler on failure
      signal(SIGINT, SIG_DFL);
      g_console_ctrl_handler = NULL;
      return false;
    }
  } else {
    // Unregister handler - restore default signal handling
    g_console_ctrl_handler = NULL;
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
  }
  return true;
}

/**
 * @brief Get environment variable value
 * @param name Environment variable name
 * @return Variable value or NULL if not found or contains invalid UTF-8
 *
 * Returns NULL if the environment variable contains invalid UTF-8 sequences,
 * helping prevent corruption from malformed environment data.
 */
const char *platform_getenv(const char *name) {
  const char *value = getenv(name);
  if (value && !utf8_is_valid(value)) {
    // Invalid UTF-8 detected - log warning and return NULL
    log_warn("Environment variable '%s' contains invalid UTF-8, ignoring", name);
    return NULL;
  }
  return value;
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

int platform_unsetenv(const char *name) {
  return unsetenv(name);
}

void platform_raise_fd_limit(unsigned int limit) {
#if defined(__linux__) || defined(__APPLE__)
  struct rlimit rl;
  if (getrlimit(RLIMIT_NOFILE, &rl) != 0)
    return;

  rlim_t target = limit > 0 ? (rlim_t)limit : rl.rlim_max;
#ifdef __APPLE__
  // macOS caps soft limit lower than hard limit
  if (target > 10240)
    target = 10240;
#endif
  if (target > rl.rlim_max)
    target = rl.rlim_max;
  if (target <= rl.rlim_cur)
    return;

  rl.rlim_cur = target;
  if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
    log_warn("Failed to raise fd limit to %llu: %s", (unsigned long long)target, strerror(errno));
  }
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
  SAFE_FREE(ptr);
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

// Thread-local storage for error strings (non-static for shared library compatibility)
__thread char error_buffer[BUFFER_SIZE_SMALL];

/**
 * @brief Get thread-safe error string
 * @param errnum Error number
 * @return Error string (thread-local storage)
 */
const char *platform_strerror(int errnum) {
  // Use strerror_r for thread safety
#if defined(__APPLE__) || !defined(__GLIBC__) || defined(USE_MUSL)
  // macOS, musl, and non-glibc use XSI-compliant strerror_r (returns int)
  if (strerror_r(errnum, error_buffer, sizeof(error_buffer)) != 0) {
    safe_snprintf(error_buffer, sizeof(error_buffer), "Unknown error %d", errnum);
  }
#else
  // glibc uses GNU strerror_r which returns a char*
  char *result = strerror_r(errnum, error_buffer, sizeof(error_buffer));
  if (result != error_buffer && result != NULL) {
    // GNU strerror_r may return a static string, but never NULL in normal cases
    strncpy(error_buffer, result, sizeof(error_buffer) - 1);
    error_buffer[sizeof(error_buffer) - 1] = '\0';
  } else if (result == NULL) {
    // Defensive: handle unexpected NULL return
    safe_snprintf(error_buffer, sizeof(error_buffer), "Unknown error %d", errnum);
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
 * @param name Debug name for the file descriptor
 * @param pathname File path
 * @param flags Open flags
 * @param ... Mode (if O_CREAT is specified)
 * @return File descriptor on success, -1 on failure
 */
int platform_open(const char *name, const char *pathname, int flags, ...) {
  if (!name) {
    return -1;
  }

  int fd = -1;
  int mode = 0;
  if (flags & O_CREAT) {
    va_list args;
    va_start(args, flags);
    mode = va_arg(args, int);
    va_end(args);
    fd = open(pathname, flags, mode);
  } else {
    fd = open(pathname, flags);
  }

  if (fd >= 0) {
    NAMED_REGISTER_FD(fd, name);
    log_dev("Opened file descriptor %d for %s at path %s", fd, name, pathname);
  }

  return fd;
}

/**
 * @brief Open file descriptor with platform-safe mode
 * @param name Debug name for the stream
 * @param fd File descriptor
 * @param mode File mode
 * @return File pointer on success, NULL on failure
 */
FILE *platform_fdopen(const char *name, int fd, const char *mode) {
  if (!name) {
    return NULL;
  }

  FILE *stream = fdopen(fd, mode);
  if (stream && fd >= 0) {
    NAMED_REGISTER_FD(fd, name);
    log_dev("Wrapped file descriptor %d with stream for %s", fd, name);
  }
  return stream;
}

/**
 * @brief Read from file descriptor
 * @param fd File descriptor
 * @param buf Buffer to read into
 * @param count Number of bytes to read
 * @return Number of bytes read, or -1 on error
 */
ssize_t platform_read(int fd, void *buf, size_t count) {
#if defined(PLATFORM_IOS) && defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
  if (fd == STDIN_FILENO)
    return ios_stdin_bridge_read(buf, count);
#endif
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
  if (!buf || count == 0) {
    log_warn("Invalid parameters: fd=%d, buf=%p, count=%zu", fd, buf, count);
  }
#if defined(PLATFORM_IOS) && defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
  return ios_render_bridge_write(fd, buf, count);
#else
  return write(fd, buf, count);
#endif
}

/**
 * @brief Close file descriptor
 * @param fd File descriptor
 * @return 0 on success, -1 on failure
 */
int platform_close(int fd) {
  return close(fd);
}

/**
 * @brief Open file with platform-safe mode
 * @param name Debug name for the file
 * @param filename File path
 * @param mode File mode (e.g., "r", "w", "a", "rb", "wb")
 * @return File pointer on success, NULL on failure
 */
FILE *platform_fopen(const char *name, const char *filename, const char *mode) {
  if (!name) {
    return NULL;
  }

  FILE *stream = fopen(filename, mode);
  if (stream) {
    int fd = fileno(stream);
    if (fd >= 0) {
      NAMED_REGISTER_FD(fd, name);
      log_dev("Opened file stream with file descriptor %d for %s", fd, name);
    }
  }
  return stream;
}

FILE *platform_tmpfile(void) {
  return tmpfile();
}

/**
 * @brief Delete file
 * @param pathname File path
 * @return 0 on success, -1 on failure
 */
int platform_unlink(const char *pathname) {
  return unlink(pathname);
}

/**
 * @brief Change file permissions
 * @param pathname File path
 * @param mode Permission mode (e.g., 0600, 0700)
 * @return 0 on success, -1 on failure
 */
int platform_chmod(const char *pathname, int mode) {
  return chmod(pathname, (mode_t)mode);
}

// ============================================================================
// Crash Handling
// ============================================================================

/**
 * @brief Crash signal handler
 */
static const char *get_signal_name(int sig) {
  switch (sig) {
  case SIGSEGV:
    return "SIGSEGV";
  case SIGABRT:
    return "SIGABRT";
  case SIGFPE:
    return "SIGFPE";
  case SIGILL:
    return "SIGILL";
  case SIGBUS:
    return "SIGBUS";
  default:
    return "UNKNOWN";
  }
}

static void crash_handler(int sig, siginfo_t *info, void *context) {
  (void)context; // Suppress unused parameter warning

  const char *signal_name = get_signal_name(sig);
  if (info) {
#ifndef NDEBUG
    // Only capture backtraces in Debug builds
    log_error("*** CRASH DETECTED ***\nSignal: %d (%s)\nSignal Info: si_code=%d, si_addr=%p", sig, signal_name,
              info->si_code, info->si_addr);
    backtrace_log_frames(LOG_ERROR, 0, 1);
#else
    log_error("*** CRASH DETECTED ***\nSignal: %d (%s)\nSignal Info: si_code=%d, si_addr=%p\nBacktrace disabled in "
              "Release builds",
              sig, signal_name, info->si_code, info->si_addr);
#endif
  } else {
#ifndef NDEBUG
    // Only capture backtraces in Debug builds
    log_error("*** CRASH DETECTED ***\nSignal: %d (%s)", sig, signal_name);
    backtrace_log_frames(LOG_ERROR, 0, 1);
#else
    log_error("*** CRASH DETECTED ***\nSignal: %d (%s)\nBacktrace disabled in Release builds", sig, signal_name);
#endif
  }

  // Restore default handler and re-raise signal
  struct sigaction sa;
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  (void)sigaction(sig, &sa, NULL);
  (void)raise(sig);
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
  sigaction(SIGSEGV, &sa, NULL); // Segmentation fault
  sigaction(SIGABRT, &sa, NULL); // Abort
  sigaction(SIGFPE, &sa, NULL);  // Floating point exception
  sigaction(SIGILL, &sa, NULL);  // Illegal instruction
  sigaction(SIGBUS, &sa, NULL);  // Bus error
}

// Safe string functions are defined in platform/posix/string.c

// ============================================================================
// Safe Memory Functions
// ============================================================================

asciichat_error_t platform_memcpy(void *dest, size_t dest_size, const void *src, size_t count) {
  // Validate parameters
  if (!dest || !src) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pointers for memcpy");
  }

  if (count > dest_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer overflow protection: count=%zu > dest_size=%zu", count, dest_size);
  }

  // Use standard memcpy with bounds checking already done
  memcpy(dest, src, count);
  return ASCIICHAT_OK;
}

asciichat_error_t platform_memset(void *dest, size_t dest_size, int ch, size_t count) {
  // Validate parameters
  if (!dest) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pointer for memset");
  }

  if (count > dest_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer overflow protection: count=%zu > dest_size=%zu", count, dest_size);
  }

  // Use standard memset with bounds checking already done
  memset(dest, ch, count);
  return ASCIICHAT_OK;
}

asciichat_error_t platform_memmove(void *dest, size_t dest_size, const void *src, size_t count) {
  // Validate parameters
  if (!dest || !src) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pointers for memmove");
  }

  if (count > dest_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Buffer overflow protection: count=%zu > dest_size=%zu", count, dest_size);
  }

  // Use standard memmove with bounds checking already done
  memmove(dest, src, count);
  return ASCIICHAT_OK; // Success
}

/**
 * Platform-safe strcpy wrapper
 *
 * Uses strcpy_s on Windows when available (C11) and strncpy with bounds checking on POSIX.
 * Always null-terminates the destination string.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string
 * @return 0 on success, non-zero on error
 */
asciichat_error_t platform_strcpy(char *dest, size_t dest_size, const char *src) {
  if (!dest || !src) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid pointers for strcpy");
  }
  if (dest_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Destination buffer size is zero");
  }

  size_t src_len = strlen(src);
  if (src_len >= dest_size) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Source string too long for destination buffer: %zu >= %zu", src_len,
                     dest_size);
  }

  // Use strncpy with bounds checking and ensure null termination
  strncpy(dest, src, dest_size - 1);
  dest[dest_size - 1] = '\0'; // Ensure null termination
  return ASCIICHAT_OK;        // Success
}

/**
 * Resolve hostname to IPv4 address (POSIX implementation)
 *
 * Performs DNS resolution to convert a hostname to an IPv4 address string.
 * Uses standard POSIX getaddrinfo() function.
 *
 * @param hostname Hostname to resolve (e.g., "example.com")
 * @param ipv4_out Buffer to store the resolved IPv4 address (e.g., "192.168.1.1")
 * @param ipv4_out_size Size of the output buffer
 * @return 0 on success, -1 on failure
 */
asciichat_error_t platform_resolve_hostname_to_ipv4(const char *hostname, char *ipv4_out, size_t ipv4_out_size) {
  if (!hostname || !ipv4_out || ipv4_out_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for hostname resolution");
  }

  char ascii_hostname[HOSTNAME_BUFFER_SIZE];
  asciichat_error_t normalized = hostname_to_ascii(hostname, ascii_hostname, sizeof(ascii_hostname));
  if (normalized != ASCIICHAT_OK)
    return normalized;
  hostname = ascii_hostname;

  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;       // IPv4 only
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  int ret;
  {
    ASCII_OTEL_SCOPE(DNS_RESOLVE);
    if (otel_scope.retained)
      gotel_attr_string(&otel_scope.scope, "dns.question.name", hostname);
    ret = getaddrinfo(hostname, NULL, &hints, &result);
    if (ret && otel_scope.retained)
      gotel_error(&otel_scope.scope, "dns.error", "DNS lookup failed");
  }
  if (ret != 0) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to resolve hostname: %s", hostname);
  }

  if (!result) {
    // Note: Don't call freeaddrinfo(NULL) - it's undefined behavior
    return SET_ERRNO(ERROR_NETWORK, "No address found for hostname: %s", hostname);
  }

  // Extract IPv4 address from first result
  asciichat_error_t format_result = format_ip_address(result->ai_family, result->ai_addr, ipv4_out, ipv4_out_size);
  freeaddrinfo(result);

  if (format_result != ASCIICHAT_OK) {
    return format_result;
  }

  return ASCIICHAT_OK;
}

#if !defined(PLATFORM_IOS) || !defined(__IPHONE_OS_VERSION_MIN_REQUIRED)
/**
 * Load system CA certificates for TLS/HTTPS (POSIX implementation)
 *
 * Tries common system paths for CA certificate bundles on Linux and macOS.
 * Reads the first available bundle into memory as PEM data.
 *
 * @param pem_data_out Pointer to receive allocated PEM data (caller must free)
 * @param pem_size_out Pointer to receive size of PEM data
 * @return 0 on success, -1 on failure
 */
asciichat_error_t platform_load_system_ca_certs(char **pem_data_out, size_t *pem_size_out) {
  if (!pem_data_out || !pem_size_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for CA cert loading");
  }

  const char *env_ca_path = SAFE_GETENV("SSL_CERT_FILE");
  if (env_ca_path && *env_ca_path) {
    FILE *f = fopen(env_ca_path, "rb");
    if (f) {
      fseek(f, 0, SEEK_END);
      long file_size = ftell(f);
      fseek(f, 0, SEEK_SET);
      if (file_size > 0 && file_size <= 10 * 1024 * 1024) {
        char *pem_data = SAFE_MALLOC((size_t)file_size + 1, char *);
        if (!pem_data) {
          fclose(f);
          return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for CA certificates");
        }
        size_t bytes_read = fread(pem_data, 1, (size_t)file_size, f);
        fclose(f);
        if (bytes_read == (size_t)file_size) {
          pem_data[bytes_read] = '\0';
          *pem_data_out = pem_data;
          *pem_size_out = bytes_read;
          return ASCIICHAT_OK;
        }
        SAFE_FREE(pem_data);
      } else {
        fclose(f);
      }
    }
  }

  // Common CA certificate bundle paths (ordered by likelihood)
  // On macOS, prefer Homebrew ca-certificates if installed (more up-to-date than system)
  static const char *ca_paths[] = {
#ifdef __APPLE__
      "/opt/homebrew/opt/ca-certificates/share/ca-certificates/cacert.pem", // Homebrew ca-certificates (Apple Silicon)
      "/usr/local/opt/ca-certificates/share/ca-certificates/cacert.pem",    // Homebrew ca-certificates (Intel Mac)
#endif
      "/etc/ssl/certs/ca-certificates.crt",                // Debian/Ubuntu/Gentoo/Arch
      "/etc/pki/tls/certs/ca-bundle.crt",                  // RHEL/CentOS/Fedora
      "/etc/ssl/cert.pem",                                 // OpenBSD/macOS/Alpine
      "/usr/local/etc/openssl/cert.pem",                   // Homebrew OpenSSL on macOS
      "/etc/ssl/ca-bundle.pem",                            // OpenSUSE
      "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem", // CentOS/RHEL 7+
      "/usr/share/ssl/certs/ca-bundle.crt",                // Old Red Hat
      "/usr/local/share/certs/ca-root-nss.crt",            // FreeBSD
      "/etc/openssl/certs/ca-certificates.crt",            // OpenWall (musl)
      NULL};

  // Try each path until we find one that exists and is readable
  for (int i = 0; ca_paths[i] != NULL; i++) {
    FILE *f = fopen(ca_paths[i], "rb");
    if (!f) {
      continue; // Try next path
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size <= 0 || file_size > 10 * 1024 * 1024) {
      // Empty file or suspiciously large (>10MB)
      fclose(f);
      continue;
    }

    // Allocate buffer for PEM data
    char *pem_data = SAFE_MALLOC(file_size + 1, char *);
    if (!pem_data) {
      fclose(f);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for CA certificates");
    }

    // Read entire file
    size_t bytes_read = fread(pem_data, 1, (size_t)file_size, f);
    fclose(f);

    if (bytes_read != (size_t)file_size) {
      SAFE_FREE(pem_data);
      return SET_ERRNO(ERROR_CRYPTO, "Failed to read complete CA certificate file");
    }

    // Null-terminate (PEM is text format)
    // We verified bytes_read == file_size above, and allocated file_size + 1 bytes.
    // file_size is bounded by the check on line 863 (0 < file_size <= 10MB).
    // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound) - file_size is bounded by allocation
    pem_data[file_size] = '\0';

    // Success!
    *pem_data_out = pem_data;
    *pem_size_out = bytes_read;
    log_debug("Loaded CA certificates from: %s (%zu bytes)", ca_paths[i], bytes_read);
    return ASCIICHAT_OK;
  }

  // No CA bundle found
  return SET_ERRNO(ERROR_CRYPTO, "No CA certificate bundle found in standard locations");
}
#endif

/**
 * @brief Get the system temporary directory path (POSIX implementation)
 *
 * Returns /tmp for Unix systems (Linux/macOS), verifying it exists and is writable.
 *
 * @param temp_dir Buffer to store the temporary directory path
 * @param path_size Size of the buffer
 * @return true on success, false on failure
 */
bool platform_get_temp_dir(char *temp_dir, size_t path_size) {
  if (temp_dir == NULL || path_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: temp_dir=%s, path_size=%zu", temp_dir, path_size);
    return false;
  }

  // On Unix systems, /tmp is the standard temporary directory
  const char *tmp = "/tmp";

  // Check if buffer is large enough
  if (strlen(tmp) >= path_size) {
    return false;
  }

  // Verify the directory exists and is writable
  if (access(tmp, W_OK) != 0) {
    // /tmp doesn't exist or isn't writable
    return false;
  }

  // Copy the path
  SAFE_STRNCPY(temp_dir, tmp, path_size);
  return true;
}

bool platform_get_cwd(char *cwd, size_t path_size) {
  if (!cwd || path_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: cwd=%s, path_size=%zu", cwd, path_size);
    return false;
  }

  if (getcwd(cwd, path_size) == NULL) {
    return false;
  }

  return true;
}

int platform_access(const char *path, int mode) {
  if (!path) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: path=%s", path);
    return -1;
  }

  // Map platform-independent modes to POSIX modes
  int posix_mode;
  switch (mode) {
  case 0: // PLATFORM_ACCESS_EXISTS
    posix_mode = F_OK;
    break;
  case 2: // PLATFORM_ACCESS_WRITE
    posix_mode = W_OK;
    break;
  case 4: // PLATFORM_ACCESS_READ
    posix_mode = R_OK;
    break;
  default:
    return -1; // Invalid mode
  }

  return access(path, posix_mode);
}

// ============================================================================
// Stream Redirection
// ============================================================================

platform_stderr_redirect_handle_t platform_stderr_redirect_to_null(void) {
  platform_stderr_redirect_handle_t handle = {.original_fd = -1, .devnull_fd = -1};

  // Save current stderr file descriptor
  handle.original_fd = dup(STDERR_FILENO);
  if (handle.original_fd < 0) {
    return handle; // Failed to dup stderr
  }

  // Open /dev/null
  handle.devnull_fd = platform_open("devnull_redirect", "/dev/null", O_WRONLY, 0);
  if (handle.devnull_fd < 0) {
    close(handle.original_fd);
    handle.original_fd = -1;
    return handle; // Failed to open /dev/null
  }

  // Redirect stderr to /dev/null
  if (dup2(handle.devnull_fd, STDERR_FILENO) < 0) {
    close(handle.original_fd);
    close(handle.devnull_fd);
    handle.original_fd = -1;
    handle.devnull_fd = -1;
    return handle; // Failed to redirect
  }

  return handle;
}

void platform_stderr_restore(platform_stderr_redirect_handle_t handle) {
  // Restore original stderr
  if (handle.original_fd >= 0) {
    dup2(handle.original_fd, STDERR_FILENO);
    close(handle.original_fd);
  }

  // Close /dev/null
  if (handle.devnull_fd >= 0) {
    close(handle.devnull_fd);
  }
}

void platform_stdio_redirect_to_null_permanent(void) {
  int dev_null = platform_open("devnull_redirect", "/dev/null", O_WRONLY, 0);
  if (dev_null >= 0) {
    dup2(dev_null, STDERR_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    close(dev_null);
  }
}

/**
 * @brief Redirect both stdout and stderr to /dev/null (restorable)
 * @return Handle that can be passed to platform_stdout_stderr_restore()
 *
 * This is useful for suppressing library output during initialization.
 * Always call platform_stdout_stderr_restore() with the returned handle
 * to restore the original file descriptors.
 */
platform_stderr_redirect_handle_t platform_stdout_stderr_redirect_to_null(void) {
  platform_stderr_redirect_handle_t handle = {.original_fd = -1, .devnull_fd = -1};

  // For stdout, we'll use original_fd to store the saved stdout
  // We reuse the same structure but use it differently:
  // original_fd stores the saved stdout
  // devnull_fd stores the saved stderr (to be restored together)
  int saved_stdout = dup(STDOUT_FILENO);
  int saved_stderr = dup(STDERR_FILENO);
  if (saved_stdout < 0 || saved_stderr < 0) {
    if (saved_stdout >= 0)
      close(saved_stdout);
    if (saved_stderr >= 0)
      close(saved_stderr);
    return handle;
  }

  // Open /dev/null for redirection
  int devnull = platform_open("devnull_redirect", "/dev/null", O_WRONLY, 0);
  if (devnull < 0) {
    close(saved_stdout);
    close(saved_stderr);
    return handle;
  }

  // Redirect both stdout and stderr to /dev/null
  if (dup2(devnull, STDOUT_FILENO) < 0 || dup2(devnull, STDERR_FILENO) < 0) {
    close(devnull);
    close(saved_stdout);
    close(saved_stderr);
    return handle;
  }

  close(devnull);

  // Store both saved FDs in the handle (we'll need both on restore)
  // Use original_fd for stdout, devnull_fd for stderr
  handle.original_fd = saved_stdout;
  handle.devnull_fd = saved_stderr;

  return handle;
}

/**
 * @brief Restore stdout and stderr after platform_stdout_stderr_redirect_to_null()
 * @param handle Handle returned by platform_stdout_stderr_redirect_to_null()
 */
void platform_stdout_stderr_restore(platform_stderr_redirect_handle_t handle) {
  // Restore stdout
  if (handle.original_fd >= 0) {
    dup2(handle.original_fd, STDOUT_FILENO);
    close(handle.original_fd);
  }

  // Restore stderr
  if (handle.devnull_fd >= 0) {
    dup2(handle.devnull_fd, STDERR_FILENO);
    close(handle.devnull_fd);
  }
}

/**
 * @brief Forcefully terminate the process immediately without cleanup (POSIX)
 * @param exit_code Exit code to return
 * @note Does not return - process is terminated
 */
void platform_force_exit(int exit_code) {
  _exit(exit_code);
}

/**
 * @brief Register multiple signal handlers at once (POSIX)
 * @param handlers Array of signal handler descriptors
 * @param count Number of handlers
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * POSIX implementation registers each signal using platform_signal().
 * Windows implementation handles this differently via console control handlers.
 */
asciichat_error_t platform_register_signal_handlers(const platform_signal_handler_t *handlers, int count) {
  if (!handlers || count <= 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid handlers or count");
  }

  for (int i = 0; i < count; i++) {
    signal_handler_t old_handler = platform_signal(handlers[i].sig, handlers[i].handler);
    if (old_handler == SIG_ERR) {
      log_warn("Failed to register signal handler for signal %d", handlers[i].sig);
      // Continue registering others instead of failing completely
    }
  }

  return ASCIICHAT_OK;
}

/**
 * Request coarse timer precision (POSIX no-op)
 */
asciichat_error_t platform_request_timer_precision(int precision) {
  (void)precision; // Unused on POSIX
  // POSIX systems don't have a timer precision API like Windows
  return ASCIICHAT_OK;
}

/**
 * Restore default timer precision (POSIX no-op)
 */
asciichat_error_t platform_restore_timer_resolution(void) {
  // POSIX systems don't have a timer precision API like Windows
  return ASCIICHAT_OK;
}

/**
 * Execute a subprocess using fork+exec (POSIX implementation)
 */
// Serialize foreground ownership checks and changes across concurrent loaders.
static pthread_mutex_t subprocess_terminal_mutex = PTHREAD_MUTEX_INITIALIZER;
static pid_t subprocess_terminal_owner = -1;

void terminal_input_lock(void) {
  pthread_mutex_lock(&subprocess_terminal_mutex);
}

bool terminal_input_trylock(void) {
  return pthread_mutex_trylock(&subprocess_terminal_mutex) == 0;
}

void terminal_input_unlock(void) {
  pthread_mutex_unlock(&subprocess_terminal_mutex);
}

static int subprocess_set_foreground(int fd, pid_t group) {
  sigset_t block, previous;
  sigemptyset(&block);
  sigaddset(&block, SIGTTOU);
  if (pthread_sigmask(SIG_BLOCK, &block, &previous) != 0)
    return -1;
  int result = tcsetpgrp(fd, group);
  pthread_sigmask(SIG_SETMASK, &previous, NULL);
  return result;
}

// A child may exit before its start gate is released. Do not let a broken
// gate deliver SIGPIPE to the application, and preserve any pre-existing one.
bool terminal_release_subprocess_gate(int fd) {
  sigset_t blocked, previous, pending;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  if (pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0)
    return false;
  sigpending(&pending);
  bool already_pending = sigismember(&pending, SIGPIPE) == 1;
  ssize_t written;
  do {
    written = write(fd, "x", 1);
  } while (written < 0 && errno == EINTR);
  if (written < 0 && errno == EPIPE && !already_pending && sigpending(&pending) == 0 &&
      sigismember(&pending, SIGPIPE) == 1) {
    int received;
    sigwait(&blocked, &received);
  }
  pthread_sigmask(SIG_SETMASK, &previous, NULL);
  return written == 1;
}

int platform_execute_subprocess(const char *executable, const char **argv, char *output_buffer, size_t output_size,
                                bool (*cancel)(void *), void *data) {
  if (!executable || !argv || !argv[0]) {
    log_error("Invalid subprocess parameters");
    return -1;
  }
  bool capture = output_buffer && output_size > 0;
  int output_pipe[2] = {-1, -1};
  if (capture) {
    output_buffer[0] = '\0';
    if (pipe(output_pipe) != 0)
      return -1;
    if (fcntl(output_pipe[0], F_SETFD, FD_CLOEXEC) < 0 || fcntl(output_pipe[1], F_SETFD, FD_CLOEXEC) < 0) {
      close(output_pipe[0]);
      close(output_pipe[1]);
      return -1;
    }
  }
  int tty = cancel ? open("/dev/tty", O_RDWR | O_CLOEXEC) : -1;
  int start_pipe[2] = {-1, -1};
  bool gate_open = false;
  if (tty >= 0 && isatty(STDIN_FILENO)) {
    pthread_mutex_lock(&subprocess_terminal_mutex);
    pid_t foreground = tcgetpgrp(tty);
    gate_open = foreground == getpgrp() || (subprocess_terminal_owner > 0 && foreground == subprocess_terminal_owner);
    pthread_mutex_unlock(&subprocess_terminal_mutex);
  }
  if (gate_open && (pipe(start_pipe) != 0 || fcntl(start_pipe[0], F_SETFD, FD_CLOEXEC) < 0 ||
                    fcntl(start_pipe[1], F_SETFD, FD_CLOEXEC) < 0)) {
    if (start_pipe[0] >= 0)
      close(start_pipe[0]);
    if (start_pipe[1] >= 0)
      close(start_pipe[1]);
    if (capture) {
      close(output_pipe[0]);
      close(output_pipe[1]);
    }
    close(tty);
    return -1;
  }
  pid_t pid = fork();
  if (pid < 0) {
    if (capture) {
      close(output_pipe[0]);
      close(output_pipe[1]);
    }
    if (gate_open) {
      close(start_pipe[0]);
      close(start_pipe[1]);
    }
    if (tty >= 0)
      close(tty);
    log_error("Failed to fork process: %s", SAFE_STRERROR(errno));
    return -1;
  }
  if (pid == 0) {
    if (tty >= 0)
      close(tty);
    if (cancel && setpgid(0, 0) != 0)
      _exit(126);
    if (gate_open) {
      close(start_pipe[1]);
      char ignored;
      ssize_t count;
      do {
        count = read(start_pipe[0], &ignored, 1);
      } while (count < 0 && errno == EINTR);
      close(start_pipe[0]);
      if (count != 1)
        _exit(126);
    }
    if (capture) {
      close(output_pipe[0]);
      if (output_pipe[1] == STDOUT_FILENO) {
        if (fcntl(STDOUT_FILENO, F_SETFD, 0) < 0)
          _exit(126);
      } else {
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0)
          _exit(126);
        close(output_pipe[1]);
      }
    }
    // The forked child must not run inherited log locks or atexit handlers.
    execvp(executable, (char *const *)argv);
    _exit(127);
  }
  if (cancel)
    (void)setpgid(pid, pid);
  if (gate_open)
    close(start_pipe[0]);
  bool capture_failed = false;
  bool wait_failed = false;
  bool cancelled = false;
  size_t position = 0;
  int status = 0;
  bool exited = false;
  bool pipe_open = capture;
  bool terminal_stopped = false;
  bool terminal_owned = false;
  struct termios terminal_attributes;
  bool have_terminal_attributes = false;
  if (capture)
    close(output_pipe[1]);
  while (!exited || pipe_open) {
    if (cancel && cancel(data)) {
      cancelled = true;
      kill(-pid, SIGKILL);
      if (!exited) {
        kill(pid, SIGKILL);
        siginfo_t info = {0};
        int waited;
        do {
          waited = waitid(P_PID, (id_t)pid, &info, WEXITED | WNOWAIT);
        } while (waited < 0 && errno == EINTR);
        wait_failed = waited < 0;
      }
      if (pipe_open)
        close(output_pipe[0]);
      if (gate_open) {
        close(start_pipe[1]);
        gate_open = false;
      }
      if (capture)
        output_buffer[position] = '\0';
      break;
    }
    if (pipe_open) {
      struct pollfd fd = {.fd = output_pipe[0], .events = POLLIN | POLLHUP};
      int ready = poll(&fd, 1, 50);
      if (ready < 0 && errno != EINTR) {
        capture_failed = true;
        close(output_pipe[0]);
        pipe_open = false;
      } else if (ready > 0) {
        char chunk[PLATFORM_POSIX_SYSTEM_CHUNK_BUFFER_SIZE];
        ssize_t count = read(output_pipe[0], chunk, sizeof(chunk));
        if (count > 0) {
          size_t available = output_size - 1 - position;
          size_t copied = (size_t)count < available ? (size_t)count : available;
          memcpy(output_buffer + position, chunk, copied);
          position += copied;
          if ((size_t)count > available)
            capture_failed = true;
        } else if (count == 0 || errno != EINTR) {
          if (count < 0)
            capture_failed = true;
          close(output_pipe[0]);
          pipe_open = false;
        }
      }
    } else if (!exited) {
      poll(NULL, 0, 50);
    }
    if (!exited) {
      siginfo_t info = {0};
      int waited = waitid(P_PID, (id_t)pid, &info, WEXITED | WSTOPPED | WNOHANG | WNOWAIT);
      if (waited == 0 && info.si_pid == pid) {
        if (info.si_code == CLD_STOPPED) {
          // Consume stop notifications so the next wait can observe exit.
          siginfo_t stopped = {0};
          (void)waitid(P_PID, (id_t)pid, &stopped, WSTOPPED | WNOHANG);
          if (info.si_status == SIGTTIN && tty >= 0)
            terminal_stopped = true;
          else {
            // A stopped foreground child otherwise blocks the TUI indefinitely.
            capture_failed = true;
            kill(-pid, SIGKILL);
            kill(pid, SIGKILL);
          }
        } else {
          exited = true;
        }
      } else if (waited < 0 && errno != EINTR) {
        exited = true;
        capture_failed = true;
        wait_failed = true;
      }
    }
    if ((gate_open || terminal_stopped) && !exited) {
      pthread_mutex_lock(&subprocess_terminal_mutex);
      pid_t foreground = tcgetpgrp(tty);
      if (terminal_stopped && terminal_owned && foreground == pid) {
        terminal_stopped = false;
        kill(-pid, SIGCONT);
      } else if (foreground == getpgrp()) {
        have_terminal_attributes = tcgetattr(tty, &terminal_attributes) == 0;
        // The application still renders while the child owns input. Prevent a
        // terminal configured with TOSTOP from suspending those writes.
        bool cleared_tostop = false;
        if (have_terminal_attributes && (terminal_attributes.c_lflag & TOSTOP)) {
          struct termios foreground_attributes = terminal_attributes;
          foreground_attributes.c_lflag &= ~(tcflag_t)TOSTOP;
          cleared_tostop = tcsetattr(tty, TCSANOW, &foreground_attributes) == 0;
        }
        if (have_terminal_attributes && (!(terminal_attributes.c_lflag & TOSTOP) || cleared_tostop) &&
            subprocess_set_foreground(tty, pid) == 0) {
          terminal_owned = true;
          subprocess_terminal_owner = pid;
          if (gate_open) {
            bool released = terminal_release_subprocess_gate(start_pipe[1]);
            close(start_pipe[1]);
            gate_open = false;
            if (!released) {
              capture_failed = true;
              kill(-pid, SIGKILL);
            }
          }
          if (terminal_stopped) {
            terminal_stopped = false;
            kill(-pid, SIGCONT);
          }
        } else {
          if (cleared_tostop)
            tcsetattr(tty, TCSANOW, &terminal_attributes);
          capture_failed = true;
          kill(-pid, SIGKILL);
          if (gate_open) {
            close(start_pipe[1]);
            gate_open = false;
          }
          terminal_stopped = false;
        }
      }
      pthread_mutex_unlock(&subprocess_terminal_mutex);
    }
  }
  if (capture)
    output_buffer[position] = '\0';
  if (gate_open)
    close(start_pipe[1]);
  if (terminal_owned) {
    pthread_mutex_lock(&subprocess_terminal_mutex);
    if (tcgetpgrp(tty) == pid && subprocess_set_foreground(tty, getpgrp()) == 0 && have_terminal_attributes)
      tcsetattr(tty, TCSANOW, &terminal_attributes);
    if (subprocess_terminal_owner == pid)
      subprocess_terminal_owner = -1;
    pthread_mutex_unlock(&subprocess_terminal_mutex);
  }
  if (tty >= 0)
    close(tty);
  if (!wait_failed) {
    pid_t waited;
    do {
      waited = waitpid(pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != pid)
      wait_failed = true;
  }
  return !cancelled && !capture_failed && !wait_failed && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
#endif // !_WIN32
