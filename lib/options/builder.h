/**
 * @file builder.h
 * @brief Options builder API for flexible command-line option configuration
 * @ingroup options
 *
 * This module provides a builder pattern for constructing option configurations
 * that can be used by library consumers to create custom tools based on ascii-chat
 * modes or build entirely custom option sets from scratch.
 *
 * **Features**:
 * - Three-tier parsing (binary → mode → mode-specific)
 * - Required fields with environment variable fallbacks
 * - Option dependencies (REQUIRES, CONFLICTS, IMPLIES)
 * - Cross-field validation (validators receive full options struct)
 * - Automatic string memory management (auto-strdup, cleanup)
 * - Grouped help output
 * - Preset configs for server/client/mirror/acds modes
 *
 * **Usage Pattern**:
 * 1. Create builder (from scratch or preset)
 * 2. Add options with builder functions
 * 3. Add dependencies if needed
 * 4. Build immutable config
 * 5. Parse command line into options struct
 * 6. Validate (required, dependencies, custom)
 * 7. Use options in application
 * 8. Cleanup memory before exit
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "common.h"
#include "options/options.h" // For option_mode_bitmask_t

/**
 * @brief Option value types
 *
 * Defines the type of value an option accepts.
 */
typedef enum {
  OPTION_TYPE_BOOL,     ///< Boolean flag (--flag, no value)
  OPTION_TYPE_INT,      ///< Integer value (--count 42)
  OPTION_TYPE_STRING,   ///< String value (--name foo)
  OPTION_TYPE_DOUBLE,   ///< Floating point (--ratio 1.5)
  OPTION_TYPE_CALLBACK, ///< Custom parser function
  OPTION_TYPE_ACTION    ///< Action that executes and may exit (--list-webcams, etc.)
} option_type_t;

/**
 * @brief Completion input type for smart shell completions
 *
 * Specifies the kind of input an option expects, used to generate
 * better completions with value suggestions, ranges, and examples.
 */
typedef enum {
  OPTION_INPUT_NONE,     ///< No input (boolean flag)
  OPTION_INPUT_ENUM,     ///< Choose from fixed set of enum values
  OPTION_INPUT_NUMERIC,  ///< Numeric value with optional min/max/step
  OPTION_INPUT_STRING,   ///< Free-form text input
  OPTION_INPUT_FILEPATH, ///< File path completion
  OPTION_INPUT_CHOICE    ///< Dynamic choice (e.g., from system)
} option_input_type_t;

/**
 * @brief Metadata for shell completion generation
 *
 * Stores additional information about options to enable smart shell
 * completions with value suggestions, numeric ranges, examples, etc.
 * This data is used by all shell completion generators (bash, fish, zsh, powershell).
 */
typedef struct {
  // Enum values with descriptions
  const char **enum_values;       ///< Enum value strings (e.g., {"auto", "none", "16", "256", "truecolor"})
  size_t enum_count;              ///< Number of enum values
  const char **enum_descriptions; ///< Descriptions parallel to enum_values (e.g., "Auto-detect from terminal")

  // Numeric range
  struct {
    int min;  ///< Minimum value (or 0 if no limit)
    int max;  ///< Maximum value (or 0 if no limit)
    int step; ///< Step size (0 = no step, continuous)
  } numeric_range;

  // Examples
  const char **examples; ///< Example values or command invocations
  size_t example_count;  ///< Number of examples

  // Default value (informational, may be duplicated from descriptor)
  const char *default_value; ///< Default value as string for display

  // Input type for completions
  option_input_type_t input_type; ///< What kind of input this option expects

  // Flags
  bool is_list; ///< If true, option accepts multiple comma-separated or space-separated values

  // Validation pattern (optional)
  const char *validation_pattern; ///< Optional regex pattern for validation hints
} option_metadata_t;

/**
 * @brief Option dependency types
 *
 * Defines relationships between options.
 */
typedef enum {
  DEPENDENCY_REQUIRES,  ///< If A is set, B must be set
  DEPENDENCY_CONFLICTS, ///< If A is set, B must NOT be set
  DEPENDENCY_IMPLIES    ///< If A is set, B defaults to true/enabled
} dependency_type_t;

/**
 * @brief Option descriptor
 *
 * Describes a single command-line option with all its metadata.
 * Includes both parsing information and completion metadata for
 * generating smart shell completions.
 */
