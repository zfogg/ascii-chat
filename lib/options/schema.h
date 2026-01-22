/**
 * @file schema.h
 * @brief Schema metadata for config file options
 * @ingroup options
 *
 * This module provides declarative metadata for all configurable options
 * that can appear in TOML configuration files. The schema drives the
 * generic config parser, eliminating duplicate validation code.
 *
 * @note This is separate from builder.h's option_descriptor_t which is
 *       for CLI parsing. This schema is specifically for TOML config files.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "common.h"
#include "options/options.h"

/**
 * @brief Context where an option can appear
 */
typedef enum {
  OPTION_CONTEXT_CLI,    ///< CLI-only option (cannot appear in config)
  OPTION_CONTEXT_CONFIG, ///< Config-only option (never on CLI)
  OPTION_CONTEXT_BOTH,   ///< Can appear in both CLI and config
} option_context_t;

/**
 * @brief Option value type for config files
 */
typedef enum {
  OPTION_TYPE_STRING, ///< String value (stored in char array)
  OPTION_TYPE_INT,    ///< Integer value
  OPTION_TYPE_BOOL,   ///< Boolean value
  OPTION_TYPE_FLOAT,  ///< Floating point value
  OPTION_TYPE_DOUBLE, ///< Double precision floating point
  OPTION_TYPE_ENUM,   ///< Enum value (mapped to int internally, e.g., color_mode)
} config_option_type_t;

/**
 * @brief Validation function signature for config options
 *
 * @param value_str String value from TOML (may need parsing)
 * @param opts Full options struct (for cross-field validation if needed)
 * @param is_client True if client mode
 * @param parsed_value Output buffer for parsed value (type-specific)
 * @param error_msg Output buffer for error message
 * @param error_msg_size Size of error_msg buffer
 * @return 0 on success, -1 on error
 *
 * Note: For some validators, parsed_value may be unused (e.g., port validation
 * just checks format, doesn't return parsed value). The actual value is written
 * directly to opts via field_offset.
 */
typedef int (*config_validate_fn_t)(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                     char *error_msg, size_t error_msg_size);

/**
 * @brief Option metadata for config file parsing
 *
 * Describes a single configurable option with its TOML key, validation,
 * and storage location in options_t.
 */
typedef struct {
  const char *toml_key;      ///< TOML key path (e.g., "network.port", "client.address")
  const char *cli_flag;      ///< CLI flag name (e.g., "--port") or NULL if no CLI flag
  config_option_type_t type; ///< Value type
  option_context_t context;  ///< Where this option can appear
  const char *category;      ///< Category name (e.g., "network", "client", "audio")
  size_t field_offset;       ///< offsetof(options_t, field) - where to store value
  size_t field_size;         ///< Size of field in options_t

  // Validation
  config_validate_fn_t validate_fn; ///< Validation function (can be NULL for simple types)

  // Mode restrictions
  bool is_client_only;  ///< Only used in client mode
  bool is_server_only; ///< Only used in server mode

  // Documentation
  const char *description; ///< Description for docs/help generation

  // Type-specific constraints (optional)
  union {
    struct {
      int min;
      int max;
    } int_range;
    struct {
      double min;
      double max;
    } float_range;
  } constraints;
} config_option_metadata_t;

/**
 * @brief Get option metadata by TOML key
 * @param toml_key TOML key path (e.g., "network.port")
 * @return Pointer to metadata or NULL if not found
 */
const config_option_metadata_t *config_schema_get_by_toml_key(const char *toml_key);

/**
 * @brief Get all option metadata for a category
 * @param category Category name (e.g., "network")
 * @param count Output: number of options in category
 * @return Array of metadata pointers (NULL-terminated)
 */
const config_option_metadata_t **config_schema_get_by_category(const char *category, size_t *count);

/**
 * @brief Get all option metadata
 * @param count Output: total number of options
 * @return Array of all metadata (NULL-terminated)
 */
const config_option_metadata_t *config_schema_get_all(size_t *count);
