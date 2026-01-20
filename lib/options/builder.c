/**
 * @file builder.c
 * @brief Implementation of options builder API
 * @ingroup options
 */

#include "builder.h"
#include "log/logging.h"
#include "platform/abstraction.h"
#include "asciichat_errno.h"
#include "util/utf8.h"
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

/**
 * @brief Format a string with logging color codes if appropriate
 * Uses rotating static buffers to handle multiple calls in same statement
 */
static const char *colored_string(log_color_t color, const char *text) {
  #define COLORED_BUFFERS 4
  #define COLORED_BUFFER_SIZE 256
  static char buffers[COLORED_BUFFERS][COLORED_BUFFER_SIZE];
  static int buffer_idx = 0;

  // Check if we should use colors: TTY output and not in CLAUDECODE mode
  bool use_colors = platform_isatty(STDOUT_FILENO) && !SAFE_GETENV("CLAUDECODE");

  if (!text) {
    return "";
  }

  if (!use_colors) {
    // No colors, just return the text directly
    return text;
  }

  // Use rotating buffer to handle multiple calls in same fprintf
  char *current_buf = buffers[buffer_idx];
  buffer_idx = (buffer_idx + 1) % COLORED_BUFFERS;

  const char *color_code = log_level_color(color);
  const char *reset_code = log_level_color(LOG_COLOR_RESET);

  // Format into rotating static buffer: color_code + text + reset_code
  snprintf(current_buf, COLORED_BUFFER_SIZE, "%s%s%s", color_code, text, reset_code);
  return current_buf;
}

/**
 * @brief Print text segment with colored metadata labels
 * Colorizes "default:" labels and "env:" labels with cyan env var names
 */
static void print_colored_segment(FILE *stream, const char *seg) {
  const char *sp = seg;
  while (*sp) {
    if (strncmp(sp, "default:", 8) == 0) {
      fprintf(stream, "%s", colored_string(LOG_COLOR_FATAL, "default:"));
      sp += 8;
    } else if (strncmp(sp, "env:", 4) == 0) {
      fprintf(stream, "%s", colored_string(LOG_COLOR_FATAL, "env:"));
      sp += 4;
      while (*sp == ' ') {
        fprintf(stream, " ");
        sp++;
      }
      const char *env_start = sp;
      while (*sp && *sp != ')') sp++;
      if (sp > env_start) {
        char env_name[256];
        int len = sp - env_start;
        if (len < (int)sizeof(env_name)) {
          strncpy(env_name, env_start, len);
          env_name[len] = '\0';
          fprintf(stream, "%s", colored_string(LOG_COLOR_DEBUG, env_name));  // Cyan for env var
        }
      }
    } else {
      fputc(*sp, stream);
      sp++;
    }
  }
}

/**
 * @brief Print option string with colorized type indicators (NUM, STR, VAL)
 * Handles coloring of type placeholders while maintaining correct length for alignment
 */
static void print_colored_option(FILE *stream, const char *option_str) {
  if (!option_str || !stream) return;

  bool use_colors = platform_isatty(STDOUT_FILENO) && !SAFE_GETENV("CLAUDECODE");
  if (!use_colors) {
    fprintf(stream, "%s", colored_string(LOG_COLOR_WARN, option_str));
    return;
  }

  // Print the option string, colorizing type indicators (NUM, STR, VAL)
  const char *p = option_str;
  const char *color_code = log_level_color(LOG_COLOR_INFO);    // Green for type indicators
  const char *warn_code = log_level_color(LOG_COLOR_WARN);     // Yellow for option name
  const char *reset_code = log_level_color(LOG_COLOR_RESET);

  fprintf(stream, "%s", warn_code);

  while (*p) {
    // Check if we're at a type indicator
    const char *type_start = NULL;
    int type_len = 0;

    if ((strncmp(p, "NUM", 3) == 0 && (p[3] == '\0' || p[3] == ' '))) {
      type_start = p;
      type_len = 3;
    } else if ((strncmp(p, "STR", 3) == 0 && (p[3] == '\0' || p[3] == ' '))) {
      type_start = p;
      type_len = 3;
    } else if ((strncmp(p, "VAL", 3) == 0 && (p[3] == '\0' || p[3] == ' '))) {
      type_start = p;
      type_len = 3;
    }

    if (type_start) {
      // Switch to info color for type indicator
      fprintf(stream, "%s%.*s%s", color_code, type_len, type_start, warn_code);
      p += type_len;
    } else {
      fprintf(stream, "%c", *p);
      p++;
    }
  }

  fprintf(stream, "%s", reset_code);
}

/**
 * @brief Word-wrap description text to fit in terminal width
 * Wraps plain text, then applies colors to metadata labels
 */
