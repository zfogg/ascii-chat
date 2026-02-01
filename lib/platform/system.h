#pragma once

/**
 * @file platform/system.h
 * @brief Cross-platform system functions interface for ascii-chat
 * @ingroup platform
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
 * - Binary path checking
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <time.h>
#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signal handler function type
 * @param sig Signal number
 *
 * @ingroup platform
 */
typedef void (*signal_handler_t)(int);

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
 * @ingroup platform
 */
asciichat_error_t platform_init(void);

/**
 * @brief Cleanup platform-specific subsystems
 *
 * Performs cleanup for platform-specific subsystems.
 * Should be called during program shutdown.
 *
 * @ingroup platform
 */
void platform_cleanup(void);

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
 * @ingroup platform
 */
void platform_force_exit(int exit_code);

/**
 * @brief Sleep for a specified number of milliseconds
 * @param ms Number of milliseconds to sleep
 *
 * Sleeps the current thread for the specified duration.
 *
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
// Code should use usleep() directly or platform_sleep_usec() directly
#else
// MSVC/Clang on Windows: Define usleep macro to use platform_sleep_usec
#define usleep(usec) platform_sleep_usec(usec)
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
 * @ingroup platform
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
 * @ingroup platform
 */
asciichat_error_t platform_gtime(const time_t *timer, struct tm *result);

/**
 * @brief Get the current process ID
 * @return Process ID of the calling process
 *
 * @ingroup platform
 */
int platform_get_pid(void);

/**
 * @brief Get the current username
 * @return Pointer to username string (may be static, do not free)
 *
 * Returns the username of the current user.
 *
 * @note The returned string may be a static buffer. Do not modify or free it.
 *
 * @ingroup platform
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
 * @ingroup platform
 */
signal_handler_t platform_signal(int sig, signal_handler_t handler);

/**
 * @brief Signal handler descriptor for bulk registration
 * @ingroup platform
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
 * @ingroup platform
 */
asciichat_error_t platform_register_signal_handlers(const platform_signal_handler_t *handlers, int count);

/**
 * @brief Console control event types (cross-platform Ctrl+C handling)
 *
 * @ingroup platform
 */
typedef enum {
  CONSOLE_CTRL_C = 0,     /**< Ctrl+C pressed (SIGINT equivalent) */
  CONSOLE_CTRL_BREAK = 1, /**< Ctrl+Break pressed (Windows only, maps to SIGINT on Unix) */
  CONSOLE_CLOSE = 2,      /**< Console window closed */
  CONSOLE_LOGOFF = 3,     /**< User logoff event (Windows only) */
  CONSOLE_SHUTDOWN = 4    /**< System shutdown event (Windows only) */
} console_ctrl_event_t;

/**
 * @brief Console control handler callback type
 * @param event The control event that occurred
 * @return true if the event was handled, false to pass to next handler
 *
 * @note On Windows, this is called from a separate thread, not from signal context
 * @note On Unix, this is called from signal context (limited safe operations)
 *
 * @ingroup platform
 */
typedef bool (*console_ctrl_handler_t)(console_ctrl_event_t event);

/**
 * @brief Register a console control handler (for Ctrl+C, etc.)
 * @param handler Handler function to register, or NULL to unregister
 * @return true on success, false on failure
 *
 * This provides cross-platform handling for console control events like Ctrl+C.
 * - On Windows: Uses SetConsoleCtrlHandler() for proper signal handling
 * - On Unix: Uses sigaction() for SIGINT/SIGTERM handling
 *
 * Unlike platform_signal() which uses CRT signal() on Windows (known issues),
 * this function uses the native Windows API for reliable Ctrl+C handling.
 *
 * @note Only one handler is supported at a time. Registering a new handler
 *       replaces the previous one.
 *
 * @ingroup platform
 */
bool platform_set_console_ctrl_handler(console_ctrl_handler_t handler);

/**
 * @brief Get an environment variable value
 * @param name Environment variable name
 * @return Pointer to value string (or NULL if not set), do not free
 *
 * Returns the value of the specified environment variable.
 *
 * @note The returned string may be a static buffer. Do not modify or free it.
 *
 * @ingroup platform
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
 * @ingroup platform
 */
int platform_setenv(const char *name, const char *value);

