/**
 * @file builder.c
 * @brief Implementation of options builder API
 * @ingroup options
 */

#include "internal.h"
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/common.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/options/layout.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/string.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

// Use platform utilities
#include <ascii-chat/platform/util.h>
#include <ascii-chat/platform/terminal.h>
#define asprintf platform_asprintf

// ============================================================================
// ============================================================================
// Help Formatting Helper Functions
// ============================================================================

/**
 * @brief Get help placeholder string for an option type
 * @param desc Option descriptor
 * @return Pointer to string literal ("NUM", "STR", "VAL"), custom placeholder, or empty string
 */
const char *get_option_help_placeholder_str(const option_descriptor_t *desc) {
  if (!desc) {
    return "";
  }
  // Check for custom placeholder first
  if (desc->arg_placeholder != NULL) {
    return desc->arg_placeholder;
  }
  return options_get_type_placeholder(desc->type);
}

/**
 * @brief Format option default value as a string
 * @param desc Option descriptor
 * @param buf Output buffer
 * @param bufsize Size of buffer
 * @return Number of characters written to buf
 */
int format_option_default_value_str(const option_descriptor_t *desc, char *buf, size_t bufsize) {
  if (!desc || !buf || bufsize == 0) {
    return 0;
  }

  // For callback options with enums, look up the enum string by matching the default value
  if (desc->type == OPTION_TYPE_CALLBACK && desc->metadata.enum_values && desc->default_value) {
    int default_int_val = 0;
    memcpy(&default_int_val, desc->default_value, sizeof(int));

    // Try to find matching enum value
    if (desc->metadata.enum_integer_values) {
      for (size_t i = 0; i < desc->metadata.enum_count; i++) {
        if (desc->metadata.enum_integer_values[i] == default_int_val) {
          return safe_snprintf(buf, bufsize, "%s", desc->metadata.enum_values[i]);
        }
      }
    } else {
      // Fallback: assume sequential 0-based indices if integer values not provided
      if (default_int_val >= 0 && (size_t)default_int_val < desc->metadata.enum_count) {
        return safe_snprintf(buf, bufsize, "%s", desc->metadata.enum_values[default_int_val]);
      }
    }
  }

  // For callback options storing numeric types (int/double/float), format them as numbers
  if (desc->type == OPTION_TYPE_CALLBACK && desc->default_value && desc->metadata.enum_values == NULL) {
    // Check if this is a numeric callback by looking for numeric_range metadata
    // Numeric callbacks have min/max range constraints set (max != 0 indicates a range was set)
    if (desc->metadata.numeric_range.max != 0 || desc->metadata.numeric_range.min != 0) {
      // This is a numeric callback option - extract and format the value
      // Determine if it's int or double based on range (int max is 2147483647)
      double default_double = 0.0;
      if (desc->metadata.numeric_range.max <= 2147483647.0 && desc->metadata.numeric_range.min >= -2147483648.0) {
        // Integer-ranged value (like port 1-65535) - stored as int
        int default_int = 0;
        memcpy(&default_int, desc->default_value, sizeof(int));
        default_double = (double)default_int;
      } else {
        // Double-ranged value - stored as double
        memcpy(&default_double, desc->default_value, sizeof(double));
      }

      // Format with appropriate precision (remove trailing zeros for integers)
      char formatted[32];
      safe_snprintf(formatted, sizeof(formatted), "%.1f", default_double);

      // Remove trailing zeros after decimal point
      char *dot = strchr(formatted, '.');
      if (dot) {
        char *end = formatted + strlen(formatted) - 1;
        while (end > dot && *end == '0') {
          end--;
        }
        if (*end == '.') {
          *end = '\0';
        } else {
          *(end + 1) = '\0';
        }
      }

      return safe_snprintf(buf, bufsize, "%s", formatted);
    }
  }

  // For callback options with string defaults (no enum, no numeric range)
  // Treat default_value as a const char* string pointer
  if (desc->type == OPTION_TYPE_CALLBACK && desc->default_value && desc->metadata.enum_values == NULL &&
      desc->metadata.numeric_range.max == 0 && desc->metadata.numeric_range.min == 0) {
    const char *str_default = (const char *)desc->default_value;
    if (str_default && str_default[0] != '\0') {
      return safe_snprintf(buf, bufsize, "%s", str_default);
    }
  }

  return options_format_default_value(desc->type, desc->default_value, buf, bufsize);
}

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * @brief Check if an option applies to a specific mode using bitmask
 * @param desc Option descriptor
 * @param mode Mode to check (use invalid value like -1 for binary help)
 * @param for_binary_help If true, show binary options; if false, hide them
 * @return true if option should be shown for this mode
 */
bool option_applies_to_mode(const option_descriptor_t *desc, asciichat_mode_t mode, bool for_binary_help) {
  if (!desc) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Descriptor is NULL");
    return false;
  }

  // When for_binary_help is true (i.e., for 'ascii-chat --help'),
  // show options that apply to the default mode (DISCOVERY) or binary-level options only.
  // Don't show mode-specific options for other modes (server, client, mirror, discovery-service).
  if (for_binary_help) {
    // Binary-level options or options that apply to discovery mode (the default)
    option_mode_bitmask_t default_modes = OPTION_MODE_BINARY | OPTION_MODE_DISCOVERY;
    return (desc->mode_bitmask & default_modes) != 0 && !desc->hide_from_binary_help;
  }

  // For mode-specific help (non-discovery modes), show only options for that mode.
  // Do not show binary options here unless it also specifically applies to the mode.
  // The 'mode' parameter is the actual mode (e.g., MODE_CLIENT, MODE_SERVER).
  if (mode < 0 || mode > MODE_DISCOVERY) { // Use MODE_INVALID as upper bound for valid modes
    return false;
  }
  option_mode_bitmask_t mode_bit = (1 << mode);

  // Check if it's a binary option. If so, only show if it also explicitly applies to this mode.
  if ((desc->mode_bitmask & OPTION_MODE_BINARY) && !(desc->mode_bitmask & mode_bit)) {
    return false; // Binary options not shown in mode-specific help unless also mode-specific
  }

  bool applies = (desc->mode_bitmask & mode_bit) != 0;

  return applies && !desc->hide_from_mode_help;
}

/**
 * @brief Grow descriptor array if needed
 */
void ensure_descriptor_capacity(options_builder_t *builder) {
  if (builder->num_descriptors >= builder->descriptor_capacity) {
    size_t new_capacity = builder->descriptor_capacity * 2;
    option_descriptor_t *new_descriptors =
        SAFE_REALLOC(builder->descriptors, new_capacity * sizeof(option_descriptor_t), option_descriptor_t *);
    if (!new_descriptors) {
      log_fatal("Failed to reallocate descriptors array");
      return;
    }
    builder->descriptors = new_descriptors;
    builder->descriptor_capacity = new_capacity;
  }
}

/**
 * @brief Grow dependency array if needed
 */
void ensure_dependency_capacity(options_builder_t *builder) {
  if (builder->num_dependencies >= builder->dependency_capacity) {
    size_t new_capacity = builder->dependency_capacity * 2;
    option_dependency_t *new_dependencies =
        SAFE_REALLOC(builder->dependencies, new_capacity * sizeof(option_dependency_t), option_dependency_t *);
    if (!new_dependencies) {
      log_fatal("Failed to reallocate dependencies array");
      return;
    }
    builder->dependencies = new_dependencies;
    builder->dependency_capacity = new_capacity;
  }
}

