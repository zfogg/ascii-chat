/**
 * @file builder.c
 * @brief Implementation of options builder API
 * @ingroup options
 */

#include "builder.h"
#include "common.h"
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

// Use platform utilities
#include "platform/util.h"
#include "platform/terminal.h"
#define asprintf platform_asprintf

// Initial capacities for dynamic arrays
#define INITIAL_DESCRIPTOR_CAPACITY 32
#define INITIAL_DEPENDENCY_CAPACITY 16
#define INITIAL_POSITIONAL_ARG_CAPACITY 8
#define INITIAL_OWNED_STRINGS_CAPACITY 32

// ============================================================================
// Type Handler Registry for Builder Operations
// ============================================================================
// TECHNICAL DEBT FIX: The code had 8 parallel switch(desc->type) blocks
// for different operations (is_set, apply_env, apply_cli, format_help).
// This registry consolidates them into 4 handler function pointers.

/**
 * @brief Type handler for builder operations
 */
typedef struct {
  // Check if option value differs from default
  bool (*is_set)(const void *field, const option_descriptor_t *desc);
  // Apply environment variable and set defaults
  void (*apply_env)(void *field, const char *env_value, const option_descriptor_t *desc);
  // Parse CLI argument value
  asciichat_error_t (*apply_cli)(void *field, const char *opt_value, const option_descriptor_t *desc);
  // Format value for help text placeholder (e.g., "NUM", "STR")
  void (*format_help_placeholder)(char *buf, size_t bufsize);
} option_builder_handler_t;

// Forward declarations
static bool is_set_bool(const void *field, const option_descriptor_t *desc);
static bool is_set_int(const void *field, const option_descriptor_t *desc);
static bool is_set_string(const void *field, const option_descriptor_t *desc);
static bool is_set_double(const void *field, const option_descriptor_t *desc);
static bool is_set_callback(const void *field, const option_descriptor_t *desc);
static bool is_set_action(const void *field, const option_descriptor_t *desc);

static void apply_env_bool(void *field, const char *env_value, const option_descriptor_t *desc);
static void apply_env_int(void *field, const char *env_value, const option_descriptor_t *desc);
static void apply_env_string(void *field, const char *env_value, const option_descriptor_t *desc);
static void apply_env_double(void *field, const char *env_value, const option_descriptor_t *desc);
static void apply_env_callback(void *field, const char *env_value, const option_descriptor_t *desc);
static void apply_env_action(void *field, const char *env_value, const option_descriptor_t *desc);

static asciichat_error_t apply_cli_bool(void *field, const char *opt_value, const option_descriptor_t *desc);
static asciichat_error_t apply_cli_int(void *field, const char *opt_value, const option_descriptor_t *desc);
static asciichat_error_t apply_cli_string(void *field, const char *opt_value, const option_descriptor_t *desc);
static asciichat_error_t apply_cli_double(void *field, const char *opt_value, const option_descriptor_t *desc);
static asciichat_error_t apply_cli_callback(void *field, const char *opt_value, const option_descriptor_t *desc);
static asciichat_error_t apply_cli_action(void *field, const char *opt_value, const option_descriptor_t *desc);

static void format_help_placeholder_bool(char *buf, size_t bufsize);
static void format_help_placeholder_int(char *buf, size_t bufsize);
static void format_help_placeholder_string(char *buf, size_t bufsize);
static void format_help_placeholder_double(char *buf, size_t bufsize);
static void format_help_placeholder_callback(char *buf, size_t bufsize);
static void format_help_placeholder_action(char *buf, size_t bufsize);

// Handler registry - indexed by option_type_t
static const option_builder_handler_t g_builder_handlers[] = {
    [OPTION_TYPE_BOOL] = {is_set_bool, apply_env_bool, apply_cli_bool, format_help_placeholder_bool},
    [OPTION_TYPE_INT] = {is_set_int, apply_env_int, apply_cli_int, format_help_placeholder_int},
    [OPTION_TYPE_STRING] = {is_set_string, apply_env_string, apply_cli_string, format_help_placeholder_string},
    [OPTION_TYPE_DOUBLE] = {is_set_double, apply_env_double, apply_cli_double, format_help_placeholder_double},
    [OPTION_TYPE_CALLBACK] = {is_set_callback, apply_env_callback, apply_cli_callback,
                              format_help_placeholder_callback},
    [OPTION_TYPE_ACTION] = {is_set_action, apply_env_action, apply_cli_action, format_help_placeholder_action},
};

// ============================================================================
// Handler Function Implementations
// ============================================================================

// --- is_set handlers ---
static bool is_set_bool(const void *field, const option_descriptor_t *desc) {
  // Safely read the bool value (may be uninitialized)
  unsigned char value_byte = 0;
  memcpy(&value_byte, field, 1);
  bool value = (value_byte != 0);

  bool default_val = false;
  if (desc && desc->default_value) {
    unsigned char default_byte = 0;
    memcpy(&default_byte, desc->default_value, 1);
    default_val = (default_byte != 0);
  }
  return value != default_val;
}

static bool is_set_int(const void *field, const option_descriptor_t *desc) {
  int value = *(const int *)field;
  int default_val = desc->default_value ? *(const int *)desc->default_value : 0;
  return value != default_val;
}

static bool is_set_string(const void *field, const option_descriptor_t *desc) {
  const char *value = (const char *)field;
  const char *default_val = NULL;
  if (desc->default_value) {
    default_val = *(const char *const *)desc->default_value;
  }
  if (!default_val) {
    return (value && value[0] != '\0');
  }
  return strcmp(value, default_val) != 0;
}

static bool is_set_double(const void *field, const option_descriptor_t *desc) {
  double value = 0.0;
  double default_val = 0.0;
  memcpy(&value, field, sizeof(double));
  if (desc->default_value) {
    memcpy(&default_val, desc->default_value, sizeof(double));
  }
  return value != default_val;
}

