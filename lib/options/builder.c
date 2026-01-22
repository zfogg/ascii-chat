/**
 * @file builder.c
 * @brief Implementation of options builder API
 * @ingroup options
 */

#include "builder.h"
#include "log/logging.h"
#include "layout.h"
#include "platform/abstraction.h"
#include "platform/terminal.h"
#include "asciichat_errno.h"
#include "util/utf8.h"
#include "util/string.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>

// Platform-specific getopt
#ifdef _WIN32
#include "platform/windows/getopt.h"
#else
#include <getopt.h>
#endif

// Initial capacities for dynamic arrays
#define INITIAL_DESCRIPTOR_CAPACITY 32
#define INITIAL_DEPENDENCY_CAPACITY 16
#define INITIAL_POSITIONAL_ARG_CAPACITY 8
#define INITIAL_OWNED_STRINGS_CAPACITY 32

// ============================================================================
// Internal Helper Functions
// ============================================================================

/**
 * @brief Grow descriptor array if needed
 */
static void ensure_descriptor_capacity(options_builder_t *builder) {
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
static void ensure_dependency_capacity(options_builder_t *builder) {
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
static void ensure_positional_arg_capacity(options_builder_t *builder) {
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

/**
 * @brief Track an owned string for cleanup
 */
__attribute__((unused)) static void track_owned_string(options_config_t *config, char *str) {
  if (!str)
    return;

  if (config->num_owned_strings >= config->owned_strings_capacity) {
    size_t new_capacity = config->owned_strings_capacity * 2;
    if (new_capacity == 0)
      new_capacity = INITIAL_OWNED_STRINGS_CAPACITY;

    char **new_owned = SAFE_REALLOC(config->owned_strings, new_capacity * sizeof(char *), char **);
    if (!new_owned) {
      log_fatal("Failed to reallocate owned_strings array");
      return;
    }
    config->owned_strings = new_owned;
    config->owned_strings_capacity = new_capacity;
  }

  config->owned_strings[config->num_owned_strings++] = str;
}

/**
 * @brief Find option descriptor by long name
 */
static const option_descriptor_t *find_option(const options_config_t *config, const char *long_name) {
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
static bool is_option_set(const options_config_t *config, const void *options_struct, const char *option_name) {
  const option_descriptor_t *desc = find_option(config, option_name);
  if (!desc)
    return false;

  const char *base = (const char *)options_struct;
  const void *field = base + desc->offset;

  switch (desc->type) {
  case OPTION_TYPE_BOOL: {
    bool value = *(const bool *)field;
    bool default_val = desc->default_value ? *(const bool *)desc->default_value : false;
    return value != default_val;
  }
  case OPTION_TYPE_INT: {
    int value = *(const int *)field;
    int default_val = desc->default_value ? *(const int *)desc->default_value : 0;
    return value != default_val;
  }
  case OPTION_TYPE_STRING: {
    const char *value = *(const char *const *)field;
    const char *default_val = desc->default_value ? *(const char *const *)desc->default_value : NULL;

    // Both NULL or both same pointer = not set
    if (value == default_val)
      return false;
    // One NULL = different = set
    if (!value || !default_val)
      return true;
    // Compare strings
    return strcmp(value, default_val) != 0;
  }
  case OPTION_TYPE_DOUBLE: {
    double value = 0.0;
    double default_val = 0.0;
    // Use memcpy to safely handle potentially misaligned memory
    memcpy(&value, field, sizeof(double));
    if (desc->default_value) {
      memcpy(&default_val, desc->default_value, sizeof(double));
    }
    return value != default_val;
  }
  case OPTION_TYPE_CALLBACK:
    // For callbacks, assume set if not NULL
    return *(const void **)field != NULL;

  case OPTION_TYPE_ACTION:
    // Actions execute immediately and don't store values
    return false;
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
  builder->struct_size = struct_size;
  builder->program_name = NULL;
  builder->description = NULL;

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
                                   pos_arg->parse_fn);
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

  config->struct_size = builder->struct_size;
  config->program_name = builder->program_name;
  config->description = builder->description;

  // Initialize memory management
  config->owned_strings = NULL;
  config->num_owned_strings = 0;
  config->owned_strings_capacity = 0;

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
                              .owns_memory = false};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_string(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                                const char *default_value, const char *help_text, const char *group, bool required,
                                const char *env_var_name, bool (*validate)(const void *, char **)) {
  ensure_descriptor_capacity(builder);

  // Store default string pointer in static storage to avoid stack-use-after-return
  static const char *defaults[256];
  static size_t num_defaults = 0;

  const void *default_ptr = NULL;
  if (default_value) {
    if (num_defaults >= 256) {
      log_error("Too many string options (max 256)");
      return;
    }
    defaults[num_defaults] = default_value;
    default_ptr = &defaults[num_defaults++];
  }

  option_descriptor_t desc = {
      .long_name = long_name,
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
      .owns_memory = true // Strings are always owned
  };

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_double(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                                double default_value, const char *help_text, const char *group, bool required,
                                const char *env_var_name, bool (*validate)(const void *, char **)) {
  ensure_descriptor_capacity(builder);

  static double defaults[256];
  static size_t num_defaults = 0;

  if (num_defaults >= 256) {
    log_error("Too many double options (max 256)");
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
                              .owns_memory = false};

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
                              .optional_arg = false};

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
                              .optional_arg = optional_arg};

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
                              .owns_memory = false};

  builder->descriptors[builder->num_descriptors++] = desc;
}

void options_builder_add_descriptor(options_builder_t *builder, const option_descriptor_t *descriptor) {
  if (!builder || !descriptor)
    return;

  ensure_descriptor_capacity(builder);
  builder->descriptors[builder->num_descriptors++] = *descriptor;
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
                                         .parse_fn = parse_fn};

  builder->positional_args[builder->num_positional_args++] = pos_arg;
}

// ============================================================================
// Programmatic Help Generation
// ============================================================================

void options_builder_add_usage(options_builder_t *builder, const char *mode, const char *positional, bool show_options,
                               const char *description) {
  if (!builder || !description)
    return;

  ensure_usage_line_capacity(builder);

  usage_descriptor_t usage = {
      .mode = mode, .positional = positional, .show_options = show_options, .description = description};

  builder->usage_lines[builder->num_usage_lines++] = usage;
}

void options_builder_add_example(options_builder_t *builder, const char *mode, const char *args,
                                 const char *description) {
  if (!builder || !description)
    return;

  ensure_example_capacity(builder);

  example_descriptor_t example = {.mode = mode, .args = args, .description = description};

  builder->examples[builder->num_examples++] = example;
}

void options_builder_add_mode(options_builder_t *builder, const char *name, const char *description) {
  if (!builder || !name || !description)
    return;

  ensure_mode_capacity(builder);

  help_mode_descriptor_t mode = {.name = name, .description = description};

  builder->modes[builder->num_modes++] = mode;
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
    (void)fprintf(stderr, "Error: Unexpected positional argument '%s'\n", remaining_argv[0]);
    return ERROR_USAGE;
  }

  // Check if required positional args are missing
  for (size_t i = 0; i < config->num_positional_args; i++) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[i];
    if (pos_arg->required && remaining_argc == 0) {
      (void)fprintf(stderr, "Error: Missing required positional argument '%s'\n", pos_arg->name);
      if (pos_arg->help_text) {
        (void)fprintf(stderr, "  %s\n", pos_arg->help_text);
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
        (void)fprintf(stderr, "Error parsing positional argument '%s': %s\n", pos_arg->name, error_msg);
        free(error_msg);
      } else {
        (void)fprintf(stderr, "Error parsing positional argument '%s': %s\n", pos_arg->name, arg);
      }
      return ERROR_USAGE;
    }

    // Advance by consumed args (usually 1, but can be more for multi-arg parsers)
    arg_index += consumed;
  }

  // Check for extra unconsumed positional arguments
  if (arg_index < remaining_argc) {
    (void)fprintf(stderr, "Error: Unexpected positional argument '%s'\n", remaining_argv[arg_index]);
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

    switch (desc->type) {
    case OPTION_TYPE_BOOL: {
      bool value = false;
      if (env_value) {
        // Parse env var as bool
        value = (strcmp(env_value, "1") == 0 || strcmp(env_value, "true") == 0 || strcmp(env_value, "yes") == 0 ||
                 strcmp(env_value, "on") == 0);
      } else if (desc->default_value) {
        value = *(const bool *)desc->default_value;
      }
      *(bool *)field = value;
      break;
    }

    case OPTION_TYPE_INT: {
      int value = 0;
      if (env_value) {
        // Parse env var as int
        char *endptr;
        long parsed = strtol(env_value, &endptr, 10);
        if (*endptr == '\0' && parsed >= INT_MIN && parsed <= INT_MAX) {
          value = (int)parsed;
        }
      } else if (desc->default_value) {
        value = *(const int *)desc->default_value;
      }
      // Use memcpy to handle potentially misaligned memory safely
      memcpy(field, &value, sizeof(int));
      break;
    }

    case OPTION_TYPE_STRING: {
      const char *value = NULL;
      if (env_value) {
        value = env_value;
      } else if (desc->default_value) {
        value = *(const char *const *)desc->default_value;
      }

      // Copy into fixed-size buffer (same as parsing logic in options_config_parse)
      // String fields in options_t are char[OPTIONS_BUFF_SIZE] arrays, not pointers
      if (value && value[0] != '\0') {
        char *dest = (char *)field;
        snprintf(dest, OPTIONS_BUFF_SIZE, "%s", value);
        dest[OPTIONS_BUFF_SIZE - 1] = '\0'; // Ensure null termination
      }
      break;
    }

    case OPTION_TYPE_DOUBLE: {
      double value = 0.0;
      if (env_value) {
        char *endptr;
        value = strtod(env_value, &endptr);
        if (*endptr != '\0') {
          value = 0.0; // Parse failed
        }
      } else if (desc->default_value) {
        value = *(const double *)desc->default_value;
      }
      // Use memcpy to handle potentially misaligned memory safely
      memcpy(field, &value, sizeof(double));
      break;
    }

    case OPTION_TYPE_CALLBACK:
      // Callbacks don't have automatic defaults in set_defaults
      // Defaults are applied during parsing via the parse_fn callback
      // Note: value_size is not stored in descriptor, so we can't memcpy safely
      break;

    case OPTION_TYPE_ACTION:
      // Actions don't store defaults - they execute when encountered
      break;
    }
  }

  return ASCIICHAT_OK;
}

