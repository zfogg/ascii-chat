/**
 * @file public_api.c
 * @brief Public API functions for the options registry
 * @ingroup options
 *
 * This file contains all the public-facing functions that external code
 * uses to interact with the options registry. These functions are declared
 * in ascii-chat/options/registry.h.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/registry/common.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/actions.h>
#include <string.h>

asciichat_error_t options_registry_add_all_to_builder(options_builder_t *builder) {
  if (!builder) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Builder is NULL");
  }

  registry_init_size();

  for (size_t i = 0; i < g_registry_size; i++) {
    const registry_entry_t *entry = &g_options_registry[i];
    if (!entry->long_name) {
      continue;
    }
    // Silent - no debug logging needed

    switch (entry->type) {
    case OPTION_TYPE_STRING:
      options_builder_add_string(builder, entry->long_name, entry->short_name, entry->offset,
                                 entry->default_value ? (const char *)entry->default_value : "", entry->help_text,
                                 entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_INT:
      // Use metadata-aware function if metadata is present
      if (entry->metadata.numeric_range.max != 0 || entry->metadata.enum_count > 0) {
        options_builder_add_int_with_metadata(builder, entry->long_name, entry->short_name, entry->offset,
                                              entry->default_value ? *(const int *)entry->default_value : 0,
                                              entry->help_text, entry->group, entry->required, entry->env_var_name,
                                              entry->validate_fn, &entry->metadata);
      } else {
        options_builder_add_int(builder, entry->long_name, entry->short_name, entry->offset,
                                entry->default_value ? *(const int *)entry->default_value : 0, entry->help_text,
                                entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      }
      break;
    case OPTION_TYPE_BOOL:
      options_builder_add_bool(builder, entry->long_name, entry->short_name, entry->offset,
                               entry->default_value ? *(const bool *)entry->default_value : false, entry->help_text,
                               entry->group, entry->required, entry->env_var_name);
      break;
    case OPTION_TYPE_DOUBLE:
      // Use metadata-aware function if metadata is present (has numeric range)
      if (entry->metadata.numeric_range.max != 0) {
        options_builder_add_double_with_metadata(builder, entry->long_name, entry->short_name, entry->offset,
                                                 entry->default_value ? *(const double *)entry->default_value : 0.0,
                                                 entry->help_text, entry->group, entry->required, entry->env_var_name,
                                                 entry->validate_fn, &entry->metadata);
      } else {
        options_builder_add_double(builder, entry->long_name, entry->short_name, entry->offset,
                                   entry->default_value ? *(const double *)entry->default_value : 0.0, entry->help_text,
                                   entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      }
      break;
    case OPTION_TYPE_CALLBACK:
      // Always use metadata-aware function to preserve enum/metadata information
      options_builder_add_callback_with_metadata(builder, entry->long_name, entry->short_name, entry->offset,
                                                 entry->default_value, entry->default_value_size, entry->parse_fn,
                                                 entry->help_text, entry->group, entry->required, entry->env_var_name,
                                                 entry->optional_arg, &entry->metadata);
      break;
    case OPTION_TYPE_ACTION:
      // Actions are now registered as options with help text
      // Look up the corresponding action function based on option name
      if (strcmp(entry->long_name, "list-webcams") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_webcams, entry->help_text,
                                   entry->group);
      } else if (strcmp(entry->long_name, "list-microphones") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_microphones,
                                   entry->help_text, entry->group);
      } else if (strcmp(entry->long_name, "list-speakers") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_list_speakers, entry->help_text,
                                   entry->group);
      } else if (strcmp(entry->long_name, "show-capabilities") == 0) {
        options_builder_add_action(builder, entry->long_name, entry->short_name, action_show_capabilities,
                                   entry->help_text, entry->group);
      } else if (strcmp(entry->long_name, "help") == 0 || strcmp(entry->long_name, "version") == 0) {
        // Help and version are handled specially in options.c, just add them for help display
        // They don't have actual action functions - pass a dummy one
        options_builder_add_action(builder, entry->long_name, entry->short_name, NULL, entry->help_text, entry->group);
      }
      break;
    }

    // Set mode bitmask on the last added descriptor
    options_builder_set_mode_bitmask(builder, entry->mode_bitmask);

    // Set custom arg_placeholder if defined
    if (entry->arg_placeholder) {
      options_builder_set_arg_placeholder(builder, entry->arg_placeholder);
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Get raw access to registry for completions filtering
 *
 * Returns a pointer to the internal registry array. The array is NULL-terminated
 * (final entry has long_name == NULL). Used by completions generators.
 *
 * @return Pointer to registry array (read-only), or NULL on error
 */
