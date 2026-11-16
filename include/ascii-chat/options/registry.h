/**
 * @file registry.h
 * @brief Central registry of all command-line options with mode applicability
 * @ingroup options
 *
 * This module defines the single source of truth for all command-line options
 * that can be used across all modes (server, client, mirror, discovery service, etc.).
 *
 * ## Architecture Overview
 *
 * The registry is the **single source of truth** for option definitions. It works with:
 * - **Builder** (`builder.h`): Use builder to construct custom option configurations,
 *   populated from the registry via `options_registry_add_all_to_builder()`.
 * - **RCU Thread-Safety** (`rcu.h`): Registered options are parsed and published to
 *   RCU for lock-free thread-safe access via `GET_OPTION()` macro.
 * - **Options System** (`options.h`): The unified parsing system uses the registry
 *   to build option configurations for different modes.
 *
 * ## Design Philosophy
 *
 * - **Single Definition**: Each option defined exactly once with all metadata (no duplication)
 * - **Mode Bitmasks**: Each option includes `mode_bitmask` indicating which modes it applies to
 * - **Automatic Filtering**: Registry functions automatically filter by mode
 * - **Immutable After Load**: Once loaded, registry is read-only (no runtime modifications)
 * - **Shared with Builder**: Registry options added to builder via `options_registry_add_all_to_builder()`
 * - **Completion Metadata**: Each option includes shell completion hints (enums, ranges, examples)
 *
 * ## Registry Structure
 *
 * The registry is defined in `registry.c` as a static array of `option_descriptor_t`
 * structures. Each descriptor includes complete metadata:
 *
 * **Identification**:
 * - Long name (e.g., "port")
 * - Short name (e.g., 'p')
 * - Argument placeholder (e.g., "NUM", "STR", "PATH")
 *
 * **Type and Storage**:
 * - Type: BOOL, INT, STRING, DOUBLE, CALLBACK, ACTION
 * - Offset into `options_t` struct (via `offsetof()`)
 *
 * **Documentation**:
 * - Help text (brief description for --help output)
 * - Group name (for organizing help sections: "NETWORK OPTIONS", "DISPLAY OPTIONS", etc.)
 * - Visibility flags (hide_from_binary_help, hide_from_mode_help)
 *
 * **Values and Validation**:
 * - Default value pointer
 * - Required flag (must user provide this option?)
 * - Environment variable fallback (e.g., PORT, PASSWORD_FILE)
 * - Custom validator function (validates across option fields)
 *
 * **Parsing and Actions**:
 * - Custom parser for OPTION_TYPE_CALLBACK
 * - Action function for OPTION_TYPE_ACTION (executes immediately, may exit)
 *
 * **Mode Applicability**:
 * - Bitmask indicating which modes this option applies to
 *
 * **Completion Metadata** (Phase 3):
 * - Enum values and descriptions (for shell completions)
 * - Numeric range (min, max, step)
 * - Example values
 * - Input type hint (ENUM, NUMERIC, FILEPATH, CHOICE, etc.)
 *
 * ## Mode Bitmask System
 *
 * Each option includes a `mode_bitmask` that controls where it appears:
 *
 * **Mode Bitmask Values**:
 * - `OPTION_MODE_BINARY`: Parsed before mode detection (--help, --version, --log-file, --log-level)
 * - `OPTION_MODE_SERVER`: Server-only options (--max-clients, --discovery-service, etc.)
 * - `OPTION_MODE_CLIENT`: Client-only options (--color, --audio, --snapshot, etc.)
 * - `OPTION_MODE_MIRROR`: Mirror mode options (local webcam preview)
 * - `OPTION_MODE_DISCOVERY_SVC`: Discovery service (ACDS) options
 * - `OPTION_MODE_ALL`: Available in all modes
 *
 * **Examples**:
 * ```c
 * .mode_bitmask = OPTION_MODE_CLIENT | OPTION_MODE_MIRROR  // Client and mirror modes
 * .mode_bitmask = OPTION_MODE_SERVER                        // Server mode only
 * .mode_bitmask = OPTION_MODE_BINARY | OPTION_MODE_CLIENT   // Binary + client options
 * .mode_bitmask = OPTION_MODE_ALL                           // All modes
 * ```
 *
 * **How Mode Filtering Works**:
 * 1. User runs `ascii-chat client` or `ascii-chat server`
 * 2. Mode detection determines detected_mode (MODE_CLIENT, MODE_SERVER, etc.)
 * 3. Registry functions filter options by matching mode_bitmask
 * 4. Parser only accepts options for the detected mode
 * 5. Help output only shows relevant options
 *
 * ## Usage Patterns
 *
 * **Pattern 1: Get all options for a specific mode**
 * ```c
 * size_t num_opts;
 * const option_descriptor_t *opts = options_registry_get_for_mode(MODE_CLIENT, &num_opts);
 * for (size_t i = 0; i < num_opts; i++) {
 *     printf(\"%s: %s\\n\", opts[i].long_name, opts[i].help_text);
 * }
 * ```
 *
 * **Pattern 2: Look up individual options**
 * ```c
 * const option_descriptor_t *port_opt = options_registry_find_by_name(\"port\");
 * if (port_opt && (port_opt->mode_bitmask & OPTION_MODE_SERVER)) {
 *     printf(\"Port option available in server mode\\n\");
 * }
 * ```
 *
 * **Pattern 3: Populate builder with registry options**
 * ```c
 * options_builder_t *builder = options_builder_create(sizeof(options_t));
 * options_registry_add_all_to_builder(builder);  // Add all registered options
 * options_config_t *config = options_builder_build(builder);
 * ```
 *
 * **Pattern 4: Get shell completion metadata**
 * ```c
 * const option_metadata_t *meta = options_registry_get_metadata(\"color-mode\");
 * if (meta && meta->input_type == OPTION_INPUT_ENUM) {
 *     size_t count;
 *     const char **values = options_registry_get_enum_values(\"color-mode\", NULL, &count);
 *     // Use values for shell completion suggestions
 * }
 * ```
 *
 * ## Adding New Options to Registry
 *
 * To add a new option to the registry:
 *
 * 1. **Add field to `options_t` struct** in `include/ascii-chat/options/options.h`
 * 2. **Add default constant** in `options.h` (e.g., `OPT_MY_OPTION_DEFAULT`)
 * 3. **Add registry entry** in `lib/options/registry.c` with:
 *    - Long and short names
 *    - Type and storage offset (via offsetof)
 *    - Default value and validation function
 *    - Help text and group name
 *    - Mode bitmask(s) indicating which modes apply
 *    - Completion metadata (if enum, ranges, or examples needed)
 *
 * **Example Registry Entry**:
 * ```c
 * {
 *     .long_name = \"max-clients\",
 *     .short_name = 'c',
 *     .type = OPTION_TYPE_INT,
 *     .offset = offsetof(options_t, max_clients),
 *     .default_value = &(int){OPT_MAX_CLIENTS_DEFAULT},
 *     .help_text = \"Maximum number of simultaneous clients\",
 *     .group = \"NETWORK OPTIONS\",
 *     .mode_bitmask = OPTION_MODE_SERVER,
 *     .validate = validate_max_clients,  // Custom validator
 *     .metadata = {
 *         .input_type = OPTION_INPUT_NUMERIC,
 *         .numeric_range = {.min = 1, .max = 100, .step = 1},
 *         .examples = (const char *[]){\"1\", \"4\", \"10\", NULL},
 *     }
 * }
 * ```
 *
 * ## Registry Lookup Functions
 *
 * **Core Functions**:
 * - `options_registry_add_all_to_builder()`: Populate builder with all registry options
 * - `options_registry_find_by_name()`: Find option by long name (e.g., \"port\")
 * - `options_registry_find_by_short()`: Find option by short name (e.g., 'p')
 * - `options_registry_get_for_mode()`: Get all options for a specific mode
 * - `options_registry_get_binary_options()`: Get all binary-level options
 * - `options_registry_get_for_display()`: Get options filtered for help/completions display
 *
 * **Completion Metadata Functions** (Phase 3):
 * - `options_registry_get_metadata()`: Get complete metadata for option
 * - `options_registry_get_enum_values()`: Get enum values and descriptions
 * - `options_registry_get_numeric_range()`: Get numeric min/max/step
 * - `options_registry_get_examples()`: Get example values
 * - `options_registry_get_input_type()`: Get input type hint
 *
 * ## RCU Thread-Safety Integration
 *
 * After registry options are parsed through the builder, they're published via RCU:
 * ```c
 * // Build config from registered options
 * options_builder_t *builder = options_builder_create(sizeof(options_t));
 * options_registry_add_all_to_builder(builder);
 * options_config_t *config = options_builder_build(builder);
 *
 * // Parse command line
 * options_t opts = options_t_new();
 * options_config_parse(config, argc, argv, &opts, MODE_DETECTION_RESULT, ...);
 *
 * // Publish to RCU (lock-free access from worker threads)
 * options_state_init();
 * options_state_set(&opts);
 *
 * // Worker threads now read lock-free via GET_OPTION() macro
 * ```
 *
 * @see builder.h - Builder API for constructing option configurations
 * @see rcu.h - Thread-safe RCU-based access to published options
 * @see options.h - Unified options parsing system (uses registry internally)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include "../options/options.h"
#include "../options/builder.h"

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

// ============================================================================
// Completion Metadata Access (Phase 3)
// ============================================================================

/**
 * @brief Get complete metadata for an option
 *
 * Retrieves all completion metadata for the given option,
 * including enum values, numeric ranges, examples, input type, etc.
 *
 * @param long_name Long name of option to look up
 * @return Pointer to metadata structure, or NULL if option not found
 *
 * @note The returned pointer points to data within the registry
 *       and should not be modified or freed.
 */