// ============================================================================
// Unified Argument Parser (Supports Mixed Positional and Flag Arguments)
// ============================================================================

/**
 * @brief Check if an argument looks like a flag
 *
 * An argument is considered a flag if it starts with `-`.
 * Special cases:
 * - `--` is treated as the end-of-options marker
 * - Numbers starting with `-` (like `-5`) could be ambiguous, but we treat them as flags
 */
static bool is_flag_argument(const char *arg) {
  if (!arg || arg[0] == '\0')
    return false;
  return arg[0] == '-';
}

/**
 * @brief Find option descriptor by short or long name
 */
static const option_descriptor_t *find_option_descriptor(const options_config_t *config, const char *opt_name) {
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
 * @brief Parse a single flag option with its argument
 *
 * Handles both long options (--name, --name=value) and short options (-n value)
 *
 * @param config Options configuration
 * @param argv Current argv array
 * @param argv_index Current index in argv
 * @param argc Total number of arguments
 * @param options_struct Destination struct for parsed values
 * @param consumed_count Output: number of argv elements consumed (1 or 2)
 * @return Error code
 */
static asciichat_error_t parse_single_flag(const options_config_t *config, char **argv, int argv_index, int argc,
                                           void *options_struct, int *consumed_count) {
  *consumed_count = 1;

  const char *arg = argv[argv_index];
  char *long_opt_value = NULL;
  char *equals = NULL;

  // Handle long options with `=` (e.g., --port=8080)
  if (strncmp(arg, "--", 2) == 0) {
    equals = strchr(arg, '=');
    if (equals) {
      long_opt_value = equals + 1;
      // Temporarily null-terminate to get option name
      *equals = '\0';
    }
  }

  // Find matching descriptor
  const option_descriptor_t *desc = find_option_descriptor(config, arg);
  if (!desc) {
    if (equals)
      *equals = '='; // Restore
    return SET_ERRNO(ERROR_USAGE, "Unknown option: %s", arg);
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
      (*consumed_count)++;
    } else if (desc->type == OPTION_TYPE_CALLBACK && desc->optional_arg) {
      // Optional argument for callback - pass NULL
      opt_value = NULL;
    } else {
      if (equals)
        *equals = '=';
      return SET_ERRNO(ERROR_USAGE, "Option %s requires an argument", arg);
    }
  }

  // Parse value based on type
  switch (desc->type) {
  case OPTION_TYPE_BOOL:
    *(bool *)field = true;
    break;

  case OPTION_TYPE_INT: {
    char *endptr;
    long value = strtol(opt_value, &endptr, 10);
    if (*endptr != '\0' || value < INT_MIN || value > INT_MAX) {
      if (equals)
        *equals = '=';
      return SET_ERRNO(ERROR_USAGE, "Invalid integer value for %s", arg);
    }
    int int_value = (int)value;
    memcpy(field, &int_value, sizeof(int));
    break;
  }

  case OPTION_TYPE_STRING: {
    char *dest = (char *)field;
    snprintf(dest, OPTIONS_BUFF_SIZE, "%s", opt_value);
    dest[OPTIONS_BUFF_SIZE - 1] = '\0';
    break;
  }

  case OPTION_TYPE_DOUBLE: {
    char *endptr;
    double value = strtod(opt_value, &endptr);
    if (*endptr != '\0') {
      if (equals)
        *equals = '=';
      return SET_ERRNO(ERROR_USAGE, "Invalid double value for %s", arg);
    }
    memcpy(field, &value, sizeof(double));
    break;
  }

  case OPTION_TYPE_CALLBACK:
    if (desc->parse_fn) {
      char *error_msg = NULL;
      if (!desc->parse_fn(opt_value, field, &error_msg)) {
        asciichat_error_t err =
            SET_ERRNO(ERROR_USAGE, "Parse error for %s: %s", arg, error_msg ? error_msg : "unknown");
        free(error_msg);
        if (equals)
          *equals = '=';
        return err;
      }
    }
    break;

  case OPTION_TYPE_ACTION:
    if (desc->action_fn) {
      desc->action_fn();
    }
    break;
  }

  if (equals)
    *equals = '='; // Restore
  return ASCIICHAT_OK;
}

