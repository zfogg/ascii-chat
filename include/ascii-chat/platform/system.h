#pragma once

/**
 * @file platform/system.h
 * @brief Cross-platform system functions interface for ascii-chat
 * @ingroup platform_system
 * @addtogroup platform
 * @{
 *
 * This header provides unified system functions including process management,
 * environment variables, TTY operations, and signal handling.
 *
 * The interface provides:
 * - Platform initialization and cleanup
 * - Time and sleep functions
 * - Process information (PID, username)
 * - Signal handling
 * - Environment variable operations
 * - TTY/terminal operations
 * - Stack trace and crash handling
 * - Safe string and memory functions
 * - Network utilities (DNS resolution, CA certificates)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <time.h>
#ifdef _WIN32
#include <basetsd.h>
#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif
#endif
#include "../common.h"
#include "process.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signal handler function type
 * @param sig Signal number
 *
 * @ingroup platform_system
 */
typedef void (*signal_handler_t)(int);

/**
 * @brief Install crash handlers for the application
 *
 * Installs signal handlers for common crash signals (SIGSEGV, SIGABRT, etc.)
 * that will print a backtrace before terminating the process.
 *
 * @note This should be called early in program initialization.
 *
 * @ingroup debug_util
 */
void platform_install_crash_handler(void);

// ============================================================================
// System Functions
// ============================================================================

/**
 * @brief Initialize platform-specific subsystems
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes platform-specific subsystems such as Winsock on Windows.
 * Must be called before using any platform-specific functions.
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_init(void);

/**
 * @brief Cleanup platform-specific subsystems
 *
 * Performs cleanup for platform-specific subsystems.
 * Should be called during program shutdown.
 *
 * @ingroup platform_system
 */
void platform_destroy(void);

/**
 * @brief Forcefully terminate the process immediately without cleanup
 * @param exit_code Exit code to return to the operating system
 *
 * Forcefully terminates the current process immediately without running atexit handlers
 * or cleanup code. Used when normal exit() won't suffice (e.g., handling
 * second Ctrl+C during shutdown).
 *
 * Platform-specific implementations:
 *   - Windows: ExitProcess(exit_code)
 *   - POSIX: _exit(exit_code)
 *
 * @note This function does not return. Process is terminated immediately.
 * @note Use this sparingly - prefer normal exit() whenever possible.
 *
 * @ingroup platform_system
 */
void platform_force_exit(int exit_code);

/**
 * @brief Sleep for a specified number of milliseconds
 * @param ms Number of milliseconds to sleep
 *
 * Sleeps the current thread for the specified duration.
 *
 * @ingroup platform_system
 */
void platform_sleep_ms(unsigned int ms);

/**
 * @brief Get monotonic time in microseconds
 * @return Current monotonic time in microseconds
 *
 * Returns a monotonically increasing time value in microseconds.
 * Useful for measuring elapsed time without being affected by
 * system clock changes. Thread-safe and lock-free.
 *
 * Platform-specific implementations:
 *   - Unix/POSIX: Uses CLOCK_MONOTONIC via clock_gettime()
 *   - Windows: Uses QueryPerformanceCounter()
 *
 * @note The absolute value is arbitrary; only differences are meaningful.
 * @note Wraps after approximately 584,942 years (uint64_t).
 *
 * @ingroup platform_system
 */
uint64_t platform_get_monotonic_time_us(void);

