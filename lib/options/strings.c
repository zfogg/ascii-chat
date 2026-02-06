/**
 * @file strings.c
 * @brief Centralized enum and mode string conversion - single source of truth
 * @ingroup options
 *
 * This module provides bidirectional conversion between enums and strings:
 * - Mode enum ↔ mode string ("server", "client", "mirror", etc.)
 * - Option enum values ↔ string values ("auto", "none", "16", etc.)
 * - Fuzzy matching suggestions via levenshtein distance
 *
 * All mode and enum string conversions should use this module.
 */

#include <ascii-chat/options/options.h> // For asciichat_mode_t and MODE_ enums
#include <ascii-chat/options/strings.h>
#include <ascii-chat/options/levenshtein.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/platform/terminal.h> // For term_color_level_t
#include <ascii-chat/video/palette.h>     // For palette_type_t
#include <ascii-chat/common.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Mode String Conversions
// ============================================================================

/**
 * @brief Mode descriptor for string conversion
 */
typedef struct {
  asciichat_mode_t mode;
  const char *name;
  const char *description;
} mode_descriptor_t;

/**
 * @brief Mode registry - single source of truth for mode names
 *
 * Note: MODE_DISCOVERY is intentionally omitted from this registry.
 * It's the default mode when no mode is specified, and users cannot
 * explicitly invoke it with "ascii-chat default" or "ascii-chat discovery".
 */
static const mode_descriptor_t mode_registry[] = {
    {MODE_SERVER, "server", "Multi-client video chat server"},
    {MODE_CLIENT, "client", "Connect to ascii-chat server"},
    {MODE_MIRROR, "mirror", "Local webcam preview (no networking)"},
    {MODE_DISCOVERY_SERVICE, "discovery-service", "Session discovery server (ACDS)"},
    {MODE_DISCOVERY_SERVICE, "acds", "Alias for discovery-service"},
    {MODE_INVALID, NULL, NULL} // Terminator
};

const char *asciichat_mode_to_string(asciichat_mode_t mode) {
  // Special case: MODE_DISCOVERY is the default mode, show as "default" to users
  if (mode == MODE_DISCOVERY) {
    return "default";
  }

  for (size_t i = 0; mode_registry[i].name != NULL; i++) {
    if (mode_registry[i].mode == mode) {
      return mode_registry[i].name;
    }
  }
  return "unknown";
}

asciichat_mode_t asciichat_string_to_mode(const char *str) {
  if (!str) {
    return MODE_INVALID;
  }

  for (size_t i = 0; mode_registry[i].name != NULL; i++) {
    if (strcmp(mode_registry[i].name, str) == 0) {
      return mode_registry[i].mode;
    }
  }

  return MODE_INVALID;
}

bool asciichat_is_valid_mode_string(const char *str) {
  return asciichat_string_to_mode(str) != MODE_INVALID;
}

size_t asciichat_get_all_mode_strings(const char ***out_names) {
  static const char *names[32]; // Max 32 modes
  size_t count = 0;

  for (size_t i = 0; mode_registry[i].name != NULL && count < 32; i++) {
    // Skip duplicates (like "acds" alias)
    bool duplicate = false;
    for (size_t j = 0; j < count; j++) {
      if (strcmp(names[j], mode_registry[i].name) == 0) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      names[count++] = mode_registry[i].name;
    }
  }

  if (out_names) {
    *out_names = names;
  }
  return count;
}

const char *asciichat_suggest_mode(const char *input) {
  if (!input) {
    return NULL;
  }

  const char **names = NULL;
  size_t count = asciichat_get_all_mode_strings(&names);

  // Create NULL-terminated array for levenshtein_find_similar
  static const char *null_terminated[33]; // Max 32 modes + NULL
  for (size_t i = 0; i < count && i < 32; i++) {
    null_terminated[i] = names[i];
  }
  null_terminated[count] = NULL;

  return levenshtein_find_similar(input, null_terminated);
}

// ============================================================================
// Color Mode String Conversions
// ============================================================================

const char *color_mode_to_string(terminal_color_mode_t mode) {
  switch (mode) {
  case TERM_COLOR_AUTO:
    return "auto";
  case TERM_COLOR_NONE:
    return "none";
  case TERM_COLOR_16:
    return "16";
  case TERM_COLOR_256:
    return "256";
  case TERM_COLOR_TRUECOLOR:
    return "truecolor";
  default:
    return "unknown";
  }
}

// ============================================================================
// Render Mode String Conversions
// ============================================================================

const char *render_mode_to_string(render_mode_t mode) {
  switch (mode) {
  case RENDER_MODE_FOREGROUND:
    return "foreground";
  case RENDER_MODE_BACKGROUND:
    return "background";
  case RENDER_MODE_HALF_BLOCK:
    return "half-block";
  default:
    return "unknown";
  }
}

// ============================================================================
// Palette Type String Conversions
// ============================================================================

const char *palette_type_to_string(palette_type_t type) {
  switch (type) {
  case PALETTE_STANDARD:
    return "standard";
  case PALETTE_BLOCKS:
    return "blocks";
  case PALETTE_DIGITAL:
    return "digital";
  case PALETTE_MINIMAL:
    return "minimal";
  case PALETTE_COOL:
    return "cool";
  case PALETTE_CUSTOM:
    return "custom";
  default:
    return "unknown";
  }
}

// ============================================================================
// Generic Enum Value Fuzzy Matching
// ============================================================================

const char *asciichat_suggest_enum_value(const char *option_name, const char *input) {
  if (!option_name || !input) {
    return NULL;
  }

  size_t value_count = 0;
  const char **values = options_get_enum_values(option_name, &value_count);

  if (!values || value_count == 0) {
    return NULL;
  }

  // Create NULL-terminated array for levenshtein_find_similar
  static const char *null_terminated[64]; // Max 64 enum values
  for (size_t i = 0; i < value_count && i < 63; i++) {
    null_terminated[i] = values[i];
  }
  null_terminated[value_count] = NULL;

  return levenshtein_find_similar(input, null_terminated);
}
