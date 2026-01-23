/**
 * @file registry.h
 * @brief Central registry of all command-line options with mode applicability
 * @ingroup options
 *
 * This module defines the single source of truth for all command-line options
 * that can be used across all modes (server, client, mirror, discovery service, etc.).
 *
 * **Philosophy**:
 *
 * - **Single Definition**: Each option defined exactly once with all metadata
 * - **Mode Bitmasks**: Each option includes `mode_bitmask` indicating which modes it applies to
 * - **Automatic Filtering**: Registry functions automatically filter by mode
 * - **Immutable After Load**: Once loaded, registry is read-only (no runtime modifications)
 * - **Shared with Builder**: Registry options added to builder via `options_registry_add_all_to_builder()`
 *
 * **Architecture**:
 *
 * The registry is defined in `registry.c` as a static array of `option_descriptor_t`
 * structures. Each descriptor includes:
 *
 * - **Identification**: Long name (e.g., "port"), short name (e.g., 'p')
 * - **Type**: BOOL, INT, STRING, DOUBLE, CALLBACK, ACTION
 * - **Storage**: Offset into `options_t` struct (via `offsetof()`)
 * - **Documentation**: Help text, group name, visibility flags
 * - **Defaults**: Default value pointer, required flag, env var fallback
 * - **Validation**: Custom validator function for this option
 * - **Parsing**: Custom parser for OPTION_TYPE_CALLBACK
 * - **Mode**: Bitmask indicating which modes this option applies to
 *
 * **Mode Bitmasks**:
 *
 * Each option includes a `mode_bitmask` that controls where it appears:
 *
 * ```c
 * .mode_bitmask = OPTION_MODE_CLIENT | OPTION_MODE_MIRROR  // Client and mirror modes only
 * .mode_bitmask = OPTION_MODE_SERVER                        // Server mode only
 * .mode_bitmask = OPTION_MODE_BINARY | OPTION_MODE_CLIENT   // Binary + client modes
 * .mode_bitmask = OPTION_MODE_ALL                           // All modes
 * ```
 *
 * **Usage Pattern**:
 *
 * ```c
 * // Get all options for a specific mode
 * size_t num_opts;
 * const option_descriptor_t *opts = options_registry_get_for_mode(
 *     MODE_CLIENT, &num_opts);
 * for (size_t i = 0; i < num_opts; i++) {
 *     printf("%s: %s\\n", opts[i].long_name, opts[i].help_text);
 * }
 *
 * // Look up individual options
 * const option_descriptor_t *port_opt = options_registry_find_by_name("port");
 * if (port_opt && (port_opt->mode_bitmask & OPTION_MODE_SERVER)) {
 *     // Port option available in server mode
 * }
 *
 * // Add all registry options to builder
 * options_builder_t *builder = options_builder_create("myapp");
 * options_registry_add_all_to_builder(builder);
 * ```
 *
 * **Adding New Options**:
 *
 * To add a new option:
 *
 * 1. **Add field to `options_t`** in `options.h`
 * 2. **Add default constant** (e.g., `OPT_MY_OPTION_DEFAULT`)
 * 3. **Add registry entry** in `registry.c` with:
 *    - Long and short names
 *    - Type and storage offset
 *    - Default value and validation
 *    - Help text and group
 *    - Mode bitmask(s)
 *
 * **Registry Lookup Functions**:
 *
 * - `options_registry_add_all_to_builder()`: Populate builder with all registry options
 * - `options_registry_find_by_name()`: Find option by long name (e.g., "port")
 * - `options_registry_find_by_short()`: Find option by short name (e.g., 'p')
 * - `options_registry_get_for_mode()`: Get all options for a specific mode
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

/**
 * @brief Get all binary-level options
 *
 * Returns an array of all binary-level options (those with OPTION_MODE_BINARY).
 * The array is allocated and must be freed by the caller.
 *
 * @param num_options OUTPUT: Number of options returned
 * @return Array of option descriptors (caller must free), or NULL on error
 */
const option_descriptor_t *options_registry_get_binary_options(size_t *num_options);

/**
 * @brief Get options for help/completions display with unified filtering
 *
 * Returns options filtered using the same logic as the help system.
 * This ensures help output and completions are always in sync.
 *
 * Uses the same filtering rules as options_print_help_for_mode():
 * - For binary-level help (mode == MODE_DISCOVERY): shows all options that apply to any mode
 * - For mode-specific help: shows only options for that mode (binary options excluded unless also mode-specific)
 * - Respects hide_from_binary_help and hide_from_mode_help flags
 *
 * @param mode Mode to filter for (use MODE_DISCOVERY for binary-level help)
 * @param for_binary_help If true, use binary-help filtering; if false, use mode-specific filtering
 * @param num_options OUTPUT: Number of options returned
 * @return Array of option descriptors (caller must free), or NULL on error
 *
 * @note This is the AUTHORITATIVE filtering function for both help and completions.
 *       Always use this function to ensure consistency across the application.
 */
const option_descriptor_t *options_registry_get_for_display(asciichat_mode_t mode, bool for_binary_help,
                                                             size_t *num_options);

#ifdef __cplusplus
}
#endif
