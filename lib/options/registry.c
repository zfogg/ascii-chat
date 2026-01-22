/**
 * @file registry.c
 * @brief Central options registry implementation
 * @ingroup options
 *
 * Defines all command-line options exactly once with mode bitmasks.
 * This is the single source of truth for all options.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "options/registry.h"
#include "options/parsers.h" // For parse_log_level, parse_color_mode, parse_palette_type, parse_palette_chars, etc.
#include "options/validation.h"
#include "options/common.h"
#include "common.h"
#include "log/logging.h"
#include "platform/terminal.h"
#include "video/palette.h"

#include <stdlib.h>
#include <string.h>

// Parser functions from parsers.h and presets.c
// parse_log_level, parse_color_mode, parse_render_mode, parse_palette_type, parse_palette_chars are in parsers.h
// parse_verbose_flag, parse_timestamp, parse_cookies_from_browser are static in presets.c
// For now, we'll need to make these non-static or move them to parsers.h
// Stub implementations for functions that are static in presets.c
static bool parse_verbose_flag(const char *arg, void *dest, char **error_msg) {
  (void)error_msg;
  unsigned short int *verbose_level = (unsigned short int *)dest;
  if (!arg || arg[0] == '\0') {
    (*verbose_level)++;
    return true;
  }
  long value = strtol(arg, NULL, 10);
  if (value >= 0 && value <= 100) {
    *verbose_level = (unsigned short int)value;
    return true;
  }
  (*verbose_level)++;
  return true;
}

static bool parse_timestamp(const char *arg, void *dest, char **error_msg) {
  if (!arg || arg[0] == '\0') {
    if (error_msg) {
      *error_msg = strdup("--seek requires a timestamp argument");
    }
    return false;
  }

  double *timestamp = (double *)dest;
  char *endptr;
  long strtol_result;

  // Count colons to determine format
  int colon_count = 0;
  for (const char *p = arg; *p; p++) {
    if (*p == ':')
      colon_count++;
  }

  if (colon_count == 0) {
    // Plain seconds format: "30" or "30.5"
    *timestamp = strtod(arg, &endptr);
    if (*endptr != '\0' || *timestamp < 0.0) {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected non-negative seconds");
      }
      return false;
    }
    return true;
  } else if (colon_count == 1) {
    // MM:SS or MM:SS.ms format
    strtol_result = strtol(arg, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0) {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected MM:SS or MM:SS.ms format");
      }
      return false;
    }
    long minutes = strtol_result;
    double seconds = strtod(endptr + 1, &endptr);
    if (*endptr != '\0' && *endptr != '.' && *endptr != '\0') {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected MM:SS or MM:SS.ms format");
      }
      return false;
    }
    *timestamp = minutes * 60.0 + seconds;
    return true;
  } else if (colon_count == 2) {
    // HH:MM:SS or HH:MM:SS.ms format
    strtol_result = strtol(arg, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0) {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected HH:MM:SS or HH:MM:SS.ms format");
      }
      return false;
    }
    long hours = strtol_result;
    strtol_result = strtol(endptr + 1, &endptr, 10);
    if (*endptr != ':' || strtol_result < 0 || strtol_result >= 60) {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected HH:MM:SS or HH:MM:SS.ms format");
      }
      return false;
    }
    long minutes = strtol_result;
    double seconds = strtod(endptr + 1, &endptr);
    if (*endptr != '\0' && *endptr != '.' && *endptr != '\0') {
      if (error_msg) {
        *error_msg = strdup("Invalid timestamp: expected HH:MM:SS or HH:MM:SS.ms format");
      }
      return false;
    }
    *timestamp = hours * 3600.0 + minutes * 60.0 + seconds;
    return true;
  } else {
    if (error_msg) {
      *error_msg = strdup("Invalid timestamp format: too many colons");
    }
    return false;
  }
}

static bool parse_cookies_from_browser(const char *arg, void *dest, char **error_msg) {
  (void)error_msg; // Unused but required by function signature

  char *browser_buf = (char *)dest;
  const size_t max_size = 256;

  if (!arg || arg[0] == '\0') {
    // No argument provided, default to chrome
    strncpy(browser_buf, "chrome", max_size - 1);
    browser_buf[max_size - 1] = '\0';
    return true;
  }

  // Copy the provided browser name
  strncpy(browser_buf, arg, max_size - 1);
  browser_buf[max_size - 1] = '\0';
  return true;
}

/**
 * @brief Registry entry - stores option definition with mode bitmask
 */
typedef struct {
  const char *long_name;
  char short_name;
  option_type_t type;
  size_t offset;
  const void *default_value;
  size_t default_value_size;
  const char *help_text;
  const char *group;
  bool required;
  const char *env_var_name;
  bool (*validate_fn)(const void *options_struct, char **error_msg);
  bool (*parse_fn)(const char *arg, void *dest, char **error_msg);
  bool owns_memory;
  bool optional_arg;
  option_mode_bitmask_t mode_bitmask;
} registry_entry_t;

// Static defaults for registry entries
static const log_level_t g_default_log_level = DEFAULT_LOG_LEVEL;
static const unsigned short int g_default_verbose = 0;
static const bool g_default_quiet = OPT_QUIET_DEFAULT;
static const int g_default_width = OPT_WIDTH_DEFAULT;
static const int g_default_height = OPT_HEIGHT_DEFAULT;

