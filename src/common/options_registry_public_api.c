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
#include <stdlib.h>

static void registry_flags_change(flags_context_t *context, const char *name, const flags_value_t *old_value, const flags_value_t *new_value, void *user_data) {
  (void)name;
  (void)old_value;
  const registry_entry_t *entry = user_data;
  if (!context || !entry || !new_value)
    return;
  if (entry->type == OPTION_TYPE_ACTION) {
    option_action_fn action = options_action_callback(entry->long_name, false);
    if (action)
      action();
    return;
  }
  if (!entry->parse_fn)
    return;
  void *storage = flags_context_storage(context);
  if (!storage)
    return;
  char *error = NULL;
  entry->parse_fn(new_value->string, (char *)storage + entry->offset, &error);
  free(error);
}

static bool registry_flags_parse(flags_context_t *context, const char *text, flags_value_t *value, void *user_data) {
  const registry_entry_t *entry = user_data;
  if (!context || !entry || !text || !value || !entry->parse_fn)
    return false;
  void *storage = flags_context_storage(context);
  if (!storage)
    return false;
  char *error = NULL;
  bool parsed = entry->parse_fn(text, (char *)storage + entry->offset, &error);
  free(error);
  if (!parsed)
    return false;
  value->string = text;
  return true;
}

asciichat_error_t options_registry_add_all_to_flags_builder(flags_schema_builder_t *builder, option_mode_bitmask_t mode_mask) {
  if (!builder)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Flags builder is NULL");
  registry_init_size();
  for (size_t i = 0; i < g_registry_size; ++i) {
    const registry_entry_t *entry = &g_options_registry[i];
    if (!entry->long_name || (entry->mode_bitmask != OPTION_MODE_NONE && !(entry->mode_bitmask & mode_mask) && !(entry->mode_bitmask & OPTION_MODE_BINARY)))
      continue;
    flags_value_type_t type;
    flags_option_behavior_t behavior = FLAGS_BEHAVIOR_VALUE;
    switch (entry->type) {
    case OPTION_TYPE_BOOL:
      type = FLAGS_TYPE_BOOL;
      break;
    case OPTION_TYPE_INT:
      type = FLAGS_TYPE_INT64;
      break;
    case OPTION_TYPE_DOUBLE:
      type = FLAGS_TYPE_DOUBLE;
      break;
    case OPTION_TYPE_STRING:
      type = FLAGS_TYPE_STRING;
      break;
    case OPTION_TYPE_CALLBACK:
      type = FLAGS_TYPE_CUSTOM;
      break;
    case OPTION_TYPE_ACTION:
      type = FLAGS_TYPE_CUSTOM;
      behavior = FLAGS_BEHAVIOR_ACTION;
      break;
    default:
      continue;
    }
    flags_option_spec_t option = {.name = entry->long_name,
                                  .short_name = entry->short_name,
                                  .description = entry->help_text,
                                  .group = entry->group,
                                  .required = entry->required,
                                  .metavar = entry->arg_placeholder,
                                  .environment_name = entry->env_var_name,
                                  .type = type,
                                  .behavior = behavior,
                                  .default_operation = entry->type == OPTION_TYPE_BOOL ? FLAGS_VALUE_TOGGLE : FLAGS_VALUE_ASSIGN,
                                  .argument_requirement = entry->type == OPTION_TYPE_BOOL || entry->type == OPTION_TYPE_ACTION ? FLAGS_ARGUMENT_NONE
                                                          : entry->optional_arg                                                ? FLAGS_ARGUMENT_OPTIONAL
                                                                                                                               : FLAGS_ARGUMENT_REQUIRED,
                                  .storage_offset = entry->offset,
                                  .storage_size = entry->type == OPTION_TYPE_STRING   ? sizeof(char *)
                                                  : entry->type == OPTION_TYPE_DOUBLE ? sizeof(double)
                                                  : entry->type == OPTION_TYPE_BOOL   ? sizeof(bool)
                                                                                      : sizeof(int),
                                  .default_value = entry->default_value,
                                  .enum_values = entry->metadata.enum_values,
                                  .enum_value_count = 0,
                                  .dynamic_string = entry->owns_memory,
                                  .parse = entry->type == OPTION_TYPE_CALLBACK ? registry_flags_parse : NULL,
                                  .validation_regex = entry->metadata.validation_pattern,
                                  .on_change = entry->type == OPTION_TYPE_ACTION || (entry->type == OPTION_TYPE_STRING && entry->parse_fn) ? registry_flags_change : NULL,
                                  .user_data = (void *)entry};
    if (entry->type == OPTION_TYPE_CALLBACK || entry->type == OPTION_TYPE_ACTION)
      option.storage_size = 0;
    if (entry->metadata.enum_values) {
      while (entry->metadata.enum_values[option.enum_value_count])
        ++option.enum_value_count;
    }
    if (!flags_schema_builder_add_option(builder, &option))
      return SET_ERRNO(ERROR_MEMORY, "Failed to add %s to libflags schema", entry->long_name);
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

  // Count enum values (NULL-terminated array)
  size_t enum_count = 0;
  while (meta->enum_values[enum_count] != NULL) {
    enum_count++;
  }

  *count = enum_count;
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