static bool is_set_callback(const void *field, const option_descriptor_t *desc) {
  (void)desc;
  const void *value = NULL;
  memcpy(&value, field, sizeof(const void *));
  return value != NULL;
}

static bool is_set_action(const void *field, const option_descriptor_t *desc) {
  (void)field;
  (void)desc;
  return false;
}

// --- apply_env handlers ---
static void apply_env_bool(void *field, const char *env_value, const option_descriptor_t *desc) {
  // Safely read the current bool value (may be uninitialized)
  unsigned char current_byte = 0;
  memcpy(&current_byte, field, 1);
  bool current_value = (current_byte != 0);

  // Safely read the default value
  bool default_val = false;
  if (desc && desc->default_value) {
    unsigned char default_byte = 0;
    memcpy(&default_byte, desc->default_value, 1);
    default_val = (default_byte != 0);
  }

  if (current_value != default_val) {
    return; // Already set, skip env var
  }

  bool value = false;
  if (env_value) {
    value = (strcmp(env_value, "1") == 0 || strcmp(env_value, "true") == 0 || strcmp(env_value, "yes") == 0 ||
             strcmp(env_value, "on") == 0);
  } else if (desc && desc->default_value) {
    unsigned char default_byte = 0;
    memcpy(&default_byte, desc->default_value, 1);
    value = (default_byte != 0);
  }

  unsigned char value_byte = value ? 1 : 0;
  memcpy(field, &value_byte, 1);
}

static void apply_env_int(void *field, const char *env_value, const option_descriptor_t *desc) {
  int current_value = 0;
  memcpy(&current_value, field, sizeof(int));
  int default_val = desc->default_value ? *(const int *)desc->default_value : 0;
  if (current_value != default_val) {
    return; // Already set, skip env var
  }
  int value = 0;
  if (env_value) {
    char *endptr;
    long parsed = strtol(env_value, &endptr, 10);
    if (*endptr == '\0' && parsed >= INT_MIN && parsed <= INT_MAX) {
      value = (int)parsed;
    }
  } else if (desc->default_value) {
    value = *(const int *)desc->default_value;
  }
  memcpy(field, &value, sizeof(int));
}

static void apply_env_string(void *field, const char *env_value, const option_descriptor_t *desc) {
  const char *current_value = (const char *)field;
  const char *default_val = NULL;
  if (desc->default_value) {
    default_val = *(const char *const *)desc->default_value;
  }
  if (current_value && current_value[0] != '\0') {
    if (!default_val || strcmp(current_value, default_val) != 0) {
      return; // Already set, skip
    }
  }
  const char *value = NULL;
  if (env_value) {
    value = env_value;
  } else if (desc->default_value) {
    value = *(const char *const *)desc->default_value;
  }
  if (value && value[0] != '\0') {
    char *dest = (char *)field;
    snprintf(dest, OPTIONS_BUFF_SIZE, "%s", value);
    dest[OPTIONS_BUFF_SIZE - 1] = '\0';
  }
}

static void apply_env_double(void *field, const char *env_value, const option_descriptor_t *desc) {
  double current_value = 0.0;
  memcpy(&current_value, field, sizeof(double));
  double default_val = 0.0;
  if (desc->default_value) {
    memcpy(&default_val, desc->default_value, sizeof(double));
  }
  if (current_value != default_val) {
    return; // Already set, skip env var
  }
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
  memcpy(field, &value, sizeof(double));
}

static void apply_env_callback(void *field, const char *env_value, const option_descriptor_t *desc) {
  (void)field;
  (void)env_value;
  // For callbacks, would need parse_fn - handled separately in options_config_set_defaults
  (void)desc;
}

static void apply_env_action(void *field, const char *env_value, const option_descriptor_t *desc) {
  (void)field;
  (void)env_value;
  (void)desc;
}

// --- apply_cli handlers ---
static asciichat_error_t apply_cli_bool(void *field, const char *opt_value, const option_descriptor_t *desc) {
  (void)opt_value;
  (void)desc;
  unsigned char value_byte = 1; // true
  memcpy(field, &value_byte, 1);
  return ASCIICHAT_OK;
}

static asciichat_error_t apply_cli_int(void *field, const char *opt_value, const option_descriptor_t *desc) {
  (void)desc;
  char *endptr;
  long value = strtol(opt_value, &endptr, 10);
  if (*endptr != '\0' || value < INT_MIN || value > INT_MAX) {
    return ERROR_USAGE;
  }
  int int_value = (int)value;
  memcpy(field, &int_value, sizeof(int));
  return ASCIICHAT_OK;
}

static asciichat_error_t apply_cli_string(void *field, const char *opt_value, const option_descriptor_t *desc) {
  (void)desc;
  char *dest = (char *)field;
  snprintf(dest, OPTIONS_BUFF_SIZE, "%s", opt_value);
  dest[OPTIONS_BUFF_SIZE - 1] = '\0';
  return ASCIICHAT_OK;
}

static asciichat_error_t apply_cli_double(void *field, const char *opt_value, const option_descriptor_t *desc) {
  (void)desc;
  char *endptr;
  double value = strtod(opt_value, &endptr);
  if (*endptr != '\0') {
    return ERROR_USAGE;
  }
  memcpy(field, &value, sizeof(double));
  return ASCIICHAT_OK;
}