/**
 * @brief Grow positional arg array if needed
 */
void ensure_positional_arg_capacity(options_builder_t *builder) {
  if (builder->num_positional_args >= builder->positional_arg_capacity) {
    size_t new_capacity = builder->positional_arg_capacity * 2;
    if (new_capacity == 0)
      new_capacity = INITIAL_POSITIONAL_ARG_CAPACITY;

    positional_arg_descriptor_t *new_positional = SAFE_REALLOC(
        builder->positional_args, new_capacity * sizeof(positional_arg_descriptor_t), positional_arg_descriptor_t *);
    if (!new_positional) {
      log_fatal("Failed to reallocate positional_args array");
      return;
    }
    builder->positional_args = new_positional;
    builder->positional_arg_capacity = new_capacity;
  }
}

/**
 * @brief Grow usage line array if needed
 */
static void ensure_usage_line_capacity(options_builder_t *builder) {
  if (builder->num_usage_lines >= builder->usage_line_capacity) {
    size_t new_capacity = builder->usage_line_capacity * 2;
    if (new_capacity == 0)
      new_capacity = INITIAL_DESCRIPTOR_CAPACITY;

    usage_descriptor_t *new_usage =
        SAFE_REALLOC(builder->usage_lines, new_capacity * sizeof(usage_descriptor_t), usage_descriptor_t *);
    if (!new_usage) {
      log_fatal("Failed to reallocate usage_lines array");
      return;
    }
    builder->usage_lines = new_usage;
    builder->usage_line_capacity = new_capacity;
  }
}

/**
 * @brief Grow examples array if needed
 */
static void ensure_example_capacity(options_builder_t *builder) {
  if (builder->num_examples >= builder->example_capacity) {
    size_t new_capacity = builder->example_capacity * 2;
    if (new_capacity == 0)
      new_capacity = INITIAL_DESCRIPTOR_CAPACITY;

    example_descriptor_t *new_examples =
        SAFE_REALLOC(builder->examples, new_capacity * sizeof(example_descriptor_t), example_descriptor_t *);
    if (!new_examples) {
      log_fatal("Failed to reallocate examples array");
      return;
    }
    builder->examples = new_examples;
    builder->example_capacity = new_capacity;
  }
}

/**
 * @brief Grow modes array if needed
 */
static void ensure_mode_capacity(options_builder_t *builder) {
  if (builder->num_modes >= builder->mode_capacity) {
    size_t new_capacity = builder->mode_capacity * 2;
    if (new_capacity == 0)
      new_capacity = INITIAL_DESCRIPTOR_CAPACITY;

    help_mode_descriptor_t *new_modes =
        SAFE_REALLOC(builder->modes, new_capacity * sizeof(help_mode_descriptor_t), help_mode_descriptor_t *);
    if (!new_modes) {
      log_fatal("Failed to reallocate modes array");
      return;
    }
    builder->modes = new_modes;
    builder->mode_capacity = new_capacity;
  }
}

static void ensure_custom_section_capacity(options_builder_t *builder) {
  if (builder->num_custom_sections >= builder->custom_section_capacity) {
    size_t new_capacity = builder->custom_section_capacity * 2;
    if (new_capacity == 0)
      new_capacity = INITIAL_DESCRIPTOR_CAPACITY;

    custom_section_descriptor_t *new_sections = SAFE_REALLOC(
        builder->custom_sections, new_capacity * sizeof(custom_section_descriptor_t), custom_section_descriptor_t *);
    if (!new_sections) {
      log_fatal("Failed to reallocate custom sections array");
      return;
    }
    builder->custom_sections = new_sections;
    builder->custom_section_capacity = new_capacity;
  }
}

/**
 * @brief Find option descriptor by long name
 */
const option_descriptor_t *find_option(const options_config_t *config, const char *long_name) {
  for (size_t i = 0; i < config->num_descriptors; i++) {
    if (strcmp(config->descriptors[i].long_name, long_name) == 0) {
      return &config->descriptors[i];
    }
  }
  return NULL;
}

/**
 * @brief Check if an option is set (has non-default value)
 */
bool is_option_set(const options_config_t *config, const void *options_struct, const char *option_name) {
  const option_descriptor_t *desc = find_option(config, option_name);
  if (!desc)
    return false;

  const char *base = (const char *)options_struct;
  const void *field = base + desc->offset;

  // Use handler registry to check if option is set
  if (desc->type >= 0 && desc->type < (int)(NUM_OPTION_TYPES)) {
    if (g_builder_handlers[desc->type].is_set) {
      return g_builder_handlers[desc->type].is_set(field, desc);
    }
  }

  return false;
}

// ============================================================================
// Builder Lifecycle
// ============================================================================

options_builder_t *options_builder_create(size_t struct_size) {
  options_builder_t *builder = SAFE_MALLOC(sizeof(options_builder_t), options_builder_t *);
  if (!builder) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate options builder");
    return NULL;
  }

  builder->descriptors = SAFE_MALLOC(INITIAL_DESCRIPTOR_CAPACITY * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!builder->descriptors) {
    free(builder);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate descriptors array");
    return NULL;
  }

  builder->dependencies = SAFE_MALLOC(INITIAL_DEPENDENCY_CAPACITY * sizeof(option_dependency_t), option_dependency_t *);
  if (!builder->dependencies) {
    SAFE_FREE(builder->descriptors);
    SAFE_FREE(builder);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate dependencies array");
    return NULL;
  }

  builder->num_descriptors = 0;
  builder->descriptor_capacity = INITIAL_DESCRIPTOR_CAPACITY;
  builder->num_dependencies = 0;
  builder->dependency_capacity = INITIAL_DEPENDENCY_CAPACITY;
  builder->positional_args = NULL;
  builder->num_positional_args = 0;
  builder->positional_arg_capacity = 0;
  builder->usage_lines = NULL;
  builder->num_usage_lines = 0;
  builder->usage_line_capacity = 0;
  builder->examples = NULL;
  builder->num_examples = 0;
  builder->example_capacity = 0;
  builder->modes = NULL;
  builder->num_modes = 0;
  builder->mode_capacity = 0;
  builder->custom_sections = NULL;
  builder->num_custom_sections = 0;
  builder->custom_section_capacity = 0;
  builder->struct_size = struct_size;
  builder->program_name = NULL;
  builder->description = NULL;
  builder->owned_strings_builder = NULL;
  builder->num_owned_strings_builder = 0;
  builder->owned_strings_builder_capacity = 0;

  return builder;
}

options_builder_t *options_builder_from_preset(const options_config_t *preset) {
  if (!preset) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Preset config is NULL");
    return NULL;
  }

  options_builder_t *builder = options_builder_create(preset->struct_size);
  if (!builder) {
    return NULL;
  }

  builder->program_name = preset->program_name;
  builder->description = preset->description;

  // Copy all descriptors
  for (size_t i = 0; i < preset->num_descriptors; i++) {
    options_builder_add_descriptor(builder, &preset->descriptors[i]);
  }

  // Copy all dependencies
  for (size_t i = 0; i < preset->num_dependencies; i++) {
    options_builder_add_dependency(builder, &preset->dependencies[i]);
  }

  // Copy all positional arguments
  for (size_t i = 0; i < preset->num_positional_args; i++) {
    const positional_arg_descriptor_t *pos_arg = &preset->positional_args[i];
    options_builder_add_positional(builder, pos_arg->name, pos_arg->help_text, pos_arg->required,
                                   pos_arg->section_heading, pos_arg->examples, pos_arg->num_examples,
                                   pos_arg->mode_bitmask, pos_arg->parse_fn);
  }

  return builder;
}

