/**
 * @file handlers.c
 * @brief Type handler implementations for builder operations
 * @ingroup options
 *
 * Implements handler functions for each option type (bool, int, string, etc.)
 * that handle checking if set, applying environment variables, parsing CLI args,
 * and formatting help placeholders.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/builder/internal.h>
#include <ascii-chat/options/common.h>
#include <ascii-chat/util/string.h>
#include <string.h>
#include <limits.h>
#include <stdlib.h>

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

// ============================================================================
// Handler Registry - Exported for use by other modules
// ============================================================================

const option_builder_handler_t g_builder_handlers[] = {
    [OPTION_TYPE_BOOL] = {is_set_bool, apply_env_bool, apply_cli_bool, format_help_placeholder_bool},
    [OPTION_TYPE_INT] = {is_set_int, apply_env_int, apply_cli_int, format_help_placeholder_int},
    [OPTION_TYPE_STRING] = {is_set_string, apply_env_string, apply_cli_string, format_help_placeholder_string},
    [OPTION_TYPE_DOUBLE] = {is_set_double, apply_env_double, apply_cli_double, format_help_placeholder_double},
    [OPTION_TYPE_CALLBACK] = {is_set_callback, apply_env_callback, apply_cli_callback,
                              format_help_placeholder_callback},
    [OPTION_TYPE_ACTION] = {is_set_action, apply_env_action, apply_cli_action, format_help_placeholder_action},
};
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
    safe_snprintf(dest, OPTIONS_BUFF_SIZE, "%s", value);
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
// Helper function to parse boolean value from string
// Accepts: "true", "1", "yes", "on" → true
//          "false", "0", "no", "off" → false
// Returns ERROR_USAGE if value is invalid
static asciichat_error_t parse_bool_value(const char *value_str, bool *out_value, const option_descriptor_t *desc) {
  if (!value_str || value_str[0] == '\0') {
    return SET_ERRNO(ERROR_USAGE, "Option --%s requires a value (true/false, yes/no, 1/0, on/off)",
                     desc ? desc->long_name : "unknown");
  }

  // Check for true values
  if (strcasecmp(value_str, "true") == 0 || strcasecmp(value_str, "yes") == 0 || strcasecmp(value_str, "1") == 0 ||
      strcasecmp(value_str, "on") == 0) {
    *out_value = true;
    return ASCIICHAT_OK;
  }

  // Check for false values
  if (strcasecmp(value_str, "false") == 0 || strcasecmp(value_str, "no") == 0 || strcasecmp(value_str, "0") == 0 ||
      strcasecmp(value_str, "off") == 0) {
    *out_value = false;
    return ASCIICHAT_OK;
  }

  // Invalid value
  return SET_ERRNO(ERROR_USAGE, "Invalid boolean value for --%s: '%s' (use: true/false, yes/no, 1/0, on/off)",
                   desc ? desc->long_name : "unknown", value_str);
}

static asciichat_error_t apply_cli_bool(void *field, const char *opt_value, const option_descriptor_t *desc) {
  bool new_value;

  if (opt_value == NULL) {
    // No value provided (flag without =value), toggle the current value
    unsigned char current_byte = 0;
    memcpy(&current_byte, field, 1);
    bool current_value = (current_byte != 0);
    new_value = !current_value;
  } else {
    // Value provided (--option=value), parse it
    asciichat_error_t parse_result = parse_bool_value(opt_value, &new_value, desc);
    if (parse_result != ASCIICHAT_OK) {
      return parse_result;
    }
  }

  // Set the new value
  unsigned char new_value_byte = new_value ? 1 : 0;
  memcpy(field, &new_value_byte, 1);
  return ASCIICHAT_OK;
}

static asciichat_error_t apply_cli_int(void *field, const char *opt_value, const option_descriptor_t *desc) {
  // Reject empty strings
  if (!opt_value || opt_value[0] == '\0') {
    return SET_ERRNO(ERROR_USAGE, "Option --%s requires a numeric value", desc ? desc->long_name : "unknown");
  }

  char *endptr;
  long value = strtol(opt_value, &endptr, 10);
  if (*endptr != '\0' || value < INT_MIN || value > INT_MAX) {
    return ERROR_USAGE;
  }
  int int_value = (int)value;

  // Check numeric range constraints if defined in descriptor's metadata
  if (desc && desc->metadata.numeric_range.max != 0) {
    if (int_value < desc->metadata.numeric_range.min || int_value > desc->metadata.numeric_range.max) {
      return SET_ERRNO(ERROR_USAGE, "Value %d out of range [%d-%d]", int_value, desc->metadata.numeric_range.min,
                       desc->metadata.numeric_range.max);
    }
  }

  memcpy(field, &int_value, sizeof(int));
  return ASCIICHAT_OK;
}

static asciichat_error_t apply_cli_string(void *field, const char *opt_value, const option_descriptor_t *desc) {
  // Reject empty strings for certain important options like --key
  if (opt_value && opt_value[0] == '\0' && desc && desc->long_name) {
    if (strcmp(desc->long_name, "key") == 0) {
      return SET_ERRNO(ERROR_USAGE, "Option --%s cannot be empty", desc->long_name);
    }
  }

  char *dest = (char *)field;
  safe_snprintf(dest, OPTIONS_BUFF_SIZE, "%s", opt_value);
  dest[OPTIONS_BUFF_SIZE - 1] = '\0';
  return ASCIICHAT_OK;
}

static asciichat_error_t apply_cli_double(void *field, const char *opt_value, const option_descriptor_t *desc) {
  (void)desc;
  char *endptr;
  double value = strtod(opt_value, &endptr);
  // Check: endptr must have advanced past the input, and must be at the string end
  if (endptr == opt_value || *endptr != '\0') {
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
  safe_snprintf(buf, bufsize, "[BOOLEAN]");
}

static void format_help_placeholder_int(char *buf, size_t bufsize) {
  safe_snprintf(buf, bufsize, "INTEGER");
}

static void format_help_placeholder_string(char *buf, size_t bufsize) {
  safe_snprintf(buf, bufsize, "STRING");
}

static void format_help_placeholder_double(char *buf, size_t bufsize) {
  safe_snprintf(buf, bufsize, "NUMBER");
}

static void format_help_placeholder_callback(char *buf, size_t bufsize) {
  safe_snprintf(buf, bufsize, "VAL");
}

static void format_help_placeholder_action(char *buf, size_t bufsize) {
  (void)buf;
  (void)bufsize;
  // Actions don't have placeholders
}