/**
 * @brief Check if a file descriptor is a terminal
 * @param fd File descriptor to check
 * @return Non-zero if fd is a terminal, 0 otherwise
 *
 * @ingroup platform
 */
int platform_isatty(int fd);

/**
 * @brief Get the name of the terminal associated with a file descriptor
 * @param fd File descriptor
 * @return Pointer to terminal name string (or NULL), may be static, do not free
 *
 * Returns the name of the terminal device associated with the file descriptor.
 *
 * @note The returned string may be a static buffer. Do not modify or free it.
 *
 * @ingroup platform
 */
const char *platform_ttyname(int fd);

/**
 * @brief Synchronize a file descriptor to disk
 * @param fd File descriptor to sync
 * @return 0 on success, non-zero on error
 *
 * Forces all buffered data for the file descriptor to be written to disk.
 *
 * @ingroup platform
 */
int platform_fsync(int fd);

// ============================================================================
// Stream Redirection
// ============================================================================

/**
 * @brief Handle for temporary stderr redirection
 *
 * Opaque handle returned by platform_stderr_redirect_to_null() that can be used
 * to restore stderr to its original destination.
 *
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
 */
void platform_stdio_redirect_to_null_permanent(void);

/**
 * @brief Get a backtrace of the current call stack
 * @param buffer Array of pointers to store return addresses
 * @param size Maximum number of frames to capture
 * @return Number of frames captured
 *
 * Captures the current call stack into the provided buffer.
 * Returns the number of frames actually captured.
 *
 * @ingroup platform
 */
int platform_backtrace(void **buffer, int size);

/**
 * @brief Convert backtrace addresses to symbol names
 * @param buffer Array of return addresses from platform_backtrace()
 * @param size Number of frames in buffer
 * @return Array of symbol name strings, or NULL on error
 *
 * Converts the return addresses from platform_backtrace() into
 * human-readable symbol names (function names, file names, line numbers).
 *
 * @note The returned array must be freed with platform_backtrace_symbols_free().
 *
 * @ingroup platform
 */
char **platform_backtrace_symbols(void *const *buffer, int size);

/**
 * @brief Free symbol array returned by platform_backtrace_symbols()
 * @param strings Array of symbol strings to free
 *
 * Frees the memory allocated by platform_backtrace_symbols().
 *
 * @ingroup platform
 */
void platform_backtrace_symbols_free(char **strings);

/**
 * @brief Install crash handlers for the application
 *
 * Installs signal handlers for common crash signals (SIGSEGV, SIGABRT, etc.)
 * that will print a backtrace before terminating the process.
 *
 * @note This should be called early in program initialization.
 *
 * @ingroup platform
 */
void platform_install_crash_handler(void);

/**
 * @brief Callback type for filtering backtrace frames
 * @param frame The frame string to check
 * @return true if the frame should be skipped, false to include it
 *
 * @ingroup platform
 */
typedef bool (*backtrace_frame_filter_t)(const char *frame);

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
 *
 * @ingroup platform
 */
void platform_print_backtrace_symbols(const char *label, char **symbols, int count, int skip_frames, int max_frames,
                                      backtrace_frame_filter_t filter);

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
 *
 * @ingroup platform
 */
int platform_format_backtrace_symbols(char *buffer, size_t buffer_size, const char *label, char **symbols, int count,
                                      int skip_frames, int max_frames, backtrace_frame_filter_t filter);

/**
 * @brief Print a backtrace of the current call stack
 * @param skip_frames Number of frames to skip from the top
 *
 * Captures a backtrace and prints it using platform_print_backtrace_symbols().
 * Useful for debugging crashes or errors.
 *
 * @ingroup platform
 */
void platform_print_backtrace(int skip_frames);

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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
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
 * @ingroup platform
 */
asciichat_error_t platform_strcpy(char *dest, size_t dest_size, const char *src);

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
 * @ingroup platform
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
 * @ingroup platform
 */
asciichat_error_t platform_load_system_ca_certs(char **pem_data_out, size_t *pem_size_out);