/**
 * @brief Unified argument parser supporting mixed positional and flag arguments
 *
 * Unlike the traditional two-phase approach, this parser handles both positional
 * and flag arguments in a single pass, allowing them to be intermixed in any order.
 *
 * Special case: `--` stops all option parsing and treats remaining args as positional.
 */
static asciichat_error_t options_config_parse_unified(const options_config_t *config, int argc, char **argv,
                                                      void *options_struct) {
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

    // Parse this flag
    int consumed = 0;
    asciichat_error_t err = parse_single_flag(config, argv, i, argc, options_struct, &consumed);
    if (err != ASCIICHAT_OK) {
      SAFE_FREE(positional_args);
      return err;
    }

    // Skip consumed arguments
    i += (consumed - 1);
  }

  // Now parse positional arguments
  if (config->num_positional_args > 0) {
    // Parse positional arguments in order
    int arg_index = 0;
    for (size_t pos_idx = 0; pos_idx < config->num_positional_args && arg_index < positional_count; pos_idx++) {
      const positional_arg_descriptor_t *pos_arg = &config->positional_args[pos_idx];

      const char *arg = positional_args[arg_index];
      char **remaining = (arg_index + 1 < positional_count) ? &positional_args[arg_index + 1] : NULL;
      int num_remaining = positional_count - arg_index - 1;
      char *error_msg = NULL;

      int consumed = pos_arg->parse_fn(arg, options_struct, remaining, num_remaining, &error_msg);

      if (consumed < 0) {
        (void)fprintf(stderr, "Error parsing positional argument '%s': %s\n", pos_arg->name,
                      error_msg ? error_msg : arg);
        free(error_msg);
        SAFE_FREE(positional_args);
        return ERROR_USAGE;
      }

      arg_index += consumed;
    }

    // Check for extra unconsumed positional arguments
    if (arg_index < positional_count) {
      (void)fprintf(stderr, "Error: Unexpected positional argument '%s'\n", positional_args[arg_index]);
      SAFE_FREE(positional_args);
      return ERROR_USAGE;
    }

    // Check if required positional args are missing
    for (size_t i = 0; i < config->num_positional_args; i++) {
      const positional_arg_descriptor_t *pos_arg = &config->positional_args[i];
      if (pos_arg->required && i >= (size_t)arg_index) {
        (void)fprintf(stderr, "Error: Missing required positional argument '%s'\n", pos_arg->name);
        if (pos_arg->help_text) {
          (void)fprintf(stderr, "  %s\n", pos_arg->help_text);
        }
        SAFE_FREE(positional_args);
        return ERROR_USAGE;
      }
    }
  } else if (positional_count > 0) {
    // No positional args expected, but we got some
    (void)fprintf(stderr, "Error: Unexpected positional argument '%s'\n", positional_args[0]);
    SAFE_FREE(positional_args);
    return ERROR_USAGE;
  }

  SAFE_FREE(positional_args);
  return ASCIICHAT_OK;
}

