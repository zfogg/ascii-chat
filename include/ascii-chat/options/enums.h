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
 * @brief Enum value mapping for an option
 */
typedef struct {
  const char *option_name; ///< Long option name (e.g., "log-level")
  const char **values;     ///< Array of valid string values
  size_t value_count;      ///< Number of values in array
} enum_descriptor_t;

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

#ifdef __cplusplus
}
#endif
