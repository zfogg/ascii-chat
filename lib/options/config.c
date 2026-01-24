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
#include "platform/stat.h"
#include "platform/fs.h"
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

/**
 * @name Internal Macros
 * @{
 */

/**
 * @brief Print configuration warning using the logging system
 *
 * Uses log_warn so that config warnings respect the --quiet flag and
 * are routed through the logging system for proper filtering.
 */
#define CONFIG_WARN(fmt, ...)                                                                                          \
  do {                                                                                                                 \
    log_warn("Config file: " fmt, ##__VA_ARGS__);                                                                      \
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

// ============================================================================
// Type Handler Registry - Consolidates 4 duplicated switch statements
// ============================================================================
// TECHNICAL DEBT FIX: The code previously had 4 separate switch(meta->type)
// blocks for extract, parse, write, and format operations. This consolidated
// registry eliminates ~300 lines of code duplication while maintaining all
// type-specific logic and special case handling.

/**
 * @brief Union holding all possible parsed option values
 */
typedef union {
  char str_value[OPTIONS_BUFF_SIZE]; // STRING type
  int int_value;                     // INT type
  bool bool_value;                   // BOOL type
  float float_value;                 // DOUBLE type (float variant)
  double double_value;               // DOUBLE type (double variant)
} option_parsed_value_t;

/**
 * @brief Type handler - encapsulates all 4 operations for one option type
 *
 * Each operation is a stage in the config parsing pipeline:
 * 1. Extract: TOML datum → intermediate (value_str, int_val, etc)
 * 2. Parse: intermediate → validated parsed value
 * 3. Write: parsed value → struct field
 * 4. Format: struct field → TOML output string
 */
typedef struct {
  void (*extract)(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                  bool *has_value);
  asciichat_error_t (*parse_validate)(const char *value_str, const config_option_metadata_t *meta,
                                      option_parsed_value_t *parsed, char *error_msg, size_t error_size);
  asciichat_error_t (*write_to_struct)(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                       options_t *opts, char *error_msg, size_t error_size);
  void (*format_output)(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                        size_t bufsize);
} option_type_handler_t;

// Forward declarations
static void extract_string(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                           bool *has_value);
static void extract_int(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                        bool *has_value);
static void extract_bool(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                         bool *has_value);
static void extract_double(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                           bool *has_value);
static asciichat_error_t parse_validate_string(const char *value_str, const config_option_metadata_t *meta,
                                               option_parsed_value_t *parsed, char *error_msg, size_t error_size);
static asciichat_error_t parse_validate_int(const char *value_str, const config_option_metadata_t *meta,
                                            option_parsed_value_t *parsed, char *error_msg, size_t error_size);
static asciichat_error_t parse_validate_bool(const char *value_str, const config_option_metadata_t *meta,
                                             option_parsed_value_t *parsed, char *error_msg, size_t error_size);
static asciichat_error_t parse_validate_double(const char *value_str, const config_option_metadata_t *meta,
                                               option_parsed_value_t *parsed, char *error_msg, size_t error_size);
static asciichat_error_t write_string(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                      options_t *opts, char *error_msg, size_t error_size);
static asciichat_error_t write_int(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                   options_t *opts, char *error_msg, size_t error_size);
static asciichat_error_t write_bool(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                    options_t *opts, char *error_msg, size_t error_size);
static asciichat_error_t write_double(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                      options_t *opts, char *error_msg, size_t error_size);
static void format_string(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                          size_t bufsize);
static void format_int(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                       size_t bufsize);
static void format_bool(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                        size_t bufsize);
static void format_double(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                          size_t bufsize);

// Handler registry - indexed by option_type_t
static const option_type_handler_t g_type_handlers[] = {
    [OPTION_TYPE_STRING] = {extract_string, parse_validate_string, write_string, format_string},
    [OPTION_TYPE_INT] = {extract_int, parse_validate_int, write_int, format_int},
    [OPTION_TYPE_BOOL] = {extract_bool, parse_validate_bool, write_bool, format_bool},
    [OPTION_TYPE_DOUBLE] = {extract_double, parse_validate_double, write_double, format_double},
    [OPTION_TYPE_CALLBACK] = {NULL, NULL, NULL, NULL},
    [OPTION_TYPE_ACTION] = {NULL, NULL, NULL, NULL},
};

// ============================================================================
// Type Handler Implementations
// ============================================================================

/**
 * @brief Extract STRING value from TOML datum
 */
static void extract_string(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                           bool *has_value) {
  (void)int_val;
  (void)bool_val;
  (void)double_val;

  if (datum.type == TOML_STRING) {
    const char *str = get_toml_string_validated(datum);
    if (str && strlen(str) > 0) {
      SAFE_STRNCPY(value_str, str, BUFFER_SIZE_MEDIUM);
      *has_value = true;
    }
  } else if (datum.type == TOML_INT64) {
    // Convert integer to string (e.g., port = 7777)
    SAFE_SNPRINTF(value_str, BUFFER_SIZE_MEDIUM, "%lld", (long long)datum.u.int64);
    *has_value = true;
  }
}

/**
 * @brief Extract INT value from TOML datum
 */
static void extract_int(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                        bool *has_value) {
  (void)bool_val;
  (void)double_val;

  if (datum.type == TOML_INT64) {
    *int_val = (int)datum.u.int64;
    SAFE_SNPRINTF(value_str, BUFFER_SIZE_MEDIUM, "%d", *int_val);
    *has_value = true;
  } else if (datum.type == TOML_STRING) {
    const char *str = get_toml_string_validated(datum);
    if (str) {
      SAFE_STRNCPY(value_str, str, BUFFER_SIZE_MEDIUM);
      *has_value = true;
    }
  }
}

/**
 * @brief Extract BOOL value from TOML datum
 */
static void extract_bool(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                         bool *has_value) {
  (void)int_val;
  (void)double_val;

  if (datum.type == TOML_BOOLEAN) {
    *bool_val = datum.u.boolean;
    // Also set value_str for parse_validate phase
    SAFE_STRNCPY(value_str, *bool_val ? "true" : "false", BUFFER_SIZE_MEDIUM);
    *has_value = true;
  }
}

/**
 * @brief Extract DOUBLE value from TOML datum
 */
static void extract_double(toml_datum_t datum, char *value_str, int *int_val, bool *bool_val, double *double_val,
                           bool *has_value) {
  (void)int_val;
  (void)bool_val;

  if (datum.type == TOML_FP64) {
    *double_val = datum.u.fp64;
    SAFE_SNPRINTF(value_str, BUFFER_SIZE_MEDIUM, "%.10g", *double_val);
    *has_value = true;
  } else if (datum.type == TOML_STRING) {
    const char *str = get_toml_string_validated(datum);
    if (str) {
      SAFE_STRNCPY(value_str, str, BUFFER_SIZE_MEDIUM);
      *has_value = true;
    }
  }
}

/**
 * @brief Parse and validate STRING value
 */
static asciichat_error_t parse_validate_string(const char *value_str, const config_option_metadata_t *meta,
                                               option_parsed_value_t *parsed, char *error_msg, size_t error_size) {
  (void)meta;
  (void)error_msg;
  (void)error_size;

  SAFE_STRNCPY(parsed->str_value, value_str, sizeof(parsed->str_value));
  return ASCIICHAT_OK;
}

/**
 * @brief Parse and validate INT value
 */
static asciichat_error_t parse_validate_int(const char *value_str, const config_option_metadata_t *meta,
                                            option_parsed_value_t *parsed, char *error_msg, size_t error_size) {
  // Detect enum fields by checking field_offset
  int enum_val = -1;
  bool is_enum = false;

  if (meta->field_offset == offsetof(options_t, color_mode)) {
    enum_val = validate_opt_color_mode(value_str, error_msg, error_size);
    is_enum = true;
  } else if (meta->field_offset == offsetof(options_t, render_mode)) {
    enum_val = validate_opt_render_mode(value_str, error_msg, error_size);
    is_enum = true;
  } else if (meta->field_offset == offsetof(options_t, palette_type)) {
    enum_val = validate_opt_palette(value_str, error_msg, error_size);
    is_enum = true;
  }

  if (is_enum) {
    // Enum parsing
    if (enum_val < 0) {
      if (strlen(error_msg) == 0) {
        SAFE_SNPRINTF(error_msg, error_size, "Invalid enum value: %s", value_str);
      }
      return ERROR_CONFIG;
    }
    parsed->int_value = enum_val;
  } else {
    // Regular integer parsing
    char *endptr = NULL;
    long parsed_val = strtol(value_str, &endptr, 10);
    if (*endptr != '\0') {
      SAFE_SNPRINTF(error_msg, error_size, "Invalid integer: %s", value_str);
      return ERROR_CONFIG;
    }
    if (parsed_val < INT_MIN || parsed_val > INT_MAX) {
      SAFE_SNPRINTF(error_msg, error_size, "Integer out of range: %s", value_str);
      return ERROR_CONFIG;
    }
    parsed->int_value = (int)parsed_val;
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Parse and validate BOOL value
 */
static asciichat_error_t parse_validate_bool(const char *value_str, const config_option_metadata_t *meta,
                                             option_parsed_value_t *parsed, char *error_msg, size_t error_size) {
  (void)meta;
  (void)error_msg;
  (void)error_size;

  // Parse boolean from string representation ("true" or "false")
  if (value_str && (strcmp(value_str, "true") == 0 || strcmp(value_str, "1") == 0 || strcmp(value_str, "yes") == 0)) {
    parsed->bool_value = true;
  } else {
    parsed->bool_value = false;
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Parse and validate DOUBLE value
 */
static asciichat_error_t parse_validate_double(const char *value_str, const config_option_metadata_t *meta,
                                               option_parsed_value_t *parsed, char *error_msg, size_t error_size) {
  (void)meta;

  char *endptr = NULL;
  double parsed_val = strtod(value_str, &endptr);
  if (*endptr != '\0') {
    SAFE_SNPRINTF(error_msg, error_size, "Invalid float: %s", value_str);
    return ERROR_CONFIG;
  }
  parsed->float_value = (float)parsed_val;
  return ASCIICHAT_OK;
}

/**
 * @brief Write STRING value to struct field
 */
static asciichat_error_t write_string(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                      options_t *opts, char *error_msg, size_t error_size) {
  (void)error_msg;
  (void)error_size;

  char *field_ptr = ((char *)opts) + meta->field_offset;
  const char *final_value = parsed->str_value;

  // Special handling for path-based options (keys, log files)
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
        return path_result;
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

  return ASCIICHAT_OK;
}

/**
 * @brief Write INT value to struct field
 */
static asciichat_error_t write_int(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                   options_t *opts, char *error_msg, size_t error_size) {
  (void)error_msg;
  (void)error_size;

  char *field_ptr = ((char *)opts) + meta->field_offset;

  // Handle unsigned short int fields (webcam_index)
  if (meta->field_size == sizeof(unsigned short int)) {
    *(unsigned short int *)field_ptr = (unsigned short int)parsed->int_value;
  } else {
    *(int *)field_ptr = parsed->int_value;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Write BOOL value to struct field
 */
static asciichat_error_t write_bool(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                    options_t *opts, char *error_msg, size_t error_size) {
  (void)error_msg;
  (void)error_size;

  char *field_ptr = ((char *)opts) + meta->field_offset;

  // Handle unsigned short int bool fields (common in options_t)
  if (meta->field_size == sizeof(unsigned short int)) {
    *(unsigned short int *)field_ptr = parsed->bool_value ? 1 : 0;
  } else {
    *(bool *)field_ptr = parsed->bool_value;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Write DOUBLE value to struct field
 */
static asciichat_error_t write_double(const option_parsed_value_t *parsed, const config_option_metadata_t *meta,
                                      options_t *opts, char *error_msg, size_t error_size) {
  (void)error_msg;
  (void)error_size;

  char *field_ptr = ((char *)opts) + meta->field_offset;

  // Check field_size to distinguish float from double
  // Use memcpy for alignment-safe writes
  if (meta->field_size == sizeof(float)) {
    float float_val = (float)parsed->float_value;
    memcpy(field_ptr, &float_val, sizeof(float));
  } else {
    double double_val = (double)parsed->float_value;
    memcpy(field_ptr, &double_val, sizeof(double));
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Format STRING value for TOML output
 */
static void format_string(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                          size_t bufsize) {
  (void)field_size;
  (void)meta;

  const char *str_value = (const char *)field_ptr;
  if (str_value && strlen(str_value) > 0) {
    SAFE_SNPRINTF(buf, bufsize, "\"%s\"", str_value);
  } else {
    SAFE_SNPRINTF(buf, bufsize, "\"\"");
  }
}

/**
 * @brief Format INT value for TOML output
 */
static void format_int(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                       size_t bufsize) {
  (void)meta;

  int int_value = 0;
  if (field_size == sizeof(unsigned short int)) {
    int_value = *(unsigned short int *)field_ptr;
  } else {
    int_value = *(int *)field_ptr;
  }
  SAFE_SNPRINTF(buf, bufsize, "%d", int_value);
}

/**
 * @brief Format BOOL value for TOML output
 */
static void format_bool(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                        size_t bufsize) {
  (void)meta;

  bool bool_value = false;
  if (field_size == sizeof(unsigned short int)) {
    bool_value = *(unsigned short int *)field_ptr != 0;
  } else {
    bool_value = *(bool *)field_ptr;
  }
  SAFE_SNPRINTF(buf, bufsize, "%s", bool_value ? "true" : "false");
}

/**
 * @brief Format DOUBLE value for TOML output
 */
static void format_double(const char *field_ptr, size_t field_size, const config_option_metadata_t *meta, char *buf,
                          size_t bufsize) {
  (void)meta;

  if (field_size == sizeof(float)) {
    float float_value = 0.0f;
    memcpy(&float_value, field_ptr, sizeof(float));
    SAFE_SNPRINTF(buf, bufsize, "%.1f", (double)float_value);
  } else {
    double double_value = 0.0;
    memcpy(&double_value, field_ptr, sizeof(double));
    SAFE_SNPRINTF(buf, bufsize, "%.1f", double_value);
  }
}

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

    // Validate mode compatibility using mode_bitmask
    // If mode_bitmask is 0 or BINARY, option applies to all modes
    if (meta->mode_bitmask != 0 && !(meta->mode_bitmask & OPTION_MODE_BINARY)) {
      // Option has specific mode restrictions - check if current mode matches
      bool applies_to_mode = false;
      if (detected_mode >= 0 && detected_mode <= MODE_DISCOVERY) {
        option_mode_bitmask_t mode_bit = (1 << detected_mode);
        applies_to_mode = (meta->mode_bitmask & mode_bit) != 0;
      }

      if (!applies_to_mode) {
        log_debug("Config: Option '%s' is not supported for this mode (skipping)", meta->toml_key);
        if (strict) {
          return SET_ERRNO(ERROR_CONFIG, "Option '%s' is not supported for this mode", meta->toml_key);
        }
        continue;
      }
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

    // Extract value based on type using handler
    char value_str[BUFFER_SIZE_MEDIUM] = {0};
    bool has_value = false;
    int int_val = 0;
    bool bool_val = false;
    double double_val = 0.0;

    // Use type handler for extraction (consolidated from 5 switch cases)
    if (g_type_handlers[meta->type].extract) {
      g_type_handlers[meta->type].extract(datum, value_str, &int_val, &bool_val, &double_val, &has_value);
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

    // Parse and validate value using handler
    char error_msg[BUFFER_SIZE_SMALL] = {0};
    option_parsed_value_t parsed = {0};
    asciichat_error_t parse_result = ASCIICHAT_OK;

    // Use type handler for parsing/validation (consolidated from 5 switch cases)
    if (g_type_handlers[meta->type].parse_validate) {
      parse_result = g_type_handlers[meta->type].parse_validate(value_str, meta, &parsed, error_msg, sizeof(error_msg));
    } else if (meta->type != OPTION_TYPE_CALLBACK && meta->type != OPTION_TYPE_ACTION) {
      // Handler exists for all types except CALLBACK and ACTION
      parse_result = ERROR_CONFIG;
    } else {
      // Skip callback and action types (not loaded from config)
      continue;
    }

    if (parse_result != ASCIICHAT_OK) {
      CONFIG_WARN("Invalid %s value '%s': %s (skipping)", meta->toml_key, value_str, error_msg);
      if (strict) {
        if (first_error == ASCIICHAT_OK) {
          first_error = SET_ERRNO(ERROR_CONFIG, "Invalid %s: %s", meta->toml_key, error_msg);
        }
        continue;
      }
      continue;
    }

    // Write value to options_t using handler
    if (g_type_handlers[meta->type].write_to_struct) {
      char error_msg_write[BUFFER_SIZE_SMALL] = {0};
      asciichat_error_t write_result =
          g_type_handlers[meta->type].write_to_struct(&parsed, meta, opts, error_msg_write, sizeof(error_msg_write));
      if (write_result != ASCIICHAT_OK) {
        CONFIG_WARN("Failed to write %s: %s (skipping)", meta->toml_key, error_msg_write);
        if (strict) {
          if (first_error == ASCIICHAT_OK) {
            first_error = write_result;
          }
          continue;
        }
        continue;
      }
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

  // Handle password warning (check both crypto and security sections)
  toml_datum_t password = toml_seek(toptab, "crypto.password");
  if (password.type == TOML_UNKNOWN) {
    password = toml_seek(toptab, "security.password");
  }
  if (password.type == TOML_STRING) {
    const char *password_str = get_toml_string_validated(password);
    if (password_str && strlen(password_str) > 0) {
      CONFIG_WARN("Password stored in config file is insecure! Use CLI --password instead.");
    }
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
  // Only print if terminal output is enabled (suppress with --quiet)
  if (config_path && log_get_terminal_output()) {
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
      char error_buffer[BUFFER_SIZE_MEDIUM];
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
  // Only print if terminal output is enabled (suppress with --quiet)
  if (log_get_terminal_output()) {
    (void)fprintf(stderr, "Loaded configuration from: %s\n", display_path);
  }
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
  FILE *output_file = NULL;
  bool use_file = false;

  defer(SAFE_FREE(config_path_expanded));
  // Don't use defer for output_file - we'll handle it manually

  // Determine if we're writing to a file or stdout
  if (config_path && strlen(config_path) > 0) {
    // User provided a filepath - we must write to that file or error
    use_file = true;

    // Use custom path provided
    config_path_expanded = expand_path(config_path);
    if (!config_path_expanded) {
      // If expansion fails, try using as-is (might already be absolute)
      config_path_expanded = platform_strdup(config_path);
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
      // Use fprintf directly so prompts display even when logging is suppressed
      (void)fprintf(stderr, "Config file already exists: %s\n", config_path_expanded);

      bool overwrite = platform_prompt_yes_no("Overwrite", false); // Default to No
      if (!overwrite) {
        (void)fprintf(stderr, "Config file creation cancelled.\n");
        return SET_ERRNO(ERROR_CONFIG, "User cancelled overwrite");
      }

      // User confirmed overwrite - continue to create file (will overwrite existing)
      (void)fprintf(stderr, "Overwriting existing config file...\n");
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
      // Create directory recursively (handles both Windows and POSIX)
      asciichat_error_t mkdir_result = platform_mkdir_recursive(dir_path, DIR_PERM_PRIVATE);
      if (mkdir_result != ASCIICHAT_OK) {
        SAFE_FREE(dir_path);
        return mkdir_result;
      }
    }
    SAFE_FREE(dir_path);

    // Open file for writing
    output_file = platform_fopen(config_path_expanded, "w");
    if (!output_file) {
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create config file: %s", config_path_expanded);
    }
  } else {
    // No filepath provided - write to stdout
    output_file = stdout;
  }

  // Write version comment
  (void)fprintf(output_file, "# ascii-chat configuration file\n");
  (void)fprintf(output_file, "# Generated by ascii-chat v%d.%d.%d-%s\n", ASCII_CHAT_VERSION_MAJOR,
                ASCII_CHAT_VERSION_MINOR, ASCII_CHAT_VERSION_PATCH, ASCII_CHAT_GIT_VERSION);
  (void)fprintf(output_file, "#\n");
  (void)fprintf(output_file, "# If you upgrade ascii-chat and this version comment changes, you may need to\n");
  (void)fprintf(output_file, "# delete and regenerate this file with: ascii-chat --config-create\n");
  (void)fprintf(output_file, "#\n\n");

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
    (void)fprintf(output_file, "[%s]\n", category);

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
        (void)fprintf(output_file, "# %s\n", meta->description);
      }

      // Format and write the option value using handler
      if (g_type_handlers[meta->type].format_output) {
        char formatted_value[BUFFER_SIZE_MEDIUM] = {0};
        g_type_handlers[meta->type].format_output(field_ptr, meta->field_size, meta, formatted_value,
                                                  sizeof(formatted_value));
        (void)fprintf(output_file, "%s = %s\n", key_name, formatted_value);
      }

      written_flags[opt_idx] = true;
    }

    // Add blank line between sections
    (void)fprintf(output_file, "\n");
  }

  // Close file if we opened one (but not stdout)
  if (use_file && output_file) {
    SAFE_FCLOSE(output_file);
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
  char system_config_path[PLATFORM_MAX_PATH_LENGTH];
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
