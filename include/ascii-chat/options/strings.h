/**
 * @file strings.h
 * @brief Centralized enum and mode string conversion API
 * @ingroup options
 *
 * Single source of truth for all enum/mode ↔ string conversions.
 * Use these functions instead of hardcoded string literals.
 */

#pragma once

#include <stddef.h>
#include <stdbool.h>

// Include headers for type definitions
// Note: options.h includes these, so we just need options.h
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/enums.h>

// ============================================================================
// Mode String Conversions
// ============================================================================

/**
 * @brief Convert mode enum to string
 * @param mode Mode enum value
 * @return Mode name string (e.g., "server", "client", "mirror")
 */
const char *asciichat_mode_to_string(asciichat_mode_t mode);

/**
 * @brief Convert string to mode enum
 * @param str Mode name string
 * @return Mode enum value, or MODE_UNKNOWN if invalid
 */
asciichat_mode_t asciichat_string_to_mode(const char *str);

/**
 * @brief Check if string is a valid mode name
 * @param str String to check
 * @return true if valid mode name, false otherwise
 */
bool asciichat_is_valid_mode_string(const char *str);

/**
 * @brief Get all mode strings
 * @param out_names Output array of mode name pointers
 * @return Number of mode names
 */
size_t asciichat_get_all_mode_strings(const char ***out_names);

/**
 * @brief Suggest closest matching mode using fuzzy matching
 * @param input Invalid mode string
 * @return Suggested mode name, or NULL if no close match
 */
const char *asciichat_suggest_mode(const char *input);

// ============================================================================
// Enum Value String Conversions
// ============================================================================

/**
 * @brief Convert color mode enum to string
 * @param mode Color mode value
 * @return Color mode string (e.g., "auto", "256", "truecolor")
 */
const char *color_mode_to_string(terminal_color_mode_t mode);

/**
 * @brief Convert render mode enum to string
 * @param mode Render mode value
 * @return Render mode string (e.g., "foreground", "background", "half-block")
 */
const char *render_mode_to_string(render_mode_t mode);

/**
 * @brief Convert palette type enum to string
 * @param type Palette type value
 * @return Palette type string (e.g., "standard", "blocks", "cool")
 */
const char *palette_type_to_string(palette_type_t type);

// ============================================================================
// Generic Enum Fuzzy Matching
// ============================================================================

/**
 * @brief Suggest closest matching enum value using fuzzy matching
 * @param option_name Option name (e.g., "color-mode", "palette")
 * @param input Invalid enum value string
 * @return Suggested enum value, or NULL if no close match
 */
const char *asciichat_suggest_enum_value(const char *option_name, const char *input);
