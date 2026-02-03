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
#include "../common.h"
#include "../options/options.h"
#include "../options/builder.h"

/**
 * @brief Context where an option can appear
 */
typedef enum {
  OPTION_CONTEXT_CLI,    ///< CLI-only option (cannot appear in config)
  OPTION_CONTEXT_CONFIG, ///< Config-only option (never on CLI)
  OPTION_CONTEXT_BOTH,   ///< Can appear in both CLI and config
} option_context_t;

/**
 * @brief Validation function from options builder
 *
 * This is the same validation function signature used by the options builder.
 * It receives the full options struct and can perform cross-field validation.
 *
 * @param options_struct Full options struct (cast to void* for generic use)
 * @param error_msg Output: error message (allocated by validator, caller frees)
 * @return true if valid, false if invalid
 */
typedef bool (*builder_validate_fn_t)(const void *options_struct, char **error_msg);

/**
 * @brief Option metadata for config file parsing
 *
 * Describes a single configurable option with its TOML key, validation,
 * and storage location in options_t.
 */
typedef struct {
  const char *toml_key;     ///< TOML key path (e.g., "network.port", "client.address")
  const char *cli_flag;     ///< CLI flag name (e.g., "--port") or NULL if no CLI flag
  option_type_t type;       ///< Value type (from builder)
  option_context_t context; ///< Where this option can appear
  const char *category;     ///< Category name (e.g., "network", "client", "audio")
  size_t field_offset;      ///< offsetof(options_t, field) - where to store value
  size_t field_size;        ///< Size of field in options_t

  // Validation - uses builder's validate function directly
  builder_validate_fn_t validate_fn; ///< Builder's validation function (can be NULL for simple types)

  // Custom parsing for OPTION_TYPE_CALLBACK
  bool (*parse_fn)(const char *arg, void *dest, char **error_msg); ///< Custom parser for callbacks (or NULL)

  // Mode applicability bitmask
  option_mode_bitmask_t mode_bitmask; ///< Which modes this option applies to

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

/**
 * @brief Build schema dynamically from options builder configs
 *
 * This function builds the config schema by merging all mode configs (server, client, mirror, etc.).
 * It generates TOML keys, CLI flags, categories, and types from the builder's option descriptors.
 *
 * @param configs Array of option configs (can contain NULL entries)
 * @param num_configs Number of configs in the array
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @note This should be called once during initialization before any config parsing
 */
asciichat_error_t config_schema_build_from_configs(const options_config_t **configs, size_t num_configs);

/**
 * @brief Clean up dynamically allocated schema resources
 *
 * Frees all memory allocated by config_schema_build_from_configs(),
 * including the dynamic schema array and all associated strings.
 * Safe to call multiple times or if schema was never built.
 *
 * @note This should be called during shutdown to prevent memory leaks
 */
void config_schema_cleanup(void);