/**
 * @brief Request coarse (reduced resolution) timer precision
 *
 * Requests the OS to use coarse timer precision, reducing power consumption
 * and improving system responsiveness at the cost of lower timer accuracy.
 *
 * Platform-specific implementations:
 *   - Windows: Calls timeBeginPeriod(1) to reduce timer resolution from ~15ms to ~1ms
 *   - POSIX: No-op (POSIX systems don't provide this feature)
 *
 * This is typically called during application startup if you need timer precision
 * (e.g., for real-time audio/video). Must be balanced with platform_restore_timer_resolution().
 *
 * @param precision Desired timer precision in milliseconds (typically 1)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note On Windows, this increases power consumption slightly
 * @note POSIX systems return ASCIICHAT_OK but do nothing
 * @note Must call platform_restore_timer_resolution() to restore default behavior
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_request_timer_precision(int precision);

/**
 * @brief Restore default timer precision
 *
 * Restores the default system timer precision, undoing a previous call to
 * platform_request_timer_precision(). Reduces power consumption.
 *
 * Platform-specific implementations:
 *   - Windows: Calls timeEndPeriod(1) to restore default timer resolution
 *   - POSIX: No-op (POSIX systems don't provide this feature)
 *
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note Safe to call without a matching platform_request_timer_precision() call
 * @note POSIX systems return ASCIICHAT_OK but do nothing
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_restore_timer_resolution(void);

/**
 * @brief Enable system sleep prevention (keepawake mode)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Prevents the operating system from entering sleep mode while the application is running.
 *
 * Platform-specific implementations:
 *   - macOS: Uses IOKit power assertions (IOPMAssertionCreateWithName)
 *   - Linux: Uses systemd-inhibit if available, gracefully degrades if unavailable
 *   - Windows: Uses SetThreadExecutionState with ES_SYSTEM_REQUIRED and ES_DISPLAY_REQUIRED
 *
 * @note Logs warning and returns OK if platform doesn't support keepawake
 * @note Safe to call multiple times (checks for already-enabled state)
 * @note Call platform_disable_keepawake() to revert
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_enable_keepawake(void);

/**
 * @brief Disable system sleep prevention (allow OS to sleep)
 *
 * Allows the operating system to enter sleep mode. This reverts the effect
 * of platform_enable_keepawake().
 *
 * Platform-specific implementations:
 *   - macOS: Releases the IOKit power assertion
 *   - Linux: Closes the systemd-inhibit file descriptor
 *   - Windows: Clears all execution state flags
 *
 * @note Safe to call even if keepawake was never enabled
 * @note Safe to call multiple times
 *
 * @ingroup platform_system
 */
void platform_disable_keepawake(void);

#ifdef _WIN32
// usleep macro - only define if not already declared (GCC's unistd.h declares it as a function)
// On Windows with GCC/Clang, usleep may be available via unistd.h, so we check for that
// If it's not available, we provide our macro wrapper
#ifndef _WIN32_USLEEP_AVAILABLE
// Check if usleep is declared (GCC's unistd.h provides it)
#if defined(__GNUC__) && !defined(__clang__)
// INFO: GCC on Windows: unistd.h declares usleep, so don't redefine it
// Code should use usleep() directly or platform_sleep_us() directly
#else
// MSVC/Clang on Windows: Define usleep macro to use platform_sleep_us
#define usleep(usec) platform_sleep_us(usec)
#endif
// sleep() in seconds mapped to platform_sleep_us() in microseconds
#ifndef sleep
#define sleep(sec) platform_sleep_us((sec) * 1000000U)
#endif
#endif
#endif

/**
 * @brief Platform-safe localtime wrapper
 *
 * Uses localtime_s on Windows and localtime_r on POSIX.
 * Thread-safe on all platforms.
 *
 * @param timer Pointer to time_t value
 * @param result Pointer to struct tm to receive result
 * @return ASCIICHAT_OK on success, error code on error
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_localtime(const time_t *timer, struct tm *result);

/**
 * @brief Platform-safe gmtime wrapper
 *
 * Uses gmtime_s on Windows and gmtime_r on POSIX.
 * Thread-safe on all platforms.
 *
 * @param timer Pointer to time_t value
 * @param result Pointer to struct tm to receive result
 * @return ASCIICHAT_OK on success, error code on error
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_gtime(const time_t *timer, struct tm *result);

/**
 * @brief Get the current username
 * @return Pointer to username string (may be static, do not free)
 *
 * Returns the username of the current user.
 *
 * @note The returned string may be a static buffer. Do not modify or free it.
 *
 * @ingroup platform_system
 */
const char *platform_get_username(void);

/**
 * @brief Set a signal handler
 * @param sig Signal number (e.g., SIGINT, SIGTERM)
 * @param handler Signal handler function (or SIG_DFL, SIG_IGN)
 * @return Previous signal handler, or SIG_ERR on error
 *
 * Registers a signal handler for the specified signal.
 *
 * @ingroup platform_system
 */