typedef struct {
  // Identification
  const char *long_name; ///< Long option name (e.g., "port")
  char short_name;       ///< Short option char (e.g., 'p', or '\0' if none)

  // Type and storage
  option_type_t type; ///< Value type
  size_t offset;      ///< offsetof(struct, field) - where to store value

  // Documentation
  const char *help_text;      ///< Description for --help
  const char *group;          ///< Group name for help sections (e.g., "NETWORK OPTIONS")
  bool hide_from_mode_help;   ///< If true, don't show in mode-specific help (binary-level only)
  bool hide_from_binary_help; ///< If true, don't show in binary-level help (e.g., in release builds)

  // Default and validation
  const void *default_value; ///< Pointer to default value (or NULL if required)
  bool required;             ///< If true, option must be provided
  const char *env_var_name;  ///< Environment variable fallback (or NULL)

  // Validation - receives full options struct for cross-field validation
  bool (*validate)(const void *options_struct, char **error_msg);

  // Custom parsing (for OPTION_TYPE_CALLBACK)
  bool (*parse_fn)(const char *arg, void *dest, char **error_msg);

  // Action callback (for OPTION_TYPE_ACTION)
  void (*action_fn)(void); ///< Action to execute (may call exit)

  // Memory management
  bool owns_memory; ///< If true, strings are strdup'd and freed on cleanup

  // Optional argument support (for OPTION_TYPE_CALLBACK)
  bool optional_arg; ///< If true, argument is optional (for callbacks like --verbose)

  // Mode applicability
  option_mode_bitmask_t mode_bitmask; ///< Which modes this option applies to

  // Completion metadata (NEW - for smart shell completions)
  option_metadata_t metadata; ///< Metadata for shell completions (enums, ranges, examples, etc.)
} option_descriptor_t;

/**
 * @brief Usage line descriptor for programmatic USAGE generation
 *
 * Stores components separately so colors can be applied semantically:
 * - mode: magenta
 * - positional args: green
 * - options: yellow
 */
typedef struct {
  const char *mode;        ///< NULL or mode name (e.g., "server") or "<mode>" placeholder
  const char *positional;  ///< NULL or positional args (e.g., "[bind-addr]", "<session-string>")
  bool show_options;       ///< true = show "[options...]" suffix
  const char *description; ///< Help text for this usage pattern
} usage_descriptor_t;

/**
 * @brief Example descriptor for programmatic EXAMPLES generation
 *
 * Stores command components separately for semantic coloring:
 * - mode: magenta
 * - args: green
 */
typedef struct {
  const char *mode; ///< NULL or mode name (e.g., "server", "client")
  const char *args; // Command-line arguments part of the example
  const char *description;
  bool owns_args_memory; // True if args memory should be freed by options_config_destroy
} example_descriptor_t;

/**
 * @brief Mode descriptor for programmatic MODES generation (for help output)
 */
typedef struct {
  const char *name;        ///< Mode name (e.g., "server", "client")
  const char *description; ///< Mode description (e.g., "Run as multi-client video chat server")
} help_mode_descriptor_t;

/**
 * @brief Option dependency
 *
 * Describes a dependency relationship between two options.
 */
typedef struct {
  const char *option_name;   ///< The option that has the dependency
  dependency_type_t type;    ///< Type of dependency
  const char *depends_on;    ///< The option it depends on
  const char *error_message; ///< Custom error message (optional)
} option_dependency_t;

/**
 * @brief Positional argument descriptor
 *
 * Describes a positional (non-option) argument parsed after getopt processing.
 * These are arguments that don't start with '-' or '--'.
 *
 * Examples:
 * - Client: "ascii-chat client [address][:port]" - single positional arg with complex parsing
 * - Server: "ascii-chat server [addr1] [addr2]" - 0-2 positional args with validation
 *
 * The parse_fn receives:
 * - arg: The positional argument string
 * - config: Pointer to full options struct (can modify multiple fields)
 * - remaining: Remaining unparsed positional args (for multi-arg parsing)
 * - num_remaining: Count of remaining args
 * - error_msg: Set on failure (caller will free)
 *
 * Returns true on success, false on error.
 */
typedef struct {
  const char *name;      ///< Name for help text (e.g., "address", "bind-addr")
  const char *help_text; ///< Description for usage message
  bool required;         ///< If true, this positional arg must be provided

  // Programmatic help examples
  const char *section_heading; ///< Section heading for examples (e.g., "ADDRESS FORMATS")
  const char **examples;       ///< Array of example strings with descriptions
  size_t num_examples;         ///< Number of examples

  /**
   * Custom parser function
   *
   * @param arg Current positional argument string
   * @param config Pointer to options struct (can modify any field)
   * @param remaining Remaining positional args (or NULL if last)
   * @param num_remaining Count of remaining args
   * @param error_msg Set error message on failure (will be freed by caller)
   * @return Number of args consumed (usually 1), or -1 on error
   */
  int (*parse_fn)(const char *arg, void *config, char **remaining, int num_remaining, char **error_msg);

  // Mode applicability
  option_mode_bitmask_t mode_bitmask; ///< Which modes this positional arg applies to
} positional_arg_descriptor_t;

