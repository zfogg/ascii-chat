/**
 * @file core.c
 * @brief Internal helper functions for registry implementation
 * @ingroup options
 *
 * This file contains all the static helper functions used internally
 * by the registry implementation. These functions are not part of the
 * public API and are only used by public_api.c.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "common.h"
#include "core.h"
#include <ascii-chat/options/registry.h>
#include <string.h>

/**
 * @brief Initialize registry from category builders
 * Populates g_options_registry by concatenating all category arrays
 */
void registry_init_from_builders(void) {
  static bool initialized = false;
  if (initialized) {
    return;
  }

  size_t offset = 0;
  for (int i = 0; g_category_builders[i].entries != NULL; i++) {
    // Count entries in this category (until sentinel terminator)
    size_t count = 0;
    for (const registry_entry_t *e = g_category_builders[i].entries; e->long_name != NULL; e++) {
      count++;
    }

    // Copy category entries (count already excludes sentinel)
    if (offset + count <= 2048) {
      memcpy(&g_options_registry[offset], g_category_builders[i].entries, count * sizeof(registry_entry_t));
      offset += count;
    }
  }

  // Add final null terminator
  if (offset < 2048) {
    g_options_registry[offset] = (registry_entry_t){.long_name = NULL,
                                                    .short_name = '\0',
                                                    .type = OPTION_TYPE_BOOL,
                                                    .offset = 0,
                                                    .default_value = NULL,
                                                    .default_value_size = 0,
                                                    .help_text = NULL,
                                                    .group = NULL,
                                                    .required = false,
                                                    .env_var_name = NULL,
                                                    .validate_fn = NULL,
                                                    .parse_fn = NULL,
                                                    .owns_memory = false,
                                                    .optional_arg = false,
                                                    .mode_bitmask = OPTION_MODE_NONE,
                                                    .metadata = {0}};
  }

  initialized = true;
}

/**
 * @brief Validate that no short or long options appear more than once in the registry
 * @return Result of SET_ERRNO if duplicates found, ASCIICHAT_OK if valid
 */
asciichat_error_t registry_validate_unique_options(void) {
  // Check for duplicate long options
  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    const char *long_name = g_options_registry[i].long_name;
    if (!long_name || long_name[0] == '\0') {
      continue; // Skip empty long names
    }

    for (size_t j = i + 1; g_options_registry[j].long_name != NULL; j++) {
      if (strcmp(g_options_registry[j].long_name, long_name) == 0) {
        return SET_ERRNO(ERROR_CONFIG, "Duplicate long option '--%s' at registry indices %zu and %zu", long_name, i, j);
      }
    }
  }

  // Check for duplicate short options (skip if '\0')
  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    char short_name = g_options_registry[i].short_name;
    if (short_name == '\0') {
      continue; // Skip if no short option
    }

    for (size_t j = i + 1; g_options_registry[j].long_name != NULL; j++) {
      if (g_options_registry[j].short_name == short_name) {
        return SET_ERRNO(ERROR_CONFIG,
                         "Duplicate short option '-%c' for '--%s' and '--%s' at registry indices %zu and %zu",
                         short_name, g_options_registry[i].long_name, g_options_registry[j].long_name, i, j);
      }
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Initialize registry size and metadata
 */
void registry_init_size(void) {
  registry_init_from_builders();

  if (g_registry_size == 0) {
    for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
      g_registry_size++;
    }
    // Validate that all options have unique short and long names
    registry_validate_unique_options();
    // DEPRECATED: Metadata is now initialized compile-time in registry entries
    // registry_populate_metadata_for_critical_options();
    g_metadata_populated = true;
  }
}

/**
 * @brief Get a registry entry by long name
 * @note This is used internally for option lookup
 */
const registry_entry_t *registry_find_entry_by_name(const char *long_name) {
  if (!long_name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Long name is NULL");
    return NULL;
  }

  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    if (strcmp(g_options_registry[i].long_name, long_name) == 0) {
      return &g_options_registry[i];
    }
  }
  return NULL;
}