// Static registry array - will be populated with all options in Phase 2
// For Phase 1, this is a minimal example showing the structure
static const registry_entry_t g_options_registry[] = {
    // LOGGING GROUP (binary-level)
    {"log-file", 'L', OPTION_TYPE_STRING, offsetof(options_t, log_file), "", 0, "Redirect logs to FILE", "LOGGING",
     false, "ASCII_CHAT_LOG_FILE", NULL, NULL, false, false, OPTION_MODE_BINARY},
    {"log-level", '\0', OPTION_TYPE_CALLBACK, offsetof(options_t, log_level), &g_default_log_level, sizeof(log_level_t),
     "Set log level: dev, debug, info, warn, error, fatal", "LOGGING", false, NULL, NULL, parse_log_level, false, false,
     OPTION_MODE_BINARY},
    {"verbose", 'V', OPTION_TYPE_CALLBACK, offsetof(options_t, verbose_level), &g_default_verbose,
     sizeof(unsigned short int), "Increase log verbosity (stackable: -VV, -VVV, or --verbose)", "LOGGING", false, NULL,
     NULL, parse_verbose_flag, false, true, OPTION_MODE_BINARY},
    {"quiet", 'q', OPTION_TYPE_BOOL, offsetof(options_t, quiet), &g_default_quiet, sizeof(bool),
     "Disable console logging (log to file only)", "LOGGING", false, NULL, NULL, NULL, false, false,
     OPTION_MODE_BINARY},

    // TERMINAL GROUP (client, mirror, discovery)
    {"width", 'x', OPTION_TYPE_INT, offsetof(options_t, width), &g_default_width, sizeof(int),
     "Terminal width in characters", "TERMINAL", false, "ASCII_CHAT_WIDTH", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},
    {"height", 'y', OPTION_TYPE_INT, offsetof(options_t, height), &g_default_height, sizeof(int),
     "Terminal height in characters", "TERMINAL", false, "ASCII_CHAT_HEIGHT", NULL, NULL, false, false,
     OPTION_MODE_CLIENT | OPTION_MODE_MIRROR | OPTION_MODE_DISCOVERY},

    // Placeholder - will be expanded with all options in Phase 2
    {NULL, '\0', OPTION_TYPE_BOOL, 0, NULL, 0, NULL, NULL, false, NULL, NULL, NULL, false, false, OPTION_MODE_NONE}};

static size_t g_registry_size = 0;

/**
 * @brief Initialize registry size
 */
static void registry_init_size(void) {
  if (g_registry_size == 0) {
    for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
      g_registry_size++;
    }
  }
}

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

    switch (entry->type) {
    case OPTION_TYPE_STRING:
      options_builder_add_string(builder, entry->long_name, entry->short_name, entry->offset,
                                 entry->default_value ? (const char *)entry->default_value : "", entry->help_text,
                                 entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_INT:
      options_builder_add_int(builder, entry->long_name, entry->short_name, entry->offset,
                              entry->default_value ? *(const int *)entry->default_value : 0, entry->help_text,
                              entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_BOOL:
      options_builder_add_bool(builder, entry->long_name, entry->short_name, entry->offset,
                               entry->default_value ? *(const bool *)entry->default_value : false, entry->help_text,
                               entry->group, entry->required, entry->env_var_name);
      break;
    case OPTION_TYPE_DOUBLE:
      options_builder_add_double(builder, entry->long_name, entry->short_name, entry->offset,
                                 entry->default_value ? *(const double *)entry->default_value : 0.0, entry->help_text,
                                 entry->group, entry->required, entry->env_var_name, entry->validate_fn);
      break;
    case OPTION_TYPE_CALLBACK:
      if (entry->optional_arg) {
        options_builder_add_callback_optional(builder, entry->long_name, entry->short_name, entry->offset,
                                              entry->default_value, entry->default_value_size, entry->parse_fn,
                                              entry->help_text, entry->group, entry->required, entry->env_var_name,
                                              entry->optional_arg);
      } else {
        options_builder_add_callback(builder, entry->long_name, entry->short_name, entry->offset,
                                     entry->default_value ? entry->default_value : NULL, entry->default_value_size,
                                     entry->parse_fn, entry->help_text, entry->group, entry->required,
                                     entry->env_var_name);
      }
      break;
    case OPTION_TYPE_ACTION:
      SET_ERRNO(ERROR_INVALID_STATE, "Actions are not supported in this function");
      // Actions are added separately, not in registry
      break;
    }

    // Set mode bitmask on the last added descriptor
    options_builder_set_mode_bitmask(builder, entry->mode_bitmask);
  }

  return ASCIICHAT_OK;
}

const option_descriptor_t *options_registry_find_by_name(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  registry_init_size();

  // This would need to search through a built config, not the registry directly
  // For now, return NULL - will be implemented when we have access to built configs
  (void)long_name;
  return NULL;
}

const option_descriptor_t *options_registry_find_by_short(char short_name) {
  if (short_name == '\0') {
    SET_ERRNO(ERROR_INVALID_PARAM, "Short name is empty");
    return NULL;
  }

  registry_init_size();

  // This would need to search through a built config, not the registry directly
  // For now, return NULL - will be implemented when we have access to built configs
  (void)short_name;
  return NULL;
}

const option_descriptor_t *options_registry_get_for_mode(asciichat_mode_t mode, size_t *num_options) {
  if (!num_options) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Number of options is NULL");
    return NULL;
  }

  registry_init_size();

  // This would filter registry by mode bitmask and return array
  // For now, return NULL - will be implemented when we have access to built configs
  (void)mode;
  *num_options = 0;
  return NULL;
}
