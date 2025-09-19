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

// Signal handler type
typedef void (*signal_handler_t)(int);

// ============================================================================
// System Functions
// ============================================================================

// Platform initialization
int platform_init(void);
void platform_cleanup(void);

// Time functions
void platform_sleep_ms(unsigned int ms);

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
int platform_localtime(const time_t *timer, struct tm *result);

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
void platform_print_backtrace(void);

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
int platform_memcpy(void *dest, size_t dest_size, const void *src, size_t count);

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
int platform_memset(void *dest, size_t dest_size, int ch, size_t count);

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
int platform_memmove(void *dest, size_t dest_size, const void *src, size_t count);

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
int platform_strcpy(char *dest, size_t dest_size, const char *src);