asciichat_error_t options_config_parse(const options_config_t *config, int argc, char **argv, void *options_struct,
                                       int *remaining_argc, char ***remaining_argv) {
  if (!config || !options_struct) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Config or options struct is NULL");
  }

  // Use the new unified parser that handles mixed positional and flag arguments
  asciichat_error_t result = options_config_parse_unified(config, argc, argv, options_struct);
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
    bool is_set = false;

    switch (desc->type) {
    case OPTION_TYPE_BOOL:
      is_set = true; // Bools are always set
      break;
    case OPTION_TYPE_INT:
      is_set = true; // Ints are always set
      break;
    case OPTION_TYPE_STRING:
      is_set = (*(const char *const *)field != NULL);
      break;
    case OPTION_TYPE_DOUBLE:
      is_set = true; // Doubles are always set
      break;
    case OPTION_TYPE_CALLBACK:
      is_set = (*(const void *const *)field != NULL);
      break;
    case OPTION_TYPE_ACTION:
      is_set = true; // Actions are never required
      break;
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

  return ASCIICHAT_OK;
}

// ============================================================================
// Programmatic Section Printers for Help Output
// ============================================================================

/**
 * @brief Calculate global max column width across all help sections
 *
 * Calculates the maximum width needed for proper alignment across
 * USAGE, EXAMPLES, OPTIONS, and MODES sections.
 */