const registry_entry_t *options_registry_get_raw(void) {
  registry_init_size();
  return g_options_registry;
}

/**
 * @brief Get total number of registry entries
 *
 * @return Number of options in registry (not including NULL terminator)
 */
size_t options_registry_get_count(void) {
  registry_init_size();
  return g_registry_size;
}

const option_descriptor_t *options_registry_find_by_name(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  registry_init_size();

  const registry_entry_t *entry = registry_find_entry_by_name(long_name);
  if (!entry) {
    // Don't log error for binary-level options like "config" that aren't in registry
    if (strcmp(long_name, "config") != 0) {
      SET_ERRNO(ERROR_NOT_FOUND, "Option not found: %s", long_name);
    }
    return NULL;
  }

  /* Create descriptor from registry entry */
  static option_descriptor_t desc;
  desc.long_name = entry->long_name;
  desc.short_name = entry->short_name;
  desc.type = entry->type;
  desc.offset = entry->offset;
  desc.help_text = entry->help_text;
  desc.group = entry->group;
  desc.hide_from_mode_help = false;
  desc.hide_from_binary_help = false;
  desc.default_value = entry->default_value;
  desc.required = entry->required;
  desc.env_var_name = entry->env_var_name;
  desc.validate = entry->validate_fn;
  desc.parse_fn = entry->parse_fn;
  desc.action_fn = NULL;
  desc.owns_memory = entry->owns_memory;
  desc.optional_arg = entry->optional_arg;
  desc.mode_bitmask = entry->mode_bitmask;

  return &desc;
}

const option_descriptor_t *options_registry_find_by_short(char short_name) {
  if (short_name == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Short name is empty");
    return NULL;
  }

  registry_init_size();

  const registry_entry_t *entry = registry_find_entry_by_short(short_name);
  if (!entry) {
    SET_ERRNO(ERROR_NOT_FOUND, "Option not found: -%c", short_name);
    return NULL;
  }

  /* Create descriptor from registry entry */
  static option_descriptor_t desc;
  desc.long_name = entry->long_name;
  desc.short_name = entry->short_name;
  desc.type = entry->type;
  desc.offset = entry->offset;
  desc.help_text = entry->help_text;
  desc.group = entry->group;
  desc.hide_from_mode_help = false;
  desc.hide_from_binary_help = false;
  desc.default_value = entry->default_value;
  desc.required = entry->required;
  desc.env_var_name = entry->env_var_name;
  desc.validate = entry->validate_fn;
  desc.parse_fn = entry->parse_fn;
  desc.action_fn = NULL;
  desc.owns_memory = entry->owns_memory;
  desc.optional_arg = entry->optional_arg;
  desc.mode_bitmask = entry->mode_bitmask;

  return &desc;
}

