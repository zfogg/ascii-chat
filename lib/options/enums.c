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
 * representations with descriptions. This eliminates duplication and ensures consistency.
 * ═════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Maps log level enum values to strings
 */
static const enum_to_string_entry_t log_level_map[] = {
    /* These enum values come from log/log.h */
    {0, "dev", "development level"},
    {1, "debug", "debug messages"},
    {2, "info", "info messages"},
    {3, "warn", "warnings"},
    {4, "error", "errors"},
    {5, "fatal", "fatal errors"},
    {-1, NULL, NULL}       /* Terminator */
};

/**
 * @brief Maps color mode enum values to strings
 */
static const enum_to_string_entry_t color_mode_map[] = {
    {0, "auto", "auto-detect based on terminal"},
    {1, "none", "disable colors"},
    {2, "16", "16 color mode"},
    {3, "256", "256 color mode"},
    {4, "truecolor", "true color (24-bit RGB)"},
    {-1, NULL, NULL}           /* Terminator */
};

/**
 * @brief Maps color filter enum values to strings
 */
static const enum_to_string_entry_t color_filter_map[] = {
    {COLOR_FILTER_NONE, "none", "no tint"},
    {COLOR_FILTER_BLACK, "black", "black tint"},
    {COLOR_FILTER_WHITE, "white", "white tint"},
    {COLOR_FILTER_GREEN, "green", "green tint"},
    {COLOR_FILTER_MAGENTA, "magenta", "magenta tint"},
    {COLOR_FILTER_FUCHSIA, "fuchsia", "fuchsia tint"},
    {COLOR_FILTER_ORANGE, "orange", "orange tint"},
    {COLOR_FILTER_TEAL, "teal", "teal tint"},
    {COLOR_FILTER_CYAN, "cyan", "cyan tint"},
    {COLOR_FILTER_PINK, "pink", "pink tint"},
    {COLOR_FILTER_RED, "red", "red tint"},
    {COLOR_FILTER_YELLOW, "yellow", "yellow tint"},
    {COLOR_FILTER_RAINBOW, "rainbow", "rainbow tint"},
    {-1, NULL, NULL}  /* Terminator */
};

/**
 * @brief Maps palette enum values to strings
 */
static const enum_to_string_entry_t palette_map[] = {
    {0, "standard", "standard palette"},
    {1, "blocks", "block characters"},
    {2, "digital", "digital display"},
    {3, "minimal", "minimal palette"},
    {4, "cool", "cool colors"},
    {5, "custom", "custom palette"},
    {-1, NULL, NULL}  /* Terminator */
};

/**
 * @brief Maps render mode enum values to strings
 */
static const enum_to_string_entry_t render_mode_map[] = {
    {RENDER_MODE_FOREGROUND, "foreground", "text foreground"},
    {RENDER_MODE_FOREGROUND, "fg", "text foreground (short)"},
    {RENDER_MODE_BACKGROUND, "background", "text background"},
    {RENDER_MODE_BACKGROUND, "bg", "text background (short)"},
    {RENDER_MODE_HALF_BLOCK, "half-block", "half-block characters"},
    {-1, NULL, NULL}  /* Terminator */
};

/**
 * @brief Maps reconnect enum values to strings
 */
static const enum_to_string_entry_t reconnect_map[] = {
    {0, "off", "disable auto-reconnect"},
    {1, "auto", "enable auto-reconnect"},
    {-1, NULL, NULL}  /* Terminator */
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
 * @brief Get all enum entries with descriptions for an option
 *
 * Returns entries containing both string values and descriptions for shell completion.
 * Used by zsh completion to show descriptions alongside values.
 *
 * @param option_name Option long name (e.g., "log-level")
 * @param entry_count OUTPUT: Number of entries returned
 * @return Array of enum entries, or NULL if not an enum option
 */
const enum_to_string_entry_t *options_get_enum_entries(const char *option_name, size_t *entry_count) {
  if (!option_name || !entry_count) {
    return NULL;
  }

  for (size_t i = 0; enum_registry[i].option_name != NULL; i++) {
    if (strcmp(enum_registry[i].option_name, option_name) == 0) {
      /* Count entries up to terminator */
      size_t count = 0;
      for (size_t j = 0; enum_registry[i].map[j].enum_value != -1; j++) {
        count++;
      }
      *entry_count = count;
      return enum_registry[i].map;
    }
  }

  *entry_count = 0;
  return NULL;
}

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
