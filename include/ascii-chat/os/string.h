#pragma once

/**
 * @file platform/string.h
 * @brief Cross-platform string operations
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * Provides platform-independent string manipulation utilities.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include "../asciichat_errno.h"

// Define ssize_t for all platforms
#ifdef _WIN32
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <sys/types.h> // For ssize_t on POSIX systems
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * String Formatting
 * ============================================================================
 */

/**
 * @brief Safe variable-argument string formatting
 * @param str Destination buffer
 * @param size Size of destination buffer
 * @param format Printf-style format string
 * @param ap Variable argument list
 * @return Number of characters written, or negative on error
 */
int platform_vsnprintf(char *str, size_t size, const char *format, va_list ap);

/**
 * @brief Allocate formatted string (asprintf replacement)
 * @param strp Pointer to string pointer (output parameter for allocated string)
 * @param format Printf-style format string
 * @param ... Variable arguments
 * @return Number of characters written (excluding null terminator), or -1 on error
 *
 * Allocates memory for a formatted string using malloc.
 * The caller must free the allocated string with free().
 */
int platform_asprintf(char **strp, const char *format, ...);

/**
 * @brief Allocate formatted string with va_list (vasprintf replacement)
 * @param strp Pointer to string pointer (output parameter for allocated string)
 * @param format Printf-style format string
 * @param ap Variable argument list
 * @return Number of characters written (excluding null terminator), or -1 on error
 */
int platform_vasprintf(char **strp, const char *format, va_list ap);

/**
 * @brief Duplicate string (strdup replacement)
 * @param s String to duplicate
 * @return Dynamically allocated copy of string, or NULL on error
 */
char *platform_strdup(const char *s);

/**
 * @brief Case-insensitive string comparison
 * @param s1 First string
 * @param s2 Second string
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2 (case-insensitive)
 */
int platform_strcasecmp(const char *s1, const char *s2);

/**
 * @brief Case-insensitive string comparison with length limit
 * @param s1 First string
 * @param s2 Second string
 * @param n Maximum number of characters to compare
 * @return 0 if equal, <0 if s1<s2, >0 if s1>s2 (case-insensitive)
 */
int platform_strncasecmp(const char *s1, const char *s2, size_t n);

/**
 * @brief Thread-safe string tokenization (strtok_r replacement)
 * @param str String to tokenize (or NULL to continue tokenizing)
 * @param delim Delimiter characters
 * @param saveptr Pointer to save tokenization state
 * @return Pointer to next token, or NULL when no more tokens
 */
char *platform_strtok_r(char *str, const char *delim, char **saveptr);

/**
 * @brief Safe string copy with size tracking (strlcpy)
 * @param dst Destination buffer
 * @param src Source string
 * @param size Size of destination buffer
 * @return Length of source string (before truncation)
 */
size_t platform_strlcpy(char *dst, const char *src, size_t size);

/**
 * @brief Safe string copy with explicit size bounds (strncpy replacement)
 * @param dst Destination buffer
 * @param dst_size Size of destination buffer
 * @param src Source string
 * @param count Maximum number of characters to copy
 * @return 0 on success, -1 on overflow/error
 */
int platform_strncpy(char *dst, size_t dst_size, const char *src, size_t count);

/**
 * @brief Cross-platform getline implementation
 * @param lineptr Pointer to buffer (can be NULL, will be allocated/reallocated)
 * @param n Pointer to buffer size
 * @param stream File stream to read from
 * @return Number of characters read (including newline), or -1 on error/EOF
 */
ssize_t platform_getline(char **lineptr, size_t *n, FILE *stream);

/* ============================================================================
 * Shell Path Escaping
 * ============================================================================
 */

/**
 * @brief Escape a path string for safe shell usage
 *
 * Escapes a file path according to platform-specific shell rules:
 *   - Windows: Wraps in double quotes, escapes internal quotes
 *   - POSIX: Wraps in single quotes (safest approach)
 *
 * @param path Input path to escape
 * @param output Buffer to store escaped path
 * @param output_size Size of output buffer
 * @return ASCIICHAT_OK on success, ERROR_BUFFER_OVERFLOW if buffer too small
 *
 * @note If output_size is too small, returns error without modifying buffer
 * @note Output string is properly null-terminated
 *
 * @par Example:
 * @code{.c}
 * char escaped[512];
 * platform_escape_shell_path("/path/to/file.txt", escaped, sizeof(escaped));
 * // Windows: "C:\path\to\file.txt"
 * // POSIX: '/path/to/file.txt'
 * @endcode
 *
 * @ingroup platform
 */
asciichat_error_t platform_escape_shell_path(const char *path, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

/** @} */
