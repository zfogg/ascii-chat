#pragma once

/**
 * @file platform/system.h
 * @ingroup platform
 * @brief Cross-platform system functions interface for ascii-chat
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
#include <time.h>
#include "../common.h"

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
 * @brief Sleep for a specified number of milliseconds
 * @param ms Number of milliseconds to sleep
 *
 * Sleeps the current thread for the specified duration.
 *
 * @ingroup platform
 */
void platform_sleep_ms(unsigned int ms);

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
 * @brief Print a backtrace using log_plain
 * @param skip_frames Number of frames to skip from the top
 *
 * Prints a backtrace of the current call stack using log_plain().
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