void options_builder_destroy(options_builder_t *builder) {
  if (!builder)
    return;

  SAFE_FREE(builder->descriptors);
  SAFE_FREE(builder->dependencies);
  SAFE_FREE(builder->positional_args);
  SAFE_FREE(builder->usage_lines);
  SAFE_FREE(builder->examples);
  SAFE_FREE(builder->modes);
  SAFE_FREE(builder->custom_sections);
  SAFE_FREE(builder->owned_strings_builder); // Free the builder's owned strings
  SAFE_FREE(builder);
}

options_config_t *options_builder_build(options_builder_t *builder) {
  if (!builder) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Builder is NULL");
    return NULL;
  }

  options_config_t *config = SAFE_MALLOC(sizeof(options_config_t), options_config_t *);
  if (!config) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate options config");
    return NULL;
  }

  // Allocate and copy descriptors
  config->descriptors = SAFE_MALLOC(builder->num_descriptors * sizeof(option_descriptor_t), option_descriptor_t *);
  if (!config->descriptors && builder->num_descriptors > 0) {
    SAFE_FREE(config);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate descriptors");
    return NULL;
  }
  memcpy(config->descriptors, builder->descriptors, builder->num_descriptors * sizeof(option_descriptor_t));
  config->num_descriptors = builder->num_descriptors;

  // Allocate and copy dependencies
  config->dependencies = SAFE_MALLOC(builder->num_dependencies * sizeof(option_dependency_t), option_dependency_t *);
  if (!config->dependencies && builder->num_dependencies > 0) {
    SAFE_FREE(config->descriptors);
    SAFE_FREE(config);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate dependencies");
    return NULL;
  }
  memcpy(config->dependencies, builder->dependencies, builder->num_dependencies * sizeof(option_dependency_t));
  config->num_dependencies = builder->num_dependencies;

  // Allocate and copy positional args
  if (builder->num_positional_args > 0) {
    config->positional_args =
        SAFE_MALLOC(builder->num_positional_args * sizeof(positional_arg_descriptor_t), positional_arg_descriptor_t *);
    if (!config->positional_args) {
      SAFE_FREE(config->descriptors);
      SAFE_FREE(config->dependencies);
      SAFE_FREE(config);
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate positional args");
      return NULL;
    }
    memcpy(config->positional_args, builder->positional_args,
           builder->num_positional_args * sizeof(positional_arg_descriptor_t));
    config->num_positional_args = builder->num_positional_args;
  } else {
    config->positional_args = NULL;
    config->num_positional_args = 0;
  }

  // Allocate and copy usage lines
  if (builder->num_usage_lines > 0) {
    config->usage_lines = SAFE_MALLOC(builder->num_usage_lines * sizeof(usage_descriptor_t), usage_descriptor_t *);
    if (!config->usage_lines) {
      SAFE_FREE(config->descriptors);
      SAFE_FREE(config->dependencies);
      SAFE_FREE(config->positional_args);
      SAFE_FREE(config);
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate usage lines");
      return NULL;
    }
    memcpy(config->usage_lines, builder->usage_lines, builder->num_usage_lines * sizeof(usage_descriptor_t));
    config->num_usage_lines = builder->num_usage_lines;
  } else {
    config->usage_lines = NULL;
    config->num_usage_lines = 0;
  }

  // Allocate and copy examples
  if (builder->num_examples > 0) {
    config->examples = SAFE_MALLOC(builder->num_examples * sizeof(example_descriptor_t), example_descriptor_t *);
    if (!config->examples) {
      SAFE_FREE(config->descriptors);
      SAFE_FREE(config->dependencies);
      SAFE_FREE(config->positional_args);
      SAFE_FREE(config->usage_lines);
      SAFE_FREE(config);
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate examples");
      return NULL;
    }
    memcpy(config->examples, builder->examples, builder->num_examples * sizeof(example_descriptor_t));
    config->num_examples = builder->num_examples;
  } else {
    config->examples = NULL;
    config->num_examples = 0;
  }

  // Allocate and copy modes
  if (builder->num_modes > 0) {
    config->modes = SAFE_MALLOC(builder->num_modes * sizeof(help_mode_descriptor_t), help_mode_descriptor_t *);
    if (!config->modes) {
      SAFE_FREE(config->descriptors);
      SAFE_FREE(config->dependencies);
      SAFE_FREE(config->positional_args);
      SAFE_FREE(config->usage_lines);
      SAFE_FREE(config->examples);
      SAFE_FREE(config);
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate modes");
      return NULL;
    }
    memcpy(config->modes, builder->modes, builder->num_modes * sizeof(help_mode_descriptor_t));
    config->num_modes = builder->num_modes;
  } else {
    config->modes = NULL;
    config->num_modes = 0;
  }

  // Allocate and copy custom sections
  if (builder->num_custom_sections > 0) {
    config->custom_sections =
        SAFE_MALLOC(builder->num_custom_sections * sizeof(custom_section_descriptor_t), custom_section_descriptor_t *);
    if (!config->custom_sections) {
      SAFE_FREE(config->descriptors);
      SAFE_FREE(config->dependencies);
      SAFE_FREE(config->positional_args);
      SAFE_FREE(config->usage_lines);
      SAFE_FREE(config->examples);
      SAFE_FREE(config->modes);
      SAFE_FREE(config);
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate custom sections");
      return NULL;
    }
    memcpy(config->custom_sections, builder->custom_sections,
           builder->num_custom_sections * sizeof(custom_section_descriptor_t));
    config->num_custom_sections = builder->num_custom_sections;
  } else {
    config->custom_sections = NULL;
    config->num_custom_sections = 0;
  }

  config->struct_size = builder->struct_size;
  config->program_name = builder->program_name;
  config->description = builder->description;

  // Initialize memory management
  config->owned_strings = builder->owned_strings_builder;
  config->num_owned_strings = builder->num_owned_strings_builder;
  config->owned_strings_capacity = builder->owned_strings_builder_capacity;

  // Clear builder's ownership so it doesn't free them
  builder->owned_strings_builder = NULL;
  builder->num_owned_strings_builder = 0;
  builder->owned_strings_builder_capacity = 0;

  return config;
}

void options_config_destroy(options_config_t *config) {
  if (!config)
    return;

  SAFE_FREE(config->descriptors);
  SAFE_FREE(config->dependencies);
  SAFE_FREE(config->positional_args);
  SAFE_FREE(config->usage_lines);
  SAFE_FREE(config->examples);
  SAFE_FREE(config->modes);
  SAFE_FREE(config->custom_sections);

  // Free all owned strings before freeing the array
  for (size_t i = 0; i < config->num_owned_strings; i++) {
    free(config->owned_strings[i]);
  }
  SAFE_FREE(config->owned_strings);

  SAFE_FREE(config);
}

// ============================================================================
// Adding Options
// ============================================================================

