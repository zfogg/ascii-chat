/**
 * @file platform/posix/system.c
 * @ingroup platform
 * @brief 🖥️ POSIX system utilities: process management, file I/O, memory mapping, and signal handling
 */

#ifndef _WIN32

#include "../abstraction.h"
#include "../internal.h"
#include "../../common.h" // For log_error()
#include "../../common/buffer_sizes.h"
#include "../../asciichat_errno.h"
#include "../../util/ip.h"
#include "../../util/string.h"
#include "../../util/time.h"
#include "../../util/utf8.h"
#include "../symbols.h" // For symbol cache
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
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
#include <stdatomic.h>
#include <sys/stat.h>
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
void platform_cleanup(void) {
  // Cleanup binary PATH cache
  platform_cleanup_binary_path_cache();

  // Print symbol cache statistics before cleanup
  symbol_cache_print_stats();

  // Clean up symbol cache
  log_debug("Platform cleanup: calling symbol_cache_cleanup()");
  symbol_cache_cleanup();
}

/**
 * @brief Sleep for specified milliseconds
 * @param ms Number of milliseconds to sleep
 */
void platform_sleep_ms(unsigned int ms) {
  usleep(ms * 1000);
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
void platform_sleep_usec(unsigned int usec) {
  usleep(usec);
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

// Global console control handler for POSIX
static console_ctrl_handler_t g_console_ctrl_handler = NULL;

/**
 * @brief POSIX signal handler that routes to console control handler
 * @param sig Signal number received
 */
static void posix_console_ctrl_signal_handler(int sig) {
  if (!g_console_ctrl_handler) {
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

// Thread-local storage for error strings
static __thread char error_buffer[BUFFER_SIZE_SMALL];

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
 * @brief Open file descriptor with platform-safe mode
 * @param fd File descriptor
 * @param mode File mode
 * @return File pointer on success, NULL on failure
 */
FILE *platform_fdopen(int fd, const char *mode) {
  return fdopen(fd, mode);
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

/**
 * @brief Open file with platform-safe mode
 * @param filename File path
 * @param mode File mode (e.g., "r", "w", "a", "rb", "wb")
 * @return File pointer on success, NULL on failure
 */
FILE *platform_fopen(const char *filename, const char *mode) {
  return fopen(filename, mode);
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
// Debug/Stack Trace Functions
// ============================================================================

/**
 * @brief Manual stack unwinding using frame pointers
 * @param buffer Array to store trace addresses
 * @param size Maximum number of addresses to retrieve
 * @return Number of addresses retrieved
 */
static int manual_backtrace(void **buffer, int size) {
  if (!buffer || size <= 0) {
    return 0;
  }

  // Get current frame pointer and instruction pointer
  void **frame = (void **)__builtin_frame_address(0);
  int depth = 0;

  // Walk the stack using frame pointers
  while (frame && depth < size) {
    // frame[0] = previous frame pointer
    // frame[1] = return address (instruction pointer)
    void *return_addr = frame[1];

    // Sanity check - return address should be in code segment
    if (!return_addr || return_addr < (void *)0x1000) {
      break;
    }

    buffer[depth++] = return_addr;

    // Move to previous frame
    void **prev_frame = (void **)frame[0];

    // Sanity checks to avoid infinite loops
    if (!prev_frame || prev_frame <= frame || (uintptr_t)prev_frame & 0x7) {
      break;
    }

    frame = prev_frame;
  }

  return depth;
}

/**
 * @brief Safe wrapper for backtrace() with weak symbol check
 */
static inline int safe_backtrace(void **buffer, int size) {
#ifdef USE_MUSL
  if (backtrace != NULL) {
    return backtrace(buffer, size);
  }
  return 0;
#else
  return backtrace(buffer, size);
#endif
}

/**
 * @brief Get stack trace
 * @param buffer Array to store trace addresses
 * @param size Maximum number of addresses to retrieve
 * @return Number of addresses retrieved
 */
int platform_backtrace(void **buffer, int size) {
  // Try libexecinfo's backtrace first (safe with weak symbols)
  int depth = safe_backtrace(buffer, size);

  // If that fails, use manual stack walking
  if (depth == 0) {
    depth = manual_backtrace(buffer, size);
  }

  return depth;
}

/**
 * @brief Convert stack trace addresses to symbols
 * @param buffer Array of addresses from platform_backtrace
 * @param size Number of addresses in buffer
 * @return Array of strings with symbol names (must be freed)
 *
 * On POSIX, always uses the symbol cache for high-quality symbol resolution
 * with proper function names, file paths, and line numbers.  The cache
 * serializes addr2line calls to prevent concurrent popen() issues and caches
 * results for subsequent lookups.
 */
char **platform_backtrace_symbols(void *const *buffer, int size) {
  // Always use cached symbol resolution with addr2line for best quality
  return symbol_cache_resolve_batch(buffer, size);
}

/**
 * @brief Free memory from platform_backtrace_symbols
 * @param strings Array returned by platform_backtrace_symbols
 */
void platform_backtrace_symbols_free(char **strings) {
  if (!strings) {
    return;
  }

  // Always use symbol_cache_free_symbols to free the symbols.
  // This function safely handles NULL entries and the terminator correctly.
  // Since platform_backtrace_symbols always returns our format, we always call
  // symbol_cache_free_symbols.
  symbol_cache_free_symbols(strings);
}

// ============================================================================
// Crash Handling
// ============================================================================

/**
 * @brief Print pre-resolved backtrace symbols with consistent formatting
 *
 * Uses colored format for all backtraces:
 *   [0] crypto_handshake_server_complete() (lib/crypto/handshake.c:1471)
 *   [1] server_crypto_handshake() (src/server/crypto.c:511)
 *
 * @param label Header label (e.g., "Backtrace", "Call stack")
 * @param symbols Array of pre-resolved symbol strings
 * @param count Number of symbols in the array
 * @param skip_frames Number of frames to skip from the start
 * @param max_frames Maximum frames to print (0 = unlimited)
 * @param filter Optional filter callback to skip specific frames (NULL = no filtering)
 */
void platform_print_backtrace_symbols(const char *label, char **symbols, int count, int skip_frames, int max_frames,
                                      backtrace_frame_filter_t filter) {
  if (!symbols || count <= 0) {
    return;
  }

  // Calculate frame limits
  int start = skip_frames;
  int end = count;
  if (max_frames > 0 && (start + max_frames) < end) {
    end = start + max_frames;
  }

  // Build entire backtrace output in buffer for single logging statement
  char buffer[8192] = {0};
  int offset = 0;

  // Add header
  offset += snprintf(buffer + offset, sizeof(buffer) - (size_t)offset, "%s\n", label);

  // Build backtrace frames with colored frame numbers
  int frame_num = 0;
  for (int i = start; i < end && offset < (int)sizeof(buffer) - 256; i++) {
    const char *symbol = symbols[i] ? symbols[i] : "???";

    // Skip frame if filter says to
    if (filter && filter(symbol)) {
      continue;
    }

    // Build colored frame number string and manually embed it in buffer
    char frame_str[16];
    snprintf(frame_str, sizeof(frame_str), "%d", frame_num);
    const char *colored_frame = colored_string(LOG_COLOR_FATAL, frame_str);
    size_t colored_len = strlen(colored_frame);

    // Append "  ["
    offset += snprintf(buffer + offset, sizeof(buffer) - (size_t)offset, "  [");

    // Append colored frame number
    if (offset + colored_len < sizeof(buffer)) {
      memcpy(buffer + offset, colored_frame, colored_len);
      offset += (int)colored_len;
    }

    // Append "] symbol\n"
    offset += snprintf(buffer + offset, sizeof(buffer) - (size_t)offset, "] %s\n", symbol);
    frame_num++;
  }

  // Log entire backtrace in single statement using logging system
  log_plain_stderr("%s", buffer);
}

/**
 * @brief Format pre-resolved backtrace symbols to a buffer
 *
 * Same format as platform_print_backtrace_symbols() but writes to a buffer.
 *
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param label Header label (e.g., "Call stack")
 * @param symbols Array of pre-resolved symbol strings
 * @param count Number of symbols in the array
 * @param skip_frames Number of frames to skip from the start
 * @param max_frames Maximum frames to print (0 = unlimited)
 * @param filter Optional filter callback to skip specific frames (NULL = no filtering)
 * @return Number of bytes written (excluding null terminator)
 */
int platform_format_backtrace_symbols(char *buffer, size_t buffer_size, const char *label, char **symbols, int count,
                                      int skip_frames, int max_frames, backtrace_frame_filter_t filter) {
  if (!buffer || buffer_size == 0 || !symbols || count <= 0) {
    return 0;
  }

  int offset = 0;

  // Header with color
  offset += snprintf(buffer + offset, buffer_size - (size_t)offset, "  %s:\n", colored_string(LOG_COLOR_WARN, label));

  // Calculate frame limits
  int start = skip_frames;
  int end = count;
  if (max_frames > 0 && (start + max_frames) < end) {
    end = start + max_frames;
  }

  // Format backtrace frames with colored frame numbers
  int frame_num = 0;
  for (int i = start; i < end && offset < (int)buffer_size - 128; i++) {
    const char *symbol = symbols[i] ? symbols[i] : "???";

    // Skip frame if filter says to
    if (filter && filter(symbol)) {
      continue;
    }

    // Build colored frame number and manually embed it in buffer
    char frame_buf[16];
    snprintf(frame_buf, sizeof(frame_buf), "%d", frame_num++);
    const char *colored_frame = colored_string(LOG_COLOR_FATAL, frame_buf);
    size_t colored_len = strlen(colored_frame);

    // Append "    ["
    offset += snprintf(buffer + offset, buffer_size - (size_t)offset, "    [");

    // Append colored frame number
    if (offset + colored_len < (int)buffer_size) {
      memcpy(buffer + offset, colored_frame, colored_len);
      offset += (int)colored_len;
    }

    // Append "] symbol\n"
    offset += snprintf(buffer + offset, buffer_size - (size_t)offset, "] %s\n", symbol);
  }

  return offset;
}

/**
 * @brief Print backtrace of the current call stack
 *
 * Captures and prints a backtrace using platform_print_backtrace_symbols().
 */
void platform_print_backtrace(int skip_frames) {
  void *buffer[32];
  int size = platform_backtrace(buffer, 32);

  if (size > 0) {
    char **symbols = platform_backtrace_symbols(buffer, size);

    // Skip platform_print_backtrace itself (1 frame) + any additional frames requested
    platform_print_backtrace_symbols("\nBacktrace", symbols, size, 1 + skip_frames, 0, NULL);

    platform_backtrace_symbols_free(symbols);
  }
}

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
    platform_print_backtrace(0);
#else
    log_error("*** CRASH DETECTED ***\nSignal: %d (%s)\nSignal Info: si_code=%d, si_addr=%p\nBacktrace disabled in "
              "Release builds",
              sig, signal_name, info->si_code, info->si_addr);
#endif
  } else {
#ifndef NDEBUG
    // Only capture backtraces in Debug builds
    log_error("*** CRASH DETECTED ***\nSignal: %d (%s)", sig, signal_name);
    platform_print_backtrace(0);
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

  struct addrinfo hints, *result = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;       // IPv4 only
  hints.ai_socktype = SOCK_STREAM; // TCP
  hints.ai_flags = 0;
  hints.ai_protocol = 0;

  int ret = getaddrinfo(hostname, NULL, &hints, &result);
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
    // NOLINTNEXTLINE(clang-analyzer-security.ArrayBound) - file_size is bounded above
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
    return false;
  }

  if (getcwd(cwd, path_size) == NULL) {
    return false;
  }

  return true;
}

int platform_access(const char *path, int mode) {
  if (!path) {
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
  handle.devnull_fd = platform_open("/dev/null", O_WRONLY, 0);
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
  int dev_null = platform_open("/dev/null", O_WRONLY, 0);
  if (dev_null >= 0) {
    dup2(dev_null, STDERR_FILENO);
    dup2(dev_null, STDOUT_FILENO);
    close(dev_null);
  }
}

// Include cross-platform system utilities (binary PATH detection)
#include "../system.c"

#endif // !_WIN32
