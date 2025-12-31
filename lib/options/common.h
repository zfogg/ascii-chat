/**
 * @file common.h
 * @brief Common utilities and helpers for option parsing
 * @ingroup options
 *
 * Shared helper functions, validators, and utilities used by client.c, server.c,
 * and mirror.c option parsing modules. This module provides:
 * - String parsing and validation utilities
 * - Option argument validation (ports, FPS, webcam indices)
 * - SSH key detection
 * - Terminal dimension management
 * - Error handling helpers
 *
 * **Design Philosophy**:
 * - Single Responsibility: Each function does one validation task
 * - Consistent Error Reporting: All validators use fprintf to stderr
 * - No Side Effects: Validators don't modify global state
 * - Reusability: Used by all mode-specific parsers
 *
 * @see options.h
 * @see client.h
 * @see server.h
 * @see mirror.h
 */

#pragma once

#include "options/options.h"
#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Option Parsing Helpers
// ============================================================================

/**
 * @brief Find a similar option name for typo suggestions
 *
 * Uses Levenshtein distance to find the most similar option name from the
 * provided options array. Only suggests options within a reasonable edit distance.
 *
 * @param unknown_opt The unknown/misspelled option name
 * @param options Array of valid option structures (must be NULL-terminated)
 * @return Suggested option name, or NULL if no good match found
 *
 * @note Uses LEVENSHTEIN_SUGGESTION_THRESHOLD to filter poor matches
 *
 * Example:
 * @code
 * const char *suggestion = find_similar_option("--colr", client_options);
 * if (suggestion) {
 *     fprintf(stderr, "Did you mean '--%s'?\n", suggestion);
 * }
 * @endcode
 */
const char *find_similar_option(const char *unknown_opt, const struct option *options);

/**
 * @brief Safely parse string to integer with validation
 *
 * Parses a string to integer using parse_int32() with full range checking.
 * Returns INT_MIN on error (NULL input, empty string, invalid format, out of range).
 *
 * @param str String to parse
 * @return Parsed integer value, or INT_MIN on error
 *
 * @warning INT_MIN is used as error sentinel, so cannot represent INT_MIN value
 *
 * Example:
 * @code
 * int val = strtoint_safe(optarg);
 * if (val == INT_MIN) {
 *     fprintf(stderr, "Invalid integer: %s\n", optarg);
 *     return ERROR_INVALID_PARAM;
 * }
 * @endcode
 */
int strtoint_safe(const char *str);

/**
 * @brief Standard option parsing error return
 *
 * Returns ERROR_INVALID_PARAM consistently across all option parsing code.
 * Inline function to ensure zero runtime overhead.
 *
 * @return ERROR_INVALID_PARAM
 */
static inline asciichat_error_t option_error_invalid(void) {
  return ERROR_INVALID_PARAM;
}

/**
 * @brief Validate and retrieve required argument for an option
 *
 * Wrapper around get_required_argument() that also sets error code on failure.
 * Used for options that must have an argument.
 *
 * @param optarg Argument value from getopt_long
 * @param argbuf Buffer for storing processed argument
 * @param argbuf_size Size of argbuf
 * @param option_name Name of the option (for error messages)
 * @param mode Current mode (client/server/mirror) for error messages
 * @return Pointer to processed argument string in argbuf, or NULL on error
 *
 * @note On error, prints message to stderr and calls option_error_invalid()
 *
 * Example:
 * @code
 * char argbuf[OPTIONS_BUFF_SIZE];
 * char *value = validate_required_argument(optarg, argbuf, sizeof(argbuf), "port", MODE_CLIENT);
 * if (!value) {
 *     return option_error_invalid();
 * }
 * @endcode
 */
char *validate_required_argument(const char *optarg, char *argbuf, size_t argbuf_size, const char *option_name,
                                 asciichat_mode_t mode);

/**
 * @brief Validate a positive integer value
 *
 * Internal option parsing helper that validates a string represents a positive
 * integer (> 0). Prints error message on failure.
 *
 * @param value_str String to validate
 * @param out_value Output parameter for validated integer
 * @param param_name Parameter name for error messages
 * @return true if valid, false otherwise
 *
 * Example:
 * @code
 * int fps;
 * if (!validate_positive_int_opt(optarg, &fps, "FPS")) {
 *     return option_error_invalid();
 * }
 * @endcode
 */
bool validate_positive_int_opt(const char *value_str, int *out_value, const char *param_name);

/**
 * @brief Validate port number (1-65535)
 *
 * Internal option parsing helper that validates a port number is in valid range.
 * Uses parse_port() for robust validation.
 *
 * @param value_str String to validate
 * @param out_port Output parameter for validated port
 * @return true if valid, false otherwise
 *
 * Example:
 * @code
 * uint16_t port;
 * if (!validate_port_opt(optarg, &port)) {
 *     return option_error_invalid();
 * }
 * opt_port = port;
 * @endcode
 */
bool validate_port_opt(const char *value_str, uint16_t *out_port);

/**
 * @brief Validate FPS value (1-144)
 *
 * Internal option parsing helper that validates FPS is in reasonable range.
 * Range chosen to support 1 FPS (slideshows) to 144 FPS (high refresh monitors).
 *
 * @param value_str String to validate
 * @param out_fps Output parameter for validated FPS
 * @return true if valid, false otherwise
 *
 * Example:
 * @code
 * int fps;
 * if (!validate_fps_opt(optarg, &fps)) {
 *     return option_error_invalid();
 * }
 * opt_fps = fps;
 * @endcode
 */
bool validate_fps_opt(const char *value_str, int *out_fps);