static asciichat_error_t apply_cli_callback(void *field, const char *opt_value, const option_descriptor_t *desc) {
  if (desc->parse_fn) {
    char *error_msg = NULL;
    if (!desc->parse_fn(opt_value, field, &error_msg)) {
      asciichat_error_t err = SET_ERRNO(ERROR_USAGE, "Parse error: %s", error_msg ? error_msg : "unknown");
      free(error_msg);
      return err;
    }
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t apply_cli_action(void *field, const char *opt_value, const option_descriptor_t *desc) {
  (void)field;
  (void)opt_value;
  if (desc->action_fn) {
    desc->action_fn();
  }
  return ASCIICHAT_OK;
}

// --- format_help_placeholder handlers ---
static void format_help_placeholder_bool(char *buf, size_t bufsize) {
  (void)buf;
  (void)bufsize;
  // Bools don't have placeholders
}

static void format_help_placeholder_int(char *buf, size_t bufsize) {
  snprintf(buf, bufsize, "NUM");
}

static void format_help_placeholder_string(char *buf, size_t bufsize) {
  snprintf(buf, bufsize, "STR");
}

static void format_help_placeholder_double(char *buf, size_t bufsize) {
  snprintf(buf, bufsize, "NUM");
}

static void format_help_placeholder_callback(char *buf, size_t bufsize) {
  snprintf(buf, bufsize, "VAL");
}

static void format_help_placeholder_action(char *buf, size_t bufsize) {
  (void)buf;
  (void)bufsize;
  // Actions don't have placeholders
}

// ============================================================================
// Help Formatting Helper Functions
// ============================================================================

/**
 * @brief Get help placeholder string for an option type
 * @param desc Option descriptor
 * @return Pointer to string literal ("NUM", "STR", "VAL"), custom placeholder, or empty string
 */
static const char *get_option_help_placeholder_str(const option_descriptor_t *desc) {
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
static int format_option_default_value_str(const option_descriptor_t *desc, char *buf, size_t bufsize) {
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
          return snprintf(buf, bufsize, "%s", desc->metadata.enum_values[i]);
        }
      }
    } else {
      // Fallback: assume sequential 0-based indices if integer values not provided
      if (default_int_val >= 0 && (size_t)default_int_val < desc->metadata.enum_count) {
        return snprintf(buf, bufsize, "%s", desc->metadata.enum_values[default_int_val]);
      }
    }
  }

  // For callback options storing numeric types (double/float), format them as numbers
  if (desc->type == OPTION_TYPE_CALLBACK && desc->default_value && desc->metadata.enum_values == NULL) {
    // Check if this is a numeric callback by looking for numeric_range metadata
    // Numeric callbacks have min/max range constraints set (max != 0 indicates a range was set)
    if (desc->metadata.numeric_range.max != 0 || desc->metadata.numeric_range.min != 0) {
      // This is a numeric callback option - extract and format the double value
      double default_double = 0.0;
      memcpy(&default_double, desc->default_value, sizeof(double));

      // Format with appropriate precision (remove trailing zeros for integers)
      char formatted[32];
      snprintf(formatted, sizeof(formatted), "%.1f", default_double);

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

      return snprintf(buf, bufsize, "%s", formatted);
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
static bool option_applies_to_mode(const option_descriptor_t *desc, asciichat_mode_t mode, bool for_binary_help) {
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

  // Use handler registry to check if option is set
  if (desc->type >= 0 && desc->type < (int)(sizeof(g_builder_handlers) / sizeof(g_builder_handlers[0]))) {
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
                              .owns_memory = false,
                              .mode_bitmask = OPTION_MODE_NONE};

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
    if (desc->type >= 0 && desc->type < (int)(sizeof(g_builder_handlers) / sizeof(g_builder_handlers[0]))) {
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
 * Uses mode_bitmask from options_struct for mode filtering.
 *
 * @param config Options configuration
 * @param argv Current argv array
 * @param argv_index Current index in argv
 * @param argc Total number of arguments
 * @param options_struct Destination struct for parsed values
 * @param consumed_count Output: number of argv elements consumed (1 or 2)
 * @return Error code
 */
static asciichat_error_t parse_single_flag_with_mode(const options_config_t *config, char **argv, int argv_index,
                                                     int argc, void *options_struct, option_mode_bitmask_t mode_bitmask,
                                                     int *consumed_count);

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

    // Try to suggest a similar option
    const char *suggestion = find_similar_option_with_mode(arg, config, mode_bitmask);
    if (suggestion) {
      log_plain_stderr("Unknown option: %s. %s", arg, suggestion);
    } else {
      log_plain_stderr("Unknown option: %s", arg);
    }
    return ERROR_USAGE;
  }

  // Check if option applies to current mode based on the passed mode_bitmask
  if (desc->mode_bitmask != 0 && !(desc->mode_bitmask & OPTION_MODE_BINARY)) {
    // Option has specific mode restrictions - use the passed mode_bitmask directly
    if (!(desc->mode_bitmask & mode_bitmask)) {
      if (equals)
        *equals = '='; // Restore
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
  if (desc->type < sizeof(g_builder_handlers) / sizeof(g_builder_handlers[0])) {
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
        if (equals)
          *equals = '='; // Restore
        return result;
      }
    }
  } else {
    if (equals)
      *equals = '='; // Restore
    return SET_ERRNO(ERROR_USAGE, "Invalid option type for %s", arg);
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
    if (desc->type >= 0 && desc->type < (int)(sizeof(g_builder_handlers) / sizeof(g_builder_handlers[0]))) {
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

  const char *binary_name = PLATFORM_BINARY_NAME;

  int max_col_width = 0;
  char temp_buf[BUFFER_SIZE_MEDIUM];

  // Check USAGE entries (capped at 45 chars for max first column)
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
    if (w > LAYOUT_COLUMN_WIDTH) {
      w = LAYOUT_COLUMN_WIDTH;
    }
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check EXAMPLES entries
  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];
    int len = 0;

    // Only prepend program name if this is not a utility command
    if (!example->is_utility_command) {
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);
    }

    if (example->args) {
      const char *colored_args = colored_string(LOG_COLOR_INFO, example->args);
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_args);
    }

    int w = utf8_display_width(temp_buf);
    if (w > LAYOUT_COLUMN_WIDTH) {
      w = LAYOUT_COLUMN_WIDTH;
    }
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check MODES entries (capped at 45 chars)
  for (size_t i = 0; i < config->num_modes; i++) {
    const char *colored_name = colored_string(LOG_COLOR_FATAL, config->modes[i].name);
    int w = utf8_display_width(colored_name);
    if (w > LAYOUT_COLUMN_WIDTH) {
      w = LAYOUT_COLUMN_WIDTH;
    }
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check OPTIONS entries (from descriptors)
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->hide_from_mode_help || desc->hide_from_binary_help || !desc->group)
      continue;

    // Build option display string with separate coloring for short and long flags
    char opts_buf[BUFFER_SIZE_SMALL];
    if (desc->short_name && desc->short_name != '\0') {
      char short_flag[16];
      snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
      char long_flag[BUFFER_SIZE_SMALL];
      snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
      // Color short flag, add comma, color long flag
      snprintf(opts_buf, sizeof(opts_buf), "%s, %s", colored_string(LOG_COLOR_WARN, short_flag),
               colored_string(LOG_COLOR_WARN, long_flag));
    } else {
      char long_flag[BUFFER_SIZE_SMALL];
      snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
      snprintf(opts_buf, sizeof(opts_buf), "%s", colored_string(LOG_COLOR_WARN, long_flag));
    }
    const char *colored_opts = opts_buf;

    int w = utf8_display_width(colored_opts);
    if (w > LAYOUT_COLUMN_WIDTH) {
      w = LAYOUT_COLUMN_WIDTH;
    }
    if (w > max_col_width)
      max_col_width = w;
  }

  // Enforce maximum column width of 45 characters
  if (max_col_width > 45) {
    max_col_width = 45;
  }

  return max_col_width;
}