const option_descriptor_t *options_registry_get_for_mode(asciichat_mode_t mode, size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Number of options is NULL");
    return NULL;
  }

  registry_init_size();

  /* Convert mode to bitmask */
  option_mode_bitmask_t mode_bitmask = 0;
  switch (mode) {
  case MODE_SERVER:
    mode_bitmask = OPTION_MODE_SERVER;
    break;
  case MODE_CLIENT:
    mode_bitmask = OPTION_MODE_CLIENT;
    break;
  case MODE_MIRROR:
    mode_bitmask = OPTION_MODE_MIRROR;
    break;
  case MODE_DISCOVERY_SERVICE:
    mode_bitmask = OPTION_MODE_DISCOVERY_SVC;
    break;
  case MODE_DISCOVERY:
    mode_bitmask = OPTION_MODE_DISCOVERY;
    break;
  default:
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid mode: %d", mode);
    *num_options = 0;
    return NULL;
  }

  /* Count matching options */
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & mode_bitmask) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  /* Allocate array for matching options */
  option_descriptor_t *filtered = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!filtered) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to allocate filtered options array");
    *num_options = 0;
    return NULL;
  }

  /* Copy matching options */
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & mode_bitmask) {
      filtered[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return filtered;
}

const option_descriptor_t *options_registry_get_binary_options(size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Number of options is NULL");
    return NULL;
  }

  registry_init_size();

  /* Count binary-level options */
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & OPTION_MODE_BINARY) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  /* Allocate array for binary options */
  option_descriptor_t *binary_opts = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!binary_opts) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to allocate binary options array");
    *num_options = 0;
    return NULL;
  }

  /* Copy binary options */
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (g_options_registry[i].mode_bitmask & OPTION_MODE_BINARY) {
      binary_opts[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return binary_opts;
}

const option_descriptor_t *options_registry_get_for_display(asciichat_mode_t mode, bool for_binary_help,
                                                            size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "num_options is NULL");
    return NULL;
  }

  registry_init_size();

  // Count matching options
  size_t count = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (registry_entry_applies_to_mode(&g_options_registry[i], mode, for_binary_help)) {
      count++;
    }
  }

  if (count == 0) {
    *num_options = 0;
    return NULL;
  }

  // Allocate array
  option_descriptor_t *descriptors = SAFE_MALLOC(count * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!descriptors) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate descriptors array");
    *num_options = 0;
    return NULL;
  }

  // Copy matching options
  size_t idx = 0;
  for (size_t i = 0; i < g_registry_size; i++) {
    if (registry_entry_applies_to_mode(&g_options_registry[i], mode, for_binary_help)) {
      descriptors[idx++] = registry_entry_to_descriptor(&g_options_registry[i]);
    }
  }

  *num_options = count;
  return descriptors;
}

// ============================================================================
// Completion Metadata Access (Phase 3 Implementation)
// ============================================================================

const option_metadata_t *options_registry_get_metadata(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  // Look up option in registry and return its metadata
  registry_init_size();
  for (size_t i = 0; i < g_registry_size; i++) {
    const registry_entry_t *entry = &g_options_registry[i];
    if (entry->long_name && strcmp(entry->long_name, long_name) == 0) {
      // Return the metadata from the registry entry
      return &entry->metadata;
    }
  }

  // If not found, return empty metadata
  static option_metadata_t empty_metadata = {0};
  return &empty_metadata;
}

const char **options_registry_get_enum_values(const char *option_name, const char ***descriptions, size_t *count) {
  if (!option_name || !count) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option name is NULL or count is NULL");
    if (count)
      *count = 0;
    return NULL;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || meta->input_type != OPTION_INPUT_ENUM || !meta->enum_values || meta->enum_values[0] == NULL) {
    SET_ERRNO(ERROR_NOT_FOUND, "Option '%s' not found", option_name);
    *count = 0;
    if (descriptions)
      *descriptions = NULL;
    return NULL;
  }

  *count = meta->enum_count;
  if (descriptions) {
    *descriptions = meta->enum_descriptions;
  }
  return meta->enum_values;
}

bool options_registry_get_numeric_range(const char *option_name, int *min_out, int *max_out, int *step_out) {
  if (!option_name || !min_out || !max_out || !step_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option name is NULL or min_out, max_out, or step_out is NULL");
    return false;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || meta->input_type != OPTION_INPUT_NUMERIC) {
    *min_out = 0;
    *max_out = 0;
    *step_out = 0;
    return false;
  }

  *min_out = meta->numeric_range.min;
  *max_out = meta->numeric_range.max;
  *step_out = meta->numeric_range.step;
  return true;
}

const char **options_registry_get_examples(const char *option_name, size_t *count) {
  if (!option_name || !count) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option name is NULL or count is NULL");
    if (count)
      *count = 0;
    return NULL;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || !meta->examples || meta->examples[0] == NULL) {
    *count = 0;
    return NULL;
  }

  // Count examples by finding NULL terminator
  size_t example_count = 0;
  for (size_t i = 0; meta->examples[i] != NULL; i++) {
    example_count++;
  }

  *count = example_count;
  return meta->examples;
}

option_input_type_t options_registry_get_input_type(const char *option_name) {
  if (!option_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option name is NULL");
    return OPTION_INPUT_NONE;
  }

  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta) {
    SET_ERRNO(ERROR_NOT_FOUND, "Option '%s' not found", option_name);
    return OPTION_INPUT_NONE;
  }

  return meta->input_type;
}