void options_builder_add_bool(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                              bool default_value, const char *help_text, const char *group, bool required,
                              const char *env_var_name) {
  ensure_descriptor_capacity(builder);

  static bool default_vals[2] = {false, true};

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_BOOL,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = &default_vals[default_value ? 1 : 0],
                              .required = required,
                              .env_var_name = env_var_name,
                              .mode_bitmask = OPTION_MODE_NONE,
                              .validate = NULL,
                              .parse_fn = NULL,
                              .owns_memory = false};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_int(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                             int default_value, const char *help_text, const char *group, bool required,
                             const char *env_var_name, bool (*validate)(const void *, char **)) {
  ensure_descriptor_capacity(builder);

  // Store default value statically
  static int defaults[256]; // Assume we won't have more than 256 int options
  static size_t num_defaults = 0;

  if (num_defaults >= 256) {
    log_error("Too many int options (max 256)");
    return;
  }

  defaults[num_defaults] = default_value;

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_INT,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = &defaults[num_defaults++],
                              .required = required,
                              .env_var_name = env_var_name,
                              .validate = validate,
                              .parse_fn = NULL,
                              .owns_memory = false,
                              .mode_bitmask = OPTION_MODE_NONE};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_int_with_metadata(options_builder_t *builder, const char *long_name, char short_name,
                                           size_t offset, int default_value, const char *help_text, const char *group,
                                           bool required, const char *env_var_name,
                                           bool (*validate)(const void *options_struct, char **error_msg),
                                           const option_metadata_t *metadata) {
  ensure_descriptor_capacity(builder);

  // Store default value statically
  static int defaults[256]; // Assume we won't have more than 256 int options
  static size_t num_defaults = 0;

  if (num_defaults >= 256) {
    log_error("Too many int options (max 256)");
    return;
  }

  defaults[num_defaults] = default_value;

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_INT,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = &defaults[num_defaults++],
                              .required = required,
                              .env_var_name = env_var_name,
                              .validate = validate,
                              .parse_fn = NULL,
                              .owns_memory = false,
                              .mode_bitmask = OPTION_MODE_NONE,
                              .metadata = metadata ? *metadata : (option_metadata_t){0}};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_string(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                                const char *default_value, const char *help_text, const char *group, bool required,
                                const char *env_var_name, bool (*validate)(const void *, char **)) {
  ensure_descriptor_capacity(builder);

  // Store default string pointer in static storage to avoid stack-use-after-return
  // Increased from 256 to 1024 to support multiple test builders without overflow
  static const char *defaults[1024];
  static size_t num_defaults = 0;

  const void *default_ptr = NULL;
  if (default_value) {
    if (num_defaults >= 1024) {
      log_error("Too many string options (max 1024)");
      return;
    }
    defaults[num_defaults] = default_value;
    default_ptr = &defaults[num_defaults++];
  }

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_STRING,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = default_ptr,
                              .required = required,
                              .env_var_name = env_var_name,
                              .validate = validate,
                              .parse_fn = NULL,
                              .owns_memory = true, // Strings are always owned
                              .mode_bitmask = OPTION_MODE_NONE};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_double(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                                double default_value, const char *help_text, const char *group, bool required,
                                const char *env_var_name, bool (*validate)(const void *, char **)) {
  ensure_descriptor_capacity(builder);

  // Increased from 256 to 1024 to support multiple test builders without overflow
  static double defaults[1024];
  static size_t num_defaults = 0;

  if (num_defaults >= 1024) {
    log_error("Too many double options (max 1024)");
    return;
  }

  defaults[num_defaults] = default_value;

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_DOUBLE,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = &defaults[num_defaults++],
                              .required = required,
                              .env_var_name = env_var_name,
                              .validate = validate,
                              .parse_fn = NULL,
                              .owns_memory = false,
                              .mode_bitmask = OPTION_MODE_NONE};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_double_with_metadata(options_builder_t *builder, const char *long_name, char short_name,
                                              size_t offset, double default_value, const char *help_text,
                                              const char *group, bool required, const char *env_var_name,
                                              bool (*validate)(const void *, char **),
                                              const option_metadata_t *metadata) {
  ensure_descriptor_capacity(builder);

  static double defaults[1024];
  static size_t num_defaults = 0;

  if (num_defaults >= 1024) {
    log_error("Too many double options (max 1024)");
    return;
  }

  defaults[num_defaults] = default_value;

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_DOUBLE,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = &defaults[num_defaults++],
                              .required = required,
                              .env_var_name = env_var_name,
                              .validate = validate,
                              .parse_fn = NULL,
                              .owns_memory = false,
                              .mode_bitmask = OPTION_MODE_NONE,
                              .metadata = metadata ? *metadata : (option_metadata_t){0}};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_callback(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                                  const void *default_value, size_t value_size,
                                  bool (*parse_fn)(const char *, void *, char **), const char *help_text,
                                  const char *group, bool required, const char *env_var_name) {
  (void)value_size; // Unused parameter
  ensure_descriptor_capacity(builder);

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_CALLBACK,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = default_value,
                              .required = required,
                              .env_var_name = env_var_name,
                              .validate = NULL,
                              .parse_fn = parse_fn,
                              .owns_memory = false,
                              .optional_arg = false,
                              .mode_bitmask = OPTION_MODE_NONE};

  builder->descriptors[builder->num_descriptors++] = desc;
}

/**
 * @brief Add callback option with optional argument support
 *
 * @param builder Builder to add to
 * @param long_name Long option name
 * @param short_name Short option char (or '\0')
 * @param offset offsetof(struct, field)
 * @param default_value Pointer to default value (or NULL)
 * @param value_size sizeof(field_type)
 * @param parse_fn Custom parser function
 * @param help_text Help description
 * @param group Group name for help
 * @param required If true, option must be provided
 * @param env_var_name Environment variable fallback (or NULL)
 * @param optional_arg If true, argument is optional (parser receives NULL if not provided)
 */
void options_builder_add_callback_optional(options_builder_t *builder, const char *long_name, char short_name,
                                           size_t offset, const void *default_value, size_t value_size,
                                           bool (*parse_fn)(const char *, void *, char **), const char *help_text,
                                           const char *group, bool required, const char *env_var_name,
                                           bool optional_arg) {
  (void)value_size; // Unused parameter
  ensure_descriptor_capacity(builder);

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_CALLBACK,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = default_value,
                              .required = required,
                              .env_var_name = env_var_name,
                              .validate = NULL,
                              .parse_fn = parse_fn,
                              .owns_memory = false,
                              .optional_arg = optional_arg,
                              .mode_bitmask = OPTION_MODE_NONE};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_callback_with_metadata(options_builder_t *builder, const char *long_name, char short_name,
                                                size_t offset, const void *default_value, size_t value_size,
                                                bool (*parse_fn)(const char *, void *, char **), const char *help_text,
                                                const char *group, bool required, const char *env_var_name,
                                                bool optional_arg, const option_metadata_t *metadata) {
  (void)value_size; // Unused parameter
  ensure_descriptor_capacity(builder);

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_CALLBACK,
                              .offset = offset,
                              .help_text = help_text,
                              .group = group,
                              .default_value = default_value,
                              .required = required,
                              .env_var_name = env_var_name,
                              .validate = NULL,
                              .parse_fn = parse_fn,
                              .owns_memory = false,
                              .optional_arg = optional_arg,
                              .mode_bitmask = OPTION_MODE_NONE,
                              .metadata = metadata ? *metadata : (option_metadata_t){0}};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_action(options_builder_t *builder, const char *long_name, char short_name,
                                void (*action_fn)(void), const char *help_text, const char *group) {
  ensure_descriptor_capacity(builder);

  option_descriptor_t desc = {.long_name = long_name,
                              .short_name = short_name,
                              .type = OPTION_TYPE_ACTION,
                              .offset = 0, // Actions don't store values
                              .help_text = help_text,
                              .group = group,
                              .default_value = NULL,
                              .required = false, // Actions are never required
                              .env_var_name = NULL,
                              .validate = NULL,
                              .parse_fn = NULL,
                              .action_fn = action_fn,
                              .owns_memory = false,
                              .hide_from_mode_help = false,
                              .hide_from_binary_help = false,
                              .mode_bitmask = OPTION_MODE_NONE};

  builder->descriptors[builder->num_descriptors++] = desc;

  // Set hide_from_binary_help after adding (so we can check the option name)
  if (strcmp(long_name, "create-man-page") == 0) {
    // Always hide from help and man page (this is a development tool)
    builder->descriptors[builder->num_descriptors - 1].hide_from_binary_help = true;
  }
}

