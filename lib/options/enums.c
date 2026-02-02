/**
 * @file enums.c
 * @brief Option enum value implementation - single source of truth
 * @ingroup options
 */

#include <string.h>
#include "options/enums.h"
#include "common.h"

/* Enum value definitions - SINGLE SOURCE OF TRUTH */

static const char *log_level_values[] = {"dev", "debug", "info", "warn", "error", "fatal"};

static const char *color_mode_values[] = {"auto", "none", "16", "256", "truecolor"};

static const char *color_filter_values[] = {"none",   "black", "white", "green", "magenta", "fuchsia",
                                            "orange", "teal",  "cyan",  "pink",  "red",     "yellow"};

static const char *palette_values[] = {"standard", "blocks", "digital", "minimal", "cool", "custom"};

static const char *render_mode_values[] = {"foreground", "fg", "background", "bg", "half-block"};

static const char *reconnect_values[] = {"off", "auto"};

/* Enum registry - maps option names to their values */
static const enum_descriptor_t enum_registry[] = {
    {.option_name = "log-level",
     .values = log_level_values,
     .value_count = sizeof(log_level_values) / sizeof(log_level_values[0])},
    {.option_name = "color-mode",
     .values = color_mode_values,
     .value_count = sizeof(color_mode_values) / sizeof(color_mode_values[0])},
    {.option_name = "color-filter",
     .values = color_filter_values,
     .value_count = sizeof(color_filter_values) / sizeof(color_filter_values[0])},
    {.option_name = "palette",
     .values = palette_values,
     .value_count = sizeof(palette_values) / sizeof(palette_values[0])},
    {.option_name = "render-mode",
     .values = render_mode_values,
     .value_count = sizeof(render_mode_values) / sizeof(render_mode_values[0])},
    {.option_name = "reconnect",
     .values = reconnect_values,
     .value_count = sizeof(reconnect_values) / sizeof(reconnect_values[0])},
    /* Terminator */
    {.option_name = NULL, .values = NULL, .value_count = 0}};

const char **options_get_enum_values(const char *option_name, size_t *value_count) {
  if (!option_name || !value_count) {
    return NULL;
  }

  for (size_t i = 0; enum_registry[i].option_name != NULL; i++) {
    if (strcmp(enum_registry[i].option_name, option_name) == 0) {
      *value_count = enum_registry[i].value_count;
      return enum_registry[i].values;
    }
  }

  *value_count = 0;
  return NULL;
}

bool options_is_enum_option(const char *option_name) {
  if (!option_name) {
    return false;
  }

  for (size_t i = 0; enum_registry[i].option_name != NULL; i++) {
    if (strcmp(enum_registry[i].option_name, option_name) == 0) {
      return true;
    }
  }

  return false;
}
