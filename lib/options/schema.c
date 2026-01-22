/**
 * @file schema.c
 * @brief Schema metadata registry for config file options
 * @ingroup options
 */

#include "options/schema.h"
#include "options/validation.h"
#include "options/options.h"
#include "options/builder.h"
#include "options/presets.h"
#include "util/path.h"
#include "video/palette.h"
#include "platform/terminal.h"
#include "platform/system.h"
#include "common.h"

#include <string.h>
#include <limits.h>
#include <ctype.h>

// ============================================================================
// Validation Wrapper Functions
// ============================================================================

/**
 * @brief Wrapper for port validation
 */
static int validate_port_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                 char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  (void)parsed_value;
  return validate_opt_port(value_str, error_msg, error_msg_size);
}

/**
 * @brief Wrapper for IP address validation
 */
static int validate_ip_address_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                       char *error_msg, size_t error_msg_size) {
  (void)opts;
  char *parsed_addr = (char *)parsed_value;
  if (!parsed_addr) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Internal error: parsed_value buffer is NULL");
    }
    return -1;
  }
  // parsed_value should be a buffer of OPTIONS_BUFF_SIZE
  return validate_opt_ip_address(value_str, parsed_addr, OPTIONS_BUFF_SIZE, is_client, error_msg, error_msg_size);
}

/**
 * @brief Wrapper for positive integer validation
 */
