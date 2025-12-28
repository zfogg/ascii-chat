/**
 * @file options/validation.h
 * @brief Validation functions for options parsing
 * @ingroup options
 * @addtogroup options
 * @{
 *
 * This header provides validation functions used during command-line option
 * parsing and configuration file loading. These functions validate user input
 * and provide detailed error messages for invalid values.
 *
 * All validation functions follow a consistent pattern:
 * - Return 0 or positive value on success (often the parsed value)
 * - Return -1 or negative value on error
 * - Write error messages to the provided error_msg buffer
 *
 * @note These functions are used by both options.c and config.c
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Integer Parsing Utilities
 * @{
 */

/**
 * @brief Safely parse string to integer with validation
 * @param str String to parse (must not be NULL)
 * @return Integer value on success, INT_MIN on error
 *
 * Parses a string to an integer with comprehensive validation:
 * - Validates that string is not NULL or empty
 * - Performs base-10 conversion using strtol()
 * - Checks for partial conversions (characters left unconverted)
 * - Validates result is within int range (INT_MIN to INT_MAX)
 * - Returns INT_MIN on any error condition
 *
 * @note Returns INT_MIN on error (which is distinguishable from valid negative
 *       values since INT_MIN is a valid integer, but unlikely to be used as an
 *       option value). The options parser checks for INT_MIN to detect parse errors.
 * @note Thread-safe: Uses only local variables, no static state.
 *
 * @ingroup options
 */
ASCIICHAT_API int strtoint_safe(const char *str);

/** @} */

/**
 * @name Validation Functions
 * @{
 */

/**
 * @brief Validate port number (1-65535)
 * @param value_str Port number as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return 0 on success, -1 on error
 *
 * Validates that the port string is a valid number in the range 1-65535.
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_port(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate positive integer
 * @param value_str Integer value as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed value on success, -1 on error
 *
 * Validates that the string is a positive integer (> 0).
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_positive_int(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate non-negative integer
 * @param value_str Integer value as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed value on success, -1 on error
 *
 * Validates that the string is a non-negative integer (>= 0).
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_non_negative_int(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate color mode string
 * @param value_str Color mode value as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed color mode enum value on success, -1 on error
 *
 * Valid values: auto, none, mono, 16, 16color, 256, 256color, truecolor, 24bit
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_color_mode(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate render mode string
 * @param value_str Render mode value as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed render mode enum value on success, -1 on error
 *
 * Valid values: foreground, fg, background, bg, half-block, halfblock
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_render_mode(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate palette type string
 * @param value_str Palette type as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed palette type enum value on success, -1 on error
 *
 * Valid values: standard, blocks, digital, minimal, cool, custom
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_palette(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate log level string
 * @param value_str Log level as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed log level enum value on success, -1 on error
 *
 * Valid values (case-insensitive): dev, debug, info, warn, error, fatal
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_log_level(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate IP address or hostname
 * @param value_str IP address or hostname as string
 * @param parsed_address Buffer to store resolved/parsed address
 * @param address_size Size of parsed_address buffer
 * @param is_client True if client mode (for error messages)
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return 0 on success, -1 on error
 *
 * Validates IPv4, IPv6 addresses, or resolves hostname to IP.
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_ip_address(const char *value_str, char *parsed_address, size_t address_size,
                                          bool is_client, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate non-negative float value
 * @param value_str Float value as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed float value on success, -1.0f on error
 *
 * Validates that the string is a valid non-negative floating-point number.
 *
 * @ingroup options
 */
ASCIICHAT_API float validate_opt_float_non_negative(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate FPS value (1-144)
 * @param value_str FPS value as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed FPS value on success, -1 on error
 *
 * Validates that the FPS is in the valid range of 1-144.
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_fps(const char *value_str, char *error_msg, size_t error_msg_size);

/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
