/**
 * @file schema.c
 * @brief Schema metadata registry for config file options
 * @ingroup options
 */

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define OPTIONS_CONFIG_SCHEMA_CLI_FLAG_BUFFER_SIZE 256
#define OPTIONS_CONFIG_SCHEMA_CATEGORY_BUFFER_SIZE 64

/* Existing buffer budgets in bytes, including NUL for text storage. */
#define SCHEMA_OPTION_CAPACITY 256
#define SCHEMA_CATEGORY_OPTION_CAPACITY 64

/* Existing buffer budgets in bytes, including NUL for text storage. */
#define SCHEMA_INITIAL_STRING_CAPACITY 64

#include <ascii-chat/options/schema.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/options/validation.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/types.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/video/ascii/palette.h>
#include <libplatform/terminal.h>
#include <libplatform/system.h>
#include <ascii-chat/common.h>
#include <libflags/config/keys.h>

#include <string.h>
#include <limits.h>
#include <ctype.h>

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
 * @brief Get field size from offset and type
 */
static size_t get_field_size(option_type_t type, size_t offset) {
  // We can't determine field size from offset alone, so we'll use sizeof() on a dummy struct
  // For now, use reasonable defaults based on type
  switch (type) {
  case OPTION_TYPE_BOOL:
    return sizeof(bool);
  case OPTION_TYPE_INT:
    // Check if this is an unsigned short int field
    if (offset == offsetof(options_t, webcam_index)) {
      return sizeof(unsigned short int);
    }
    // Check if this is an enum field by offset
    if (offset == offsetof(options_t, color_mode)) {
      return sizeof(terminal_color_mode_t);
    } else if (offset == offsetof(options_t, render_mode)) {
      return sizeof(terminal_render_mode_t);
    } else if (offset == offsetof(options_t, palette_type)) {
      return sizeof(palette_type_t);
    }
    return sizeof(int);
  case OPTION_TYPE_DOUBLE:
    // Check if this is a float field instead of double
    if (offset == offsetof(options_t, microphone_sensitivity) || offset == offsetof(options_t, speakers_volume)) {
      return sizeof(float);
    }
    // Check field_size at runtime to distinguish float from double
    // For now, return double size (will be checked when writing)
    return sizeof(double);
  case OPTION_TYPE_STRING:
    // String fields are typically OPTIONS_BUFF_SIZE
    return OPTIONS_BUFF_SIZE;
  case OPTION_TYPE_CALLBACK:
    // CALLBACK options store into regular options_t fields; infer by offset.
    if (offset == offsetof(options_t, log_file) || offset == offsetof(options_t, port) || offset == offsetof(options_t, palette_custom) || offset == offsetof(options_t, yt_dlp_options)) {
      return OPTIONS_BUFF_SIZE;
    }
    if (offset == offsetof(options_t, media_seek_timestamp)) {
      return sizeof(double);
    }
    if (offset == offsetof(options_t, microphone_sensitivity) || offset == offsetof(options_t, speakers_volume)) {
      return sizeof(float);
    }
    if (offset == offsetof(options_t, verbose_level)) {
      return sizeof(unsigned short int);
    }
    return sizeof(int);
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
  if (!desc || desc->type == OPTION_TYPE_ACTION) {
    return false; // Skip actions (not an error, just filtering)
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
    size_t new_capacity = g_dynamic_strings_capacity == 0 ? SCHEMA_INITIAL_STRING_CAPACITY : g_dynamic_strings_capacity * 2;
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
  const option_descriptor_t *all_descriptors[SCHEMA_OPTION_CAPACITY] = {0}; // Max expected options
  size_t descriptor_count = 0;

  // Collect descriptors from all configs
  for (size_t cfg_idx = 0; cfg_idx < num_configs; cfg_idx++) {
    const options_config_t *config = configs[cfg_idx];
    if (!config) {
      log_debug("Config %zu is NULL, skipping", cfg_idx);
      continue;
    }

    if (!config->descriptors || config->num_descriptors == 0) {
      log_debug("Config %zu has no descriptors", cfg_idx);
      continue;
    }

    for (size_t i = 0; i < config->num_descriptors && descriptor_count < SCHEMA_OPTION_CAPACITY; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      if (!desc) {
        log_warn("Descriptor at index %zu in config %zu is NULL", i, cfg_idx);
        continue;
      }
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
  char toml_key_buffer[BUFFER_SIZE_SMALL];
  char cli_flag_buffer[OPTIONS_CONFIG_SCHEMA_CLI_FLAG_BUFFER_SIZE];
  char category_buffer[OPTIONS_CONFIG_SCHEMA_CATEGORY_BUFFER_SIZE];

  for (size_t i = 0; i < descriptor_count; i++) {
    const option_descriptor_t *desc = all_descriptors[i];
    if (!desc || !desc->long_name || !desc->group) {
      continue;
    }

    config_option_metadata_t *meta = &g_dynamic_schema[g_dynamic_schema_count++];
    // Zero-initialize the metadata struct to ensure all fields are initialized
    memset(meta, 0, sizeof(*meta));

    // Use builder's type directly
    meta->type = desc->type;

    // Generate category: lowercase the group name from builder
    SAFE_STRNCPY(category_buffer, desc->group, sizeof(category_buffer) - 1);
    category_buffer[sizeof(category_buffer) - 1] = '\0';
    str_tolower(category_buffer);

    // Generate TOML key: "category.field_name" (category is lowercase group, field_name is long_name with
    // dashes->underscores)
    const char *field_name = desc->long_name;
    if (strcmp(category_buffer, "otel") == 0 && strncmp(field_name, "otel-", 5) == 0)
      field_name += 5;
    if (!generate_toml_key(category_buffer, field_name, toml_key_buffer, sizeof(toml_key_buffer))) {
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
    if (flags_config_make_cli_flag(desc->long_name, cli_flag_buffer, sizeof(cli_flag_buffer))) {
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
    // Use builder's parse function for CALLBACK types
    meta->parse_fn = desc->parse_fn;
    meta->description = desc->help_text;

    // Set mode_bitmask from option descriptor
    // The descriptor should have mode_bitmask set from the registry
    meta->mode_bitmask = desc->mode_bitmask;

    // Copy mode_default_getter function pointer
    meta->mode_default_getter = desc->mode_default_getter;

    // Copy constraints from descriptor's metadata
    // For integer types, copy numeric_range to int_range (always copy, even if min is 0)
    memset(&meta->constraints, 0, sizeof(meta->constraints));
    // Always copy numeric_range if descriptor has it (check max or min)
    if (desc && desc->metadata.numeric_range.max > 0) {
      meta->constraints.int_range.min = desc->metadata.numeric_range.min;
      meta->constraints.int_range.max = desc->metadata.numeric_range.max;
    }
  }

  g_schema_built = true;
  return ASCIICHAT_OK;
}

asciichat_error_t config_schema_build_from_registry(void) {
  registry_init_size();
  static option_descriptor_t descriptors[OPTIONS_REGISTRY_CAPACITY];
  memset(descriptors, 0, sizeof(descriptors));
  for (size_t i = 0; i < g_registry_size; ++i)
    descriptors[i] = registry_entry_to_descriptor(&g_options_registry[i]);
  options_config_t registry_config = {.descriptors = descriptors, .num_descriptors = g_registry_size};
  const options_config_t *configs[] = {&registry_config};
  return config_schema_build_from_configs(configs, 1);
}

// ============================================================================
// Lookup Functions
// ============================================================================

const config_option_metadata_t *config_schema_get_by_toml_key(const char *toml_key) {
  if (!toml_key) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid arguments");
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
  static const config_option_metadata_t *results[SCHEMA_CATEGORY_OPTION_CAPACITY]; // Max options per category
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

  for (size_t i = 0; i < g_dynamic_schema_count && result_count < SCHEMA_CATEGORY_OPTION_CAPACITY; i++) {
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

// ============================================================================
// Schema Cleanup
// ============================================================================

void config_schema_destroy(void) {
  if (!g_schema_built) {
    return; // Nothing to clean up
  }

  // Free all dynamically allocated strings (toml_key, cli_flag, category)
  if (g_dynamic_strings) {
    for (size_t i = 0; i < g_dynamic_strings_count; i++) {
      SAFE_FREE(g_dynamic_strings[i]);
    }
    SAFE_FREE(g_dynamic_strings);
    g_dynamic_strings = NULL;
    g_dynamic_strings_count = 0;
    g_dynamic_strings_capacity = 0;
  }

  // Free the schema array itself
  if (g_dynamic_schema) {
    SAFE_FREE(g_dynamic_schema);
    g_dynamic_schema = NULL;
    g_dynamic_schema_count = 0;
  }

  g_schema_built = false;
}
