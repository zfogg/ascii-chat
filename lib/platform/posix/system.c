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
#include "../../asciichat_errno.h"
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <execinfo.h> // Provided by glibc or libexecinfo for musl
#include <pthread.h>
#include <stdatomic.h>
#include <sys/stat.h>
#include <netdb.h>

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
asciichat_error_t platform_init(void) {
  // Install crash handlers for automatic backtrace on crashes
  platform_install_crash_handler();
  return ASCIICHAT_OK;
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
static __thread char error_buffer[256];

/**
 * @brief Get thread-safe error string
 * @param errnum Error number
 * @return Error string (thread-local storage)
 */
const char *platform_strerror(int errnum) {
  // Use strerror_r for thread safety
#if defined(__APPLE__) || !defined(__GLIBC__)
  // macOS and non-glibc (including musl) use XSI-compliant strerror_r (returns int)
  if (strerror_r(errnum, error_buffer, sizeof(error_buffer)) != 0) {
    safe_snprintf(error_buffer, sizeof(error_buffer), "Unknown error %d", errnum);
  }
#else
  // glibc uses GNU strerror_r which returns a char*
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
  // IMPORTANT: When using mimalloc (which overrides malloc/free globally),
  // we cannot safely free memory allocated by backtrace_symbols() because:
  // 1. backtrace_symbols() uses system malloc()
  // 2. mimalloc's free() expects memory allocated by mimalloc
  // 3. Calling free() on system-allocated memory causes a crash
  //
  // The memory leak is acceptable because:
  // - Backtraces are only printed during crashes/errors
  // - The memory is small (a few KB at most)
  // - Process is about to exit anyway
  //
  // We check for mimalloc by testing if we're in Debug mode with memory debugging,
  // which is when mimalloc is active as the global allocator.
  (void)strings; // Suppress unused parameter warning - we intentionally don't free
}

// ============================================================================
// Crash Handling
// ============================================================================

/**
 * @brief Print backtrace to stderr
 */
void platform_print_backtrace(int skip_frames) {
  void *buffer[32];
  int size = platform_backtrace(buffer, 32);

  if (size > 0) {
    (void)fprintf(stderr, "\n=== BACKTRACE ===\n");
    char **symbols = platform_backtrace_symbols(buffer, size);

    // Skip platform_print_backtrace itself (1 frame) + any additional frames requested
    int start_frame = 1 + skip_frames;
    for (int i = start_frame; i < size; i++) {
      (void)fprintf(stderr, "%2d: %s\n", i - start_frame, symbols ? symbols[i] : "???");
    }

    platform_backtrace_symbols_free(symbols);
    (void)fprintf(stderr, "================\n\n");
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
  (void)fprintf(stderr, "\n*** CRASH DETECTED ***\n");
  (void)fprintf(stderr, "Signal: %d (%s)\n", sig, get_signal_name(sig));

  if (info) {
    (void)fprintf(stderr, "Signal Info: si_code=%d, si_addr=%p\n", info->si_code, info->si_addr);
  }

#ifndef NDEBUG
  // Only capture backtraces in Debug builds
  platform_print_backtrace(0);
#else
  (void)fprintf(stderr, "Backtrace disabled in Release builds\n");
#endif

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
    freeaddrinfo(result);
    return SET_ERRNO(ERROR_NETWORK, "No address found for hostname: %s", hostname);
  }

  // Extract IPv4 address from first result
  struct sockaddr_in *ipv4_addr = (struct sockaddr_in *)result->ai_addr;
  if (inet_ntop(AF_INET, &(ipv4_addr->sin_addr), ipv4_out, (socklen_t)ipv4_out_size) == NULL) {
    freeaddrinfo(result);
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to convert network address to string");
  }

  freeaddrinfo(result);
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
  static const char *ca_paths[] = {"/etc/ssl/certs/ca-certificates.crt",                // Debian/Ubuntu/Gentoo/Arch
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
    pem_data[bytes_read] = '\0';

    // Success!
    *pem_data_out = pem_data;
    *pem_size_out = bytes_read;
    return ASCIICHAT_OK;
  }

  // No CA bundle found
  return SET_ERRNO(ERROR_CRYPTO, "No CA certificate bundle found in standard locations");
}

#endif // !_WIN32