signal_handler_t platform_signal(int sig, signal_handler_t handler);

/**
 * @brief Signal handler descriptor for bulk registration
 * @ingroup platform_system
 */
typedef struct {
  int sig;                  /**< Signal number to handle */
  signal_handler_t handler; /**< Handler function */
} platform_signal_handler_t;

/**
 * @brief Register multiple signal handlers at once
 * @param handlers Array of signal handler descriptors
 * @param count Number of handlers in the array
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Registers multiple signal handlers in one call, reducing repeated
 * #ifdef _WIN32 blocks. On Windows, this handles SIGINT and SIGTERM
 * via console control handlers. On POSIX, uses platform_signal() for each.
 *
 * @par Example:
 * @code{.c}
 * platform_signal_handler_t handlers[] = {
 *     {SIGTERM, sigterm_handler},
 *     {SIGINT, sigint_handler},
 * };
 * platform_register_signal_handlers(handlers, 2);
 * @endcode
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_register_signal_handlers(const platform_signal_handler_t *handlers, int count);

/**
 * @brief Get an environment variable value
 * @param name Environment variable name
 * @return Pointer to value string (or NULL if not set), do not free
 *
 * Returns the value of the specified environment variable.
 *
 * @note The returned string may be a static buffer. Do not modify or free it.
 *
 * @ingroup platform_system
 */
const char *platform_getenv(const char *name);

/**
 * @brief Set an environment variable
 * @param name Environment variable name
 * @param value Environment variable value (or NULL to unset)
 * @return 0 on success, non-zero on error
 *
 * Sets or unsets an environment variable.
 *
 * @ingroup platform_system
 */
int platform_setenv(const char *name, const char *value);

/**
 * @brief Remove an environment variable
 * @param name Variable name to unset
 * @return 0 on success, -1 on failure
 * @ingroup platform_system
 */
int platform_unsetenv(const char *name);

/**
 * @brief Raise the file descriptor limit for the current process
 * @param limit Desired soft limit (0 = use platform default maximum)
 * @ingroup platform_system
 */
void platform_raise_fd_limit(unsigned int limit);

// ============================================================================
// Stream Redirection
// ============================================================================

/**
 * @brief Handle for temporary stderr redirection
 *
 * Opaque handle returned by platform_stderr_redirect_to_null() that can be used
 * to restore stderr to its original destination.
 *
 * @ingroup platform_system
 */
typedef struct {
  int original_fd; /**< Original stderr file descriptor (-1 if not redirected) */
  int devnull_fd;  /**< /dev/null file descriptor (-1 if not opened) */
} platform_stderr_redirect_handle_t;

/**
 * @brief Redirect stderr to /dev/null temporarily
 *
 * This is useful for suppressing noisy warnings from third-party libraries
 * (e.g., PortAudio backend probes) that are harmless but clutter output.
 *
 * @return Handle for restoring stderr, or {-1, -1} on failure
 *
 * @note On Windows, this function returns {-1, -1} and has no effect
 * @note You must call platform_stderr_restore() with the returned handle to restore stderr
 *
 * Example:
 * @code
 * platform_stderr_redirect_handle_t handle = platform_stderr_redirect_to_null();
 * noisy_third_party_function(); // stderr output suppressed
 * platform_stderr_restore(handle);        // stderr restored
 * @endcode
 *
 * @ingroup platform_system
 */
platform_stderr_redirect_handle_t platform_stderr_redirect_to_null(void);

/**
 * @brief Restore stderr from a redirect handle
 *
 * Restores stderr to its original destination and closes the /dev/null file descriptor.
 *
 * @param handle Handle returned by platform_stderr_redirect_to_null()
 *
 * @note Safe to call with invalid handle (e.g., {-1, -1}) - will do nothing
 * @note After calling this, the handle is invalidated and should not be reused
 *
 * @ingroup platform_system
 */
void platform_stderr_restore(platform_stderr_redirect_handle_t handle);