int options_config_calculate_max_col_width(const options_config_t *config) {
  if (!config)
    return 0;

#ifdef _WIN32
  const char *binary_name = "ascii-chat.exe";
#else
  const char *binary_name = "ascii-chat";
#endif

  int max_col_width = 0;
  char temp_buf[512];

  // Check USAGE entries
  for (size_t i = 0; i < config->num_usage_lines; i++) {
    const usage_descriptor_t *usage = &config->usage_lines[i];
    int len = 0;

    len += snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);

    if (usage->mode) {
      const char *colored_mode = colored_string(LOG_COLOR_FATAL, usage->mode);
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_mode);
    }

    if (usage->positional) {
      const char *colored_pos = colored_string(LOG_COLOR_INFO, usage->positional);
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_pos);
    }

    if (usage->show_options) {
      const char *options_text =
          (usage->mode && strcmp(usage->mode, "<mode>") == 0) ? "[mode-options...]" : "[options...]";
      const char *colored_opts = colored_string(LOG_COLOR_WARN, options_text);
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_opts);
    }

    int w = utf8_display_width(temp_buf);
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check EXAMPLES entries
  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];
    int len = 0;

    len += snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);

    if (example->mode) {
      const char *colored_mode = colored_string(LOG_COLOR_FATAL, example->mode);
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_mode);
    }

    if (example->args) {
      const char *colored_args = colored_string(LOG_COLOR_INFO, example->args);
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_args);
    }

    int w = utf8_display_width(temp_buf);
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check MODES entries
  for (size_t i = 0; i < config->num_modes; i++) {
    const char *colored_name = colored_string(LOG_COLOR_FATAL, config->modes[i].name);
    int w = utf8_display_width(colored_name);
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check OPTIONS entries (from descriptors)
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->hide_from_mode_help || !desc->group)
      continue;

    // Build option display string with separate coloring for short and long flags
    char opts_buf[256];
    if (desc->short_name && desc->short_name != '\0') {
      char short_flag[16];
      snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
      char long_flag[256];
      snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
      // Color short flag, add comma, color long flag
      snprintf(opts_buf, sizeof(opts_buf), "%s, %s",
               colored_string(LOG_COLOR_WARN, short_flag),
               colored_string(LOG_COLOR_WARN, long_flag));
    } else {
      char long_flag[256];
      snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
      snprintf(opts_buf, sizeof(opts_buf), "%s", colored_string(LOG_COLOR_WARN, long_flag));
    }
    const char *colored_opts = opts_buf;

    int w = utf8_display_width(colored_opts);
    if (w > max_col_width)
      max_col_width = w;
  }

  return max_col_width;
}

/**
 * @brief Print USAGE section programmatically
 *
 * Builds colored usage lines from components:
 * - mode: magenta (using LOG_COLOR_FATAL)
 * - positional: green (using LOG_COLOR_INFO)
 * - options: yellow (using LOG_COLOR_WARN)
 */
static void print_usage_section(const options_config_t *config, FILE *stream, int term_width, int max_col_width) {
  if (!config || !stream || config->num_usage_lines == 0) {
    return;
  }

#ifdef _WIN32
  const char *binary_name = "ascii-chat.exe";
#else
  const char *binary_name = "ascii-chat";
#endif

  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "USAGE:"));

  // Build colored syntax strings using colored_string() for all components
  for (size_t i = 0; i < config->num_usage_lines; i++) {
    const usage_descriptor_t *usage = &config->usage_lines[i];
    char usage_buf[512];
    int len = 0;

    // Start with binary name
    len += snprintf(usage_buf + len, sizeof(usage_buf) - len, "%s", binary_name);

    // Add mode if present (magenta color)
    if (usage->mode) {
      len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_FATAL, usage->mode));
    }

    // Add positional args if present (green color)
    if (usage->positional) {
      len +=
          snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_INFO, usage->positional));
    }

    // Add options suffix if requested (yellow color)
    if (usage->show_options) {
      const char *options_text =
          (usage->mode && strcmp(usage->mode, "<mode>") == 0) ? "[mode-options...]" : "[options...]";
      len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_WARN, options_text));
    }

    // Print with layout function using global column width
    layout_print_two_column_row(stream, usage_buf, usage->description, max_col_width, term_width);
  }
  fprintf(stream, "\n");
}

/**
 * @brief Print EXAMPLES section programmatically
 *
 * Builds colored example commands from components:
 * - mode: magenta (using LOG_COLOR_FATAL)
 * - args/flags: yellow (using LOG_COLOR_WARN)
 */
static void print_examples_section(const options_config_t *config, FILE *stream, int term_width, int max_col_width) {
  if (!config || !stream || config->num_examples == 0) {
    return;
  }

#ifdef _WIN32
  const char *binary_name = "ascii-chat.exe";
#else
  const char *binary_name = "ascii-chat";
#endif

  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "EXAMPLES:"));

  // Build colored command strings using colored_string() for all components
  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];
    char cmd_buf[512];
    int len = 0;

    // Start with binary name
    len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s", binary_name);

    // Add mode if present (magenta color)
    if (example->mode) {
      len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, " %s", colored_string(LOG_COLOR_FATAL, example->mode));
    }

    // Add args/flags if present (flags=yellow, arguments=green)
    if (example->args) {
      len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, " ");

      // Parse args to color flags and arguments separately
      const char *p = example->args;
      char current_token[256];
      int token_len = 0;

      while (*p) {
        if (*p == ' ') {
          // Flush current token if any
          if (token_len > 0) {
            current_token[token_len] = '\0';
            // Color flags (start with -) yellow, arguments green
            if (current_token[0] == '-') {
              len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s ",
                            colored_string(LOG_COLOR_WARN, current_token));
            } else {
              len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s ",
                            colored_string(LOG_COLOR_INFO, current_token));
            }
            token_len = 0;
          }
          p++;
          // Skip multiple spaces
          while (*p == ' ') p++;
        } else {
          current_token[token_len++] = *p;
          p++;
        }
      }

      // Flush last token
      if (token_len > 0) {
        current_token[token_len] = '\0';
        if (current_token[0] == '-') {
          len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                        colored_string(LOG_COLOR_WARN, current_token));
        } else {
          len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                        colored_string(LOG_COLOR_INFO, current_token));
        }
      }

      // Remove trailing space if added
      if (len > 0 && cmd_buf[len-1] == ' ') {
        len--;
      }
    }

    // Print with layout function using global column width
    layout_print_two_column_row(stream, cmd_buf, example->description, max_col_width, term_width);
  }

  fprintf(stream, "\n");
}