/* ============================================================================
 * Per-Section Column Width Calculation (Abstract)
 * ============================================================================ */

/**
 * @brief Calculate max column width for a specific section type
 *
 * Abstract function that calculates the maximum width needed for items
 * in a specific section (USAGE, EXAMPLES, MODES, OPTIONS, POSITIONAL_ARGUMENTS).
 * Capped at 75 characters.
 *
 * @param config Options configuration
 * @param section_type Type of section: "usage", "examples", "modes", "options", or "positional"
 * @param mode Current mode (used for filtering)
 * @param for_binary_help Whether this is for binary-level help
 * @return Maximum width needed for this section (minimum 20, maximum 75)
 */
static int calculate_section_max_col_width(const options_config_t *config, const char *section_type,
                                           asciichat_mode_t mode, bool for_binary_help) {
  if (!config || !section_type) {
    return 20; // Minimum width
  }

  int max_width = 0;
  const char *binary_name = PLATFORM_BINARY_NAME;
  char temp_buf[BUFFER_SIZE_MEDIUM];

  if (strcmp(section_type, "usage") == 0) {
    // Calculate max width for USAGE section (use plain text, no ANSI codes)
    if (config->num_usage_lines == 0)
      return 20;

    // Get mode name for filtering usage lines (same logic as options_print_help_for_mode)
    const char *mode_name = NULL;
    if (!for_binary_help) {
      switch (mode) {
      case MODE_SERVER:
        mode_name = "server";
        break;
      case MODE_CLIENT:
        mode_name = "client";
        break;
      case MODE_MIRROR:
        mode_name = "mirror";
        break;
      case MODE_DISCOVERY_SERVICE:
        mode_name = "discovery-service";
        break;
      case MODE_DISCOVERY:
        mode_name = NULL; // Binary help uses MODE_DISCOVERY but shows all usage lines
        break;
      default:
        mode_name = NULL;
        break;
      }
    }

    for (size_t i = 0; i < config->num_usage_lines; i++) {
      const usage_descriptor_t *usage = &config->usage_lines[i];

      // Filter usage lines by mode (same logic as options_print_help_for_mode)
      if (!for_binary_help) {
        // For mode-specific help, show ONLY the current mode's usage line
        // Don't show generic binary-level or placeholder lines
        if (!usage->mode || strcmp(usage->mode, mode_name) != 0) {
          continue;
        }
      }

      int len = 0;

      // Build plain text version for width calculation (no ANSI codes)
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);

      if (usage->mode) {
        len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", usage->mode);
      }

      if (usage->positional) {
        len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", usage->positional);
      }

      if (usage->show_options) {
        const char *options_text =
            (usage->mode && strcmp(usage->mode, "<mode>") == 0) ? "[mode-options...]" : "[options...]";
        len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", options_text);
      }

      int w = utf8_display_width_n(temp_buf, len);
      if (w > max_width)
        max_width = w;
    }

    // Cap USAGE section at 50 chars max (usage lines are inherently shorter)
    if (max_width > 50)
      max_width = 50;
  } else if (strcmp(section_type, "examples") == 0) {
    // Calculate max width for EXAMPLES section
    if (config->num_examples == 0)
      return 20;

    for (size_t i = 0; i < config->num_examples; i++) {
      const example_descriptor_t *example = &config->examples[i];

      // Filter examples by mode using bitmask
      if (for_binary_help) {
        if (!(example->mode_bitmask & OPTION_MODE_BINARY))
          continue;
      } else {
        uint32_t mode_bitmask = (1 << mode);
        if (!(example->mode_bitmask & mode_bitmask))
          continue;
      }

      int len = 0;
      len += snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);

      if (example->args) {
        len += snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", example->args);
      }

      int w = utf8_display_width_n(temp_buf, len);
      if (w > max_width)
        max_width = w;
    }

    // Cap EXAMPLES section at 75 chars max
    if (max_width > 75)
      max_width = 75;
  } else if (strcmp(section_type, "modes") == 0) {
    // Calculate max width for MODES section
    if (config->num_modes == 0)
      return 20;

    for (size_t i = 0; i < config->num_modes; i++) {
      // Use plain text for width calculation (no ANSI codes)
      int w = utf8_display_width(config->modes[i].name);
      if (w > max_width)
        max_width = w;
    }

    // Cap MODES section at 30 chars max (mode names are short)
    if (max_width > 30)
      max_width = 30;
  } else if (strcmp(section_type, "options") == 0) {
    // Calculate max width for OPTIONS section
    if (config->num_descriptors == 0)
      return 20;

    char option_str[BUFFER_SIZE_MEDIUM];

    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];

      // Filter by mode and visibility
      if (!option_applies_to_mode(desc, mode, for_binary_help) || !desc->group || desc->hide_from_mode_help ||
          desc->hide_from_binary_help) {
        continue;
      }

      int option_len = 0;

      if (desc->short_name) {
        char short_flag[16];
        snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
        char long_flag[BUFFER_SIZE_SMALL];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s, %s",
                               colored_string(LOG_COLOR_WARN, short_flag), colored_string(LOG_COLOR_WARN, long_flag));
      } else {
        char long_flag[BUFFER_SIZE_SMALL];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                               colored_string(LOG_COLOR_WARN, long_flag));
      }

      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, " ");
        const char *placeholder = get_option_help_placeholder_str(desc);
        if (placeholder[0] != '\0') {
          option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                 colored_string(LOG_COLOR_INFO, placeholder));
        }
      }

      int w = utf8_display_width(option_str);
      if (w > max_width)
        max_width = w;
    }
  } else if (strcmp(section_type, "positional") == 0) {
    // Calculate max width for POSITIONAL ARGUMENTS section
    if (config->num_positional_args == 0)
      return 20;

    option_mode_bitmask_t current_mode_bitmask = 1U << mode;

    for (size_t pa_idx = 0; pa_idx < config->num_positional_args; pa_idx++) {
      const positional_arg_descriptor_t *pos_arg = &config->positional_args[pa_idx];

      // Filter by mode_bitmask
      if (pos_arg->mode_bitmask != 0 && !(pos_arg->mode_bitmask & current_mode_bitmask)) {
        continue;
      }

      if (pos_arg->examples) {
        for (size_t i = 0; i < pos_arg->num_examples; i++) {
          const char *example = pos_arg->examples[i];
          const char *p = example;

          // Skip leading spaces
          while (*p == ' ')
            p++;
          const char *first_part = p;

          // Find double-space delimiter
          while (*p && !(*p == ' ' && *(p + 1) == ' '))
            p++;
          int first_len_bytes = (int)(p - first_part);

          int w = utf8_display_width_n(first_part, first_len_bytes);
          if (w > max_width)
            max_width = w;
        }
      }
    }
  }

  // Cap at 75 characters
  if (max_width > 75)
    max_width = 75;

  return max_width > 20 ? max_width : 20;
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

  const char *binary_name = PLATFORM_BINARY_NAME;

  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "USAGE:"));

  // Build colored syntax strings using colored_string() for all components
  for (size_t i = 0; i < config->num_usage_lines; i++) {
    const usage_descriptor_t *usage = &config->usage_lines[i];
    char usage_buf[BUFFER_SIZE_MEDIUM];
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
static void print_examples_section(const options_config_t *config, FILE *stream, int term_width, int max_col_width,
                                   asciichat_mode_t mode, bool for_binary_help) {
  if (!config || !stream || config->num_examples == 0) {
    return;
  }

  const char *binary_name = PLATFORM_BINARY_NAME;

  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "EXAMPLES:"));

  // Build colored command strings using colored_string() for all components
  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];

    // Filter examples based on mode bitmask
    if (for_binary_help) {
      // Binary help shows only examples with OPTION_MODE_BINARY flag
      if (!(example->mode_bitmask & OPTION_MODE_BINARY)) {
        continue;
      }
    } else {
      // For mode-specific help, show examples with matching mode
      // Convert mode enum to bitmask
      uint32_t mode_bitmask = (1 << mode);
      if (!(example->mode_bitmask & mode_bitmask)) {
        continue;
      }
    }

    char cmd_buf[BUFFER_SIZE_MEDIUM];
    int len = 0;

    // Only add binary name if this is not a utility command
    if (!example->is_utility_command) {
      len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s", binary_name);
    }

    // Add args/flags if present (flags=yellow, arguments=green, utility programs=white)
    if (example->args) {
      // Only add space if we've already added something (binary name)
      if (len > 0) {
        len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, " ");
      }

      // For utility commands, color everything white except flags (yellow)
      // For regular examples, color flags yellow and arguments green
      if (example->is_utility_command) {
        // Utility command: only flags get yellow, everything else is white
        const char *p = example->args;
        char current_token[BUFFER_SIZE_SMALL];
        int token_len = 0;

        while (*p) {
          if (*p == ' ' || *p == '|' || *p == '>' || *p == '<') {
            // Flush current token if any
            if (token_len > 0) {
              current_token[token_len] = '\0';
              // Flags (start with -) are yellow, everything else is white
              if (current_token[0] == '-') {
                len +=
                    snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s", colored_string(LOG_COLOR_WARN, current_token));
              } else {
                len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                                colored_string(LOG_COLOR_RESET, current_token));
              }
              token_len = 0;
            }
            // Add the separator (space, pipe, redirect, etc) in white
            if (*p != ' ') {
              len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s ",
                              colored_string(LOG_COLOR_RESET, (*p == '|')   ? "|"
                                                              : (*p == '>') ? ">"
                                                                            : "<"));
            } else {
              len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, " ");
            }
            p++;
            // Skip multiple spaces
            while (*p == ' ')
              p++;
          } else {
            current_token[token_len++] = *p;
            p++;
          }
        }

        // Flush last token
        if (token_len > 0) {
          current_token[token_len] = '\0';
          if (current_token[0] == '-') {
            len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s", colored_string(LOG_COLOR_WARN, current_token));
          } else {
            len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s", colored_string(LOG_COLOR_RESET, current_token));
          }
        }
      } else {
        // Regular example: flags=yellow, arguments=green
        const char *p = example->args;
        char current_token[BUFFER_SIZE_SMALL];
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
            while (*p == ' ')
              p++;
          } else {
            current_token[token_len++] = *p;
            p++;
          }
        }

        // Flush last token
        if (token_len > 0) {
          current_token[token_len] = '\0';
          if (current_token[0] == '-') {
            len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s", colored_string(LOG_COLOR_WARN, current_token));
          } else {
            len += snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s", colored_string(LOG_COLOR_INFO, current_token));
          }
        }
      }

      // Remove trailing space if added
      if (len > 0 && cmd_buf[len - 1] == ' ') {
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
    char mode_buf[BUFFER_SIZE_SMALL];
    snprintf(mode_buf, sizeof(mode_buf), "%s", colored_string(LOG_COLOR_FATAL, config->modes[i].name));
    layout_print_two_column_row(stream, mode_buf, config->modes[i].description, max_col_width, term_width);
  }

  fprintf(stream, "\n");
}