/**
 * @brief Redirect both stdout and stderr to /dev/null (restorable)
 *
 * Suppresses output from both stdout and stderr. This is useful when initializing
 * libraries that may output diagnostic messages that would corrupt terminal rendering.
 *
 * @return Handle to pass to platform_stdout_stderr_restore() to restore streams
 *
 * @note Always call platform_stdout_stderr_restore() with the returned handle
 * @note The returned handle uses original_fd for stdout, devnull_fd for stderr
 *
 * @ingroup platform_system
 */
platform_stderr_redirect_handle_t platform_stdout_stderr_redirect_to_null(void);

/**
 * @brief Restore stdout and stderr after platform_stdout_stderr_redirect_to_null()
 *
 * Restores both stdout and stderr to their original destinations.
 *
 * @param handle Handle returned by platform_stdout_stderr_redirect_to_null()
 *
 * @note Safe to call with invalid handle - will do nothing
 *
 * @ingroup platform_system
 */
void platform_stdout_stderr_restore(platform_stderr_redirect_handle_t handle);

/**
 * @brief Permanently redirect stderr and stdout to /dev/null
 *
 * This is used before exit() to prevent cleanup handlers from writing to the console
 * after we've already displayed final messages to the user.
 *
 * @note On Windows, this function has no effect
 * @note This is a one-way operation - streams cannot be restored
 *
 * @ingroup platform_system
 */
void platform_stdio_redirect_to_null_permanent(void);

/**
 * @brief Install crash handlers for the application
 *
 * Installs signal handlers for common crash signals (SIGSEGV, SIGABRT, etc.)
 * that will print a backtrace before terminating the process.
 *
 * @note This should be called early in program initialization.
 *
 * @ingroup platform_system
 */
void platform_install_crash_handler(void);

// ============================================================================
// Safe String Functions
// ============================================================================

/**
 * @brief Platform-safe snprintf wrapper
 *
 * Uses snprintf_s on Windows and snprintf with additional safety on POSIX.
 * Always null-terminates the output buffer.
 *
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of characters written (excluding null terminator) or negative on error
 *
 * @ingroup platform_system
 */
int safe_snprintf(char *buffer, size_t buffer_size, const char *format, ...);

/**
 * @brief Platform-safe fprintf wrapper
 *
 * Uses fprintf_s on Windows and fprintf on POSIX.
 *
 * @param stream File stream
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of characters written or negative on error
 *
 * @ingroup platform_system
 */
int safe_fprintf(FILE *stream, const char *format, ...);

/**
 * @brief Platform-safe vsnprintf wrapper
 *
 * Uses the appropriate vsnprintf implementation for the platform.
 * Safely formats a string with va_list argument.
 *
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param format Format string
 * @param ap Variable argument list
 * @return Number of characters written (excluding null terminator) or negative on error
 *
 * @ingroup platform_system
 */
int safe_vsnprintf(char *buffer, size_t buffer_size, const char *format, va_list ap);

/**
 * Check return value of snprintf/fprintf and cast to void if needed
 *
 * This macro satisfies clang-tidy cert-err33-c by explicitly handling
 * the return value of printf family functions.
 */
#define SAFE_IGNORE_PRINTF_RESULT(expr) ((void)(expr))

// ============================================================================
// Safe Memory Functions
// ============================================================================

/**
 * @brief Platform-safe memcpy wrapper
 *
 * Uses memcpy_s on Windows when available (C11) and memcpy with bounds checking on POSIX.
 * Provides consistent interface across platforms.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source buffer
 * @param count Number of bytes to copy
 * @return ASCIICHAT_OK on success, error code on error
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_memcpy(void *dest, size_t dest_size, const void *src, size_t count);

/**
 * @brief Platform-safe memset wrapper
 *
 * Uses memset_s on Windows when available (C11) and memset with bounds checking on POSIX.
 * Provides consistent interface across platforms.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param ch Value to set (cast to unsigned char)
 * @param count Number of bytes to set
 * @return ASCIICHAT_OK on success, error code on error
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_memset(void *dest, size_t dest_size, int ch, size_t count);

/**
 * @brief Platform-safe memmove wrapper
 *
 * Uses memmove_s on Windows when available (C11) and memmove with bounds checking on POSIX.
 * Handles overlapping memory regions safely.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source buffer
 * @param count Number of bytes to move
 * @return ASCIICHAT_OK on success, error code on error
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_memmove(void *dest, size_t dest_size, const void *src, size_t count);

/**
 * @brief Platform-safe strcpy wrapper
 *
 * Uses strcpy_s on Windows when available (C11) and strncpy with bounds checking on POSIX.
 * Always null-terminates the destination string.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source string
 * @return ASCIICHAT_OK on success, error code on error
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_strcpy(char *dest, size_t dest_size, const char *src);

/**
 * @brief Get the last system error code
 *
 * Returns the last error code from the operating system.
 * On Windows: Returns GetLastError()
 * On POSIX: Returns errno
 *
 * @return System error code (Windows DWORD cast to int, or errno on POSIX)
 *
 * @ingroup platform_system
 */