/**
 * @brief Print MODES section programmatically
 */
static void print_modes_section(const options_config_t *config, FILE *stream, int term_width, int max_col_width) {
  if (!config || !stream || config->num_modes == 0) {
    return;
  }

  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "MODES:"));

  // Print each mode with colored name using colored_string() and global column width
  for (size_t i = 0; i < config->num_modes; i++) {
    char mode_buf[256];
    snprintf(mode_buf, sizeof(mode_buf), "%s", colored_string(LOG_COLOR_FATAL, config->modes[i].name));
    layout_print_two_column_row(stream, mode_buf, config->modes[i].description, max_col_width, term_width);
  }

  fprintf(stream, "\n");
}

/**
 * @brief Print MODE-OPTIONS section programmatically
 */
static void print_mode_options_section(FILE *stream, int term_width, int max_col_width) {
#ifdef _WIN32
  const char *binary_name = "ascii-chat.exe";
#else
  const char *binary_name = "ascii-chat";
#endif

  // Print section header with colored "MODE-OPTIONS:" label
  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "MODE-OPTIONS:"));

  // Build colored command with components
  char usage_buf[512];
  int len = 0;

  // Binary name (no color)
  len += snprintf(usage_buf + len, sizeof(usage_buf) - len, "%s ", binary_name);

  // Mode placeholder (magenta)
  len += snprintf(usage_buf + len, sizeof(usage_buf) - len, "%s", colored_string(LOG_COLOR_FATAL, "<mode>"));

  // Space and help option (yellow)
  len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_WARN, "--help"));

  layout_print_two_column_row(stream, usage_buf, "Show options for a mode", max_col_width, term_width);

  fprintf(stream, "\n");
}

