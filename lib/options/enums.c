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
    {0, OPT_LOG_LEVEL_DEV, "development level"},
    {1, OPT_LOG_LEVEL_DEBUG, "debug messages"},
    {2, OPT_LOG_LEVEL_INFO, "info messages"},
    {3, OPT_LOG_LEVEL_WARN, "warnings"},
    {4, OPT_LOG_LEVEL_ERROR, "errors"},
    {5, OPT_LOG_LEVEL_FATAL, "fatal errors"},
    {-1, NULL, NULL}       /* Terminator */
};

/**
 * @brief Maps color mode enum values to strings
 */
static const enum_to_string_entry_t color_mode_map[] = {
    {0, OPT_COLOR_MODE_AUTO, "auto-detect based on terminal"},
    {1, OPT_COLOR_MODE_NONE, "disable colors"},
    {2, OPT_COLOR_MODE_16, "16 color mode"},
    {3, OPT_COLOR_MODE_256, "256 color mode"},
    {4, OPT_COLOR_MODE_TRUECOLOR, "true color (24-bit RGB)"},
    {-1, NULL, NULL}           /* Terminator */
};

/**
 * @brief Maps color filter enum values to strings
 */
static const enum_to_string_entry_t color_filter_map[] = {
    {COLOR_FILTER_NONE, OPT_COLOR_FILTER_NONE, "no tint"},
    {COLOR_FILTER_BLACK, OPT_COLOR_FILTER_BLACK, "black tint"},
    {COLOR_FILTER_WHITE, OPT_COLOR_FILTER_WHITE, "white tint"},
    {COLOR_FILTER_GREEN, OPT_COLOR_FILTER_GREEN, "green tint"},
    {COLOR_FILTER_MAGENTA, OPT_COLOR_FILTER_MAGENTA, "magenta tint"},
    {COLOR_FILTER_FUCHSIA, OPT_COLOR_FILTER_FUCHSIA, "fuchsia tint"},
    {COLOR_FILTER_ORANGE, OPT_COLOR_FILTER_ORANGE, "orange tint"},
    {COLOR_FILTER_TEAL, OPT_COLOR_FILTER_TEAL, "teal tint"},
    {COLOR_FILTER_CYAN, OPT_COLOR_FILTER_CYAN, "cyan tint"},
    {COLOR_FILTER_PINK, OPT_COLOR_FILTER_PINK, "pink tint"},
    {COLOR_FILTER_RED, OPT_COLOR_FILTER_RED, "red tint"},
    {COLOR_FILTER_YELLOW, OPT_COLOR_FILTER_YELLOW, "yellow tint"},
    {COLOR_FILTER_RAINBOW, OPT_COLOR_FILTER_RAINBOW, "rainbow tint"},
    {-1, NULL, NULL}  /* Terminator */
};

/**
 * @brief Maps palette enum values to strings
 */
static const enum_to_string_entry_t palette_map[] = {
    {0, OPT_PALETTE_STANDARD, "standard palette"},
    {1, OPT_PALETTE_BLOCKS, "block characters"},
    {2, OPT_PALETTE_DIGITAL, "digital display"},
    {3, OPT_PALETTE_MINIMAL, "minimal palette"},
    {4, OPT_PALETTE_COOL, "cool colors"},
    {5, OPT_PALETTE_CUSTOM, "custom palette"},
    {-1, NULL, NULL}  /* Terminator */
};

/**
 * @brief Maps render mode enum values to strings
 */
static const enum_to_string_entry_t render_mode_map[] = {
    {RENDER_MODE_FOREGROUND, OPT_RENDER_MODE_FOREGROUND, "text foreground"},
    {RENDER_MODE_FOREGROUND, OPT_RENDER_MODE_FG, "text foreground (short)"},
    {RENDER_MODE_BACKGROUND, OPT_RENDER_MODE_BACKGROUND, "text background"},
    {RENDER_MODE_BACKGROUND, OPT_RENDER_MODE_BG, "text background (short)"},
    {RENDER_MODE_HALF_BLOCK, OPT_RENDER_MODE_HALF_BLOCK, "half-block characters"},
    {-1, NULL, NULL}  /* Terminator */
};

/**
 * @brief Maps reconnect enum values to strings
 */
static const enum_to_string_entry_t reconnect_map[] = {
    {0, OPT_RECONNECT_OFF, "disable auto-reconnect"},
    {1, OPT_RECONNECT_AUTO, "enable auto-reconnect"},
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