void options_builder_add_descriptor(options_builder_t *builder, const option_descriptor_t *descriptor) {
  if (!builder || !descriptor) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Builder is NULL or descriptor is NULL");
    return;
  }
  return;

  ensure_descriptor_capacity(builder);
  builder->descriptors[builder->num_descriptors++] = *descriptor;
}

void options_builder_set_mode_bitmask(options_builder_t *builder, option_mode_bitmask_t mode_bitmask) {
  if (!builder || builder->num_descriptors == 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "Builder is NULL or has no descriptors");
    return;
  }
  builder->descriptors[builder->num_descriptors - 1].mode_bitmask = mode_bitmask;
}

void options_builder_set_arg_placeholder(options_builder_t *builder, const char *arg_placeholder) {
  if (!builder || builder->num_descriptors == 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "Builder is NULL or has no descriptors");
    return;
  }
  builder->descriptors[builder->num_descriptors - 1].arg_placeholder = arg_placeholder;
}

// ============================================================================
// Completion Metadata (Phase 2 Implementation)
// ============================================================================

/**
 * @brief Find descriptor by option name in builder
 * @param builder Builder to search
 * @param option_name Long name of option to find
 * @return Index of descriptor, or -1 if not found
 */
static int find_descriptor_in_builder(const options_builder_t *builder, const char *option_name) {
  if (!builder || !option_name) {
    return -1;
  }
  for (size_t i = 0; i < builder->num_descriptors; i++) {
    if (builder->descriptors[i].long_name && strcmp(builder->descriptors[i].long_name, option_name) == 0) {
      return (int)i;
    }
  }
  return -1;
}

void options_builder_set_enum_values(options_builder_t *builder, const char *option_name, const char **values,
                                     const char **descriptions, size_t count) {
  if (!builder || !option_name || !values || !descriptions) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Builder or arguments are NULL");
    return;
  }

  int idx = find_descriptor_in_builder(builder, option_name);
  if (idx < 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option '%s' not found in builder", option_name);
    return;
  }

  option_descriptor_t *desc = &builder->descriptors[idx];
  desc->metadata.enum_values = values;
  desc->metadata.enum_count = count;
  desc->metadata.enum_descriptions = descriptions;
}

void options_builder_set_numeric_range(options_builder_t *builder, const char *option_name, int min, int max,
                                       int step) {
  if (!builder || !option_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Builder or option_name is NULL");
    return;
  }

  int idx = find_descriptor_in_builder(builder, option_name);
  if (idx < 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option '%s' not found in builder", option_name);
    return;
  }

  option_descriptor_t *desc = &builder->descriptors[idx];
  desc->metadata.numeric_range.min = min;
  desc->metadata.numeric_range.max = max;
  desc->metadata.numeric_range.step = step;
}

void options_builder_set_examples(options_builder_t *builder, const char *option_name, const char **examples) {
  if (!builder || !option_name || !examples) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Builder or arguments are NULL");
    return;
  }

  int idx = find_descriptor_in_builder(builder, option_name);
  if (idx < 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option '%s' not found in builder", option_name);
    return;
  }

  option_descriptor_t *desc = &builder->descriptors[idx];
  desc->metadata.examples = examples;
  // Note: examples array must be null-terminated
}

void options_builder_set_input_type(options_builder_t *builder, const char *option_name,
                                    option_input_type_t input_type) {
  if (!builder || !option_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Builder or option_name is NULL");
    return;
  }

  int idx = find_descriptor_in_builder(builder, option_name);
  if (idx < 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option '%s' not found in builder", option_name);
    return;
  }

  option_descriptor_t *desc = &builder->descriptors[idx];
  desc->metadata.input_type = input_type;
}

void options_builder_mark_as_list(options_builder_t *builder, const char *option_name) {
  if (!builder || !option_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Builder or option_name is NULL");
    return;
  }

  int idx = find_descriptor_in_builder(builder, option_name);
  if (idx < 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option '%s' not found in builder", option_name);
    return;
  }

  option_descriptor_t *desc = &builder->descriptors[idx];
  desc->metadata.is_list = true;
}

void options_builder_set_default_value_display(options_builder_t *builder, const char *option_name,
                                               const char *default_value) {
  if (!builder || !option_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Builder or option_name is NULL");
    return;
  }

  int idx = find_descriptor_in_builder(builder, option_name);
  if (idx < 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Option '%s' not found in builder", option_name);
    return;
  }

  option_descriptor_t *desc = &builder->descriptors[idx];
  desc->metadata.default_value = default_value;
}

// ============================================================================
// Managing Dependencies
// ============================================================================

void options_builder_add_dependency_requires(options_builder_t *builder, const char *option_name,
                                             const char *depends_on, const char *error_message) {
  ensure_dependency_capacity(builder);

  option_dependency_t dep = {.option_name = option_name,
                             .type = DEPENDENCY_REQUIRES,
                             .depends_on = depends_on,
                             .error_message = error_message};

  builder->dependencies[builder->num_dependencies++] = dep;
}

void options_builder_add_dependency_conflicts(options_builder_t *builder, const char *option_name,
                                              const char *conflicts_with, const char *error_message) {
  ensure_dependency_capacity(builder);

  option_dependency_t dep = {.option_name = option_name,
                             .type = DEPENDENCY_CONFLICTS,
                             .depends_on = conflicts_with,
                             .error_message = error_message};

  builder->dependencies[builder->num_dependencies++] = dep;
}

void options_builder_add_dependency_implies(options_builder_t *builder, const char *option_name, const char *implies,
                                            const char *error_message) {
  ensure_dependency_capacity(builder);

  option_dependency_t dep = {
      .option_name = option_name, .type = DEPENDENCY_IMPLIES, .depends_on = implies, .error_message = error_message};

  builder->dependencies[builder->num_dependencies++] = dep;
}

void options_builder_add_dependency(options_builder_t *builder, const option_dependency_t *dependency) {
  if (!builder || !dependency)
    return;

  ensure_dependency_capacity(builder);
  builder->dependencies[builder->num_dependencies++] = *dependency;
}

void options_builder_mark_binary_only(options_builder_t *builder, const char *option_name) {
  if (!builder || !option_name)
    return;

  // Find the option by name and mark it
  for (size_t i = 0; i < builder->num_descriptors; i++) {
    if (strcmp(builder->descriptors[i].long_name, option_name) == 0) {
      builder->descriptors[i].hide_from_mode_help = true;
      return;
    }
  }

  // Option not found - this is a programming error
  log_warn("Attempted to mark non-existent option '%s' as binary-only", option_name);
}

// ============================================================================
// Positional Arguments
// ============================================================================