/**
 * @brief Check if a binary is available in the system PATH
 *
 * This function checks if the specified binary can be found in the PATH
 * by searching each directory in the PATH environment variable.
 * Results are cached to avoid repeated filesystem checks.
 *
 * On Windows: Automatically appends .exe if needed, checks with GetFileAttributesA
 * On Unix: Uses access() with X_OK to verify executable permission
 *
 * @param bin_name Base name of the binary (e.g., "ssh-keygen", "llvm-symbolizer")
 *                 On Windows, .exe extension is added automatically if not present
 * @return true if binary is in PATH and executable, false otherwise
 *
 * @note Thread-safe: Uses internal locking for cache access
 * @note First call for a binary checks filesystem, subsequent calls use cache
 * @note No external dependencies (doesn't spawn where/command -v)
 *
 * @par Example:
 * @code{.c}
 * if (platform_is_binary_in_path("ssh-keygen")) {
 *   // Use ssh-keygen
 * }
 * @endcode
 *
 * @ingroup platform
 */
bool platform_is_binary_in_path(const char *bin_name);

/**
 * @brief Cleanup the binary PATH cache
 *
 * Frees all cached binary PATH lookup results and destroys the cache.
 * Should be called during program cleanup (e.g., in platform_cleanup()).
 *
 * @note Thread-safe: Uses internal locking
 * @note Safe to call even if cache was never initialized
 *
 * @ingroup platform
 */
void platform_cleanup_binary_path_cache(void);

// ============================================================================
// Path Separator Constants
// ============================================================================

/**
 * @brief Platform-specific path separator character
 *
 * - Windows: '\\' (backslash)
 * - Unix/POSIX: '/' (forward slash)
 *
 * Use this constant instead of hardcoding separators or using #ifdef _WIN32.
 *
 * @note For string literals, use PATH_SEPARATOR_STR instead.
 *
 * @ingroup platform
 */
#ifdef _WIN32
#define PATH_DELIM '\\'
#define PATH_SEPARATOR_STR "\\"
#else
#define PATH_DELIM '/'
#define PATH_SEPARATOR_STR "/"
#endif

/**
 * @brief Platform-specific PATH environment variable separator
 *
 * - Windows: ";" (semicolon)
 * - Unix/POSIX: ":" (colon)
 *
 * @ingroup platform
 */
#ifdef _WIN32
#define PATH_ENV_SEPARATOR ";"
#else
#define PATH_ENV_SEPARATOR ":"
#endif

// ============================================================================
// File Permission Constants
// ============================================================================

/**
 * @brief File permission: Private (owner read/write only)
 *
 * Octal mode 0600: rw-------
 * Used for sensitive files like private keys, log files, and configuration files.
 *
 * @note On Windows, this is a no-op (Windows uses ACLs instead of POSIX permissions)
 *
 * @ingroup platform
 */
#define FILE_PERM_PRIVATE 0600

/**
 * @brief Directory permission: Private (owner read/write/execute only)
 *
 * Octal mode 0700: rwx------
 * Used for private directories like ~/.ascii-chat
 *
 * @note On Windows, this is a no-op (Windows uses ACLs instead of POSIX permissions)
 *
 * @ingroup platform
 */
#define DIR_PERM_PRIVATE 0700

/**
 * @brief File permission: Public read, owner write
 *
 * Octal mode 0644: rw-r--r--
 * Used for files that should be readable by others but only writable by owner.
 *
 * @ingroup platform
 */
#define FILE_PERM_PUBLIC_READ 0644

/**
 * @brief Permission mask for all permissions
 *
 * Octal mode 0777: rwxrwxrwx
 * Used for masking permission bits (e.g., st_mode & 0777)
 *
 * @ingroup platform
 */
#define FILE_PERM_MASK 0777

// ============================================================================
// Maximum Path Length
// ============================================================================

/**
 * @brief Maximum path length supported by the operating system
 *
 * Platform-specific values:
 * - Windows: 32767 characters (extended-length path with \\?\ prefix)
 * - Linux: 4096 bytes (PATH_MAX from limits.h)
 * - macOS: 1024 bytes (PATH_MAX from sys/syslimits.h)
 *
 * @note Windows legacy MAX_PATH (260) is too restrictive for modern use.
 *       We use the extended-length limit instead.
 *
 * @ingroup platform
 */
#ifdef _WIN32
#define PLATFORM_MAX_PATH_LENGTH 32767
#elif defined(__linux__)
#ifndef PATH_MAX
#define PLATFORM_MAX_PATH_LENGTH 4096
#else
#define PLATFORM_MAX_PATH_LENGTH PATH_MAX
#endif
#elif defined(__APPLE__)
#ifndef PATH_MAX
#define PLATFORM_MAX_PATH_LENGTH 1024
#else
#define PLATFORM_MAX_PATH_LENGTH PATH_MAX
#endif
#else
#define PLATFORM_MAX_PATH_LENGTH 4096
#endif