/**
 * @brief Print MODE-OPTIONS section programmatically
 */
static void print_mode_options_section(FILE *stream, int term_width, int max_col_width) {
  const char *binary_name = PLATFORM_BINARY_NAME;

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

  // Binary-level help uses MODE_DISCOVERY internally
  asciichat_mode_t mode = MODE_DISCOVERY;
  bool for_binary_help = true;

  // Calculate per-section column widths (each section is independent, capped at 75)
  int usage_max_col_width = calculate_section_max_col_width(config, "usage", mode, for_binary_help);
  int modes_max_col_width = calculate_section_max_col_width(config, "modes", mode, for_binary_help);
  int examples_max_col_width = calculate_section_max_col_width(config, "examples", mode, for_binary_help);
  int options_max_col_width = calculate_section_max_col_width(config, "options", mode, for_binary_help);

  // Print programmatically generated sections (USAGE, MODES, MODE-OPTIONS, EXAMPLES)
  print_usage_section(config, stream, term_width, usage_max_col_width);
  print_modes_section(config, stream, term_width, modes_max_col_width);
  print_mode_options_section(stream, term_width, 40); // Keep reasonable width for mode-options
  print_examples_section(config, stream, term_width, examples_max_col_width, MODE_SERVER, true);

  // Build list of unique groups in order of first appearance

  const char **unique_groups = SAFE_MALLOC(config->num_descriptors * sizeof(const char *), const char **);
  size_t num_unique_groups = 0;

  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    // Filter by mode_bitmask - for binary help, show binary options
    if (!option_applies_to_mode(desc, MODE_SERVER, for_binary_help) || !desc->group) {
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

      // Skip if not in current group or if doesn't apply to mode
      if (!option_applies_to_mode(desc, MODE_SERVER, for_binary_help) || !desc->group ||
          strcmp(desc->group, current_group) != 0) {
        continue;
      }

      // Build option string with separate coloring for short and long flags
      char option_str[BUFFER_SIZE_MEDIUM] = "";
      int option_len = 0;

      // Short name and long name with separate coloring
      if (desc->short_name) {
        char short_flag[16];
        snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
        char long_flag[BUFFER_SIZE_SMALL];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        // Color short flag, plain comma-space, color long flag
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s, %s",
                               colored_string(LOG_COLOR_WARN, short_flag), colored_string(LOG_COLOR_WARN, long_flag));
      } else {
        char long_flag[BUFFER_SIZE_SMALL];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                               colored_string(LOG_COLOR_WARN, long_flag));
      }

      // Value placeholder (colored green)
      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, " ");
        const char *placeholder = get_option_help_placeholder_str(desc);
        if (placeholder[0] != '\0') {
          option_len += snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                 colored_string(LOG_COLOR_INFO, placeholder));
        }
      }

      // Build description string (plain text, colors applied when printing)
      char desc_str[BUFFER_SIZE_MEDIUM] = "";
      int desc_len = 0;

      if (desc->help_text) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s", desc->help_text);
      }

      // Skip adding default if the description already mentions it
      bool description_has_default =
          desc->help_text && (strstr(desc->help_text, "(default:") || strstr(desc->help_text, "=default)"));

      if (desc->default_value && !description_has_default) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s ",
                             colored_string(LOG_COLOR_FATAL, "default:"));
        char default_buf[32];
        if (format_option_default_value_str(desc, default_buf, sizeof(default_buf)) > 0) {
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, default_buf));
        }
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, ")");
      }

      if (desc->required) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " [REQUIRED]");
      }

      if (desc->env_var_name) {
        // Color env: label and variable name grey
        desc_len +=
            snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s %s)",
                     colored_string(LOG_COLOR_GREY, "env:"), colored_string(LOG_COLOR_GREY, desc->env_var_name));
      }

      // Use layout function with section-specific column width for consistent alignment
      // option_str already contains colored_string() results, so pass it directly
      layout_print_two_column_row(stream, option_str, desc_str, options_max_col_width, term_width);
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
void options_config_print_options_sections_with_width(const options_config_t *config, FILE *stream, int max_col_width,
                                                      asciichat_mode_t mode) {
  if (!config || !stream) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Config or stream is NULL");
    return;
  }

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

  // CAP max_col_width: 86 if terminal wide, otherwise 45 for narrow first column
  int max_col_cap = (term_width > 170) ? 86 : 45;
  if (max_col_width > max_col_cap) {
    max_col_width = max_col_cap;
  }

  // Determine if this is binary-level help
  // Discovery mode IS the binary-level help (shows binary options + discovery options)
  // Other modes show only mode-specific options
  bool for_binary_help = (mode == MODE_DISCOVERY);
  // Build list of unique groups in order of first appearance
  const char **unique_groups = SAFE_MALLOC(config->num_descriptors * sizeof(const char *), const char **);
  size_t num_unique_groups = 0;

  // For binary-level help, we now use a two-pass system to order groups,
  // ensuring binary-level option groups appear before discovery-mode groups.
  if (for_binary_help) {
    // Pass 1: Collect groups with binary options (ensure LOGGING is first if present)
    bool logging_added = false;
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      if (desc->group && strcmp(desc->group, "LOGGING") == 0) {
        unique_groups[num_unique_groups++] = "LOGGING";
        logging_added = true;
        break;
      }
    }

    // Pass 2: Collect all other groups that apply for binary help (all modes), if not already added.
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      // An option applies if option_applies_to_mode says so for binary help (which checks OPTION_MODE_ALL)
      if (option_applies_to_mode(desc, mode, for_binary_help) && desc->group) {
        // Skip LOGGING group if we already added it (for binary help)
        if (logging_added && strcmp(desc->group, "LOGGING") == 0) {
          continue;
        }

        bool group_exists = false;
        for (size_t j = 0; j < num_unique_groups; j++) {
          if (strcmp(unique_groups[j], desc->group) == 0) {
            group_exists = true;
            break;
          }
        }
        if (!group_exists) {
          unique_groups[num_unique_groups++] = desc->group;
        }
      } else if (desc->group) {
      }
    }
  } else {
    // Original logic for other modes
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      if (option_applies_to_mode(desc, mode, for_binary_help) && desc->group) {
        bool group_exists = false;
        for (size_t j = 0; j < num_unique_groups; j++) {
          if (strcmp(unique_groups[j], desc->group) == 0) {
            group_exists = true;
            break;
          }
        }
        if (!group_exists) {
          unique_groups[num_unique_groups++] = desc->group;
        }
      }
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
      if (!option_applies_to_mode(desc, mode, for_binary_help) || !desc->group ||
          strcmp(desc->group, current_group) != 0) {
        continue;
      }

      // Build option string (flag part) with separate coloring for short and long flags
      char colored_option_str[BUFFER_SIZE_MEDIUM] = "";
      int colored_len = 0;

      // Short name and long name with separate coloring
      if (desc->short_name) {
        char short_flag[16];
        snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
        char long_flag[BUFFER_SIZE_SMALL];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        // Color short flag, plain comma-space, color long flag
        colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s, %s",
                                colored_string(LOG_COLOR_WARN, short_flag), colored_string(LOG_COLOR_WARN, long_flag));
      } else {
        char long_flag[BUFFER_SIZE_SMALL];
        snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                colored_string(LOG_COLOR_WARN, long_flag));
      }

      // Value placeholder (colored green)
      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, " ");
        const char *placeholder = get_option_help_placeholder_str(desc);
        if (placeholder[0] != '\0') {
          colored_len += snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                  colored_string(LOG_COLOR_INFO, placeholder));
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

      if (desc->default_value && !description_has_default) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s ",
                             colored_string(LOG_COLOR_FATAL, "default:"));
        char default_buf[32];
        if (format_option_default_value_str(desc, default_buf, sizeof(default_buf)) > 0) {
          desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s",
                               colored_string(LOG_COLOR_FATAL, default_buf));
        }
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, ")");
      }

      if (desc->required) {
        desc_len += snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " [REQUIRED]");
      }

      if (desc->env_var_name) {
        // Color env: label and variable name grey
        desc_len +=
            snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s %s)",
                     colored_string(LOG_COLOR_GREY, "env:"), colored_string(LOG_COLOR_GREY, desc->env_var_name));
      }

      layout_print_two_column_row(stream, colored_option_str, desc_str, max_col_width, term_width);
    }
  }

  // Cleanup
  SAFE_FREE(unique_groups);
}