void options_builder_add_positional(options_builder_t *builder, const char *name, const char *help_text, bool required,
                                    const char *section_heading, const char **examples, size_t num_examples,
                                    option_mode_bitmask_t mode_bitmask,
                                    int (*parse_fn)(const char *arg, void *config, char **remaining, int num_remaining,
                                                    char **error_msg)) {
  if (!builder || !name || !parse_fn)
    return;

  ensure_positional_arg_capacity(builder);

  positional_arg_descriptor_t pos_arg = {.name = name,
                                         .help_text = help_text,
                                         .required = required,
                                         .section_heading = section_heading,
                                         .examples = examples,
                                         .num_examples = num_examples,
                                         .mode_bitmask = mode_bitmask,
                                         .parse_fn = parse_fn};

  builder->positional_args[builder->num_positional_args++] = pos_arg;
}

// ============================================================================
// Programmatic Help Generation
// ============================================================================

/**
 * @brief Track an owned string for cleanup by the builder
 */
static void track_owned_string_builder(options_builder_t *builder, char *str) {
  if (!str)
    return;

  if (builder->num_owned_strings_builder >= builder->owned_strings_builder_capacity) {
    size_t new_capacity = builder->owned_strings_builder_capacity * 2;
    if (new_capacity == 0)
      new_capacity = INITIAL_OWNED_STRINGS_CAPACITY;

    char **new_owned = SAFE_REALLOC(builder->owned_strings_builder, new_capacity * sizeof(char *), char **);
    if (!new_owned) {
      log_fatal("Failed to reallocate builder owned_strings array");
      return;
    }
    builder->owned_strings_builder = new_owned;
    builder->owned_strings_builder_capacity = new_capacity;
  }

  builder->owned_strings_builder[builder->num_owned_strings_builder++] = str;
}

void options_builder_add_usage(options_builder_t *builder, const char *mode, const char *positional, bool show_options,
                               const char *description) {
  if (!builder || !description)
    return;

  ensure_usage_line_capacity(builder);

  usage_descriptor_t usage = {
      .mode = mode, .positional = positional, .show_options = show_options, .description = description};

  builder->usage_lines[builder->num_usage_lines++] = usage;
}

void options_builder_add_example(options_builder_t *builder, uint32_t mode_bitmask, const char *args,
                                 const char *description, bool owns_args) {
  if (!builder || !description)
    return;

  ensure_example_capacity(builder);

  example_descriptor_t example = {.mode_bitmask = mode_bitmask,
                                  .description = description,
                                  .owns_args_memory = owns_args,
                                  .is_utility_command = false};

  if (owns_args) {
    // If owning, duplicate the string and track it
    char *owned_args = strdup(args);
    if (!owned_args) {
      log_fatal("Failed to duplicate example args string");
      return;
    }
    example.args = owned_args;
    track_owned_string_builder(builder, owned_args);
  } else {
    example.args = args;
  }

  builder->examples[builder->num_examples++] = example;
}

/**
 * @brief Add an example with utility command support
 *
 * Allows marking utility commands (like pbpaste, grep, etc.) that shouldn't
 * be prepended with program name in help output.
 */
void options_builder_add_example_utility(options_builder_t *builder, uint32_t mode_bitmask, const char *args,
                                         const char *description, bool is_utility_command) {
  if (!builder || !description)
    return;

  ensure_example_capacity(builder);

  example_descriptor_t example = {.mode_bitmask = mode_bitmask,
                                  .description = description,
                                  .owns_args_memory = false,
                                  .is_utility_command = is_utility_command};

  if (args) {
    example.args = args;
  }

  builder->examples[builder->num_examples++] = example;
}

void options_builder_add_mode(options_builder_t *builder, const char *name, const char *description) {
  if (!builder || !name || !description)
    return;

  ensure_mode_capacity(builder);

  help_mode_descriptor_t mode = {.name = name, .description = description};

  builder->modes[builder->num_modes++] = mode;
}

void options_builder_add_custom_section(options_builder_t *builder, const char *heading, const char *content,
                                        option_mode_bitmask_t mode_bitmask) {
  if (!builder || !heading || !content)
    return;

  ensure_custom_section_capacity(builder);

  custom_section_descriptor_t section = {.heading = heading, .content = content, .mode_bitmask = mode_bitmask};

  builder->custom_sections[builder->num_custom_sections++] = section;
}

