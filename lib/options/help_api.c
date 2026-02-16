/**
 * @file help_api.c
 * @brief Public API for retrieving option help text
 * @ingroup options
 *
 * Provides the options_get_help_text() function for external code
 * (especially web clients) to retrieve help text for CLI options.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <ascii-chat/options/options.h>
#include <ascii-chat/options/registry/core.h>
#include <string.h>

/**
 * @brief Get help text for an option in a specific mode
 * @param mode The mode context (MODE_SERVER, MODE_CLIENT, MODE_MIRROR, etc.)
 * @param option_name The long name of the option (e.g., "color-mode", "fps")
 * @return Help text string, or NULL if option doesn't apply to this mode
 *
 * Searches the options registry for the given option name and checks if
 * it applies to the requested mode. If it does, returns the help text.
 * If the option doesn't exist or doesn't apply to the mode, returns NULL.
 */
const char *options_get_help_text(asciichat_mode_t mode, const char *option_name) {
  if (!option_name || !option_name[0]) {
    return NULL;
  }

  // Get the registry (initializes size if needed)
  registry_init_size();

  // Search through all registered options
  for (size_t i = 0; i < g_registry_size; i++) {
    const registry_entry_t *entry = &g_options_registry[i];

    // Skip empty entries or entries without a long name
    if (!entry->long_name || !entry->long_name[0]) {
      continue;
    }

    // Check if this is the option we're looking for
    if (strcmp(entry->long_name, option_name) != 0) {
      continue;
    }

    // Check if this option applies to the requested mode
    // Convert mode to bitmask for comparison
    uint32_t mode_bitmask = (1 << mode) | OPTION_MODE_BINARY;

    if ((entry->mode_bitmask & mode_bitmask) == 0) {
      // Option doesn't apply to this mode
      return NULL;
    }

    // Return the help text (or empty string if not set)
    return entry->help_text ? entry->help_text : "";
  }

  // Option not found
  return NULL;
}