static void print_wrapped_description(FILE *stream, const char *text, int indent_width, int term_width) {
  if (!text || !stream)
    return;

  // Default terminal width if not specified
  if (term_width <= 0)
    term_width = 80;

  // Available width for text after indentation
  int available_width = term_width - indent_width;
  if (available_width < 20)
    available_width = 20;

  const char *line_start = text;
  const char *last_space = NULL;
  int line_len = 0;
  const char *p = text;

  while (*p) {
    if (*p == ' ')
      last_space = p;

    line_len++;

    // Check if we need to wrap
    if (line_len >= available_width || *p == '\n') {
      // Find previous space if we exceeded width
      if (*p != '\n' && line_len >= available_width && last_space && last_space > line_start) {
        // Print text up to last space with colors applied
        int text_len = last_space - line_start;
        char seg[512];
        strncpy(seg, line_start, text_len);
        seg[text_len] = '\0';
        print_colored_segment(stream, seg);

        fprintf(stream, "\n");
        for (int i = 0; i < indent_width; i++)
          fprintf(stream, " ");
        p = last_space + 1;
        line_start = p;
        line_len = 0;
        last_space = NULL;
        continue;
      }

      if (*p == '\n') {
        // Print remaining segment with colors
        int text_len = p - line_start;
        if (text_len > 0) {
          char seg[512];
          strncpy(seg, line_start, text_len);
          seg[text_len] = '\0';
          print_colored_segment(stream, seg);
        }

        fprintf(stream, "\n");
        if (*(p + 1)) {
          for (int i = 0; i < indent_width; i++)
            fprintf(stream, " ");
        }
        p++;
        line_start = p;
        line_len = 0;
        last_space = NULL;
        continue;
      }
    }

    p++;
  }

  // Print remaining text with colors
  if (line_start < p) {
    char seg[512];
    int text_len = p - line_start;
    if (text_len < (int)sizeof(seg)) {
      strncpy(seg, line_start, text_len);
      seg[text_len] = '\0';
      print_colored_segment(stream, seg);
    }
  }
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

  // Column layout: options in first 28 chars, descriptions start at column 30
  #define OPTION_COLUMN_WIDTH 28
  #define DESCRIPTION_START_COL 30
  #define NARROW_TERMINAL_THRESHOLD 55

  // Build list of unique groups in order of first appearance
  bool use_vertical_layout = (term_width < NARROW_TERMINAL_THRESHOLD);

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
    fprintf(stream, "\n%s:\n", colored_string(LOG_COLOR_DEBUG, current_group));

    // Print all options in this group
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];

      // Skip if not in current group or if hidden
      if (desc->hide_from_mode_help || !desc->group || strcmp(desc->group, current_group) != 0) {
        continue;
      }

    // Build option string
    char option_str[256] = "";
    int option_len = 0;

    // Short name and long name
    if (desc->short_name) {
      option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "-%c, --%s", desc->short_name, desc->long_name);
    } else {
      option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "--%s", desc->long_name);
    }

    // Value placeholder
    if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
      option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, " ");
      switch (desc->type) {
      case OPTION_TYPE_INT:
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "NUM");
        break;
      case OPTION_TYPE_STRING:
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "STR");
        break;
      case OPTION_TYPE_DOUBLE:
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "NUM");
        break;
      case OPTION_TYPE_CALLBACK:
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "VAL");
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
    bool description_has_default = desc->help_text && (strstr(desc->help_text, "(default:") || strstr(desc->help_text, "=default)"));

    if (desc->default_value && desc->type != OPTION_TYPE_CALLBACK && !description_has_default) {
      desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (default: ");
      switch (desc->type) {
      case OPTION_TYPE_BOOL:
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                            *(const bool *)desc->default_value ? "true" : "false");
        break;
      case OPTION_TYPE_INT: {
        int int_val = 0;
        memcpy(&int_val, desc->default_value, sizeof(int));
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%d", int_val);
        break;
      }
      case OPTION_TYPE_STRING:
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                            *(const char *const *)desc->default_value);
        break;
      case OPTION_TYPE_DOUBLE: {
        double double_val = 0.0;
        memcpy(&double_val, desc->default_value, sizeof(double));
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%.2f", double_val);
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
      // Embed ANSI color codes directly so they're preserved across wraps
      desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (env: %s)",
                           colored_string(LOG_COLOR_DEBUG, desc->env_var_name));
    }

    // For narrow terminals, use vertical layout
    if (use_vertical_layout) {
      fprintf(stream, "  ");
      print_colored_option(stream, option_str);
      fprintf(stream, "\n  ");
      print_wrapped_description(stream, desc_str, 2, term_width);
      fprintf(stream, "\n\n");
    } else {
      // Print option and description with proper alignment
      fprintf(stream, "  ");
      print_colored_option(stream, option_str);

      // Calculate display width of option string (accounts for UTF-8)
      int option_display_width = utf8_display_width(option_str);

      // If option string is short enough, put description on same line
      if (option_display_width < OPTION_COLUMN_WIDTH) {
        // Pad to column 30, then print description with wrapping
        int current_col = 2 + option_display_width; // 2 for leading spaces
        int padding = DESCRIPTION_START_COL - current_col;
        if (padding > 0) {
          fprintf(stream, "%*s", padding, "");
        } else {
          fprintf(stream, " ");
        }
        // Print description with wrapping at column 30 indent
        print_wrapped_description(stream, desc_str, DESCRIPTION_START_COL, term_width);
        fprintf(stream, "\n");
      } else {
        // Option string too long, put description on next line with proper indent
        fprintf(stream, "\n");
        // Print indent for description column
        for (int i = 0; i < DESCRIPTION_START_COL; i++)
          fprintf(stream, " ");
        print_wrapped_description(stream, desc_str, DESCRIPTION_START_COL, term_width);
        fprintf(stream, "\n");
      }
    }
    }
  }

  // Cleanup
  SAFE_FREE(unique_groups);

  fprintf(stream, "\n");
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
