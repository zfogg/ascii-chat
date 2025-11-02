/**
 * @file platform/string.h
 * @ingroup platform
 * @brief Platform-independent safe string functions
 *
 * This header provides safe string formatting and manipulation functions that
 * satisfy clang-tidy cert-err33-c requirements. All functions ensure null
 * termination and prevent buffer overflow, making them safe replacements for
 * standard C library functions.
 *
 * CORE FEATURES:
 * ==============
 * - Safe string formatting with guaranteed null termination
 * - Buffer overflow protection for all operations
 * - Error handling and return value validation
 * - Platform-independent implementations
 * - Compliance with CERT C coding standards
 *
 * SAFETY GUARANTEES:
 * ==================
 * - All string operations ensure null termination
 * - Buffer sizes are always validated before operations
 * - Functions return meaningful error codes on failure
 * - No undefined behavior from buffer overflows
 *
 * @note All functions in this header are designed to be drop-in replacements
 *       for standard C library functions with additional safety guarantees.
 * @note Functions satisfy clang-tidy cert-err33-c requirements for safe
 *       string operations.
 * @note Return values should always be checked to ensure operations succeeded.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#pragma once

#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

/**
 * @name Safe String Formatting Functions
 * @{
 */

/**
 * @brief Safe version of snprintf that ensures null termination
 * @param buffer Buffer to write to (must not be NULL)
 * @param buffer_size Size of the buffer (must be > 0)
 * @param format Format string (must not be NULL)
 * @param ... Variable arguments for format string
 * @return Number of characters written (not including null terminator),
 *         or negative value on error
 *
 * Formats a string into a buffer with guaranteed null termination. Always
 * ensures the buffer is null-terminated even if truncation occurs, unlike
 * the standard snprintf() which may not null-terminate on some platforms.
 *
 * SAFETY FEATURES:
 * - Guaranteed null termination (buffer always ends with '\0')
 * - Buffer size validation before formatting
 * - Truncation detection via return value
 * - No buffer overflow (operation is bounded by buffer_size)
 *
 * @note Return value indicates number of characters that would have been
 *       written if buffer was large enough (not including null terminator).
 * @note If return value >= buffer_size, output was truncated.
 * @note Buffer is always null-terminated, even if truncation occurred.
 *
 * @warning Always check return value. Values >= buffer_size indicate truncation.
 *
 * @par Example
 * @code
 * char buf[64];
 * int len = safe_snprintf(buf, sizeof(buf), "Hello %s", "world");
 * if (len >= sizeof(buf)) {
 *     // Output was truncated
 * }
 * @endcode
 *
 * @ingroup platform
 */
int safe_snprintf(char *buffer, size_t buffer_size, const char *format, ...);

/**
 * @brief Safe version of fprintf
 * @param stream File stream to write to (must not be NULL)
 * @param format Format string (must not be NULL)
 * @param ... Variable arguments for format string
 * @return Number of characters written, or negative value on error
 *
 * Formats and writes to a file stream with error checking. Validates that
 * formatting operations succeed and returns meaningful error codes.
 *
 * SAFETY FEATURES:
 * - Stream validation before writing
 * - Error detection and reporting
 * - Return value indicates success or failure
 *
 * @note Return value indicates number of characters written on success.
 * @note Negative return value indicates an error occurred.
 * @note Function validates stream and format string before operations.
 *
 * @warning Always check return value. Negative values indicate errors.
 *
 * @ingroup platform
 */
int safe_fprintf(FILE *stream, const char *format, ...);

/**
 * @brief Safe version of strcat with buffer size protection
 * @param dest Destination buffer (must not be NULL)
 * @param dest_size Size of destination buffer (must be > 0)
 * @param src Source string to append (must not be NULL)
 * @return Destination buffer on success, NULL on error
 *
 * Safely appends a source string to a destination buffer with buffer size
 * protection. Prevents buffer overflow by validating available space before
 * concatenation.
 *
 * SAFETY FEATURES:
 * - Buffer size validation prevents overflow
 * - Guaranteed null termination of result
 * - Returns NULL on error (buffer too small, invalid parameters)
 * - Validates available space before appending
 *
 * @note Function validates that destination has sufficient space for source.
 * @note If concatenation would exceed buffer size, returns NULL.
 * @note Result is always null-terminated on success.
 *
 * @warning Always check return value. NULL indicates operation failed
 *          (typically buffer too small).
 *
 * @par Example
 * @code
 * char buf[64] = "Hello";
 * if (platform_strcat(buf, sizeof(buf), " world") == NULL) {
 *     // Operation failed (buffer too small)
 * }
 * @endcode
 *
 * @ingroup platform
 */
char *platform_strcat(char *dest, size_t dest_size, const char *src);

/**
 * @brief Safe version of sscanf with validation
 * @param str String to parse (must not be NULL)
 * @param format Format string (must not be NULL)
 * @param ... Variable arguments for parsed values (must not be NULL)
 * @return Number of successfully parsed items, or negative value on error
 *
 * Safely parses a string using a format specification with input validation.
 * Returns the number of successfully parsed items and validates input parameters.
 *
 * SAFETY FEATURES:
 * - Input string and format validation
 * - Return value indicates number of successfully parsed items
 * - Error detection and reporting
 *
 * @note Return value indicates number of successfully parsed format specifiers.
 * @note Negative return value indicates an error occurred.
 * @note Function validates string and format before parsing.
 *
 * @warning Always check return value. Negative values or fewer parsed items
 *          than expected indicate errors or incomplete parsing.
 *
 * @par Example
 * @code
 * int x, y;
 * int parsed = safe_sscanf("10 20", "%d %d", &x, &y);
 * if (parsed == 2) {
 *     // Both values parsed successfully
 * }
 * @endcode
 *
 * @ingroup platform
 */
int safe_sscanf(const char *str, const char *format, ...);

/** @} */