/**
 * @brief Options configuration
 *
 * A complete, immutable configuration of options for a mode.
 * Built by options_builder_t and used for parsing.
 */
typedef struct {
  option_descriptor_t *descriptors; ///< Array of option descriptors
  size_t num_descriptors;           ///< Number of descriptors

  option_dependency_t *dependencies; ///< Array of dependencies
  size_t num_dependencies;           ///< Number of dependencies

  positional_arg_descriptor_t *positional_args; ///< Array of positional argument descriptors
  size_t num_positional_args;                   ///< Number of positional arguments

  size_t struct_size;       ///< sizeof(options_t) for bounds checking
  const char *program_name; ///< For usage header
  const char *description;  ///< For usage header

  // Programmatic help generation metadata
  usage_descriptor_t *usage_lines; ///< Array of usage line descriptors
  size_t num_usage_lines;          ///< Number of usage lines

  example_descriptor_t *examples; ///< Array of example descriptors
  size_t num_examples;            ///< Number of examples

  help_mode_descriptor_t *modes; ///< Array of mode descriptors
  size_t num_modes;              ///< Number of modes

  // Memory management (internal use)
  char **owned_strings;          ///< Strdup'd strings to free on cleanup
  size_t num_owned_strings;      ///< Number of owned strings
  size_t owned_strings_capacity; ///< Allocated capacity
} options_config_t;

/**
 * @brief Options builder
 *
 * For incrementally constructing option configurations.
 * Use builder functions to add options and dependencies, then build
 * an immutable config.
 */
typedef struct {
  option_descriptor_t *descriptors; ///< Dynamic array of descriptors
  size_t num_descriptors;           ///< Current count
  size_t descriptor_capacity;       ///< Allocated capacity

  option_dependency_t *dependencies; ///< Dynamic array of dependencies
  size_t num_dependencies;           ///< Current count
  size_t dependency_capacity;        ///< Allocated capacity

  positional_arg_descriptor_t *positional_args; ///< Dynamic array of positional args
  size_t num_positional_args;                   ///< Current count
  size_t positional_arg_capacity;               ///< Allocated capacity

  usage_descriptor_t *usage_lines; ///< Dynamic array of usage lines
  size_t num_usage_lines;          ///< Current count
  size_t usage_line_capacity;      ///< Allocated capacity

  example_descriptor_t *examples; ///< Dynamic array of examples
  size_t num_examples;            ///< Current count
  size_t example_capacity;        ///< Allocated capacity

  help_mode_descriptor_t *modes; ///< Dynamic array of modes
  size_t num_modes;              ///< Current count
  size_t mode_capacity;          ///< Allocated capacity

  size_t struct_size;
  const char *program_name;
  const char *description;

  // Track dynamically allocated strings owned by the builder, to be transferred to config
  char **owned_strings_builder;
  size_t num_owned_strings_builder;
  size_t owned_strings_builder_capacity;
} options_builder_t;

// ============================================================================
// Builder Lifecycle
// ============================================================================

/**
 * @brief Create empty options builder
 *
 * @param struct_size sizeof(your_options_t) for the target options struct
 * @return New builder (must be freed with options_builder_destroy)
 */
options_builder_t *options_builder_create(size_t struct_size);

/**
 * @brief Create builder from preset config
 *
 * Copies all descriptors and dependencies from the preset.
 *
 * @param preset Preset configuration to copy
 * @return New builder (must be freed with options_builder_destroy)
 */
options_builder_t *options_builder_from_preset(const options_config_t *preset);

/**
 * @brief Free options builder
 *
 * @param builder Builder to free (can be NULL)
 */
void options_builder_destroy(options_builder_t *builder);

/**
 * @brief Build immutable options config
 *
 * Creates final config from builder. Builder is NOT consumed -
 * you must still call options_builder_destroy().
 *
 * @param builder Builder with options and dependencies
 * @return Immutable config (must be freed with options_config_destroy)
 */
options_config_t *options_builder_build(options_builder_t *builder);

/**
 * @brief Free options config
 *
 * Frees the config structure. Does NOT free strings in the options struct -
 * use options_config_cleanup() for that.
 *
 * @param config Config to free (can be NULL)
 */
void options_config_destroy(options_config_t *config);

// ============================================================================
// Adding Options
// ============================================================================