/**
 * @brief Get a registry entry by short name
 * @note This is used internally for option lookup
 */
const registry_entry_t *registry_find_entry_by_short(char short_name) {
  if (short_name == '\0') {
    return NULL;
  }

  for (size_t i = 0; g_options_registry[i].long_name != NULL; i++) {
    if (g_options_registry[i].short_name == short_name) {
      return &g_options_registry[i];
    }
  }
  return NULL;
}

/**
 * @brief Convert registry entry to option descriptor
 */
option_descriptor_t registry_entry_to_descriptor(const registry_entry_t *entry) {
  option_descriptor_t desc = {0};
  if (entry) {
    desc.long_name = entry->long_name;
    desc.short_name = entry->short_name;
    desc.type = entry->type;
    desc.offset = entry->offset;
    desc.help_text = entry->help_text;
    desc.group = entry->group;
    desc.arg_placeholder = entry->arg_placeholder;
    desc.hide_from_mode_help = false;
    // Hide discovery service options from binary-level help (they're for discovery-service mode only)
    desc.hide_from_binary_help = (entry->mode_bitmask == OPTION_MODE_DISCOVERY_SVC);
    desc.default_value = entry->default_value;
    desc.required = entry->required;
    desc.env_var_name = entry->env_var_name;
    desc.validate = entry->validate_fn;
    desc.parse_fn = entry->parse_fn;
    desc.action_fn = NULL;
    desc.owns_memory = entry->owns_memory;
    desc.optional_arg = entry->optional_arg;
    desc.mode_bitmask = entry->mode_bitmask;
    desc.metadata = entry->metadata;
  }
  return desc;
}

/**
 * @brief Check if an option applies to the given mode for display purposes
 *
 * This implements the same filtering logic as the help system's option_applies_to_mode().
 * Used by options_registry_get_for_display() to ensure completions match help output.
 *
 * @param entry Registry entry to check
 * @param mode Mode to check (use MODE_DISCOVERY for binary help)
 * @param for_binary_help If true, show all options for any mode; if false, filter by mode
 * @return true if option should be displayed for this mode
 */
bool registry_entry_applies_to_mode(const registry_entry_t *entry, asciichat_mode_t mode, bool for_binary_help) {
  if (!entry) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Entry is NULL");
    return false;
  }

  // Hardcoded list of options to hide from binary help (matches builder.c line 752)
  // These are options that have hide_from_binary_help=true set in builder.c
  const char *hidden_from_binary[] = {NULL};

  // When for_binary_help is true (i.e., for 'ascii-chat --help'),
  // we want to show all options that apply to any mode, plus binary-level options.
  if (for_binary_help) {
    // Check if this option is explicitly hidden from binary help
    for (int i = 0; hidden_from_binary[i] != NULL; i++) {
      if (strcmp(entry->long_name, hidden_from_binary[i]) == 0) {
        return false; // Hidden from binary help
      }
    }

    // An option applies if its mode_bitmask has any bit set for any valid mode.
    // OPTION_MODE_ALL is a bitmask of all modes (including OPTION_MODE_BINARY).
    return (entry->mode_bitmask & OPTION_MODE_ALL) != 0;
  }

  // For mode-specific help, show only options for that mode.
  // Do not show binary options here unless it also specifically applies to the mode.
  if (mode < 0 || mode > MODE_DISCOVERY) {
    return false;
  }
  option_mode_bitmask_t mode_bit = (1 << mode);

  // Check if it's a binary option. If so, only show if it also explicitly applies to this mode.
  if ((entry->mode_bitmask & OPTION_MODE_BINARY) && !(entry->mode_bitmask & mode_bit)) {
    return false; // Binary options not shown in mode-specific help unless also mode-specific
  }

  return (entry->mode_bitmask & mode_bit) != 0;
}
