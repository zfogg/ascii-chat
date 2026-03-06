/**
 * @file enums.h
 * @brief Option enum value definitions - single source of truth
 * @ingroup options
 *
 * Defines all enum values for options in one place so they can be used by:
 * - Option parsers
 * - Help text generation
 * - Shell completions (bash/fish/zsh/powershell)
 * - Configuration file validation
 *
 * This ensures consistency across all parts of the application.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enum value mapping with description for shell completions
 */
typedef struct {
  int enum_value;      ///< The numeric enum value
  const char *string;  ///< The string representation of that enum value
  const char *desc;    ///< Description for shell completion
} enum_to_string_entry_t;

/**
 * @brief Enum value mapping for an option
 */
typedef struct {
  const char *option_name; ///< Long option name (e.g., "log-level")
  const char **values;     ///< Array of valid string values
  size_t value_count;      ///< Number of values in array
} enum_descriptor_t;

/**
 * @brief Get enum entries with descriptions for an option
 *
 * Returns entries containing both string values and descriptions for shell completion.
 *
 * @param option_name Option long name (e.g., "log-level")
 * @param entry_count OUTPUT: Number of entries returned
 * @return Array of enum entries, or NULL if not an enum option
 */
const enum_to_string_entry_t *options_get_enum_entries(const char *option_name, size_t *entry_count);

/**
 * @brief Get enum values for an option
 *
 * @param option_name Option long name (e.g., "log-level")
 * @param value_count OUTPUT: Number of values returned
 * @return Array of valid string values, or NULL if not an enum option
 */
const char **options_get_enum_values(const char *option_name, size_t *value_count);

/**
 * @brief Check if an option has enum values
 *
 * @param option_name Option long name
 * @return true if option has enum values, false otherwise
 */
bool options_is_enum_option(const char *option_name);

/* ═══════════════════════════════════════════════════════════════════════════
 * Enum Value Constants - Used throughout the codebase
 * ═════════════════════════════════════════════════════════════════════════ */

/* log-level values */
#define OPT_LOG_LEVEL_DEV    "dev"
#define OPT_LOG_LEVEL_DEBUG  "debug"
#define OPT_LOG_LEVEL_INFO   "info"
#define OPT_LOG_LEVEL_WARN   "warn"
#define OPT_LOG_LEVEL_ERROR  "error"
#define OPT_LOG_LEVEL_FATAL  "fatal"

/* color-mode values */
#define OPT_COLOR_MODE_AUTO      "auto"
#define OPT_COLOR_MODE_NONE      "none"
#define OPT_COLOR_MODE_16        "16"
#define OPT_COLOR_MODE_256       "256"
#define OPT_COLOR_MODE_TRUECOLOR "truecolor"

/* color-filter values */
#define OPT_COLOR_FILTER_NONE     "none"
#define OPT_COLOR_FILTER_BLACK    "black"
#define OPT_COLOR_FILTER_WHITE    "white"
#define OPT_COLOR_FILTER_GREEN    "green"
#define OPT_COLOR_FILTER_MAGENTA  "magenta"
#define OPT_COLOR_FILTER_FUCHSIA  "fuchsia"
#define OPT_COLOR_FILTER_ORANGE   "orange"
#define OPT_COLOR_FILTER_TEAL     "teal"
#define OPT_COLOR_FILTER_CYAN     "cyan"
#define OPT_COLOR_FILTER_PINK     "pink"
#define OPT_COLOR_FILTER_RED      "red"
#define OPT_COLOR_FILTER_YELLOW   "yellow"
#define OPT_COLOR_FILTER_RAINBOW  "rainbow"

/* palette values */
#define OPT_PALETTE_STANDARD "standard"
#define OPT_PALETTE_BLOCKS   "blocks"
#define OPT_PALETTE_DIGITAL  "digital"
#define OPT_PALETTE_MINIMAL  "minimal"
#define OPT_PALETTE_COOL     "cool"
#define OPT_PALETTE_CUSTOM   "custom"

/* render-mode values */
#define OPT_RENDER_MODE_FOREGROUND  "foreground"
#define OPT_RENDER_MODE_FG          "fg"
#define OPT_RENDER_MODE_BACKGROUND  "background"
#define OPT_RENDER_MODE_BG          "bg"
#define OPT_RENDER_MODE_HALF_BLOCK  "half-block"

/* reconnect values */
#define OPT_RECONNECT_OFF  "off"
#define OPT_RECONNECT_AUTO "auto"

/* audio-source values */
#define OPT_AUDIO_SOURCE_AUTO  "auto"
#define OPT_AUDIO_SOURCE_MIC   "mic"
#define OPT_AUDIO_SOURCE_MEDIA "media"
#define OPT_AUDIO_SOURCE_BOTH  "both"

#ifdef __cplusplus
}
#endif
