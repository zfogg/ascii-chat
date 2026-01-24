/**
 * @file common.h
 * @brief Common utilities and helpers for option parsing across all modes
 * @ingroup options
 *
 * This module provides shared utilities used by the entire options system:
 * - Validators for numeric ranges, file existence, formats (IP, port, etc.)
 * - String parsing helpers (safe integer conversion, color mode parsing, etc.)
 * - Terminal dimension management functions
 * - Option lookup and typo suggestion (Levenshtein distance)
 * - SSH key detection and defaults
 *
 * **Design Philosophy**:
 *
 * - **Single Responsibility**: Each validator handles one specific type of validation
 * - **Consistent Error Reporting**: All validators provide helpful error messages
 * - **No Side Effects**: Validators are pure functions (no global state modification)
 * - **Reusability**: These functions are used by registry, builder, and parsers modules
 * - **Cross-Cutting Concerns**: Handles validation needs for all modes uniformly
 *
 * **Validator Functions**:
 *
 * Return conventions:
 * - Numeric validators: Return parsed value on success, INT_MIN/-1 on error
 * - Boolean validators: Return true/false with error message on failure
 * - String validators: Validate format, write to output buffer
 *
 * **Typical Usage**:
 *
 * ```c
 * // Validate port number
 * uint16_t port;
 * if (!validate_port_opt("8080", &port)) {
 *     fprintf(stderr, "Invalid port: 8080\\n");
 *     return false;
 * }
 *
 * // Parse color mode from string
 * asciichat_error_t err = parse_color_mode_option("256", opts);
 * if (err != ASCIICHAT_OK) {
 *     fprintf(stderr, "Unknown color mode\\n");
 *     return false;
 * }
 *
 * // Find similar option if user misspelled
 * const char *suggestion = find_similar_option("prot", all_options);
 * if (suggestion) {
 *     fprintf(stderr, "Did you mean: --%s?\\n", suggestion);
 * }
 * ```
 *
 * **Option Parsing Helpers**:
 *
 * - `strtoint_safe()`: Safe integer parsing with range checking
 * - `find_similar_option()`: Typo detection using Levenshtein distance
 * - `validate_positive_int_opt()`: Validate positive integers
 * - `validate_port_opt()`: Validate port numbers (1-65535)
 * - `validate_fps_opt()`: Validate frame rates
 * - `validate_webcam_index()`: Validate webcam device indices
 *
 * **Display Option Parsers**:
 *
 * - `parse_color_mode_option()`: Parse color mode strings (auto, mono, 16, 256, truecolor)
 * - `parse_render_mode_option()`: Parse render mode (foreground, background, half-block)
 * - `parse_palette_option()`: Parse palette type selection
 * - `parse_palette_chars_option()`: Parse custom palette character set
 *
 * **Terminal Functions**:
 *
 * - `update_dimensions_for_full_height()`: Adjust dimensions to use full terminal height
 * - `update_dimensions_to_terminal_size()`: Sync dimensions with current terminal size
 * - `print_project_links()`: Print ascii-chat project links and info
 *
 * **Cryptography Helpers**:
 *
 * - `detect_default_ssh_key()`: Find default SSH key if not specified
 * - `validate_options_and_report()`: Cross-field validation with error reporting
 *
 * @see options.h - Main options module
 * @see registry.h - Central registry of all options
 * @see builder.h - Builder API using these validators
 * @see validation.h - Additional validation functions
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
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
 * @brief Validate options and report errors to stderr
 *
 * Calls options_config_validate() and handles error message display and cleanup.
 * Validates all option dependencies, conflicts, and custom validators.
 *
 * @param config Options configuration (opaque pointer, defined in builder.h)
 * @param opts Options struct to validate
 * @return ASCIICHAT_OK if valid, error code otherwise
 *
 * @note Prints error message to stderr if validation fails
 * @note Frees error message internally
 *
 * Example:
 * @code
 * asciichat_error_t result = validate_options_and_report(config, opts);
 * if (result != ASCIICHAT_OK) {
 *     options_config_destroy(config);
 *     return result;
 * }
 * @endcode
 */
asciichat_error_t validate_options_and_report(const void *config, const void *opts);

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
// Shared Option Parsers (Client + Mirror Common Options)
// ============================================================================

/**
 * @brief Parse --color-mode option and set opts->color_mode
 *
 * Validates color mode string and sets opts->color_mode field.
 * Accepts: "auto", "none", "mono", "16", "16color", "256", "256color", "truecolor", "24bit"
 *
 * @param value_str Color mode string from command line
 * @param opts Options struct to update
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid mode
 *
 * @note Prints error message to stderr on failure
 * @note Sets opts->color_mode on success
 */