int platform_get_last_error(void);

/**
 * @brief Get human-readable error message for a system error code
 *
 * Converts a system error code into a readable error message string.
 * Uses strerror_r on POSIX and FormatMessageA on Windows.
 *
 * @param errnum System error code
 * @return Pointer to error message string (may be static, do not modify or free)
 *
 * @note The returned string may be a static buffer. Do not modify or free it.
 * @note On POSIX, uses strerror_r with thread-local storage
 * @note On Windows, uses FormatMessageA with proper cleanup
 *
 * @ingroup platform_system
 */
const char *platform_strerror(int errnum);

/**
 * @brief Resolve hostname to IPv4 address
 *
 * Performs DNS resolution to convert a hostname to an IPv4 address string.
 * Handles platform-specific networking initialization and cleanup.
 *
 * @param hostname Hostname to resolve (e.g., "example.com")
 * @param ipv4_out Buffer to store the resolved IPv4 address (e.g., "192.168.1.1")
 * @param ipv4_out_size Size of the output buffer
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_resolve_hostname_to_ipv4(const char *hostname, char *ipv4_out, size_t ipv4_out_size);

/**
 * @brief Load system CA certificates for TLS/HTTPS
 *
 * Loads the operating system's trusted root CA certificates in PEM format.
 * This allows TLS connections to trust the same CAs that the OS trusts.
 *
 * Platform-specific paths:
 *   - Linux (Debian/Ubuntu): /etc/ssl/certs/ca-certificates.crt
 *   - Linux (RHEL/CentOS): /etc/pki/tls/certs/ca-bundle.crt
 *   - macOS: /etc/ssl/cert.pem or Security framework
 *   - Windows: Uses CryptoAPI certificate store
 *
 * @param pem_data_out Pointer to receive allocated PEM data (caller must free)
 * @param pem_size_out Pointer to receive size of PEM data
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note The caller must free the allocated PEM data with SAFE_FREE() or ALLOC_FREE().
 *
 * @par Example:
 * @code{.c}
 * char* pem_data;
 * size_t pem_size;
 * if (platform_load_system_ca_certs(&pem_data, &pem_size) == ASCIICHAT_OK) {
 *     // Use pem_data for TLS verification
 *     SAFE_FREE(pem_data);
 * }
 * @endcode
 *
 * @ingroup platform_system
 */
asciichat_error_t platform_load_system_ca_certs(char **pem_data_out, size_t *pem_size_out);

// ============================================================================
// Subprocess Execution
// ============================================================================

/**
 * @brief Execute a subprocess and optionally capture its output
 * @param executable Path to executable to run (e.g., "gpg", "/usr/bin/yt-dlp")
 * @param argv NULL-terminated array of arguments (argv[0] should be executable name or path)
 * @param output_buffer Optional buffer to store captured stdout (NULL to skip output capture)
 * @param output_size Size of output buffer (ignored if output_buffer is NULL)
 * @return Exit code of the process, or -1 on error
 *
 * Executes a subprocess using platform-specific mechanisms:
 *   - POSIX: fork() + execvp() (no output), or popen() (with output)
 *   - Windows: CreateProcess() (no output), or piped output capture (with output)
 *
 * If output_buffer is NULL or output_size is 0, stdout and stderr are inherited
 * from the parent process. Otherwise, stdout is captured into output_buffer.
 *
 * Usage examples:
 * @code
 * // No output capture
 * const char *argv[] = {"gpg", "--version", NULL};
 * int exit_code = platform_execute_subprocess("gpg", argv, NULL, 0);
 *
 * // With output capture
 * char output[1024];
 * const char *argv[] = {"gpg", "--list-secret-keys", "--with-colons", NULL};
 * int exit_code = platform_execute_subprocess("gpg", argv, output, sizeof(output));
 * if (exit_code == 0) {
 *     // Parse output (null-terminated)
 * }
 * @endcode
 *
 * @note When output_buffer is NULL: stdout and stderr inherited, fast fork+exec
 * @note When output_buffer is provided: stdout captured, stderr inherited
 * @note Output is always null-terminated when captured
 * @note Returns -1 if output_buffer is provided but too small for output
 * @note More secure than system() as it doesn't invoke a shell
 *
 * @ingroup platform_system
 */
