/**
 * @file registry.h
 * @brief Central options registry with mode bitmasks
 * @ingroup options
 *
 * This module provides a unified registry of all command-line options defined
 * exactly once with mode bitmasks indicating which modes each option applies to.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "options/options.h"
#include "options/builder.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add all options from registry to builder
 *
 * Iterates through the central options registry and adds each option
 * to the provided builder with its mode bitmask set.
 *
 * @param builder Options builder to add options to
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t options_registry_add_all_to_builder(options_builder_t *builder);

/**
 * @brief Get option descriptor by name
 *
 * Looks up an option descriptor from the registry by its long name.
 *
 * @param long_name Long option name (e.g., "port")
 * @return Pointer to option descriptor, or NULL if not found
 */
const option_descriptor_t *options_registry_find_by_name(const char *long_name);

/**
 * @brief Get option descriptor by short name
 *
 * Looks up an option descriptor from the registry by its short name.
 *
 * @param short_name Short option character (e.g., 'p')
 * @return Pointer to option descriptor, or NULL if not found
 */
const option_descriptor_t *options_registry_find_by_short(char short_name);

/**
 * @brief Get all options for a specific mode
 *
 * Returns an array of option descriptors that apply to the given mode.
 * The array is allocated and must be freed by the caller.
 *
 * @param mode Mode to filter by
 * @param num_options OUTPUT: Number of options returned
 * @return Array of option descriptors (caller must free), or NULL on error
 */
const option_descriptor_t *options_registry_get_for_mode(asciichat_mode_t mode, size_t *num_options);

#ifdef __cplusplus
}
#endif