/**
 * @brief Add boolean flag option
 *
 * @param builder Builder to add to
 * @param long_name Long option name (e.g., "verbose")
 * @param short_name Short option char (e.g., 'v', or '\0' if none)
 * @param offset offsetof(struct, field)
 * @param default_value Default value
 * @param help_text Help description
 * @param group Group name for help (e.g., "OUTPUT OPTIONS")
 * @param required If true, option must be provided
 * @param env_var_name Environment variable fallback (or NULL)
 */
void options_builder_add_bool(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                              bool default_value, const char *help_text, const char *group, bool required,
                              const char *env_var_name);

/**
 * @brief Add integer option
 *
 * @param builder Builder to add to
 * @param long_name Long option name
 * @param short_name Short option char (or '\0')
 * @param offset offsetof(struct, field)
 * @param default_value Default value
 * @param help_text Help description
 * @param group Group name for help
 * @param required If true, option must be provided
 * @param env_var_name Environment variable fallback (or NULL)
 * @param validate Validator function receiving full struct (or NULL)
 */
void options_builder_add_int(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                             int default_value, const char *help_text, const char *group, bool required,
                             const char *env_var_name, bool (*validate)(const void *options_struct, char **error_msg));

/**
 * @brief Add string option
 *
 * Strings are automatically strdup'd during parsing and freed during cleanup.
 *
 * @param builder Builder to add to
 * @param long_name Long option name
 * @param short_name Short option char (or '\0')
 * @param offset offsetof(struct, field)
 * @param default_value Default value (will be strdup'd, or NULL)
 * @param help_text Help description
 * @param group Group name for help
 * @param required If true, option must be provided
 * @param env_var_name Environment variable fallback (or NULL)
 * @param validate Validator function receiving full struct (or NULL)
 */
void options_builder_add_string(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                                const char *default_value, const char *help_text, const char *group, bool required,
                                const char *env_var_name,
                                bool (*validate)(const void *options_struct, char **error_msg));

/**
 * @brief Add double/float option
 *
 * @param builder Builder to add to
 * @param long_name Long option name
 * @param short_name Short option char (or '\0')
 * @param offset offsetof(struct, field)
 * @param default_value Default value
 * @param help_text Help description
 * @param group Group name for help
 * @param required If true, option must be provided
 * @param env_var_name Environment variable fallback (or NULL)
 * @param validate Validator function receiving full struct (or NULL)
 */
void options_builder_add_double(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                                double default_value, const char *help_text, const char *group, bool required,
                                const char *env_var_name,
                                bool (*validate)(const void *options_struct, char **error_msg));

/**
 * @brief Add option with custom callback parser
 *
 * @param builder Builder to add to
 * @param long_name Long option name
 * @param short_name Short option char (or '\0')
 * @param offset offsetof(struct, field)
 * @param default_value Pointer to default value (or NULL)
 * @param value_size sizeof(field_type)
 * @param parse_fn Custom parser function
 * @param help_text Help description
 * @param group Group name for help
 * @param required If true, option must be provided
 * @param env_var_name Environment variable fallback (or NULL)
 */
void options_builder_add_callback(options_builder_t *builder, const char *long_name, char short_name, size_t offset,
                                  const void *default_value, size_t value_size,
                                  bool (*parse_fn)(const char *arg, void *dest, char **error_msg),
                                  const char *help_text, const char *group, bool required, const char *env_var_name);

/**
 * @brief Add option with custom callback parser that supports optional arguments
 *
 * Allows the callback to receive NULL if no argument is provided (when next arg is a flag).
 *
 * @param builder Builder to add to
 * @param long_name Long option name
 * @param short_name Short option char (or '\0')
 * @param offset offsetof(struct, field)
 * @param default_value Pointer to default value (or NULL)
 * @param value_size sizeof(field_type)
 * @param parse_fn Custom parser function (receives NULL if arg not provided)
 * @param help_text Help description
 * @param group Group name for help
 * @param required If true, option must be provided
 * @param env_var_name Environment variable fallback (or NULL)
 * @param optional_arg If true, argument is optional for this callback
 */
void options_builder_add_callback_optional(options_builder_t *builder, const char *long_name, char short_name,
                                           size_t offset, const void *default_value, size_t value_size,
                                           bool (*parse_fn)(const char *arg, void *dest, char **error_msg),
                                           const char *help_text, const char *group, bool required,
                                           const char *env_var_name, bool optional_arg);