const option_metadata_t *options_registry_get_metadata(const char *long_name);

/**
 * @brief Get enum values for an option
 *
 * Retrieves enum values and descriptions for an option that has
 * OPTION_INPUT_ENUM type in its metadata.
 *
 * @param option_name Long name of option
 * @param descriptions OUTPUT: Pointer to descriptions array (or NULL if not needed)
 * @param count OUTPUT: Number of values/descriptions
 * @return Array of enum value strings, or NULL if not found or not an enum type
 *
 * Example:
 * ```c
 * const char **values;
 * const char **descs;
 * size_t count;
 * values = options_registry_get_enum_values("color-mode", &descs, &count);
 * if (values) {
 *     for (size_t i = 0; i < count; i++) {
 *         printf("%s: %s\n", values[i], descs[i]);
 *     }
 * }
 * ```
 */
const char **options_registry_get_enum_values(const char *option_name, const char ***descriptions, size_t *count);

/**
 * @brief Get numeric range for an option
 *
 * Retrieves min/max/step values for an option that has
 * OPTION_INPUT_NUMERIC type in its metadata.
 *
 * @param option_name Long name of option
 * @param min_out OUTPUT: Minimum value (or 0 if no limit)
 * @param max_out OUTPUT: Maximum value (or 0 if no limit)
 * @param step_out OUTPUT: Step size (or 0 if continuous)
 * @return true if option found and has numeric range, false otherwise
 *
 * Example:
 * ```c
 * int min, max, step;
 * if (options_registry_get_numeric_range("compression-level", &min, &max, &step)) {
 *     printf("Range: %d-%d (step: %d)\n", min, max, step);
 * }
 * ```
 */
bool options_registry_get_numeric_range(const char *option_name, int *min_out, int *max_out, int *step_out);

/**
 * @brief Get example values for an option
 *
 * Retrieves example values or command invocations for an option.
 *
 * @param option_name Long name of option
 * @param count OUTPUT: Number of examples
 * @return Array of example strings, or NULL if no examples defined
 */
const char **options_registry_get_examples(const char *option_name, size_t *count);

/**
 * @brief Get input type for an option
 *
 * Retrieves the input type (OPTION_INPUT_ENUM, OPTION_INPUT_NUMERIC, etc.)
 * for the given option.
 *
 * @param option_name Long name of option
 * @return Input type (OPTION_INPUT_NONE if not found)
 */
option_input_type_t options_registry_get_input_type(const char *option_name);

#ifdef __cplusplus
}
#endif
