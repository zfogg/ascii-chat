/**
 * @file util/int_parse.h
 * @ingroup util
 * @brief 🔢 Safe integer parsing with overflow detection and error handling
 *
 * Provides consistent, safe wrappers for parsing integers from strings with
 * proper error checking, overflow detection, and detailed error context.
 * All functions return error codes and set errno for detailed diagnostics.
 */

#pragma once

#include <stdint.h>
#include "asciichat_errno.h"

/**
 * @brief Parse signed long integer with range validation
 * @param str String to parse (must be null-terminated)
 * @param out_value Output: parsed integer value
 * @param min_value Minimum allowed value (inclusive)
 * @param max_value Maximum allowed value (inclusive)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid input or out of range
 *
 * Safe wrapper for strtol() with:
 * - NULL pointer checking
 * - Range validation (min/max bounds)
 * - Overflow/underflow detection
 * - Detailed error context
 *
 * Example:
 *   long port;
 *   if (int_parse_long("8080", &port, 1, 65535) == ASCIICHAT_OK) {
 *       // port is valid
 *   }
 */
asciichat_error_t int_parse_long(const char *str, long *out_value, long min_value, long max_value);

/**
 * @brief Parse unsigned long integer with range validation
 * @param str String to parse (must be null-terminated)
 * @param out_value Output: parsed integer value
 * @param min_value Minimum allowed value (inclusive)
 * @param max_value Maximum allowed value (inclusive)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid input or out of range
 *
 * Safe wrapper for strtoul() with:
 * - NULL pointer checking
 * - Range validation (min/max bounds)
 * - Overflow detection
 * - Detailed error context
 */
asciichat_error_t int_parse_ulong(const char *str, unsigned long *out_value, unsigned long min_value,
                                   unsigned long max_value);

/**
 * @brief Parse unsigned long long integer with range validation
 * @param str String to parse (must be null-terminated)
 * @param out_value Output: parsed integer value
 * @param min_value Minimum allowed value (inclusive)
 * @param max_value Maximum allowed value (inclusive)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid input or out of range
 *
 * Safe wrapper for strtoull() with:
 * - NULL pointer checking
 * - Range validation (min/max bounds)
 * - Overflow detection
 * - Detailed error context
 */
asciichat_error_t int_parse_ulonglong(const char *str, unsigned long long *out_value, unsigned long long min_value,
                                       unsigned long long max_value);

/**
 * @brief Parse port number (1-65535) from string
 * @param str String to parse (must be null-terminated)
 * @param out_port Output: parsed port number
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid input or out of range
 *
 * Convenience function for parsing TCP/UDP port numbers.
 * Validates that port is in valid range [1, 65535].
 *
 * Example:
 *   uint16_t port;
 *   if (int_parse_port("8080", &port) == ASCIICHAT_OK) {
 *       // port is valid
 *   }
 */
asciichat_error_t int_parse_port(const char *str, uint16_t *out_port);

/**
 * @brief Parse signed 32-bit integer with range validation
 * @param str String to parse (must be null-terminated)
 * @param out_value Output: parsed integer value
 * @param min_value Minimum allowed value (inclusive)
 * @param max_value Maximum allowed value (inclusive)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid input or out of range
 *
 * Safe wrapper for parsing 32-bit signed integers with overflow detection.
 */
asciichat_error_t int_parse_int32(const char *str, int32_t *out_value, int32_t min_value, int32_t max_value);

/**
 * @brief Parse unsigned 32-bit integer with range validation
 * @param str String to parse (must be null-terminated)
 * @param out_value Output: parsed integer value
 * @param min_value Minimum allowed value (inclusive)
 * @param max_value Maximum allowed value (inclusive)
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid input or out of range
 *
 * Safe wrapper for parsing 32-bit unsigned integers with overflow detection.
 */
asciichat_error_t int_parse_uint32(const char *str, uint32_t *out_value, uint32_t min_value, uint32_t max_value);