/**
 * @brief Add action option (executes action and may exit)
 *
 * Action options execute a callback function when encountered during parsing.
 * The callback may exit the program (e.g., --list-webcams, --version).
 *
 * @param builder Builder to add to
 * @param long_name Long option name
 * @param short_name Short option char (or '\0')
 * @param action_fn Action callback to execute
 * @param help_text Help description
 * @param group Group name for help
 */
void options_builder_add_action(options_builder_t *builder, const char *long_name, char short_name,
                                void (*action_fn)(void), const char *help_text, const char *group);

/**
 * @brief Set mode bitmask on the last added option descriptor
 *
 * Sets the mode_bitmask field on the most recently added option descriptor.
 * This allows setting mode applicability after adding an option.
 *
 * @param builder Options builder
 * @param mode_bitmask Bitmask indicating which modes this option applies to
 */
void options_builder_set_mode_bitmask(options_builder_t *builder, option_mode_bitmask_t mode_bitmask);

// ============================================================================
// Completion Metadata (NEW - Phase 2)
// ============================================================================

/**
 * @brief Set enum values with descriptions for an option
 *
 * Populates the metadata with enum values and descriptions for shell completion.
 * Both arrays must have the same length.
 *
 * @param builder Options builder
 * @param option_name Long name of option to set metadata for
 * @param values Array of enum value strings (e.g., {"auto", "none", "16", "256", "truecolor"})
 * @param descriptions Array of descriptions parallel to values
 * @param count Number of values/descriptions
 *
 * Example:
 * ```c
 * const char *color_values[] = {"auto", "none", "16", "256", "truecolor"};
 * const char *color_descs[] = {
 *     "Auto-detect from terminal",
 *     "Monochrome only",
 *     "16 colors (ANSI)",
 *     "256 colors (xterm)",
 *     "24-bit truecolor (modern terminals)"
 * };
 * options_builder_set_enum_values(builder, "color-mode", color_values, color_descs, 5);
 * options_builder_set_input_type(builder, "color-mode", OPTION_INPUT_ENUM);
 * ```
 */
void options_builder_set_enum_values(options_builder_t *builder, const char *option_name, const char **values,
                                     const char **descriptions, size_t count);

/**
 * @brief Set numeric range for an option
 *
 * Specifies minimum, maximum, and optional step for numeric input completions.
 *
 * @param builder Options builder
 * @param option_name Long name of option to set metadata for
 * @param min Minimum value (0 = no limit)
 * @param max Maximum value (0 = no limit)
 * @param step Step size (0 = continuous, no step)
 *
 * Example:
 * ```c
 * options_builder_set_numeric_range(builder, "compression-level", 1, 9, 1);
 * options_builder_set_input_type(builder, "compression-level", OPTION_INPUT_NUMERIC);
 * ```
 */
void options_builder_set_numeric_range(options_builder_t *builder, const char *option_name, int min, int max, int step);

/**
 * @brief Set example values for an option
 *
 * Provides example values or command invocations for help display and completion suggestions.
 *
 * @param builder Options builder
 * @param option_name Long name of option to set metadata for
 * @param examples Array of example strings (may be values or full commands)
 * @param count Number of examples
 *
 * Example:
 * ```c
 * const char *fps_examples[] = {"30", "60", "144"};
 * options_builder_set_examples(builder, "fps", fps_examples, 3);
 * ```
 */
void options_builder_set_examples(options_builder_t *builder, const char *option_name, const char **examples,
                                  size_t count);

/**
 * @brief Set input type for an option
 *
 * Specifies what kind of input the option expects (enum, numeric, filepath, etc.)
 * for generating appropriate shell completions.
 *
 * @param builder Options builder
 * @param option_name Long name of option to set metadata for
 * @param input_type The input type (OPTION_INPUT_ENUM, OPTION_INPUT_NUMERIC, etc.)
 *
 * Example:
 * ```c
 * options_builder_set_input_type(builder, "color-mode", OPTION_INPUT_ENUM);
 * options_builder_set_input_type(builder, "log-file", OPTION_INPUT_FILEPATH);
 * ```
 */
void options_builder_set_input_type(options_builder_t *builder, const char *option_name,
                                    option_input_type_t input_type);

/**
 * @brief Mark option as accepting multiple values
 *
 * Indicates the option accepts comma-separated or space-separated values.
 *
 * @param builder Options builder
 * @param option_name Long name of option to mark
 *
 * Example:
 * ```c
 * options_builder_mark_as_list(builder, "stun-servers");  // Accepts comma-separated URLs
 * ```
 */
void options_builder_mark_as_list(options_builder_t *builder, const char *option_name);