static int validate_positive_int_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                         char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_positive_int(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for non-negative integer validation
 */
static int validate_non_negative_int_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                             char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_non_negative_int(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for device index validation
 */
static int validate_device_index_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                         char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_device_index(value_str, error_msg, error_msg_size);
  if (val != INT_MIN && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for color mode validation
 */
static int validate_color_mode_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                       char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_color_mode(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for render mode validation
 */
static int validate_render_mode_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                        char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_render_mode(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for palette validation
 */
static int validate_palette_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                    char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_palette(value_str, error_msg, error_msg_size);
  if (val >= 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for FPS validation
 */
static int validate_fps_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  int *result = (int *)parsed_value;
  int val = validate_opt_fps(value_str, error_msg, error_msg_size);
  if (val > 0 && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for float validation
 */
static int validate_float_non_negative_wrapper(const char *value_str, options_t *opts, bool is_client,
                                               void *parsed_value, char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  float *result = (float *)parsed_value;
  float val = validate_opt_float_non_negative(value_str, error_msg, error_msg_size);
  if (val >= 0.0f && result) {
    *result = val;
    return 0;
  }
  return -1;
}

/**
 * @brief Wrapper for password validation
 */
static int validate_password_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                     char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  (void)parsed_value;
  return validate_opt_password(value_str, error_msg, error_msg_size);
}

/**
 * @brief Wrapper for path/identifier validation (for keys, log files, etc.)
 * This validates that the value is a non-empty string. Path normalization
 * happens in the parser based on path role.
 */
static int validate_path_wrapper(const char *value_str, options_t *opts, bool is_client, void *parsed_value,
                                 char *error_msg, size_t error_msg_size) {
  (void)opts;
  (void)is_client;
  if (!value_str || strlen(value_str) == 0) {
    if (error_msg) {
      SAFE_SNPRINTF(error_msg, error_msg_size, "Value is required");
    }
    return -1;
  }
  // Copy to parsed_value buffer for parser to use
  char *normalized = (char *)parsed_value;
  if (normalized) {
    SAFE_STRNCPY(normalized, value_str, OPTIONS_BUFF_SIZE);
  }
  return 0;
}

// ============================================================================
// Option Metadata Registry
// ============================================================================
// Schema is now built dynamically from options builder configs.
// See config_schema_build_from_configs() below.

// ============================================================================
// Dynamic Schema Storage
// ============================================================================

static config_option_metadata_t *g_dynamic_schema = NULL;
static size_t g_dynamic_schema_count = 0;
static bool g_schema_built = false;
static char **g_dynamic_strings = NULL; // Storage for allocated strings (toml_key, cli_flag, category)
static size_t g_dynamic_strings_count = 0;
static size_t g_dynamic_strings_capacity = 0;

// ============================================================================
// Helper Functions for Dynamic Schema Building
// ============================================================================

/**
 * @brief Convert string to lowercase
 */
static void str_tolower(char *str) {
  if (!str) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid string for str_tolower");
    return;
  }
  for (; *str; str++) {
    *str = (char)tolower((unsigned char)*str);
  }
}

/**
 * @brief Convert dashes to underscores in a string (in-place)
 */
static void dashes_to_underscores(char *str) {
  if (!str) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid string for dashes_to_underscores");
    return;
  }
  for (; *str; str++) {
    if (*str == '-') {
      *str = '_';
    }
  }
}

/**
 * @brief Generate TOML key from group and field name
 * Format: "group.field_name" (both lowercase, underscores)
 */
static char *generate_toml_key(const char *group, const char *field_name, char *buffer, size_t buffer_size) {
  if (!group || !field_name || !buffer || buffer_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments for generate_toml_key");
    return NULL;
  }

  // Copy group and convert to lowercase
  size_t group_len = strlen(group);
  if (group_len >= buffer_size) {
    SET_ERRNO(ERROR_CONFIG, "Group name is too long for buffer");
    return NULL;
  }
  memcpy(buffer, group, group_len + 1);
  str_tolower(buffer);

  // Append dot
  if (group_len + 1 >= buffer_size) {
    SET_ERRNO(ERROR_CONFIG, "Group name is too long for buffer");
    return NULL;
  }
  buffer[group_len] = '.';

  // Copy field name, convert dashes to underscores, and lowercase
  size_t field_len = strlen(field_name);
  if (group_len + 1 + field_len >= buffer_size) {
    SET_ERRNO(ERROR_INVALID_STATE, "Field name is too long for buffer");
    return NULL;
  }
  memcpy(buffer + group_len + 1, field_name, field_len + 1);
  dashes_to_underscores(buffer + group_len + 1);
  str_tolower(buffer + group_len + 1);

  return buffer;
}

/**
 * @brief Generate CLI flag from long_name
 * Format: "--long-name"
 */
static char *generate_cli_flag(const char *long_name, char *buffer, size_t buffer_size) {
  if (!long_name || !buffer || buffer_size < 3) { // Need at least "--" + 1 char
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments for generate_cli_flag");
    return NULL;
  }

  buffer[0] = '-';
  buffer[1] = '-';
  size_t name_len = strlen(long_name);
  if (name_len + 2 >= buffer_size) {
    SET_ERRNO(ERROR_CONFIG, "Long name is too long for buffer");
    return NULL;
  }
  memcpy(buffer + 2, long_name, name_len + 1);
  return buffer;
}

/**
 * @brief Get field size from offset and type
 */
static size_t get_field_size(option_type_t type, size_t offset) {
  // We can't determine field size from offset alone, so we'll use sizeof() on a dummy struct
  // For now, use reasonable defaults based on type
  switch (type) {
  case OPTION_TYPE_BOOL:
    return sizeof(bool);
  case OPTION_TYPE_INT:
    // Check if this is an enum field by offset
    if (offset == offsetof(options_t, color_mode)) {
      return sizeof(terminal_color_mode_t);
    } else if (offset == offsetof(options_t, render_mode)) {
      return sizeof(render_mode_t);
    } else if (offset == offsetof(options_t, palette_type)) {
      return sizeof(palette_type_t);
    }
    return sizeof(int);
  case OPTION_TYPE_DOUBLE:
    // Check field_size at runtime to distinguish float from double
    // For now, return double size (will be checked when writing)
    return sizeof(double);
  case OPTION_TYPE_STRING:
    // String fields are typically OPTIONS_BUFF_SIZE
    return OPTIONS_BUFF_SIZE;
  default:
    return sizeof(int); // Safe default
  }
}

// Validation wrapper functions removed - we use builder's validate functions directly

// ============================================================================
// Dynamic Schema Building
// ============================================================================

/**
 * @brief Helper function to check if descriptor should be added to schema
 */
static bool should_add_descriptor(const option_descriptor_t *desc, const option_descriptor_t **existing, size_t count) {
  if (!desc || desc->type == OPTION_TYPE_ACTION || desc->type == OPTION_TYPE_CALLBACK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid descriptor");
    return false; // Skip actions and callbacks
  }

  // Check if we already have this exact offset
  for (size_t i = 0; i < count; i++) {
    if (existing[i] && existing[i]->offset == desc->offset) {
      return false; // Already have this offset
    }
  }
  return true;
}

/**
 * @brief Helper to allocate and store a string for the dynamic schema
 */
static char *store_dynamic_string(const char *str) {
  if (!str) {
    return NULL;
  }

  // Grow string storage if needed
  if (g_dynamic_strings_count >= g_dynamic_strings_capacity) {
    size_t new_capacity = g_dynamic_strings_capacity == 0 ? 64 : g_dynamic_strings_capacity * 2;
    char **new_strings = SAFE_REALLOC(g_dynamic_strings, new_capacity * sizeof(char *), char **);
    if (!new_strings) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate new strings");
      return NULL;
    }
    g_dynamic_strings = new_strings;
    g_dynamic_strings_capacity = new_capacity;
  }

  // Allocate and copy the string
  char *stored = platform_strdup(str);
  if (!stored) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate new string");
    return NULL;
  }

  g_dynamic_strings[g_dynamic_strings_count++] = stored;
  return stored;
}

asciichat_error_t config_schema_build_from_configs(const options_config_t **configs, size_t num_configs) {
  // Free existing dynamic schema if any
  if (g_dynamic_schema) {
    SAFE_FREE(g_dynamic_schema);
    g_dynamic_schema = NULL;
    g_dynamic_schema_count = 0;
  }

  // Free existing string storage
  if (g_dynamic_strings) {
    for (size_t i = 0; i < g_dynamic_strings_count; i++) {
      SAFE_FREE(g_dynamic_strings[i]);
    }
    SAFE_FREE(g_dynamic_strings);
    g_dynamic_strings = NULL;
    g_dynamic_strings_count = 0;
    g_dynamic_strings_capacity = 0;
  }

  // Count total unique descriptors (by offset) across all configs
  // Use a simple approach: collect all descriptors, then deduplicate by offset
  const option_descriptor_t *all_descriptors[256] = {0}; // Max expected options
  size_t descriptor_count = 0;

  // Collect descriptors from all configs
  for (size_t cfg_idx = 0; cfg_idx < num_configs; cfg_idx++) {
    const options_config_t *config = configs[cfg_idx];
    if (!config) {
      continue;
    }

    for (size_t i = 0; i < config->num_descriptors && descriptor_count < 256; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      if (should_add_descriptor(desc, all_descriptors, descriptor_count)) {
        all_descriptors[descriptor_count++] = desc;
      }
    }
  }

  // Allocate schema array
  g_dynamic_schema = SAFE_MALLOC(descriptor_count * sizeof(config_option_metadata_t), config_option_metadata_t *);
  if (!g_dynamic_schema) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate dynamic schema");
  }

  // Build schema entries
  g_dynamic_schema_count = 0;
  char toml_key_buffer[256];
  char cli_flag_buffer[256];
  char category_buffer[64];

  for (size_t i = 0; i < descriptor_count; i++) {
    const option_descriptor_t *desc = all_descriptors[i];
    if (!desc || !desc->long_name || !desc->group) {
      continue;
    }

    config_option_metadata_t *meta = &g_dynamic_schema[g_dynamic_schema_count++];

    // Use builder's type directly
    meta->type = desc->type;

    // Generate category: lowercase the group name from builder
    strncpy(category_buffer, desc->group, sizeof(category_buffer) - 1);
    category_buffer[sizeof(category_buffer) - 1] = '\0';
    str_tolower(category_buffer);

    // Generate TOML key: "category.field_name" (category is lowercase group, field_name is long_name with
    // dashes->underscores)
    if (!generate_toml_key(category_buffer, desc->long_name, toml_key_buffer, sizeof(toml_key_buffer))) {
      g_dynamic_schema_count--; // Skip this entry
      continue;
    }

    // Store generated strings (allocate and store)
    meta->toml_key = store_dynamic_string(toml_key_buffer);
    meta->category = store_dynamic_string(category_buffer);
    if (!meta->toml_key || !meta->category) {
      // Failed to allocate strings, skip this entry
      g_dynamic_schema_count--;
      continue;
    }

    // Generate CLI flag: "--long-name"
    if (generate_cli_flag(desc->long_name, cli_flag_buffer, sizeof(cli_flag_buffer))) {
      meta->cli_flag = store_dynamic_string(cli_flag_buffer);
      if (!meta->cli_flag) {
        // Failed to allocate, but continue (cli_flag can be NULL)
      }
    } else {
      meta->cli_flag = NULL;
    }

    // Set other fields
    meta->context = OPTION_CONTEXT_BOTH; // Most options can appear in both CLI and config
    meta->field_offset = desc->offset;
    meta->field_size = get_field_size(meta->type, desc->offset);
    // Use builder's validate function directly - it receives the full options struct
    meta->validate_fn = desc->validate;
    meta->description = desc->help_text;

    // Determine mode restrictions by checking which configs contain this option (by offset)
    bool found_in_configs[5] = {false}; // Track which configs contain this option
    // Config order: server, client, mirror, acds, discovery

    for (size_t cfg_idx = 0; cfg_idx < num_configs && cfg_idx < 5; cfg_idx++) {
      const options_config_t *config = configs[cfg_idx];
      if (!config) {
        continue;
      }

      for (size_t j = 0; j < config->num_descriptors; j++) {
        if (config->descriptors[j].offset == desc->offset) {
          found_in_configs[cfg_idx] = true;
          break;
        }
      }
    }

    // Determine restrictions:
    // - Client-only: appears in client/mirror/discovery (indices 1,2,4) but NOT in server/acds (indices 0,3)
    // - Server-only: appears in server/acds (indices 0,3) but NOT in client/mirror/discovery (indices 1,2,4)
    bool in_client_modes = (num_configs > 1 && found_in_configs[1]) || (num_configs > 2 && found_in_configs[2]) ||
                           (num_configs > 4 && found_in_configs[4]);
    bool in_server_modes = (num_configs > 0 && found_in_configs[0]) || (num_configs > 3 && found_in_configs[3]);

    meta->is_client_only = in_client_modes && !in_server_modes;
    meta->is_server_only = in_server_modes && !in_client_modes;

    // Initialize constraints
    memset(&meta->constraints, 0, sizeof(meta->constraints));
  }

  g_schema_built = true;
  return ASCIICHAT_OK;
}

// ============================================================================
// Lookup Functions
// ============================================================================

const config_option_metadata_t *config_schema_get_by_toml_key(const char *toml_key) {
  if (!toml_key) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments for config_schema_get_by_toml_key");
    return NULL;
  }

  // Schema must be built before use
  if (!g_schema_built || !g_dynamic_schema) {
    SET_ERRNO(ERROR_INVALID_STATE, "Schema not built");
    return NULL;
  }

  for (size_t i = 0; i < g_dynamic_schema_count; i++) {
    if (g_dynamic_schema[i].toml_key && strcmp(g_dynamic_schema[i].toml_key, toml_key) == 0) {
      return &g_dynamic_schema[i];
    }
  }

  return NULL;
}