asciichat_error_t parse_color_mode_option(const char *value_str, options_t *opts);

/**
 * @brief Parse --render-mode option and set opts->render_mode
 *
 * Validates render mode string and sets opts->render_mode field.
 * Accepts: "foreground", "fg", "background", "bg", "half-block", "halfblock"
 *
 * @param value_str Render mode string from command line
 * @param opts Options struct to update
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid mode
 *
 * @note Prints error message to stderr on failure
 * @note Sets opts->render_mode on success
 */
asciichat_error_t parse_render_mode_option(const char *value_str, options_t *opts);

/**
 * @brief Parse --palette option and set opt_palette_type
 *
 * Validates palette type string and sets global opt_palette_type variable.
 * Accepts: "standard", "blocks", "digital", "minimal", "cool", "custom"
 *
 * @param value_str Palette type string from command line
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM on invalid palette
 *
 * @note Prints error message to stderr on failure
 * @note Sets opt_palette_type global variable on success
 */
asciichat_error_t parse_palette_option(const char *value_str, options_t *opts);

/**
 * @brief Parse --palette-chars option and set opt_palette_custom
 *
 * Validates custom palette characters and sets global opt_palette_custom,
 * opt_palette_custom_set, and opt_palette_type variables.
 *
 * @param value_str Custom palette characters from command line
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM if too long
 *
 * @note Prints error message to stderr on failure
 * @note Sets opt_palette_custom, opt_palette_custom_set, and opt_palette_type on success
 * @note Maximum length is 255 characters (sizeof(opt_palette_custom) - 1)
 */
asciichat_error_t parse_palette_chars_option(const char *value_str, options_t *opts);

/**
 * @brief Parse --width option and set opts->width
 *
 * Validates width value and sets opts->width and opts->auto_width fields.
 *
 * @param value_str Width value from command line
 * @param opts Options struct to update
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM if invalid
 *
 * @note Prints error message to stderr on failure
 * @note Sets opts->width and opts->auto_width = false on success
 */
asciichat_error_t parse_width_option(const char *value_str, options_t *opts);

/**
 * @brief Parse --height option and set opts->height
 *
 * Validates height value and sets opts->height and opts->auto_height fields.
 *
 * @param value_str Height value from command line
 * @param opts Options struct to update
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM if invalid
 *
 * @note Prints error message to stderr on failure
 * @note Sets opts->height and opts->auto_height = false on success
 */
asciichat_error_t parse_height_option(const char *value_str, options_t *opts);

/**
 * @brief Parse --webcam-index option and set opts->webcam_index
 *
 * Validates webcam index and sets opts->webcam_index field.
 *
 * @param value_str Webcam index from command line
 * @param opts Options struct to update
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM if invalid
 *
 * @note Prints error message to stderr on failure
 * @note Sets opts->webcam_index on success
 */
asciichat_error_t parse_webcam_index_option(const char *value_str, options_t *opts);

/**
 * @brief Parse --snapshot-delay option and set opts->snapshot_delay
 *
 * Validates snapshot delay (non-negative float) and sets opts->snapshot_delay field.
 *
 * @param value_str Snapshot delay in seconds from command line
 * @param opts Options struct to update
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM if invalid
 *
 * @note Prints error message to stderr on failure
 * @note Sets opts->snapshot_delay on success
 */
asciichat_error_t parse_snapshot_delay_option(const char *value_str, options_t *opts);

/**
 * @brief Parse --log-level option and set opt_log_level
 *
 * Validates log level string and sets global opt_log_level variable.
 * Accepts: "dev", "debug", "info", "warn", "error", "fatal" (case-insensitive)
 *
 * @param value_str Log level string from command line
 * @return ASCIICHAT_OK on success, ERROR_INVALID_PARAM if invalid
 *
 * @note Prints error message to stderr on failure
 * @note Sets opt_log_level global variable on success
 * @note Uses validate_opt_log_level() from validation.h
 */
asciichat_error_t parse_log_level_option(const char *value_str, options_t *opts);

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
void update_dimensions_for_full_height(options_t *opts);

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
void update_dimensions_to_terminal_size(options_t *opts);

/**
 * @brief Print project links with link emoji and colored styling
 * @param desc Output file stream (typically stdout)
 */
void print_project_links(FILE *desc);

#ifdef __cplusplus
}
#endif
