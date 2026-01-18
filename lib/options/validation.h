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

#include "common.h"
#include "options/options.h" // For strtoint_safe()

#ifdef __cplusplus
extern "C" {
#endif

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

/**
 * @brief Validate max clients value (1-32)
 * @param value_str Max clients value as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed value on success, -1 on error
 *
 * Validates that the max clients is in the valid range of 1-32.
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_max_clients(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate compression level (1-9)
 * @param value_str Compression level as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed level on success, -1 on error
 *
 * Validates zstd compression level in range 1-9.
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_compression_level(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate reconnect value
 * @param value_str Reconnect value as string ("off", "auto", or 0-999)
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return 0 for off, -1 for auto, 1-999 for count, INT_MIN on error
 *
 * Valid values:
 * - "off" or "0": No reconnection (returns 0)
 * - "auto" or "-1": Unlimited reconnection (returns -1)
 * - "1" to "999": Retry count (returns the count)
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_reconnect(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate device index (-1 for default, 0+ for specific device)
 * @param value_str Device index as string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return Parsed index on success, INT_MIN on error
 *
 * Used for microphone-index, speakers-index, webcam-index.
 * -1 means use system default device.
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_device_index(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Validate password (8-256 characters)
 * @param value_str Password string
 * @param error_msg Buffer for error message (can be NULL)
 * @param error_msg_size Size of error message buffer
 * @return 0 on success, -1 on error
 *
 * Validates password length is 8-256 characters and contains no null bytes.
 *
 * @ingroup options
 */
ASCIICHAT_API int validate_opt_password(const char *value_str, char *error_msg, size_t error_msg_size);

/**
 * @brief Collect multiple --key flags into identity_keys array
 * @param opts Options structure to populate
 * @param argc Argument count from main()
 * @param argv Argument vector from main()
 * @return Number of keys collected on success, -1 on error
 *
 * Scans command-line arguments for all --key/-K flags and populates both
 * opts->encrypt_key (first key, for compatibility) and opts->identity_keys[]
 * array with all keys. Also sets opts->num_identity_keys.
 *
 * This enables multi-key support: servers can load multiple identity keys
 * (e.g., both SSH and GPG) and select the appropriate one during handshake
 * based on what the client expects.
 *
 * @note Must be called after options_parse() but before key loading
 * @note Supports up to MAX_IDENTITY_KEYS (32) keys
 *
 * @ingroup options
 */
ASCIICHAT_API int options_collect_identity_keys(options_t *opts, int argc, char *argv[]);

/** @} */

#ifdef __cplusplus
}
#endif

/** @} */