/**
 * @brief Set default value string in metadata
 *
 * Sets the default value string for display in completions (informational,
 * separate from the descriptor's default_value which is used for parsing).
 *
 * @param builder Options builder
 * @param option_name Long name of option to set metadata for
 * @param default_value Default value as string for display
 */
void options_builder_set_default_value_display(options_builder_t *builder, const char *option_name,
                                               const char *default_value);

/**
 * @brief Add full option descriptor (advanced)
 *
 * @param builder Builder to add to
 * @param descriptor Descriptor to copy
 */
void options_builder_add_descriptor(options_builder_t *builder, const option_descriptor_t *descriptor);

// ============================================================================
// Managing Dependencies
// ============================================================================

/**
 * @brief Add dependency: if option_name is set, depends_on must be set
 *
 * @param builder Builder to add to
 * @param option_name Option that has the dependency
 * @param depends_on Option it depends on
 * @param error_message Custom error message (or NULL for default)
 */
void options_builder_add_dependency_requires(options_builder_t *builder, const char *option_name,
                                             const char *depends_on, const char *error_message);

/**
 * @brief Add anti-dependency: if option_name is set, conflicts_with must NOT be set
 *
 * @param builder Builder to add to
 * @param option_name Option that has the conflict
 * @param conflicts_with Option it conflicts with
 * @param error_message Custom error message (or NULL for default)
 */
void options_builder_add_dependency_conflicts(options_builder_t *builder, const char *option_name,
                                              const char *conflicts_with, const char *error_message);

/**
 * @brief Add implication: if option_name is set, implies defaults to true
 *
 * @param builder Builder to add to
 * @param option_name Option that implies another
 * @param implies Option that is implied
 * @param error_message Custom message (rarely used, can be NULL)
 */
void options_builder_add_dependency_implies(options_builder_t *builder, const char *option_name, const char *implies,
                                            const char *error_message);

/**
 * @brief Add full dependency (advanced)
 *
 * @param builder Builder to add to
 * @param dependency Dependency to copy
 */
void options_builder_add_dependency(options_builder_t *builder, const option_dependency_t *dependency);

/**
 * @brief Mark an option as binary-level only (hide from mode-specific help)
 *
 * Binary-level options are still parsed by mode-specific parsers (so they work
 * anywhere in the command line), but they don't appear in mode-specific --help.
 * They should only be documented in the top-level binary help.
 *
 * @param builder Builder containing the option
 * @param option_name Long name of the option to mark
 *
 * Example:
 * ```c
 * options_builder_add_string(b, "log-file", 'L', ...);
 * options_builder_mark_binary_only(b, "log-file");
 * ```
 */
void options_builder_mark_binary_only(options_builder_t *builder, const char *option_name);

// ============================================================================
// Positional Arguments
// ============================================================================

/**
 * @brief Add positional argument descriptor
 *
 * Positional arguments are parsed after getopt finishes, from the remaining
 * argv elements that don't start with '-'. They are parsed in the order they
 * are added to the builder.
 *
 * Example (client mode):
 * ```c
 * options_builder_add_positional(
 *     builder,
 *     "address",
 *     "[address][:port] - Server address (IPv4, IPv6, or hostname) with optional port",
 *     false,  // Not required (defaults to localhost)
 *     parse_client_address_arg
 * );
 * ```
 *
 * Example (server mode):
 * ```c
 * options_builder_add_positional(
 *     builder,
 *     "bind-addr",
 *     "Bind address (IPv4 or IPv6) - can specify 0-2 addresses",
 *     false,  // Not required (defaults to localhost dual-stack)
 *     parse_server_bind_addr
 * );
 * ```
 *
 * @param builder Builder to add to
 * @param name Name for help text (e.g., "address")
 * @param help_text Description for usage message
 * @param required If true, this positional arg must be provided
 * @param section_heading Optional section heading for examples (e.g., "ADDRESS FORMATS")
 * @param examples Optional array of example strings with descriptions
 * @param num_examples Number of examples in array
 * @param mode_bitmask Which modes this positional arg applies to
 * @param parse_fn Custom parser (receives arg, config, remaining args, error_msg)
 *                 Returns number of args consumed (usually 1), or -1 on error
 */
void options_builder_add_positional(options_builder_t *builder, const char *name, const char *help_text, bool required,
                                    const char *section_heading, const char **examples, size_t num_examples,
                                    option_mode_bitmask_t mode_bitmask,
                                    int (*parse_fn)(const char *arg, void *config, char **remaining, int num_remaining,
                                                    char **error_msg));