/**
 * @brief Get the path to the current executable
 *
 * Retrieves the full path to the currently running executable using
 * platform-specific methods.
 *
 * Platform-specific implementations:
 *   - Windows: GetModuleFileNameA()
 *   - Linux: readlink("/proc/self/exe")
 *   - macOS: _NSGetExecutablePath()
 *
 * @param exe_path Buffer to store the executable path
 * @param path_size Size of the buffer
 * @return true on success, false on failure
 *
 * @note Thread-safe
 * @note Buffer should be PLATFORM_MAX_PATH_LENGTH bytes to support all paths
 *
 * @par Example:
 * @code{.c}
 * char exe_path[PLATFORM_MAX_PATH_LENGTH];
 * if (platform_get_executable_path(exe_path, sizeof(exe_path))) {
 *   // Use exe_path
 * }
 * @endcode
 *
 * @ingroup platform
 */
bool platform_get_executable_path(char *exe_path, size_t path_size);

/**
 * @brief Get the system temporary directory path
 *
 * Retrieves the path to the system's temporary directory using
 * platform-specific methods. Verifies the directory exists and is writable.
 *
 * Platform-specific implementations:
 *   - Windows: %TEMP% or %TMP% environment variable, fallback to C:\Temp
 *   - Linux/macOS: /tmp
 *
 * @param temp_dir Buffer to store the temporary directory path
 * @param path_size Size of the buffer
 * @return true on success (directory exists and is writable), false on failure
 *
 * @note Thread-safe
 * @note Returned path does not include trailing directory separator
 * @note Buffer should be at least 256 bytes to support typical paths
 * @note Returns false if the directory doesn't exist or lacks write permission
 *
 * @par Example:
 * @code{.c}
 * char temp_dir[256];
 * if (platform_get_temp_dir(temp_dir, sizeof(temp_dir))) {
 *   // temp_dir is valid and writable
 *   char log_path[512];
 *   snprintf(log_path, sizeof(log_path), "%s/myapp.log", temp_dir);
 * }
 * @endcode
 *
 * @ingroup platform
 */
bool platform_get_temp_dir(char *temp_dir, size_t path_size);

/**
 * @brief Get the current working directory of the process.
 *
 * Normalizes the result using platform-specific semantics and does not append
 * a trailing directory separator.
 *
 * @param cwd Buffer to store the current working directory
 * @param path_size Size of the buffer in bytes
 * @return true on success, false on failure (buffer too small or API error)
 */
bool platform_get_cwd(char *cwd, size_t path_size);

/**
 * @brief Access modes for platform_access()
 *
 * @ingroup platform
 */
#define PLATFORM_ACCESS_EXISTS 0 ///< Check if file/directory exists
#define PLATFORM_ACCESS_WRITE 2  ///< Check if file/directory is writable
#define PLATFORM_ACCESS_READ 4   ///< Check if file/directory is readable

/**
 * @brief Check file/directory access permissions
 *
 * Platform-safe wrapper for access() / _access(). Tests whether the calling
 * process has the requested access to the specified path.
 *
 * Platform-specific implementations:
 *   - POSIX: Uses access() with F_OK, R_OK, W_OK, X_OK modes
 *   - Windows: Uses _access() with 0, 2, 4, 6 modes
 *
 * @param path File or directory path to check
 * @param mode Access mode to test (PLATFORM_ACCESS_EXISTS, PLATFORM_ACCESS_WRITE, PLATFORM_ACCESS_READ)
 * @return 0 on success (access permitted), -1 on failure (access denied or path doesn't exist)
 *
 * @note Thread-safe on all platforms
 * @note Does not follow symbolic links on POSIX (uses access() not faccessat())
 * @note Returns -1 if path is NULL
 *
 * @par Example:
 * @code{.c}
 * if (platform_access("/tmp", PLATFORM_ACCESS_WRITE) == 0) {
 *   // Directory is writable
 * }
 * @endcode
 *
 * @ingroup platform
 */
int platform_access(const char *path, int mode);

#ifdef __cplusplus
}
#endif

/** @} */