void options_config_print_usage(const options_config_t *config, FILE *stream) {
  if (!config || !stream)
    return;

  // Detect terminal width from COLUMNS env var or use default
  int term_width = 80;
  const char *cols_env = SAFE_GETENV("COLUMNS");
  if (cols_env) {
    int cols = atoi(cols_env);
    if (cols > 40)
      term_width = cols;
  }

  // Calculate global max column width across all sections for consistent alignment
  int max_col_width = options_config_calculate_max_col_width(config);

  // Print programmatically generated sections (USAGE, MODES, MODE-OPTIONS, EXAMPLES)
  print_usage_section(config, stream, term_width, max_col_width);
  print_modes_section(config, stream, term_width, max_col_width);
  print_mode_options_section(stream, term_width, max_col_width);
  print_examples_section(config, stream, term_width, max_col_width);

  // Build list of unique groups in order of first appearance

  const char **unique_groups = SAFE_MALLOC(config->num_descriptors * sizeof(const char *), const char **);
  size_t num_unique_groups = 0;

  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->hide_from_mode_help || !desc->group) {
      continue;
    }

    // Check if this group is already in the list
    bool group_exists = false;
    for (size_t j = 0; j < num_unique_groups; j++) {
      if (unique_groups[j] && strcmp(unique_groups[j], desc->group) == 0) {
        group_exists = true;
        break;
      }
    }

    // Add new group to list
    if (!group_exists && num_unique_groups < config->num_descriptors) {
      unique_groups[num_unique_groups++] = desc->group;
    }
  }

  // Print options grouped by group name
  for (size_t g = 0; g < num_unique_groups; g++) {
    const char *current_group = unique_groups[g];
    // Only add leading newline for groups after the first one
    if (g > 0) {
      fprintf(stream, "\n");
    }
    fprintf(stream, "%s:\n", colored_string(LOG_COLOR_DEBUG, current_group));

    // Print all options in this group
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];

      // Skip if not in current group or if hidden
      if (desc->hide_from_mode_help || !desc->group || strcmp(desc->group, current_group) != 0) {
        continue;
      }

      // Build option string with separate coloring for short and long flags
      char option_str[512] = "";
      int option_len = 0;

      // Short name and long name with separate coloring
      if (desc->short_name) {
        char short_flag[16];
        snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
        char long_flag[256];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        // Color short flag, plain comma-space, color long flag
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s, %s",
                               colored_string(LOG_COLOR_WARN, short_flag),
                               colored_string(LOG_COLOR_WARN, long_flag));
      } else {
        char long_flag[256];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                               colored_string(LOG_COLOR_WARN, long_flag));
      }

      // Value placeholder (colored green)
      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, " ");
        switch (desc->type) {
        case OPTION_TYPE_INT:
          option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                 colored_string(LOG_COLOR_INFO, "NUM"));
          break;
        case OPTION_TYPE_STRING:
          option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                 colored_string(LOG_COLOR_INFO, "STR"));
          break;
        case OPTION_TYPE_DOUBLE:
          option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                 colored_string(LOG_COLOR_INFO, "NUM"));
          break;
        case OPTION_TYPE_CALLBACK:
          option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                 colored_string(LOG_COLOR_INFO, "VAL"));
          break;
        default:
          break;
        }
      }

      // Build description string (plain text, colors applied when printing)
      char desc_str[512] = "";
      int desc_len = 0;

      if (desc->help_text) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s", desc->help_text);
      }

      // Skip adding default if the description already mentions it
      bool description_has_default =
          desc->help_text && (strstr(desc->help_text, "(default:") || strstr(desc->help_text, "=default)"));

      if (desc->default_value && desc->type != OPTION_TYPE_CALLBACK && !description_has_default) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s ",
                             colored_string(LOG_COLOR_FATAL, "default:"));
        switch (desc->type) {
        case OPTION_TYPE_BOOL:
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, *(const bool *)desc->default_value ? "true" : "false"));
          break;
        case OPTION_TYPE_INT: {
          int int_val = 0;
          memcpy(&int_val, desc->default_value, sizeof(int));
          char int_buf[32];
          snprintf(int_buf, sizeof(int_buf), "%d", int_val);
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, int_buf));
          break;
        }
        case OPTION_TYPE_STRING:
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, *(const char *const *)desc->default_value));
          break;
        case OPTION_TYPE_DOUBLE: {
          double double_val = 0.0;
          memcpy(&double_val, desc->default_value, sizeof(double));
          char double_buf[32];
          snprintf(double_buf, sizeof(double_buf), "%.2f", double_val);
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, double_buf));
          break;
        }
        default:
          break;
        }
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, ")");
      }

      if (desc->required) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " [REQUIRED]");
      }

      if (desc->env_var_name) {
        // Color env: label and variable name grey
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s %s)",
                             colored_string(LOG_COLOR_GREY, "env:"),
                             colored_string(LOG_COLOR_GREY, desc->env_var_name));
      }

      // Use layout function with global column width for consistent alignment
      // option_str already contains colored_string() results, so pass it directly
      layout_print_two_column_row(stream, option_str, desc_str, max_col_width, term_width);
    }
  }

  // Cleanup
  SAFE_FREE(unique_groups);

  fprintf(stream, "\n");
}

/**
 * @brief Print only the USAGE section
 *
 * Splits usage printing from other sections to allow custom content
 * (like positional argument examples) to be inserted between USAGE and other sections.
 */
void options_config_print_usage_section(const options_config_t *config, FILE *stream) {
  if (!config || !stream)
    return;

  // Detect terminal width from COLUMNS env var or use default
  int term_width = 80;
  const char *cols_env = SAFE_GETENV("COLUMNS");
  if (cols_env) {
    int cols = atoi(cols_env);
    if (cols > 40)
      term_width = cols;
  }

  // Calculate global max column width across all sections for consistent alignment
  int max_col_width = options_config_calculate_max_col_width(config);

  // Print only USAGE section
  print_usage_section(config, stream, term_width, max_col_width);
}

/**
 * @brief Print everything except the USAGE section
 *
 * Prints MODES, MODE-OPTIONS, EXAMPLES, and OPTIONS sections.
 * Used with options_config_print_usage_section to allow custom content in between.
 */
