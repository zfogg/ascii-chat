#pragma once

/**
 * @file system.h
 * @brief Cross-platform system functions interface for ASCII-Chat
 *
 * This header provides unified system functions including process management,
 * environment variables, TTY operations, and signal handling.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <signal.h>
#include <time.h>
#include "../common.h"

// Signal handler type
typedef void (*signal_handler_t)(int);

// ============================================================================
// System Functions
// ============================================================================

// Platform initialization
asciichat_error_t platform_init(void);
void platform_cleanup(void);

// Time functions
void platform_sleep_ms(unsigned int ms);

#ifdef _WIN32
#define usleep(usec) platform_sleep_usec(usec)
#endif

/**
 * Platform-safe localtime wrapper
 *
 * Uses localtime_s on Windows and localtime_r on POSIX.
 * Thread-safe on all platforms.
 *
 * @param timer Pointer to time_t value
 * @param result Pointer to struct tm to receive result
 * @return 0 on success, non-zero on error
 */
asciichat_error_t platform_localtime(const time_t *timer, struct tm *result);

// Process functions
int platform_get_pid(void);
const char *platform_get_username(void);

// Signal handling
signal_handler_t platform_signal(int sig, signal_handler_t handler);

// Environment variables
const char *platform_getenv(const char *name);
int platform_setenv(const char *name, const char *value);

// TTY functions
int platform_isatty(int fd);
const char *platform_ttyname(int fd);
int platform_fsync(int fd);

// Debug/stack trace functions
int platform_backtrace(void **buffer, int size);
char **platform_backtrace_symbols(void *const *buffer, int size);
void platform_backtrace_symbols_free(char **strings);

// Crash handling
void platform_install_crash_handler(void);
void platform_print_backtrace(int skip_frames);

// ============================================================================
// Safe String Functions
// ============================================================================

#include <stdio.h>
#include <stddef.h>

/**
 * Platform-safe snprintf wrapper
 *
 * Uses snprintf_s on Windows and snprintf with additional safety on POSIX.
 * Always null-terminates the output buffer.
 *
 * @param buffer Output buffer
 * @param buffer_size Size of output buffer
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of characters written (excluding null terminator) or negative on error
 */
int safe_snprintf(char *buffer, size_t buffer_size, const char *format, ...);

/**
 * Platform-safe fprintf wrapper
 *
 * Uses fprintf_s on Windows and fprintf on POSIX.
 *
 * @param stream File stream
 * @param format Format string
 * @param ... Variable arguments
 * @return Number of characters written or negative on error
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
 * Platform-safe memcpy wrapper
 *
 * Uses memcpy_s on Windows when available (C11) and memcpy with bounds checking on POSIX.
 * Provides consistent interface across platforms.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source buffer
 * @param count Number of bytes to copy
 * @return 0 on success, non-zero on error
 */
asciichat_error_t platform_memcpy(void *dest, size_t dest_size, const void *src, size_t count);

/**
 * Platform-safe memset wrapper
 *
 * Uses memset_s on Windows when available (C11) and memset with bounds checking on POSIX.
 * Provides consistent interface across platforms.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param ch Value to set (cast to unsigned char)
 * @param count Number of bytes to set
 * @return 0 on success, non-zero on error
 */
asciichat_error_t platform_memset(void *dest, size_t dest_size, int ch, size_t count);

/**
 * Platform-safe memmove wrapper
 *
 * Uses memmove_s on Windows when available (C11) and memmove with bounds checking on POSIX.
 * Handles overlapping memory regions safely.
 *
 * @param dest Destination buffer
 * @param dest_size Size of destination buffer
 * @param src Source buffer
 * @param count Number of bytes to move
 * @return 0 on success, non-zero on error
 */
asciichat_error_t platform_memmove(void *dest, size_t dest_size, const void *src, size_t count);

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
asciichat_error_t platform_strcpy(char *dest, size_t dest_size, const char *src);

/**
 * Resolve hostname to IPv4 address
 *
 * Performs DNS resolution to convert a hostname to an IPv4 address string.
 * Handles platform-specific networking initialization and cleanup.
 *
 * @param hostname Hostname to resolve (e.g., "example.com")
 * @param ipv4_out Buffer to store the resolved IPv4 address (e.g., "192.168.1.1")
 * @param ipv4_out_size Size of the output buffer
 * @return 0 on success, -1 on failure
 */
asciichat_error_t platform_resolve_hostname_to_ipv4(const char *hostname, char *ipv4_out, size_t ipv4_out_size);

/**
 * Load system CA certificates for TLS/HTTPS
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
 * @return 0 on success, -1 on failure
 *
 * Example:
 *   char* pem_data;
 *   size_t pem_size;
 *   if (platform_load_system_ca_certs(&pem_data, &pem_size) == 0) {
 *       // Use pem_data for TLS verification
 *       SAFE_FREE(pem_data);
 *   }
 */
asciichat_error_t platform_load_system_ca_certs(char **pem_data_out, size_t *pem_size_out);

/**
 * Check if a binary is available in the system PATH
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
 * Examples:
 * @code
 * if (platform_is_binary_in_path("ssh-keygen")) {
 *   // Use ssh-keygen
 * }
 *
 * if (platform_is_binary_in_path("llvm-symbolizer")) {
 *   // Use llvm-symbolizer
 * }
 * @endcode
 */
bool platform_is_binary_in_path(const char *bin_name);

/**
 * Cleanup the binary PATH cache
 *
 * Frees all cached binary PATH lookup results and destroys the cache.
 * Should be called during program cleanup (e.g., in platform_cleanup()).
 *
 * @note Thread-safe: Uses internal locking
 * @note Safe to call even if cache was never initialized
 */
void platform_cleanup_binary_path_cache(void);

/**
 * Maximum path length supported by the operating system
 *
 * Platform-specific values:
 * - Windows: 32767 characters (extended-length path with \\?\ prefix)
 * - Linux: 4096 bytes (PATH_MAX from limits.h)
 * - macOS: 1024 bytes (PATH_MAX from sys/syslimits.h)
 *
 * Note: Windows legacy MAX_PATH (260) is too restrictive for modern use.
 * We use the extended-length limit instead.
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
 * Get the path to the current executable
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
 * Example:
 * @code
 * char exe_path[PLATFORM_MAX_PATH_LENGTH];
 * if (platform_get_executable_path(exe_path, sizeof(exe_path))) {
 *   // Use exe_path
 * }
 * @endcode
 */
bool platform_get_executable_path(char *exe_path, size_t path_size);