int platform_execute_subprocess(const char *executable, const char **argv,
                                char *output_buffer, size_t output_size);

// ============================================================================
// I/O Functions
// ============================================================================

/**
 * @brief Write all bytes to a file descriptor, handling partial writes
 * @param fd File descriptor to write to
 * @param buf Buffer containing data to write
 * @param count Number of bytes to write
 * @return Number of bytes successfully written (may be less than count on retry limit)
 *
 * Handles partial writes and EAGAIN errors, retrying up to 1000 times before giving up.
 * This ensures that data is fully written even when dealing with non-blocking I/O or
 * interrupted syscalls.
 *
 * @note On EAGAIN/EWOULDBLOCK, sleeps 100us before retrying instead of busy-waiting
 * @note Returns early on fatal write errors (logs warning but continues retrying)
 * @note If NULL buffer or 0 count is passed, returns 0 immediately
 *
 * @ingroup platform_system
 */
size_t platform_write_all(int fd, const void *buf, size_t count);

/**
 * @brief Platform-safe write function
 * @param fd File descriptor to write to
 * @param buf Buffer containing data to write
 * @param count Number of bytes to write
 * @return Number of bytes written on success, -1 on error
 *
 * Cross-platform write function that handles Windows-specific quirks
 * (e.g., CRLF line endings) and provides consistent behavior across platforms.
 *
 * @note On Windows, automatically handles line ending conversion if needed.
 * @note On POSIX, equivalent to standard write().
 *
 * @ingroup platform_system
 */
ssize_t platform_write(int fd, const void *buf, size_t count);

/**
 * @brief Find which binary(ies) contain a given address
 *
 * Platform-independent interface to scan the runtime memory map and find
 * which loaded binary owns a given address. Used by backtrace symbol resolution
 * to map runtime addresses to binaries, which are then passed to llvm-symbolizer
 * or addr2line for function name lookup.
 *
 * **Implementations:**
 * - Linux: Scans `/proc/self/maps` to find executable segments
 * - macOS: Iterates dyld-loaded images
 * - Windows: Enumerates process modules via EnumProcessModules()
 *
 * @param addr Runtime address to look up
 * @param matches Output array to populate with matches (can be NULL)
 * @param max_matches Maximum number of matches to return
 * @return Number of matches found (0, 1, or rarely 2)
 *
 * @note Multiple matches are possible but rare (would indicate overlapping
 *       mapped regions, a configuration error).
 */
typedef struct {
  /** Path to the binary (exe, .so, dylib, DLL, etc.) */
  char path[PLATFORM_MAX_PATH_LENGTH];
  /** File offset within the binary (for symbolizer) */
  uintptr_t file_offset;
} platform_binary_match_t;

/**
 * @par Example:
 * @code{.c}
 * platform_binary_match_t matches[2];
 * int count = get_binary_file_address_offsets(backtrace_address, matches, 2);
 * for (int i = 0; i < count; i++) {
 *     // Call llvm-symbolizer with matches[i].path and matches[i].file_offset
 *     printf("Address %p is in %s at offset %lx\n",
 *            backtrace_address, matches[i].path, matches[i].file_offset);
 * }
 * @endcode
 *
 * @ingroup platform_system
 */
int get_binary_file_address_offsets(const void *addr, platform_binary_match_t *matches, int max_matches);

#ifdef __cplusplus
}
#endif

/** @} */