/**
 * @brief Parse positional arguments
 *
 * Parses positional arguments from remaining_argv after getopt processing.
 * Calls each positional arg descriptor's parse_fn in order.
 *
 * This is called automatically by options_config_parse(), but can also be
 * called separately if you want custom control over the parsing flow.
 *
 * @param config Options configuration
 * @param remaining_argc Count of remaining args
 * @param remaining_argv Array of remaining args
 * @param options_struct Options struct to fill
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse errors
 */
asciichat_error_t options_config_parse_positional(const options_config_t *config, int remaining_argc,
                                                  char **remaining_argv, void *options_struct);

// ============================================================================
// Programmatic Help Generation
// ============================================================================

/**
 * @brief Add usage line descriptor
 *
 * Adds a usage line to be printed in the USAGE section. Components are colored
 * separately during printing:
 * - mode: magenta
 * - positional: green
 * - "[options...]": yellow
 *
 * @param builder Builder instance
 * @param mode Mode name (NULL for binary-level, or "server", "<mode>", etc.)
 * @param positional Positional args (NULL or "[bind-addr]", "<session-string>", etc.)
 * @param show_options True to append "[options...]" or "[mode-options...]"
 * @param description Help text for this usage pattern
 *
 * Example:
 * ```c
 * options_builder_add_usage(b, NULL, NULL, true,
 *                           "Start a new session");
 *
 * options_builder_add_usage(b, NULL, "<session-string>", true,
 *                           "Join an existing session");
 *
 * options_builder_add_usage(b, "<mode>", NULL, true,
 *                           "Run in a specific mode");
 * ```
 */
void options_builder_add_usage(options_builder_t *builder, const char *mode, const char *positional, bool show_options,
                               const char *description);

/**
 * @brief Add example descriptor
 *
 * Adds an example command to be printed in the EXAMPLES section. Components
 * are colored separately during printing:
 * - mode: magenta
 * - args: green
 *
 * @param builder Builder instance
 * @param mode Mode name (NULL or "server", "client", "mirror", etc.)
 * @param args Arguments (NULL or "example.com", "swift-river-mountain", etc.)
 * @param description Help text for this example
 *
 * Example:
 * ```c
 * options_builder_add_example(b, NULL, NULL,
 *                             "Start new session");
 *
 * options_builder_add_example(b, "server", NULL,
 *                             "Run as dedicated server");
 *
 * options_builder_add_example(b, "client", "example.com",
 *                             "Connect to specific server");
 * ```
 */
void options_builder_add_example(options_builder_t *builder, const char *mode, const char *args,
                                 const char *description, bool owns_args);

/**
 * @brief Add mode descriptor
 *
 * Adds a mode to be printed in the MODES section. The mode name
 * is colored magenta during printing.
 *
 * @param builder Builder instance
 * @param name Mode name (e.g., "server", "client", "mirror")
 * @param description Mode description
 *
 * Example:
 * ```c
 * options_builder_add_mode(b, "server", "Run as multi-client video chat server");
 * options_builder_add_mode(b, "client", "Run as video chat client (connect to server)");
 * options_builder_add_mode(b, "mirror", "View local webcam as ASCII art (no server)");
 * ```
 */
void options_builder_add_mode(options_builder_t *builder, const char *name, const char *description);

// ============================================================================
// Preset Configurations
// ============================================================================

/**
 * @brief Get binary-level options preset
 *
 * Binary options are parsed BEFORE mode selection.
 * Includes: --help, --version, --log-file, --log-level, etc.
 *
 * @param program_name Optional program name (defaults to "ascii-chat")
 * @param description Optional program description
 * @return Preset config (caller must free after use)
 */
const options_config_t *options_preset_unified(const char *program_name, const char *description);

// ============================================================================
// Parsing and Validation
// ============================================================================

/**
 * @brief Set default values in options struct
 *
 * Sets defaults from descriptors, checking environment variables
 * for options with env_var_name set.
 *
 * @param config Options configuration
 * @param options_struct Options struct to initialize
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t options_config_set_defaults(const options_config_t *config, void *options_struct);

/**
 * @brief Parse command-line arguments
 *
 * Parses argv using the option descriptors. Strings are automatically
 * strdup'd. Environment variables are checked for missing options.
 *
 * @param config Options configuration
 * @param argc Argument count
 * @param argv Argument vector
 * @param options_struct Options struct to fill
 * @param remaining_argc Optional: receives count of non-option args
 * @param remaining_argv Optional: receives array of non-option args
 * @return ASCIICHAT_OK on success, ERROR_USAGE on parse errors
 */
asciichat_error_t options_config_parse(const options_config_t *config, int argc, char **argv, void *options_struct,
                                       option_mode_bitmask_t detected_mode, int *remaining_argc,
                                       char ***remaining_argv);