/**
 * @brief Validate webcam index using the common device index validator
 *
 * Validates webcam index is a non-negative integer. Unlike audio device indices,
 * webcam indices do not support -1 (default).
 *
 * @param value_str String to validate
 * @param out_index Output parameter for validated index
 * @return true if valid, false otherwise
 *
 * Example:
 * @code
 * unsigned short int webcam_idx;
 * if (!validate_webcam_index(optarg, &webcam_idx)) {
 *     return option_error_invalid();
 * }
 * opt_webcam_index = webcam_idx;
 * @endcode
 */
bool validate_webcam_index(const char *value_str, unsigned short int *out_index);

/**
 * @brief Detect default SSH key path for the current user
 *
 * Checks if ~/.ssh/id_ed25519 exists and is a regular file. Only supports Ed25519
 * keys (modern, secure, fast).
 *
 * @param key_path Buffer to store detected key path
 * @param path_size Size of key_path buffer
 * @return ASCIICHAT_OK if found, ERROR_CRYPTO_KEY with helpful message otherwise
 *
 * @note Uses expand_path() to resolve tilde (~) in path
 * @note Prints message to stderr suggesting key generation if not found
 *
 * Example:
 * @code
 * char key_path[OPTIONS_BUFF_SIZE];
 * if (detect_default_ssh_key(key_path, sizeof(key_path)) == ASCIICHAT_OK) {
 *     log_debug("Using default SSH key: %s", key_path);
 *     SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", key_path);
 * }
 * @endcode
 */
asciichat_error_t detect_default_ssh_key(char *key_path, size_t path_size);

// ============================================================================
// Argument Processing Helpers
// ============================================================================

/**
 * @brief Strip equals sign prefix from option argument
 *
 * Internal helper that handles GNU-style long options with = syntax (--option=value).
 * Copies argument to buffer and returns pointer past the '=' if present.
 *
 * @param opt_value Raw option value from getopt_long
 * @param buffer Buffer to store processed value
 * @param buffer_size Size of buffer
 * @return Pointer to value in buffer (past '=' if present), or NULL if empty
 *
 * @note Returns NULL for empty strings after stripping '='
 * @note Buffer is always null-terminated via SAFE_SNPRINTF
 *
 * Example:
 * @code
 * char argbuf[OPTIONS_BUFF_SIZE];
 * char *value = strip_equals_prefix("=1234", argbuf, sizeof(argbuf));
 * // value points to "1234" in argbuf
 * @endcode
 */
char *strip_equals_prefix(const char *opt_value, char *buffer, size_t buffer_size);

/**
 * @brief Handle required arguments with consistent error messages
 *
 * Validates that an option has a non-empty argument and processes it.
 * Returns NULL on error with appropriate error message printed.
 *
 * Handles edge cases:
 * - NULL or empty opt_value
 * - getopt_long bug where option name is returned as argument
 * - Arguments with '=' prefix (GNU-style --option=value)
 *
 * @param opt_value Argument value from getopt_long
 * @param buffer Buffer for storing processed argument
 * @param buffer_size Size of buffer
 * @param option_name Name of the option (for error messages)
 * @param mode Current mode (client/server/mirror) for error messages
 * @return Pointer to processed argument string in buffer, or NULL on error
 *
 * @note Prints "option '--<name>' requires an argument" to stderr on error
 * @note Used internally by validate_required_argument()
 *
 * Example:
 * @code
 * char argbuf[OPTIONS_BUFF_SIZE];
 * char *value = get_required_argument(optarg, argbuf, sizeof(argbuf), "key", MODE_CLIENT);
 * if (!value) {
 *     return option_error_invalid();
 * }
 * SAFE_SNPRINTF(opt_encrypt_key, OPTIONS_BUFF_SIZE, "%s", value);
 * @endcode
 */
char *get_required_argument(const char *opt_value, char *buffer, size_t buffer_size, const char *option_name,
                            asciichat_mode_t mode);

/**
 * @brief Read password from stdin with prompt
 * @param prompt Prompt message to display to user
 * @return Allocated password string (caller must free), or NULL on error
 *
 * Prompts user for password input using prompt_password_simple() from util/password.h.
 * Returns dynamically allocated string that must be freed by caller.
 *
 * @note Returns NULL if password input fails or if not running in a TTY.
 * @note Caller must use SAFE_FREE() to deallocate returned string.
 */
char *read_password_from_stdin(const char *prompt);

// ============================================================================
// Terminal Dimension Utilities
// ============================================================================

/**
 * @brief Update dimensions for full-height mode
 *
 * Sets opt_height to terminal height when auto-detected. Used during initialization
 * to maximize vertical space usage.
 *
 * Behavior:
 * - Both auto: Set both width and height to terminal size
 * - Only height auto: Set height to terminal height
 * - Only width auto: Set width to terminal width
 * - Neither auto: No change
 *
 * @note Does not use log_debug because logging may not be initialized yet
 * @note Fails silently if terminal size detection fails (keeps defaults)
 *
 * Example:
 * @code
 * // During options_init():
 * if (auto_height || auto_width) {
 *     update_dimensions_for_full_height();
 * }
 * @endcode
 */
void update_dimensions_for_full_height(void);

/**
 * @brief Update dimensions to current terminal size
 *
 * Updates opt_width and opt_height to current terminal size for auto-detected
 * dimensions. Used after logging is initialized (can use log_debug).
 *
 * Behavior:
 * - auto_width: Set width to terminal width
 * - auto_height: Set height to terminal height
 * - Neither: No change
 *
 * @note Logs debug messages about dimension updates
 * @note Logs debug message if terminal size detection fails
 *
 * Example:
 * @code
 * // After logging initialization:
 * update_dimensions_to_terminal_size();
 * log_info("Terminal dimensions: %dx%d", opt_width, opt_height);
 * @endcode
 */
void update_dimensions_to_terminal_size(void);

#ifdef __cplusplus
}
#endif