asciichat_error_t options_config_parse_positional(const options_config_t *config, int remaining_argc,
                                                  char **remaining_argv, void *options_struct) {
  if (!config || !options_struct) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Config or options struct is NULL");
  }

  if (remaining_argc < 0 || (remaining_argc > 0 && !remaining_argv)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid remaining args");
  }

  // No positional args defined - but we got some positional arguments
  if (config->num_positional_args == 0 && remaining_argc > 0) {
    log_error("Error: Unexpected positional argument '%s'", remaining_argv[0]);
    return ERROR_USAGE;
  }

  // Check if required positional args are missing
  for (size_t i = 0; i < config->num_positional_args; i++) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[i];
    if (pos_arg->required && remaining_argc == 0) {
      log_error("Error: Missing required positional argument '%s'", pos_arg->name);
      if (pos_arg->help_text) {
        log_error("  %s", pos_arg->help_text);
      }
      return ERROR_USAGE;
    }
  }

  // Parse positional arguments
  int arg_index = 0;
  for (size_t i = 0; i < config->num_positional_args && arg_index < remaining_argc; i++) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[i];

    const char *arg = remaining_argv[arg_index];
    char **remaining = (arg_index + 1 < remaining_argc) ? &remaining_argv[arg_index + 1] : NULL;
    int num_remaining = remaining_argc - arg_index - 1;
    char *error_msg = NULL;

    // Call custom parser
    int consumed = pos_arg->parse_fn(arg, options_struct, remaining, num_remaining, &error_msg);

    if (consumed < 0) {
      // Parse error
      if (error_msg) {
        log_error("Error parsing positional argument '%s': %s", pos_arg->name, error_msg);
        free(error_msg);
      } else {
        log_error("Error parsing positional argument '%s': %s", pos_arg->name, arg);
      }
      return ERROR_USAGE;
    }

    // Advance by consumed args (usually 1, but can be more for multi-arg parsers)
    arg_index += consumed;
  }

  // Check for extra unconsumed positional arguments
  if (arg_index < remaining_argc) {
    log_error("Error: Unexpected positional argument '%s'", remaining_argv[arg_index]);
    return ERROR_USAGE;
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// Parsing and Validation
// ============================================================================

asciichat_error_t options_config_set_defaults(const options_config_t *config, void *options_struct) {
  if (!config || !options_struct) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Config or options struct is NULL");
  }

  char *base = (char *)options_struct;

  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    void *field = base + desc->offset;

    // Check environment variable first
    const char *env_value = NULL;
    if (desc->env_var_name) {
      env_value = SAFE_GETENV(desc->env_var_name);
    }

    // Use handler registry to apply environment variables and defaults
    if (desc->type >= 0 && desc->type < (int)(NUM_OPTION_TYPES)) {
      if (g_builder_handlers[desc->type].apply_env) {
        g_builder_handlers[desc->type].apply_env(field, env_value, desc);
      }
    } else if (desc->type == OPTION_TYPE_CALLBACK && desc->parse_fn && desc->default_value) {
      // Special handling for callbacks with parse_fn
      char *error_msg = NULL;
      desc->parse_fn(NULL, field, &error_msg);
      if (error_msg) {
        free(error_msg);
      }
    }
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// Unified Argument Parser (Supports Mixed Positional and Flag Arguments)
// ============================================================================

/**
 * @brief Check if an argument is a known mode keyword
 *
 * Mode keywords should not be consumed as option values.
 */
static bool is_mode_keyword(const char *arg) {
  if (!arg || arg[0] == '\0')
    return false;

  // Known mode keywords that should not be consumed as values
  static const char *mode_keywords[] = {"server", "client", "mirror", "acds", "discovery", "discovery-service", NULL};

  for (int i = 0; mode_keywords[i] != NULL; i++) {
    if (strcmp(arg, mode_keywords[i]) == 0) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Check if an argument looks like a flag
 *
 * An argument is considered a flag if it starts with `-` or is a mode keyword.
 * Special cases:
 * - `--` is treated as the end-of-options marker
 * - Numbers starting with `-` (like `-5`) could be ambiguous, but we treat them as flags
 * - Mode keywords (server, client, mirror, etc.) should not be consumed as values
 */
static bool is_flag_argument(const char *arg) {
  if (!arg || arg[0] == '\0')
    return false;
  return arg[0] == '-' || is_mode_keyword(arg);
}

/**
 * @brief Find option descriptor by short or long name
 */
const option_descriptor_t *find_option_descriptor(const options_config_t *config, const char *opt_name) {
  if (!opt_name || opt_name[0] == '\0')
    return NULL;

  // Handle short options: -x (single dash, single char)
  if (opt_name[0] == '-' && opt_name[1] != '-' && opt_name[1] != '\0') {
    // This is a short option like -x
    char short_char = opt_name[1];
    for (size_t i = 0; i < config->num_descriptors; i++) {
      if (config->descriptors[i].short_name == short_char) {
        return &config->descriptors[i];
      }
    }
    // Short option not found
    return NULL;
  }

  // Handle long options: --name (double dash)
  if (opt_name[0] == '-' && opt_name[1] == '-' && opt_name[2] != '\0') {
    const char *long_name = opt_name + 2; // Skip the '--'
    for (size_t i = 0; i < config->num_descriptors; i++) {
      if (strcmp(config->descriptors[i].long_name, long_name) == 0) {
        return &config->descriptors[i];
      }
    }
    // Long option not found
    return NULL;
  }

  // Not a recognized option format
  return NULL;
}

/**
 * @brief Parse a single flag with explicit mode_bitmask for validation
 *
 * This variant is called from options_config_parse_unified with the mode_bitmask
 * parameter to ensure proper mode-based option filtering.
 *
 * @param config Options configuration
 * @param argv Argument vector
 * @param argv_index Index of the argument to parse
 * @param argc Argument count
 * @param options_struct Destination struct for parsed values
 * @param mode_bitmask Mode bitmask for filtering options
 * @param consumed_count Output: number of argv elements consumed (1 or 2)
 * @return Error code
 */
static asciichat_error_t parse_single_flag_with_mode(const options_config_t *config, char **argv, int argv_index,
                                                     int argc, void *options_struct, option_mode_bitmask_t mode_bitmask,
                                                     int *consumed_count) {
  *consumed_count = 1;
  // Validate argv_index is within bounds to prevent SIGBUS
  if (argv_index < 0 || argv_index >= argc) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "argv_index %d out of bounds [0, %d)", argv_index, argc);
  }

  const char *arg = argv[argv_index];
  char *long_opt_value = NULL;
  char *equals = NULL;
  char *arg_copy = NULL;            // Copy of arg for safe parsing without modifying original
  const char *arg_for_lookup = arg; // What to use for option lookup

  // Handle long options with `=` (e.g., --port=8080)
  if (strncmp(arg, "--", 2) == 0) {
    equals = strchr(arg, '=');
    if (equals) {
      long_opt_value = equals + 1;
      // Make a copy of the arg string and null-terminate it to get the option name
      // This avoids modifying the original argv string, which is important for
      // thread safety and to prevent issues with forked child processes
      size_t arg_len = equals - arg;
      arg_copy = SAFE_MALLOC(arg_len + 1, char *);
      if (arg_copy) {
        memcpy(arg_copy, arg, arg_len);
        arg_copy[arg_len] = '\0';
        arg_for_lookup = arg_copy;
      } else {
        return ERROR_MEMORY;
      }
    }
  }

  // Find matching descriptor
  const option_descriptor_t *desc = find_option_descriptor(config, arg_for_lookup);
  if (!desc) {
    // Try to suggest a similar option
    const char *suggestion = find_similar_option_with_mode(arg_for_lookup, config, mode_bitmask);
    if (suggestion) {
      log_plain_stderr("Unknown option: %s. %s", arg_for_lookup, suggestion);
    } else {
      log_plain_stderr("Unknown option: %s", arg_for_lookup);
    }
    SAFE_FREE(arg_copy);
    return ERROR_USAGE;
  }

  // Check if option applies to current mode based on the passed mode_bitmask
  if (desc->mode_bitmask != 0 && !(desc->mode_bitmask & OPTION_MODE_BINARY)) {
    // Option has specific mode restrictions - use the passed mode_bitmask directly
    if (!(desc->mode_bitmask & mode_bitmask)) {
      SAFE_FREE(arg_copy);
      return SET_ERRNO(ERROR_USAGE, "Option %s is not supported for this mode", arg);
    }
  }

  void *field = (char *)options_struct + desc->offset;
  const char *opt_value = NULL;

  // Get option value if needed
  if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
    if (long_opt_value) {
      // Value came from --name=value
      opt_value = long_opt_value;
    } else if (argv_index + 1 < argc && !is_flag_argument(argv[argv_index + 1])) {
      // Value from next argument
      opt_value = argv[argv_index + 1];
      *consumed_count = 2;
    } else {
      // No value provided
      // Check if this option allows optional arguments
      if (!desc->optional_arg) {
        if (equals)
          *equals = '='; // Restore
        return SET_ERRNO(ERROR_USAGE, "Option %s requires a value", arg);
      }
      // For optional arguments, pass NULL to the parser
      opt_value = NULL;
    }
  }

  // Handle option based on type using the handler registry
  if (desc->type < NUM_OPTION_TYPES) {
    const option_builder_handler_t *handler = &g_builder_handlers[desc->type];

    if (desc->type == OPTION_TYPE_BOOL || desc->type == OPTION_TYPE_ACTION) {
      // Boolean/action flags don't need a value
      asciichat_error_t result = handler->apply_cli(field, NULL, desc);
      if (result != ASCIICHAT_OK) {
        if (equals)
          *equals = '='; // Restore
        return result;
      }
    } else {
      // Other types need a value (INT, STRING, DOUBLE, CALLBACK)
      asciichat_error_t result = handler->apply_cli(field, opt_value, desc);
      if (result != ASCIICHAT_OK) {
        SAFE_FREE(arg_copy);
        return result;
      }
    }
  } else {
    SAFE_FREE(arg_copy);
    return SET_ERRNO(ERROR_USAGE, "Invalid option type for %s", arg);
  }

  SAFE_FREE(arg_copy);
  return ASCIICHAT_OK;
}

/**
 * @brief Unified argument parser supporting mixed positional and flag arguments
 *
 * Unlike the traditional two-phase approach, this parser handles both positional
 * and flag arguments in a single pass, allowing them to be intermixed in any order.
 *
 * Special case: `--` stops all option parsing and treats remaining args as positional.
 *
 * @param config Options configuration
 * @param argc Argument count
 * @param argv Argument vector
 * @param options_struct Options struct to populate
 * @param detected_mode Detected mode for filtering positional args by mode_bitmask
 */