/**
 * @brief Print everything except the USAGE section (backward compatibility wrapper)
 *
 * Calls options_config_print_options_sections_with_width with auto-calculation.
 */
void options_config_print_options_sections(const options_config_t *config, FILE *stream, asciichat_mode_t mode) {
  options_config_print_options_sections_with_width(config, stream, 0, mode);
}

// ============================================================================
// Unified Help Printing Function
// ============================================================================

/**
 * @brief Print help for a specific mode or binary level
 *
 * This is the single unified function for all help output (binary level and all modes).
 * It handles common layout logic, terminal detection, and section printing.
 *
 * @param config Options config with all options (binary + all modes)
 * @param mode Mode to show help for (use -1 for binary-level help)
 * @param program_name Full program name with mode (e.g., "ascii-chat server")
 * @param description Brief description of the mode/binary
 * @param desc Output file stream (usually stdout)
 */
void options_print_help_for_mode(const options_config_t *config, asciichat_mode_t mode, const char *program_name,
                                 const char *description, FILE *desc) {
  if (!config || !desc) {
    if (desc) {
      fprintf(desc, "Error: Failed to create options config\n");
    } else {
      SET_ERRNO(ERROR_INVALID_PARAM, "Config or desc is NULL");
    }
    return;
  }

  // Print program name and description (color mode name magenta if it's a mode-specific help)
  if (program_name) {
    const char *space = strchr(program_name, ' ');
    if (space && mode >= 0) {
      // Mode-specific help: color the mode name
      int binary_len = space - program_name;
      (void)fprintf(desc, "%.*s %s - %s\n\n", binary_len, program_name, colored_string(LOG_COLOR_FATAL, space + 1),
                    description);
    } else {
      // Binary-level help: use colored_string for the program name too
      (void)fprintf(desc, "%s - %s\n\n", colored_string(LOG_COLOR_FATAL, program_name), description);
    }
  }

  // Print project links
  print_project_links(desc);
  (void)fprintf(desc, "\n");

  // Detect terminal width
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

  // Determine if this is binary-level help (called for 'ascii-chat --help')
  // Binary help uses MODE_DISCOVERY as the mode value
  bool for_binary_help = (mode == MODE_DISCOVERY);

  // Print USAGE section (with section-specific column width and mode filtering)
  fprintf(desc, "%s\n", colored_string(LOG_COLOR_DEBUG, "USAGE:"));
  if (config->num_usage_lines > 0) {
    // Get mode name for filtering usage lines
    const char *mode_name = NULL;
    switch (mode) {
    case MODE_SERVER:
      mode_name = "server";
      break;
    case MODE_CLIENT:
      mode_name = "client";
      break;
    case MODE_MIRROR:
      mode_name = "mirror";
      break;
    case MODE_DISCOVERY_SERVICE:
      mode_name = "discovery-service";
      break;
    case MODE_DISCOVERY:
      mode_name = NULL; // Binary help shows all usage lines
      break;
    default:
      mode_name = NULL;
      break;
    }

    int usage_max_col_width = calculate_section_max_col_width(config, "usage", mode, for_binary_help);

    for (size_t i = 0; i < config->num_usage_lines; i++) {
      const usage_descriptor_t *usage = &config->usage_lines[i];

      // Filter usage lines by mode
      if (!for_binary_help) {
        // For mode-specific help, show ONLY the current mode's usage line
        // Don't show generic binary-level or placeholder lines
        if (!usage->mode || strcmp(usage->mode, mode_name) != 0) {
          continue;
        }
      }

      char usage_buf[BUFFER_SIZE_MEDIUM];
      int len = 0;

      len += snprintf(usage_buf + len, sizeof(usage_buf) - len, "ascii-chat");

      if (usage->mode) {
        len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_FATAL, usage->mode));
      }

      if (usage->positional) {
        len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s",
                        colored_string(LOG_COLOR_INFO, usage->positional));
      }

      if (usage->show_options) {
        const char *options_text =
            (usage->mode && strcmp(usage->mode, "<mode>") == 0) ? "[mode-options...]" : "[options...]";
        len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_WARN, options_text));
      }

      layout_print_two_column_row(desc, usage_buf, usage->description, usage_max_col_width, term_width);
    }
  }
  fprintf(desc, "\n");

  // Print positional argument examples (with mode filtering and section-specific column width)
  if (config->num_positional_args > 0) {
    // First, check if any positional args apply to this mode
    option_mode_bitmask_t current_mode_bitmask = 1U << mode;
    bool has_applicable_positional_args = false;

    for (size_t pa_idx = 0; pa_idx < config->num_positional_args; pa_idx++) {
      const positional_arg_descriptor_t *pos_arg = &config->positional_args[pa_idx];

      // Filter by mode_bitmask (matching the parsing code logic)
      if (pos_arg->mode_bitmask != 0 && !(pos_arg->mode_bitmask & current_mode_bitmask)) {
        continue;
      }

      if (pos_arg->section_heading && pos_arg->examples && pos_arg->num_examples > 0) {
        has_applicable_positional_args = true;
        break;
      }
    }

    // Only print the section if there are applicable positional args
    if (has_applicable_positional_args) {
      int positional_max_col_width = calculate_section_max_col_width(config, "positional", mode, false);

      for (size_t pa_idx = 0; pa_idx < config->num_positional_args; pa_idx++) {
        const positional_arg_descriptor_t *pos_arg = &config->positional_args[pa_idx];

        // Filter by mode_bitmask (matching the parsing code logic)
        if (pos_arg->mode_bitmask != 0 && !(pos_arg->mode_bitmask & current_mode_bitmask)) {
          continue;
        }

        if (pos_arg->section_heading && pos_arg->examples && pos_arg->num_examples > 0) {
          (void)fprintf(desc, "%s\n", colored_string(LOG_COLOR_DEBUG, pos_arg->section_heading));

          for (size_t i = 0; i < pos_arg->num_examples; i++) {
            const char *example = pos_arg->examples[i];
            const char *p = example;
            const char *desc_start = NULL;

            while (*p == ' ')
              p++;
            const char *first_part = p;

            while (*p && !(*p == ' ' && *(p + 1) == ' '))
              p++;
            int first_len_bytes = (int)(p - first_part);

            while (*p == ' ')
              p++;
            if (*p) {
              desc_start = p;
            }

            char colored_first_part[256];
            snprintf(colored_first_part, sizeof(colored_first_part), "%.*s", first_len_bytes, first_part);
            char colored_result[512];
            snprintf(colored_result, sizeof(colored_result), "%s", colored_string(LOG_COLOR_INFO, colored_first_part));

            layout_print_two_column_row(desc, colored_result, desc_start ? desc_start : "", positional_max_col_width,
                                        term_width);
          }
          (void)fprintf(desc, "\n");
        }
      }
    }
  }

  // Print EXAMPLES section (with section-specific column width)
  int examples_max_col_width = calculate_section_max_col_width(config, "examples", mode, for_binary_help);
  print_examples_section(config, desc, term_width, examples_max_col_width, mode, for_binary_help);

  // Print options sections (with section-specific column width for options)
  int options_max_col_width = calculate_section_max_col_width(config, "options", mode, for_binary_help);
  options_config_print_options_sections_with_width(config, desc, options_max_col_width, mode);
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