void options_config_print_options_sections_with_width(const options_config_t *config, FILE *stream, int max_col_width) {
  if (!config || !stream)
    return;

  // Detect terminal width - try actual terminal size first, fallback to COLUMNS env var
  int term_width = 80;
  terminal_size_t term_size;
  if (terminal_get_size(&term_size) == ASCIICHAT_OK && term_size.cols > 40) {
    term_width = term_size.cols;
  } else {
    const char *cols_env = SAFE_GETENV("COLUMNS");
    if (cols_env) {
      int cols = atoi(cols_env);
      if (cols > 40)
        term_width = cols;
    }
  }

  // Calculate column width if not provided
  if (max_col_width <= 0) {
    max_col_width = options_config_calculate_max_col_width(config);
  }

  // Print all sections except USAGE (MODE-OPTIONS only appears in binary-level help, not mode-specific help)
  print_modes_section(config, stream, term_width, max_col_width);
  print_examples_section(config, stream, term_width, max_col_width);

  // Build list of unique groups in order of first appearance
  const char **unique_groups = SAFE_MALLOC(config->num_descriptors * sizeof(const char *), const char **);
  size_t num_unique_groups = 0;

  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->hide_from_mode_help || !desc->group) {
      continue;
    }

    // Check if this group is already in the list
    bool group_exists = false;
    for (size_t j = 0; j < num_unique_groups; j++) {
      if (unique_groups[j] && strcmp(unique_groups[j], desc->group) == 0) {
        group_exists = true;
        break;
      }
    }
    if (!group_exists && num_unique_groups < config->num_descriptors) {
      unique_groups[num_unique_groups++] = desc->group;
    }
  }

  // Print options grouped by category
  for (size_t gi = 0; gi < num_unique_groups; gi++) {
    const char *current_group = unique_groups[gi];
    // Add newline before each group except the first
    if (gi > 0) {
      fprintf(stream, "\n");
    }
    fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, current_group));

    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      if (desc->hide_from_mode_help || !desc->group || strcmp(desc->group, current_group) != 0) {
        continue;
      }

      // Build option string (flag part) with separate coloring for short and long flags
      char colored_option_str[512] = "";
      int colored_len = 0;

      // Short name and long name with separate coloring
      if (desc->short_name) {
        char short_flag[16];
        snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
        char long_flag[256];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        // Color short flag, plain comma-space, color long flag
        colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s, %s",
                                colored_string(LOG_COLOR_WARN, short_flag),
                                colored_string(LOG_COLOR_WARN, long_flag));
      } else {
        char long_flag[256];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                colored_string(LOG_COLOR_WARN, long_flag));
      }

      // Value placeholder (colored green)
      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, " ");
        switch (desc->type) {
        case OPTION_TYPE_INT:
          colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                  colored_string(LOG_COLOR_INFO, "NUM"));
          break;
        case OPTION_TYPE_STRING:
          colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                  colored_string(LOG_COLOR_INFO, "STR"));
          break;
        case OPTION_TYPE_DOUBLE:
          colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                  colored_string(LOG_COLOR_INFO, "NUM"));
          break;
        case OPTION_TYPE_CALLBACK:
          colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                  colored_string(LOG_COLOR_INFO, "VAL"));
          break;
        default:
          break;
        }
      }

      // Build description with defaults and env vars
      char desc_str[1024] = "";
      int desc_len = 0;

      if (desc->help_text) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s", desc->help_text);
      }

      // Skip adding default if the description already mentions it
      bool description_has_default =
          desc->help_text && (strstr(desc->help_text, "(default:") || strstr(desc->help_text, "=default)"));

      if (desc->default_value && desc->type != OPTION_TYPE_CALLBACK && !description_has_default) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s ",
                             colored_string(LOG_COLOR_FATAL, "default:"));
        switch (desc->type) {
        case OPTION_TYPE_BOOL:
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, *(const bool *)desc->default_value ? "true" : "false"));
          break;
        case OPTION_TYPE_INT: {
          int int_val = 0;
          memcpy(&int_val, desc->default_value, sizeof(int));
          char int_buf[32];
          snprintf(int_buf, sizeof(int_buf), "%d", int_val);
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, int_buf));
          break;
        }
        case OPTION_TYPE_STRING:
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, *(const char *const *)desc->default_value));
          break;
        case OPTION_TYPE_DOUBLE: {
          double double_val = 0.0;
          memcpy(&double_val, desc->default_value, sizeof(double));
          char double_buf[32];
          snprintf(double_buf, sizeof(double_buf), "%.2f", double_val);
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, double_buf));
          break;
        }
        default:
          break;
        }
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, ")");
      }

      if (desc->required) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " [REQUIRED]");
      }

      if (desc->env_var_name) {
        // Color env: label and variable name grey
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s %s)",
                             colored_string(LOG_COLOR_GREY, "env:"),
                             colored_string(LOG_COLOR_GREY, desc->env_var_name));
      }

      layout_print_two_column_row(stream, colored_option_str, desc_str, max_col_width, term_width);
    }
  }

  // Cleanup
  SAFE_FREE(unique_groups);

  fprintf(stream, "\n");
}

/**
 * @brief Print everything except the USAGE section (backward compatibility wrapper)
 *
 * Calls options_config_print_options_sections_with_width with auto-calculation.
 */
void options_config_print_options_sections(const options_config_t *config, FILE *stream) {
  options_config_print_options_sections_with_width(config, stream, 0);
}

void options_config_cleanup(const options_config_t *config, void *options_struct) {
  if (!config || !options_struct)
    return;

  // Free all owned strings
  for (size_t i = 0; i < config->num_owned_strings; i++) {
    free(config->owned_strings[i]);
  }

  // Reset owned strings tracking
  ((options_config_t *)config)->num_owned_strings = 0;

  // NULL out string fields
  char *base = (char *)options_struct;
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->type == OPTION_TYPE_STRING && desc->owns_memory) {
      char **field = (char **)(base + desc->offset);
      *field = NULL;
    }
  }
}
