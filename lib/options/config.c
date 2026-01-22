/**
 * @file config.c
 * @ingroup config
 * @brief 📋 TOML configuration file parser with schema validation and CLI override support
 */

#include "options/config.h"
#include "common/error_codes.h"
#include "options/options.h"
#include "options/validation.h"
#include "options/schema.h"
#include "options/rcu.h"
#include "util/path.h"
#include "util/utf8.h"
#include "common.h"
#include "platform/terminal.h"
#include "platform/system.h"
#include "platform/question.h"
#include "crypto/crypto.h"
#include "log/logging.h"
#include "video/palette.h"
#include "version.h"
#include "tooling/defer/defer.h"

#include <ascii-chat-deps/tomlc17/src/tomlc17.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <direct.h>
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#define mkdir(path, mode) _mkdir(path)
#else
#include <unistd.h>
#endif

/**
 * @name Internal Macros
 * @{
 */

/**
 * @brief Print configuration warning to stderr
 *
 * Config warnings are printed directly to stderr because logging may not be
 * initialized yet when configuration is loaded. This ensures users see
 * validation errors immediately.
 */
#define CONFIG_WARN(fmt, ...)                                                                                          \
  do {                                                                                                                 \
    (void)fprintf(stderr, "WARNING: Config file: " fmt "\n", ##__VA_ARGS__);                                           \
    (void)fflush(stderr);                                                                                              \
  } while (0)

/**
 * @brief Print configuration debug message
 *
 * Debug messages use the logging system if it's initialized, otherwise
 * they are silently dropped.
 */
#define CONFIG_DEBUG(fmt, ...)                                                                                         \
  do {                                                                                                                 \
    /* Debug messages are only shown in debug builds after logging is initialized */                                   \
    /* Use log_debug which safely checks initialization itself */                                                      \
    log_debug(fmt, ##__VA_ARGS__);                                                                                     \
  } while (0)

/** @} */

// Configuration state tracking removed - not needed with schema-driven approach
// CLI arguments always override config values, so no need to track what was set

/* Validation functions are now provided by options/validation.h */

/**
 * @name TOML Value Extraction Helpers
 * @{
 *
 * Helper functions to safely extract typed values from TOML datum structures.
 */

/**
 * @brief Validate and return a TOML string value
 * @param datum TOML datum to extract string from
 * @return String value if valid UTF-8, NULL otherwise
 *
 * Validates that the TOML string value contains valid UTF-8.
 * Rejects invalid UTF-8 sequences for security and robustness.
 * @ingroup config
 */
static const char *get_toml_string_validated(toml_datum_t datum) {
  if (datum.type != TOML_STRING) {
    SET_ERRNO(ERROR_INVALID_PARAM, "not a toml string");
    return NULL;
  }

  const char *str = datum.u.s;
  if (!str) {
    SET_ERRNO(ERROR_INVALID_PARAM, "no toml string");
    return NULL;
  }

  // Validate UTF-8 encoding using utility function
  if (!utf8_is_valid(str)) {
    log_warn("Config value contains invalid UTF-8 sequence");
    return NULL;
  }

  return str;
}

/** @} */

/**
 * @name Schema-Based Configuration Parser
 * @{
 */

/**
 * @brief Apply configuration from TOML using schema metadata
 * @param toptab Root TOML table
 * @param is_client True for client mode, false for server mode
 * @param opts Options structure to populate
 * @param strict If true, return error on first validation failure
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Generic schema-driven parser that:
 * 1. Iterates through all options in schema
 * 2. Checks if option applies to current mode
 * 3. Looks up TOML value
 * 4. Validates and converts type
 * 5. Writes to options_t using field_offset
 *
 * Handles special cases:
 * - palette.chars auto-setting palette_type
 * - Path normalization for keys/log files
 * - Type coercion (int/string, float/string)
 * - crypto.no_encrypt special logic
 */
static asciichat_error_t config_apply_schema(toml_datum_t toptab, asciichat_mode_t detected_mode, options_t *opts,
                                             bool strict) {
  size_t metadata_count = 0;
  const config_option_metadata_t *metadata = config_schema_get_all(&metadata_count);
  asciichat_error_t first_error = ASCIICHAT_OK;

  // Track which options were set to avoid duplicates (e.g., log_file vs logging.log_file)
  // Use a simple array on the stack - metadata_count is small (< 50)
  bool option_set_flags[64] = {0}; // Max expected options
  if (metadata_count > 64) {
    CONFIG_WARN("Too many options in schema (%zu), some may be skipped", metadata_count);
  }

  for (size_t i = 0; i < metadata_count; i++) {
    const config_option_metadata_t *meta = &metadata[i];

    // Skip if not applicable to current mode - validate using mode_bitmask
    bool applies_to_mode = false;
    if (meta->mode_bitmask & OPTION_MODE_BINARY) {
      // Binary options are always valid
      applies_to_mode = true;
    } else if (detected_mode >= 0 && detected_mode <= MODE_DISCOVERY) {
      // Check if option applies to detected mode
      option_mode_bitmask_t mode_bit = (1 << detected_mode);
      applies_to_mode = (meta->mode_bitmask & mode_bit) != 0;
    }

    if (!applies_to_mode) {
      // Option doesn't apply to this mode - skip it
      if (strict) {
        CONFIG_WARN("Option '%s' is not valid for mode %d (skipping)", meta->toml_key, detected_mode);
      }
      continue;
    }

    // Skip if already set (avoid processing duplicates like log_file vs logging.log_file)
    if (option_set_flags[i]) {
      continue;
    }

    // Look up TOML value
    toml_datum_t datum = toml_seek(toptab, meta->toml_key);
    if (datum.type == TOML_UNKNOWN) {
      continue; // Option not present in config
    }

    // Extract value based on type
    char value_str[512] = {0};
    bool has_value = false;
    int int_val = 0;
    bool bool_val = false;
    double double_val = 0.0;

    switch (meta->type) {
    case OPTION_TYPE_STRING: {
      // String options can come as TOML_STRING or TOML_INT64 (e.g., port as integer)
      if (datum.type == TOML_STRING) {
        const char *str = get_toml_string_validated(datum);
        if (str && strlen(str) > 0) {
          SAFE_STRNCPY(value_str, str, sizeof(value_str));
          has_value = true;
        }
      } else if (datum.type == TOML_INT64) {
        // Convert integer to string (e.g., port = 7777)
        SAFE_SNPRINTF(value_str, sizeof(value_str), "%lld", (long long)datum.u.int64);
        has_value = true;
      }
      break;
    }
    case OPTION_TYPE_INT: {
      // INT type handles both regular integers and enums (enums come as strings)
      if (datum.type == TOML_INT64) {
        int_val = (int)datum.u.int64;
        has_value = true;
        // Convert to string for validation
        SAFE_SNPRINTF(value_str, sizeof(value_str), "%d", int_val);
      } else if (datum.type == TOML_STRING) {
        // Could be an enum (color_mode, render_mode, palette_type) or string representation of int
        const char *str = get_toml_string_validated(datum);
        if (str) {
          SAFE_STRNCPY(value_str, str, sizeof(value_str));
          has_value = true;
        }
      }
      break;
    }
    case OPTION_TYPE_BOOL: {
      if (datum.type == TOML_BOOLEAN) {
        bool_val = datum.u.boolean;
        has_value = true;
      }
      break;
    }
    case OPTION_TYPE_DOUBLE: {
      // DOUBLE type handles both float and double (check field_size to distinguish)
      if (datum.type == TOML_FP64) {
        double_val = datum.u.fp64;
        has_value = true;
        // Convert to string for validation
        SAFE_SNPRINTF(value_str, sizeof(value_str), "%.10g", double_val);
      } else if (datum.type == TOML_STRING) {
        const char *str = get_toml_string_validated(datum);
        if (str) {
          SAFE_STRNCPY(value_str, str, sizeof(value_str));
          has_value = true;
        }
      }
      break;
    }
    }

    if (!has_value) {
      continue;
    }

    // Special handling for palette.chars (auto-sets palette_type to CUSTOM)
    if (strcmp(meta->toml_key, "palette.chars") == 0) {
      const char *chars_str = get_toml_string_validated(datum);
      if (chars_str && strlen(chars_str) > 0) {
        if (strlen(chars_str) < sizeof(opts->palette_custom)) {
          SAFE_STRNCPY(opts->palette_custom, chars_str, sizeof(opts->palette_custom));
          opts->palette_custom[sizeof(opts->palette_custom) - 1] = '\0';
          opts->palette_custom_set = true;
          opts->palette_type = PALETTE_CUSTOM;
          option_set_flags[i] = true;
        } else {
          CONFIG_WARN("Invalid palette.chars: too long (%zu chars, max %zu, skipping)", strlen(chars_str),
                      sizeof(opts->palette_custom) - 1);
          if (strict) {
            return SET_ERRNO(ERROR_CONFIG, "palette.chars too long");
          }
        }
      }
      continue;
    }

    // Parse and validate string values using validation functions from validation.h
    // These are the same functions used by the options builder
    char error_msg[256] = {0};
    char parsed_buffer[OPTIONS_BUFF_SIZE] = {0};
    int parsed_int = 0;
    float parsed_float = 0.0f;
    bool parse_success = true;

    // Parse string value based on type using validation functions
    switch (meta->type) {
    case OPTION_TYPE_STRING: {
      // For strings, we still need to validate format (e.g., IP addresses, ports)
      // Use validation functions that handle string parsing
      // Most string fields don't need validation, but some do (ports, IPs, paths)
      // For now, just copy the value - validation happens via builder's validate function after writing
      SAFE_STRNCPY(parsed_buffer, value_str, sizeof(parsed_buffer));
      break;
    }
    case OPTION_TYPE_INT: {
      // Enums are stored as OPTION_TYPE_INT in builder but come as strings in TOML
      // Check field_offset to detect enum fields (color_mode, render_mode, palette_type)
      // Check if this is an enum field (comes as string in TOML) or regular int
      int enum_val = -1;
      bool is_enum = false;

      // Detect enum fields by checking field_offset
      if (meta->field_offset == offsetof(options_t, color_mode)) {
        enum_val = validate_opt_color_mode(value_str, error_msg, sizeof(error_msg));
        is_enum = true;
      } else if (meta->field_offset == offsetof(options_t, render_mode)) {
        enum_val = validate_opt_render_mode(value_str, error_msg, sizeof(error_msg));
        is_enum = true;
      } else if (meta->field_offset == offsetof(options_t, palette_type)) {
        enum_val = validate_opt_palette(value_str, error_msg, sizeof(error_msg));
        is_enum = true;
      }

      if (is_enum) {
        // Enum parsing
        if (enum_val < 0) {
          parse_success = false;
          if (strlen(error_msg) == 0) {
            SAFE_SNPRINTF(error_msg, sizeof(error_msg), "Invalid enum value: %s", value_str);
          }
        } else {
          parsed_int = enum_val;
        }
      } else {
        // Regular integer parsing
        char *endptr = NULL;
        long parsed = strtol(value_str, &endptr, 10);
        if (*endptr != '\0') {
          parse_success = false;
          SAFE_SNPRINTF(error_msg, sizeof(error_msg), "Invalid integer: %s", value_str);
        } else if (parsed < INT_MIN || parsed > INT_MAX) {
          parse_success = false;
          SAFE_SNPRINTF(error_msg, sizeof(error_msg), "Integer out of range: %s", value_str);
        } else {
          parsed_int = (int)parsed;
        }
      }
      break;
    }
    case OPTION_TYPE_DOUBLE: {
      char *endptr = NULL;
      double parsed = strtod(value_str, &endptr);
      if (*endptr != '\0') {
        parse_success = false;
        SAFE_SNPRINTF(error_msg, sizeof(error_msg), "Invalid float: %s", value_str);
      } else {
        parsed_float = (float)parsed;
      }
      break;
    }
    case OPTION_TYPE_BOOL:
      // Already parsed above
      break;
    }

    if (!parse_success) {
      CONFIG_WARN("Invalid %s value '%s': %s (skipping)", meta->toml_key, value_str, error_msg);
      if (strict) {
        if (first_error == ASCIICHAT_OK) {
          first_error = SET_ERRNO(ERROR_CONFIG, "Invalid %s: %s", meta->toml_key, error_msg);
        }
        continue;
      }
      continue;
    }

    // Write value to options_t using field_offset
    char *field_ptr = ((char *)opts) + meta->field_offset;

    switch (meta->type) {
    case OPTION_TYPE_STRING: {
      // For string types, use parsed_buffer if validator filled it (e.g., IP address normalization)
      // Otherwise use original value_str
      const char *final_value = (parsed_buffer[0] != '\0') ? parsed_buffer : value_str;

      // Special handling for path-based options (keys, log files)
      // Check if this is a path-based option by examining the key name
      bool is_path_option = (strstr(meta->toml_key, "key") != NULL || strstr(meta->toml_key, "log_file") != NULL ||
                             strstr(meta->toml_key, "keyfile") != NULL);
      if (is_path_option) {
        // Check if it's a path that needs normalization
        if (path_looks_like_path(final_value)) {
          char *normalized = NULL;
          path_role_t role = PATH_ROLE_CONFIG_FILE; // Default
          if (strstr(meta->toml_key, "key") != NULL) {
            role = (strstr(meta->toml_key, "server_key") != NULL || strstr(meta->toml_key, "client_keys") != NULL)
                       ? PATH_ROLE_KEY_PUBLIC
                       : PATH_ROLE_KEY_PRIVATE;
          } else if (strstr(meta->toml_key, "log_file") != NULL) {
            role = PATH_ROLE_LOG_FILE;
          }

          asciichat_error_t path_result = path_validate_user_path(final_value, role, &normalized);
          if (path_result != ASCIICHAT_OK) {
            SAFE_FREE(normalized);
            if (strict) {
              if (first_error == ASCIICHAT_OK) {
                first_error = path_result;
              }
              continue;
            }
            continue;
          }
          SAFE_STRNCPY(field_ptr, normalized, meta->field_size);
          SAFE_FREE(normalized);
        } else {
          // Not a path, just an identifier (e.g., "gpg:keyid", "github:user")
          SAFE_STRNCPY(field_ptr, final_value, meta->field_size);
        }

        // Auto-enable encryption for crypto.key, crypto.password, crypto.keyfile
        if (strstr(meta->toml_key, "crypto.key") != NULL || strstr(meta->toml_key, "crypto.password") != NULL ||
            strstr(meta->toml_key, "crypto.keyfile") != NULL) {
          opts->encrypt_enabled = 1;
        }
      } else {
        SAFE_STRNCPY(field_ptr, final_value, meta->field_size);
      }
      break;
    }
    case OPTION_TYPE_INT: {
      // Use validator result if available, otherwise use original parsed value
      int final_int_val = int_val; // Default to original value
      if (meta->validate_fn) {
        // Validator returned parsed value in parsed_value (int*)
        final_int_val = parsed_int;
      }
      // Handle unsigned short int fields (webcam_index)
      if (meta->field_size == sizeof(unsigned short int)) {
        *(unsigned short int *)field_ptr = (unsigned short int)final_int_val;
      } else {
        *(int *)field_ptr = final_int_val;
      }
      break;
    }
    case OPTION_TYPE_BOOL: {
      // Handle unsigned short int bool fields (common in options_t)
      if (meta->field_size == sizeof(unsigned short int)) {
        *(unsigned short int *)field_ptr = bool_val ? 1 : 0;
      } else {
        *(bool *)field_ptr = bool_val;
      }
      break;
    }
    case OPTION_TYPE_DOUBLE: {
      // Check field_size to distinguish float from double
      if (meta->field_size == sizeof(float)) {
        // Float field
        float float_val = (float)double_val;
        if (meta->validate_fn) {
          float_val = parsed_float;
        }
        *(float *)field_ptr = float_val;
      } else {
        // Double field
        double final_double_val = double_val; // Default to original TOML value
        if (meta->validate_fn) {
          // Validator wrote to parsed_float, cast to double
          final_double_val = (double)parsed_float;
        }
        *(double *)field_ptr = final_double_val;
      }
      break;
    }
      // Enums are OPTION_TYPE_INT, handled above in the INT case
    }

    // Call builder's validate function if it exists (for cross-field validation)
    if (meta->validate_fn) {
      char *validate_error = NULL;
      if (!meta->validate_fn(opts, &validate_error)) {
        CONFIG_WARN("Validation failed for %s: %s (skipping)", meta->toml_key,
                    validate_error ? validate_error : "validation failed");
        if (validate_error) {
          SAFE_FREE(validate_error);
        }
        if (strict) {
          if (first_error == ASCIICHAT_OK) {
            first_error = SET_ERRNO(ERROR_CONFIG, "Validation failed for %s", meta->toml_key);
          }
          continue;
        }
        continue;
      }
    }

    // Mark this option as set
    option_set_flags[i] = true;
  }

  // Handle special crypto.no_encrypt logic
  toml_datum_t no_encrypt = toml_seek(toptab, "crypto.no_encrypt");
  if (no_encrypt.type == TOML_BOOLEAN && no_encrypt.u.boolean) {
    opts->no_encrypt = 1;
    opts->encrypt_enabled = 0;
  }

  // Handle password warning
  toml_datum_t password = toml_seek(toptab, "crypto.password");
  const char *password_str = get_toml_string_validated(password);
  if (password_str && strlen(password_str) > 0) {
    CONFIG_WARN("Password stored in config file is insecure! Use CLI --password instead.");
  }

  return first_error;
}

/** @} */

/**
 * @brief Main function to load configuration from file and apply to global options
 * @param is_client `true` if loading client configuration, `false` for server configuration
 * @param config_path Optional path to config file (NULL uses default location)
 * @param strict If true, errors are fatal; if false, errors are non-fatal warnings
 * @return ASCIICHAT_OK on success, error code on failure (if strict) or non-fatal (if !strict)
 *
 * This is the main entry point for configuration loading. It:
 * 1. Expands the config file path (default location or custom path)
 * 2. Checks if the file exists and is a regular file
 * 3. Parses the TOML file using tomlc17
 * 4. Applies configuration from each section (network, client, palette, crypto, logging)
 * 5. Frees resources and returns
 *
 * Configuration file errors are non-fatal if strict is false:
 * - Missing file: Returns ASCIICHAT_OK (config file is optional)
 * - Not a regular file: Warns and returns ASCIICHAT_OK
 * - Parse errors: Warns and returns ASCIICHAT_OK
 * - Invalid values: Individual values are skipped with warnings
 *
 * If strict is true, any error causes immediate return with error code.
 *
 * @note This function should be called before `options_init()` parses command-line
 *       arguments to ensure CLI arguments can override config file values.
 *
 * @note Configuration warnings are printed to stderr because logging may not be
 *       initialized yet when this function is called.
 *
 * @ingroup config
 */
asciichat_error_t config_load_and_apply(asciichat_mode_t detected_mode, const char *config_path, bool strict,
                                        options_t *opts) {
  // detected_mode is used in config_apply_schema for bitmask validation
  char *config_path_expanded = NULL;
  defer(SAFE_FREE(config_path_expanded));

  if (config_path) {
    // Use custom path provided
    config_path_expanded = expand_path(config_path);
    if (!config_path_expanded) {
      // If expansion fails, try using as-is (might already be absolute)
      config_path_expanded = platform_strdup(config_path);
    }
  } else {
    // Use default location with XDG support
    char *config_dir = get_config_dir();
    defer(SAFE_FREE(config_dir));
    if (config_dir) {
      size_t len = strlen(config_dir) + strlen("config.toml") + 1;
      config_path_expanded = SAFE_MALLOC(len, char *);
      if (config_path_expanded) {
#ifdef _WIN32
        safe_snprintf(config_path_expanded, len, "%sconfig.toml", config_dir);
#else
        safe_snprintf(config_path_expanded, len, "%sconfig.toml", config_dir);
#endif
      }
    }

    // Fallback to ~/.ascii-chat/config.toml
    if (!config_path_expanded) {
      config_path_expanded = expand_path("~/.ascii-chat/config.toml");
    }
  }

  if (!config_path_expanded) {
    if (strict) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to resolve config file path");
    }
    return ASCIICHAT_OK;
  }

  char *validated_config_path = NULL;
  asciichat_error_t validate_result =
      path_validate_user_path(config_path_expanded, PATH_ROLE_CONFIG_FILE, &validated_config_path);
  if (validate_result != ASCIICHAT_OK) {
    return validate_result;
  }
  SAFE_FREE(config_path_expanded);
  config_path_expanded = validated_config_path;

  // Determine display path for error messages (before any early returns)
  const char *display_path = config_path ? config_path : config_path_expanded;

  // Log that we're attempting to load config (before logging is initialized, use stderr)
  if (config_path) {
    (void)fprintf(stderr, "Loading configuration from: %s\n", display_path);
    (void)fflush(stderr);
  }

  // Check if config file exists
  struct stat st;
  if (stat(config_path_expanded, &st) != 0) {
    if (strict) {
      return SET_ERRNO(ERROR_CONFIG, "Config file does not exist: '%s'", display_path);
    }
    // File doesn't exist, that's OK - not required (non-strict mode)
    return ASCIICHAT_OK;
  }

  // Verify it's a regular file
  if (!S_ISREG(st.st_mode)) {
    if (strict) {
      return SET_ERRNO(ERROR_CONFIG, "Config file exists but is not a regular file: '%s'", display_path);
    }
    CONFIG_WARN("Config file exists but is not a regular file: '%s' (skipping)", display_path);
    return ASCIICHAT_OK;
  }

  // Parse TOML file
  toml_result_t result = toml_parse_file_ex(config_path_expanded);
  defer(toml_free(result));

  if (!result.ok) {
    // result.errmsg is an array, so check its first character
    const char *errmsg = (strlen(result.errmsg) > 0) ? result.errmsg : "Unknown parse error";

    if (strict) {
      // For strict mode, return detailed error message directly
      // Note: SET_ERRNO stores the message in context, but asciichat_error_string() only returns generic codes
      // So we need to format the error message ourselves here
      char error_buffer[512];
      safe_snprintf(error_buffer, sizeof(error_buffer), "Failed to parse config file '%s': %s", display_path, errmsg);
      return SET_ERRNO(ERROR_CONFIG, "%s", error_buffer);
    }
    CONFIG_WARN("Failed to parse config file '%s': %s (skipping)", display_path, errmsg);
    return ASCIICHAT_OK; // Non-fatal error
  }

  // Apply configuration using schema-driven parser with bitmask validation
  asciichat_error_t schema_result = config_apply_schema(result.toptab, detected_mode, opts, strict);
  if (schema_result != ASCIICHAT_OK && strict) {
    return schema_result;
  }
  // In non-strict mode, continue even if some options failed validation

  CONFIG_DEBUG("Loaded configuration from %s", display_path);

  // Log successful config load (use stderr since logging may not be initialized yet)
  (void)fprintf(stderr, "Loaded configuration from: %s\n", display_path);
  (void)fflush(stderr);

  // Update RCU system with modified options (for test compatibility)
  // In real usage, options_state_set is called later after CLI parsing
  asciichat_error_t rcu_result = options_state_set(opts);
  if (rcu_result != ASCIICHAT_OK) {
    // Non-fatal - RCU might not be initialized yet in some test scenarios
    // But log as warning so tests can see if this is the issue
    CONFIG_WARN("Failed to update RCU options state: %d (values may not be persisted)", rcu_result);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t config_create_default(const char *config_path, const options_t *opts) {
  char *config_path_expanded = NULL;
  defer(SAFE_FREE(config_path_expanded));

  if (config_path) {
    // Use custom path provided
    config_path_expanded = expand_path(config_path);
    if (!config_path_expanded) {
      // If expansion fails, try using as-is (might already be absolute)
      config_path_expanded = platform_strdup(config_path);
    }
  } else {
    // Use default location with XDG support
    char *config_dir = get_config_dir();
    defer(SAFE_FREE(config_dir));
    if (config_dir) {
      size_t len = strlen(config_dir) + strlen("config.toml") + 1;
      config_path_expanded = SAFE_MALLOC(len, char *);
      if (config_path_expanded) {
        safe_snprintf(config_path_expanded, len, "%sconfig.toml", config_dir);
      }
    }

    // Fallback to ~/.ascii-chat/config.toml
    if (!config_path_expanded) {
      config_path_expanded = expand_path("~/.ascii-chat/config.toml");
    }
  }

  if (!config_path_expanded) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to resolve config file path");
  }

  char *validated_config_path = NULL;
  asciichat_error_t validate_result =
      path_validate_user_path(config_path_expanded, PATH_ROLE_CONFIG_FILE, &validated_config_path);
  if (validate_result != ASCIICHAT_OK) {
    SAFE_FREE(config_path_expanded);
    return validate_result;
  }
  SAFE_FREE(config_path_expanded);
  config_path_expanded = validated_config_path;

  // Check if file already exists
  struct stat st;
  if (stat(config_path_expanded, &st) == 0) {
    // File exists - ask user if they want to overwrite
    log_plain_stderr("Config file already exists: %s", config_path_expanded);

    bool overwrite = platform_prompt_yes_no("Overwrite", false); // Default to No
    if (!overwrite) {
      log_plain_stderr("Config file creation cancelled.");
      return SET_ERRNO(ERROR_CONFIG, "User cancelled overwrite");
    }

    // User confirmed overwrite - continue to create file (will overwrite existing)
    log_plain_stderr("Overwriting existing config file...");
  }

  // Create directory if needed
  char *dir_path = platform_strdup(config_path_expanded);
  if (!dir_path) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for directory path");
  }

  // Find the last path separator
  char *last_sep = strrchr(dir_path, PATH_DELIM);

  if (last_sep) {
    *last_sep = '\0';
    // Create directory (similar to known_hosts.c approach)
    int mkdir_result = mkdir(dir_path, DIR_PERM_PRIVATE);
    if (mkdir_result != 0 && errno != EEXIST) {
      // mkdir failed and it's not because the directory already exists
      // Verify if directory actually exists despite the error (Windows compatibility)
      struct stat test_st;
      if (stat(dir_path, &test_st) != 0) {
        // Directory doesn't exist and we couldn't create it
        asciichat_error_t err = SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create config directory: %s", dir_path);
        SAFE_FREE(dir_path);
        return err;
      }
      // Directory exists despite error, proceed
    }
  }
  SAFE_FREE(dir_path);

  // Create file with default values
  FILE *f = platform_fopen(config_path_expanded, "w");
  defer(SAFE_FCLOSE(f));
  if (!f) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create config file: %s", config_path_expanded);
  }

  // Write version comment
  (void)fprintf(f, "# ascii-chat configuration file\n");
  (void)fprintf(f, "# Generated by ascii-chat v%d.%d.%d-%s\n", ASCII_CHAT_VERSION_MAJOR, ASCII_CHAT_VERSION_MINOR,
                ASCII_CHAT_VERSION_PATCH, ASCII_CHAT_GIT_VERSION);
  (void)fprintf(f, "#\n");
  (void)fprintf(f, "# If you upgrade ascii-chat and this version comment changes, you may need to\n");
  (void)fprintf(f, "# delete and regenerate this file with: ascii-chat --config-create\n");
  (void)fprintf(f, "#\n\n");

  // Get all options from schema
  size_t metadata_count = 0;
  const config_option_metadata_t *metadata = config_schema_get_all(&metadata_count);

  // Build list of unique categories in order of first appearance
  const char *categories[16] = {0}; // Max expected categories
  size_t category_count = 0;

  for (size_t i = 0; i < metadata_count && category_count < 16; i++) {
    const char *category = metadata[i].category;
    if (!category) {
      continue;
    }

    // Check if category already in list
    bool found = false;
    for (size_t j = 0; j < category_count; j++) {
      if (categories[j] && strcmp(categories[j], category) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      categories[category_count++] = category;
    }
  }

  // Write each section dynamically from schema
  for (size_t cat_idx = 0; cat_idx < category_count; cat_idx++) {
    const char *category = categories[cat_idx];
    if (!category) {
      continue;
    }

    // Get all options for this category
    size_t cat_option_count = 0;
    const config_option_metadata_t **cat_options = config_schema_get_by_category(category, &cat_option_count);

    if (!cat_options || cat_option_count == 0) {
      continue;
    }

    // Write section header
    (void)fprintf(f, "[%s]\n", category);

    // Track which options we've written (to avoid duplicates like log_file vs logging.log_file)
    bool written_flags[64] = {0}; // Max options per category

    // Write each option in this category
    for (size_t opt_idx = 0; opt_idx < cat_option_count && opt_idx < 64; opt_idx++) {
      const config_option_metadata_t *meta = cat_options[opt_idx];
      if (!meta || !meta->toml_key) {
        continue;
      }

      // Skip if already written (duplicate)
      if (written_flags[opt_idx]) {
        continue;
      }

      // Extract key name from TOML key (e.g., "network.port" -> "port", "client.width" -> "width")
      const char *key_name = meta->toml_key;
      const char *dot = strrchr(meta->toml_key, '.');
      if (dot) {
        key_name = dot + 1;
      }

      // Skip if this is a duplicate of another option (e.g., logging.log_file vs log_file)
      // Check if we've already written an option with the same field_offset
      bool is_duplicate = false;
      for (size_t j = 0; j < opt_idx; j++) {
        if (cat_options[j] && cat_options[j]->field_offset == meta->field_offset) {
          is_duplicate = true;
          break;
        }
      }
      if (is_duplicate) {
        continue;
      }

      // Get field pointer
      const char *field_ptr = ((const char *)opts) + meta->field_offset;

      // Write description comment if available
      if (meta->description && strlen(meta->description) > 0) {
        (void)fprintf(f, "# %s\n", meta->description);
      }

      // Format and write the option value based on type
      switch (meta->type) {
      case OPTION_TYPE_STRING: {
        const char *str_value = (const char *)field_ptr;
        if (str_value && strlen(str_value) > 0) {
          (void)fprintf(f, "#%s = \"%s\"\n", key_name, str_value);
        } else {
          (void)fprintf(f, "#%s = \"\"\n", key_name);
        }
        break;
      }
      case OPTION_TYPE_INT: {
        int int_value = 0;
        if (meta->field_size == sizeof(unsigned short int)) {
          int_value = *(unsigned short int *)field_ptr;
        } else {
          int_value = *(int *)field_ptr;
        }
        (void)fprintf(f, "#%s = %d\n", key_name, int_value);
        break;
      }
      case OPTION_TYPE_BOOL: {
        bool bool_value = false;
        if (meta->field_size == sizeof(unsigned short int)) {
          bool_value = *(unsigned short int *)field_ptr != 0;
        } else {
          bool_value = *(bool *)field_ptr;
        }
        (void)fprintf(f, "#%s = %s\n", key_name, bool_value ? "true" : "false");
        break;
      }
      case OPTION_TYPE_DOUBLE: {
        // Check field_size to distinguish float from double
        if (meta->field_size == sizeof(float)) {
          float float_value = *(float *)field_ptr;
          (void)fprintf(f, "#%s = %.1f\n", key_name, (double)float_value);
        } else {
          double double_value = *(double *)field_ptr;
          (void)fprintf(f, "#%s = %.1f\n", key_name, double_value);
        }
        break;
      }
        // Enums are OPTION_TYPE_INT, handled above in the INT case
      }

      written_flags[opt_idx] = true;
    }

    // Add blank line between sections
    (void)fprintf(f, "\n");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t config_load_system_and_user(asciichat_mode_t detected_mode, const char *user_config_path, bool strict,
                                              options_t *opts) {
  // Fallback for ASCIICHAT_INSTALL_PREFIX if paths.h hasn't been generated yet
  // (prevents defer tool compilation errors during initial builds)
#ifndef ASCIICHAT_INSTALL_PREFIX
#ifdef _WIN32
#define ASCIICHAT_INSTALL_PREFIX "C:\\Program Files\\ascii-chat"
#else
#define ASCIICHAT_INSTALL_PREFIX "/usr/local"
#endif
#endif

  // Build system config path: ${INSTALL_PREFIX}/etc/ascii-chat/config.toml
  char system_config_path[1024];
  SAFE_SNPRINTF(system_config_path, sizeof(system_config_path),
                "%s" PATH_SEPARATOR_STR "etc" PATH_SEPARATOR_STR "ascii-chat" PATH_SEPARATOR_STR "config.toml",
                ASCIICHAT_INSTALL_PREFIX);

  // Load system config first (non-strict - it's optional)
  CONFIG_DEBUG("Attempting to load system config from: %s", system_config_path);
  asciichat_error_t system_result = config_load_and_apply(detected_mode, system_config_path, false, opts);
  if (system_result == ASCIICHAT_OK) {
    CONFIG_DEBUG("System config loaded successfully");
  } else {
    CONFIG_DEBUG("System config not loaded (this is normal if file doesn't exist)");
    // Clear the error context since this failure is expected/non-fatal
    CLEAR_ERRNO();
  }

  // Load user config second (with user-specified strictness)
  // User config values will override system config values
  CONFIG_DEBUG("Loading user config (strict=%s)", strict ? "true" : "false");
  asciichat_error_t user_result = config_load_and_apply(detected_mode, user_config_path, strict, opts);

  // Return user config result - errors in user config should be reported
  return user_result;
}