/**
 * @brief Validate options struct
 *
 * Checks:
 * - Required fields are set
 * - Dependencies are satisfied
 * - Custom validators pass
 *
 * @param config Options configuration
 * @param options_struct Options struct to validate
 * @param error_message Optional: receives detailed error message (must free)
 * @return ASCIICHAT_OK if valid, error code otherwise
 */
asciichat_error_t options_config_validate(const options_config_t *config, const void *options_struct,
                                          char **error_message);

/**
 * @brief Calculate global max column width for help output alignment
 *
 * Calculates the maximum width needed for proper alignment across
 * all help sections (USAGE, EXAMPLES, OPTIONS, MODES).
 *
 * @param config Options configuration
 * @return Maximum column width needed for alignment
 */
int options_config_calculate_max_col_width(const options_config_t *config);

/**
 * @brief Print usage/help text
 *
 * Generates formatted help with grouped options.
 *
 * @param config Options configuration
 * @param stream Output stream (stdout or stderr)
 */
void options_config_print_usage(const options_config_t *config, FILE *stream);

/**
 * @brief Print only the USAGE section
 *
 * Prints just the USAGE section. Useful for inserting other content (like positional
 * argument examples) between USAGE and other sections.
 *
 * @param config Options configuration
 * @param stream Output stream (stdout or stderr)
 */
void options_config_print_usage_section(const options_config_t *config, FILE *stream);

/**
 * @brief Print everything except the USAGE section
 *
 * Prints MODES, MODE-OPTIONS, EXAMPLES, and OPTIONS sections. Used with
 * options_config_print_usage_section to allow custom content in between.
 *
 * @param config Options configuration (should be unified preset with all options)
 * @param stream Output stream (stdout or stderr)
 * @param max_col_width Optional pre-calculated column width (0 = auto-calculate)
 * @param mode Optional mode to filter by (if MODE_SERVER, MODE_CLIENT, etc., filters by mode_bitmask)
 *             If NULL or invalid, shows all options (for binary-level help)
 */
void options_config_print_options_sections_with_width(const options_config_t *config, FILE *stream, int max_col_width,
                                                      asciichat_mode_t mode);

/**
 * @brief Print everything except the USAGE section (backward compatibility)
 *
 * Wrapper for options_config_print_options_sections_with_width with auto-calculation.
 *
 * @param config Options configuration
 * @param stream Output stream (stdout or stderr)
 * @param mode Optional mode to filter by (if provided, filters by mode_bitmask)
 */
void options_config_print_options_sections(const options_config_t *config, FILE *stream, asciichat_mode_t mode);

/**
 * @brief Print help for a specific mode or binary level (unified function)
 *
 * This is the single unified function for all help output (binary level and all modes).
 * It handles all layout logic, terminal detection, and section printing.
 *
 * @param config Options config with all options
 * @param mode Mode to show help for (-1 for binary-level help)
 * @param program_name Full program name (e.g., "ascii-chat server")
 * @param description Brief description
 * @param desc Output file stream
 */
void options_print_help_for_mode(const options_config_t *config, asciichat_mode_t mode, const char *program_name,
                                 const char *description, FILE *desc);

/**
 * @brief Clean up memory owned by options struct
 *
 * Frees all auto-duplicated strings and sets pointers to NULL.
 * Call this before freeing the options struct.
 *
 * @param config Options configuration
 * @param options_struct Options struct to clean up
 */
void options_config_cleanup(const options_config_t *config, void *options_struct);

// ============================================================================
// Option Formatting Utilities
// ============================================================================

/**
 * @brief Get placeholder string for option type
 *
 * Returns standardized placeholder strings for use in help text and man pages:
 * - INT/DOUBLE → "NUM"
 * - STRING → "STR"
 * - CALLBACK → "VAL"
 * - BOOL/ACTION → "" (empty string)
 *
 * @param type Option type enum value
 * @return Pointer to string literal (never NULL, may be empty)
 */
const char *options_get_type_placeholder(option_type_t type);

/**
 * @brief Format option default value to string
 *
 * Formats default value according to option type:
 * - BOOL: "true" or "false"
 * - INT: "%d" format
 * - STRING: raw string (caller applies escaping if needed)
 * - DOUBLE: "%.2f" format
 *
 * @param type Option type enum value
 * @param default_value Pointer to default value (type-specific)
 * @param buf Output buffer
 * @param bufsize Size of output buffer
 * @return Number of characters written, or 0 on error
 */
int options_format_default_value(option_type_t type, const void *default_value, char *buf, size_t bufsize);
