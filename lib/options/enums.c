/**
 * @file enums.c
 * @brief Option enum value implementation with dynamic enum-to-string mapping
 * @ingroup options
 */

#include <string.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/common.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * Enum-to-String Mapping
 *
 * Each enum type has a mapping array that links enum values to their string
 * representations. This eliminates duplication and ensures consistency.
 * ═════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Maps a single enum value to its string representation
 */
typedef struct {
  int enum_value;      ///< The numeric enum value
  const char *string;  ///< The string representation of that enum value
} enum_to_string_entry_t;

/**
 * @brief Maps log level enum values to strings
 */
static const enum_to_string_entry_t log_level_map[] = {
    /* These enum values come from log/log.h */
    {0, "dev"},      /* LOG_LEVEL_DEV */
    {1, "debug"},    /* LOG_LEVEL_DEBUG */
    {2, "info"},     /* LOG_LEVEL_INFO */
    {3, "warn"},     /* LOG_LEVEL_WARN */
    {4, "error"},    /* LOG_LEVEL_ERROR */
    {5, "fatal"},    /* LOG_LEVEL_FATAL */
    {-1, NULL}       /* Terminator */
};

/**
 * @brief Maps color mode enum values to strings
 */
static const enum_to_string_entry_t color_mode_map[] = {
    {0, "auto"},         /* COLOR_MODE_AUTO */
    {1, "none"},         /* COLOR_MODE_NONE */
    {2, "16"},           /* COLOR_MODE_16 */
    {3, "256"},          /* COLOR_MODE_256 */
    {4, "truecolor"},    /* COLOR_MODE_TRUECOLOR */
    {-1, NULL}           /* Terminator */
};

/**
 * @brief Maps color filter enum values to strings
 */
static const enum_to_string_entry_t color_filter_map[] = {
    {COLOR_FILTER_NONE, "none"},
    {COLOR_FILTER_BLACK, "black"},
    {COLOR_FILTER_WHITE, "white"},
    {COLOR_FILTER_GREEN, "green"},
    {COLOR_FILTER_MAGENTA, "magenta"},
    {COLOR_FILTER_FUCHSIA, "fuchsia"},
    {COLOR_FILTER_ORANGE, "orange"},
    {COLOR_FILTER_TEAL, "teal"},
    {COLOR_FILTER_CYAN, "cyan"},
    {COLOR_FILTER_PINK, "pink"},
    {COLOR_FILTER_RED, "red"},
    {COLOR_FILTER_YELLOW, "yellow"},
    {COLOR_FILTER_RAINBOW, "rainbow"},
    {-1, NULL}  /* Terminator */
};

/**
 * @brief Maps palette enum values to strings
 */
static const enum_to_string_entry_t palette_map[] = {
    {0, "standard"},
    {1, "blocks"},
    {2, "digital"},
    {3, "minimal"},
    {4, "cool"},
    {5, "custom"},
    {-1, NULL}  /* Terminator */
};

/**
 * @brief Maps render mode enum values to strings
 */
static const enum_to_string_entry_t render_mode_map[] = {
    {RENDER_MODE_FOREGROUND, "foreground"},
    {RENDER_MODE_FOREGROUND, "fg"},      /* Short alias for foreground */
    {RENDER_MODE_BACKGROUND, "background"},
    {RENDER_MODE_BACKGROUND, "bg"},      /* Short alias for background */
    {RENDER_MODE_HALF_BLOCK, "half-block"},
    {-1, NULL}  /* Terminator */
};

/**
 * @brief Maps reconnect enum values to strings
 */
static const enum_to_string_entry_t reconnect_map[] = {
    {0, "off"},
    {1, "auto"},
    {-1, NULL}  /* Terminator */
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Enum Registry - Maps option names to their value mappings
 * ═════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Descriptor for an enum option with its string value mapping
 */
typedef struct {
  const char *option_name;              ///< Option long name (e.g., "log-level")
  const enum_to_string_entry_t *map;    ///< Mapping array from enum values to strings
} enum_map_descriptor_t;

/**
 * @brief Registry of all enum options and their value mappings
 */
static const enum_map_descriptor_t enum_registry[] = {
    {.option_name = "log-level", .map = log_level_map},
    {.option_name = "color-mode", .map = color_mode_map},
    {.option_name = "color-filter", .map = color_filter_map},
    {.option_name = "palette", .map = palette_map},
    {.option_name = "render-mode", .map = render_mode_map},
    {.option_name = "reconnect", .map = reconnect_map},
    {.option_name = NULL, .map = NULL}  /* Terminator */
};

/**
 * @brief Get all unique string values for an option
 *
 * Extracts the list of valid string values for an enum option.
 * For options with aliases (like render-mode which has "foreground" and "fg"),
 * both values are included.
 *
 * @param option_name Option long name (e.g., "log-level")
 * @param value_count OUTPUT: Number of values returned
 * @return Array of valid string values, or NULL if not an enum option
 */
const char **options_get_enum_values(const char *option_name, size_t *value_count) {
  if (!option_name || !value_count) {
    return NULL;
  }

  /* Find the option's mapping in the registry */
  const enum_map_descriptor_t *descriptor = NULL;
  for (size_t i = 0; enum_registry[i].option_name != NULL; i++) {
    if (strcmp(enum_registry[i].option_name, option_name) == 0) {
      descriptor = &enum_registry[i];
      break;
    }
  }

  if (!descriptor || !descriptor->map) {
    *value_count = 0;
    return NULL;
  }

  /* Count non-terminator entries */
  size_t count = 0;
  for (size_t i = 0; descriptor->map[i].enum_value != -1; i++) {
    count++;
  }

  /* Build array of unique strings (skip duplicates from aliases) */
  const char **values = SAFE_MALLOC(count * sizeof(const char *), const char **);
  size_t unique_count = 0;

  for (size_t i = 0; descriptor->map[i].enum_value != -1; i++) {
    const char *str = descriptor->map[i].string;

    /* Check if we already have this string (for aliases) */
    bool already_added = false;
    for (size_t j = 0; j < unique_count; j++) {
      if (strcmp(values[j], str) == 0) {
        already_added = true;
        break;
      }
    }

    if (!already_added) {
      values[unique_count++] = str;
    }
  }

  *value_count = unique_count;
  return values;
}

/**
 * @brief Check if an option has enum values
 *
 * @param option_name Option long name
 * @return true if option has enum values, false otherwise
 */
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