static asciichat_error_t options_config_parse_unified(const options_config_t *config, int argc, char **argv,
                                                      void *options_struct, option_mode_bitmask_t detected_mode) {
  if (!config || !options_struct) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Config or options struct is NULL");
  }

  // Separate positional and flag arguments while respecting order
  char **positional_args = SAFE_MALLOC(argc * sizeof(char *), char **);
  if (!positional_args) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate positional args buffer");
  }
  int positional_count = 0;
  bool end_of_options = false;

  // Parse arguments in order
  for (int i = 1; i < argc; i++) { // Skip argv[0] (program name)
    const char *arg = argv[i];

    // Handle `--` as end-of-options marker
    if (!end_of_options && strcmp(arg, "--") == 0) {
      end_of_options = true;
      continue;
    }

    // If we've seen `--` or arg doesn't look like a flag, treat as positional
    if (end_of_options || !is_flag_argument(arg)) {
      positional_args[positional_count++] = argv[i];
      continue;
    }

    // Parse this flag with mode validation using the detected_mode bitmask
    int consumed = 0;
    asciichat_error_t err =
        parse_single_flag_with_mode(config, argv, i, argc, options_struct, detected_mode, &consumed);
    if (err != ASCIICHAT_OK) {
      SAFE_FREE(positional_args);
      return err;
    }

    // Skip consumed arguments
    i += (consumed - 1);
  }

  // Now parse positional arguments
  if (config->num_positional_args > 0) {
    // Parse positional arguments in order, filtering by mode_bitmask
    int arg_index = 0;
    for (size_t pos_idx = 0; pos_idx < config->num_positional_args && arg_index < positional_count; pos_idx++) {
      const positional_arg_descriptor_t *pos_arg = &config->positional_args[pos_idx];

      // Skip positional args that don't apply to the detected mode
      if (pos_arg->mode_bitmask != 0 && !(pos_arg->mode_bitmask & detected_mode)) {
        continue;
      }

      const char *arg = positional_args[arg_index];
      char **remaining = (arg_index + 1 < positional_count) ? &positional_args[arg_index + 1] : NULL;
      int num_remaining = positional_count - arg_index - 1;
      char *error_msg = NULL;

      int consumed = pos_arg->parse_fn(arg, options_struct, remaining, num_remaining, &error_msg);

      if (consumed < 0) {
        log_error("Error parsing positional argument '%s': %s", pos_arg->name, error_msg ? error_msg : arg);
        free(error_msg);
        SAFE_FREE(positional_args);
        return ERROR_USAGE;
      }

      arg_index += consumed;
    }

    // Check for extra unconsumed positional arguments
    if (arg_index < positional_count) {
      log_error("Error: Unexpected positional argument '%s'", positional_args[arg_index]);
      SAFE_FREE(positional_args);
      return ERROR_USAGE;
    }

    // Check if required positional args are missing
    for (size_t i = 0; i < config->num_positional_args; i++) {
      const positional_arg_descriptor_t *pos_arg = &config->positional_args[i];
      if (pos_arg->required && i >= (size_t)arg_index) {
        log_error("Error: Missing required positional argument '%s'", pos_arg->name);
        if (pos_arg->help_text) {
          log_error("  %s", pos_arg->help_text);
        }
        SAFE_FREE(positional_args);
        return ERROR_USAGE;
      }
    }
  } else if (positional_count > 0) {
    // No positional args expected, but we got some
    log_error("Error: Unexpected positional argument '%s'", positional_args[0]);
    SAFE_FREE(positional_args);
    return ERROR_USAGE;
  }

  SAFE_FREE(positional_args);
  return ASCIICHAT_OK;
}

asciichat_error_t options_config_parse(const options_config_t *config, int argc, char **argv, void *options_struct,
                                       option_mode_bitmask_t detected_mode, int *remaining_argc,
                                       char ***remaining_argv) {
  if (!config || !options_struct) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Config or options struct is NULL");
  }

  // Use the new unified parser that handles mixed positional and flag arguments
  asciichat_error_t result = options_config_parse_unified(config, argc, argv, options_struct, detected_mode);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Since unified parser handles all arguments, there are no remaining args
  // (backward compatibility: set remaining_argc to 0)
  if (remaining_argc) {
    *remaining_argc = 0;
  }
  if (remaining_argv) {
    *remaining_argv = NULL;
  }

  return ASCIICHAT_OK;
}

asciichat_error_t options_config_validate(const options_config_t *config, const void *options_struct,
                                          char **error_message) {
  if (!config || !options_struct) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Config or options struct is NULL");
  }

  const char *base = (const char *)options_struct;

  // Check required fields
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (!desc->required)
      continue;

    const void *field = base + desc->offset;
    // Use handler registry to check if option is set (for required field validation)
    bool is_set = true;
    if (desc->type >= 0 && desc->type < (int)(NUM_OPTION_TYPES)) {
      if (g_builder_handlers[desc->type].is_set) {
        is_set = g_builder_handlers[desc->type].is_set(field, desc);
      }
    }

    if (!is_set) {
      if (error_message) {
        if (desc->env_var_name) {
          asprintf(error_message, "Required option --%s is not set (set %s env var or use --%s)", desc->long_name,
                   desc->env_var_name, desc->long_name);
        } else {
          asprintf(error_message, "Required option --%s is not set", desc->long_name);
        }
      }
      return ERROR_USAGE;
    }
  }

  // Check dependencies
  for (size_t i = 0; i < config->num_dependencies; i++) {
    const option_dependency_t *dep = &config->dependencies[i];

    bool option_is_set = is_option_set(config, options_struct, dep->option_name);
    bool depends_is_set = is_option_set(config, options_struct, dep->depends_on);

    switch (dep->type) {
    case DEPENDENCY_REQUIRES:
      if (option_is_set && !depends_is_set) {
        if (error_message) {
          if (dep->error_message) {
            *error_message = strdup(dep->error_message);
          } else {
            asprintf(error_message, "Option --%s requires --%s to be set", dep->option_name, dep->depends_on);
          }
        }
        return ERROR_USAGE;
      }
      break;

    case DEPENDENCY_CONFLICTS:
      if (option_is_set && depends_is_set) {
        if (error_message) {
          if (dep->error_message) {
            *error_message = strdup(dep->error_message);
          } else {
            asprintf(error_message, "Option --%s conflicts with --%s", dep->option_name, dep->depends_on);
          }
        }
        return ERROR_USAGE;
      }
      break;

    case DEPENDENCY_IMPLIES:
      // Implies is handled during parsing, not validation
      break;
    }
  }

  // Run custom validators
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (!desc->validate)
      continue;

    char *custom_error = NULL;
    if (!desc->validate(options_struct, &custom_error)) {
      if (error_message) {
        *error_message = custom_error;
      } else {
        free(custom_error);
      }
      return ERROR_USAGE;
    }
  }

  // Cross-field validation: Check for conflicting color options
  // Cannot use --color with --color-mode none
  const options_t *opts = (const options_t *)options_struct;
  if (opts->color && opts->color_mode == TERM_COLOR_NONE) {
    if (error_message) {
      asprintf(error_message, "Option --color cannot be used with --color-mode=none (conflicting color settings)");
    }
    return ERROR_USAGE;
  }

  return ASCIICHAT_OK;
}

// Help generation functions are in builder/help.c
// Type handlers are in builder/handlers.c