const config_option_metadata_t **config_schema_get_by_category(const char *category, size_t *count) {
  static const config_option_metadata_t *results[64]; // Max options per category
  size_t result_count = 0;

  if (!category) {
    if (count) {
      *count = 0;
    }
    return NULL;
  }

  // Schema must be built before use
  if (!g_schema_built || !g_dynamic_schema) {
    if (count) {
      *count = 0;
    }
    SET_ERRNO(ERROR_INVALID_STATE, "Schema not built");
    return NULL;
  }

  for (size_t i = 0; i < g_dynamic_schema_count && result_count < 64; i++) {
    if (g_dynamic_schema[i].category && strcmp(g_dynamic_schema[i].category, category) == 0) {
      results[result_count++] = &g_dynamic_schema[i];
    }
  }

  if (count) {
    *count = result_count;
  }

  return (result_count > 0) ? results : NULL;
}

const config_option_metadata_t *config_schema_get_all(size_t *count) {
  // Schema must be built before use
  if (!g_schema_built || !g_dynamic_schema) {
    if (count) {
      *count = 0;
    }
    SET_ERRNO(ERROR_INVALID_STATE, "Schema not built");
    return NULL;
  }

  if (count) {
    *count = g_dynamic_schema_count;
  }
  return g_dynamic_schema;
}
