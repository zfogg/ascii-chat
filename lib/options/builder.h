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
 */
typedef struct {
  // Identification
  const char *long_name; ///< Long option name (e.g., "port")
  char short_name;       ///< Short option char (e.g., 'p', or '\0' if none)

  // Type and storage
  option_type_t type; ///< Value type
  size_t offset;      ///< offsetof(struct, field) - where to store value

  // Documentation
  const char *help_text;    ///< Description for --help
  const char *group;        ///< Group name for help sections (e.g., "NETWORK OPTIONS")
  bool hide_from_mode_help; ///< If true, don't show in mode-specific help (binary-level only)
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
  const char *mode;        ///< NULL or mode name (e.g., "server", "client")
  const char *args;        ///< NULL or args (e.g., "example.com", "swift-river-mountain")
  const char *description; ///< Help text for this example
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

  size_t struct_size;       ///< Target struct size
  const char *program_name; ///< Program name for usage
  const char *description;  ///< Program description for usage
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
 * @param parse_fn Custom parser (receives arg, config, remaining args, error_msg)
 *                 Returns number of args consumed (usually 1), or -1 on error
 */
void options_builder_add_positional(options_builder_t *builder, const char *name, const char *help_text, bool required,
                                    const char *section_heading, const char **examples, size_t num_examples,
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
                                 const char *description);

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
const options_config_t *options_preset_binary(const char *program_name, const char *description);

/**
 * @brief Get server mode options preset
 *
 * @param program_name Optional program name (defaults to "ascii-chat server")
 * @param description Optional program description (defaults to "Start ascii-chat server")
 * @return Preset config (caller must free after use)
 */
const options_config_t *options_preset_server(const char *program_name, const char *description);

/**
 * @brief Get client mode options preset
 *
 * @param program_name Optional program name (defaults to "ascii-chat client")
 * @param description Optional program description (defaults to "Connect to ascii-chat server")
 * @return Preset config (caller must free after use)
 */
const options_config_t *options_preset_client(const char *program_name, const char *description);

/**
 * @brief Get mirror mode options preset
 *
 * @param program_name Optional program name (defaults to "ascii-chat mirror")
 * @param description Optional program description (defaults to "Local webcam viewing (no network)")
 * @return Preset config (caller must free after use)
 */
const options_config_t *options_preset_mirror(const char *program_name, const char *description);

/**
 * @brief Get acds mode options preset
 *
 * @param program_name Optional program name (defaults to "ascii-chat acds")
 * @param description Optional program description (defaults to "ascii-chat Discovery Service")
 * @return Preset config (caller must free after use)
 */
const options_config_t *options_preset_acds(const char *program_name, const char *description);

/**
 * @brief Get discovery mode options preset
 *
 * Discovery mode allows participants to join a session and dynamically become
 * the host based on NAT quality. Combines client-like options (display, webcam,
 * audio) with ACDS discovery options.
 *
 * @param program_name Optional program name (defaults to "ascii-chat discovery")
 * @param description Optional program description
 * @return Preset config (caller must free after use)
 */
const options_config_t *options_preset_discovery(const char *program_name, const char *description);

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
                                       int *remaining_argc, char ***remaining_argv);

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
 * @param config Options configuration
 * @param stream Output stream (stdout or stderr)
 * @param max_col_width Optional pre-calculated column width (0 = auto-calculate)
 */
void options_config_print_options_sections_with_width(const options_config_t *config, FILE *stream, int max_col_width);

/**
 * @brief Print everything except the USAGE section (backward compatibility)
 *
 * Wrapper for options_config_print_options_sections_with_width with auto-calculation.
 *
 * @param config Options configuration
 * @param stream Output stream (stdout or stderr)
 */
void options_config_print_options_sections(const options_config_t *config, FILE *stream);

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
// Option Group Helpers
// ============================================================================

/**
 * @brief Add binary-level logging options to a builder
 *
 * Adds options that are common across all modes and can be used
 * before or after the mode name: --log-file, --log-level, -V, -q
 *
 * @param b Builder to add options to
 */
void options_builder_add_logging_group(options_builder_t *b);

/**
 * @brief Add terminal dimension options to a builder
 *
 * Adds --width and --height options for terminal size configuration.
 * Used by: client, mirror, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_terminal_group(options_builder_t *b);

/**
 * @brief Add webcam options to a builder
 *
 * Adds webcam device selection, flip, and test pattern options.
 * Used by: client, mirror, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_webcam_group(options_builder_t *b);

/**
 * @brief Add display/rendering options to a builder
 *
 * Adds color mode, palette, render mode, and other display options.
 * Used by: client, mirror, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_display_group(options_builder_t *b);

/**
 * @brief Add snapshot mode options to a builder
 *
 * Adds --snapshot and --snapshot-delay options for single-frame capture.
 * Used by: client, mirror, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_snapshot_group(options_builder_t *b);

/**
 * @brief Add compression and audio encoding options to a builder
 *
 * Adds --compression-level, --no-compress, --encode-audio options.
 * Used by: client, server, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_compression_group(options_builder_t *b);

/**
 * @brief Add encryption and authentication options to a builder
 *
 * Adds --encrypt, --key, --password, --keyfile, --no-encrypt options.
 * Used by: client, server, discovery, acds modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_crypto_group(options_builder_t *b);

/**
 * @brief Add port option to a builder
 *
 * Adds -p/--port option with configurable default and env var.
 * Used by: server, client, acds modes
 *
 * @param b Builder to add options to
 * @param default_port Default port value (as string)
 * @param env_var Environment variable name for port override
 */
void options_builder_add_port_option(options_builder_t *b, const char *default_port, const char *env_var);

/**
 * @brief Add ACDS discovery service options to a builder
 *
 * Adds --acds-server, --acds-port, --acds-key, --webrtc options.
 * Used by: client, server, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_acds_group(options_builder_t *b);

/**
 * @brief Add WebRTC connection strategy options to a builder
 *
 * Adds --prefer-webrtc, --no-webrtc, --webrtc-skip-stun, --webrtc-disable-turn.
 * Used by: client, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_webrtc_strategy_group(options_builder_t *b);

/**
 * @brief Add ACDS security verification options to a builder
 *
 * Adds --acds-insecure and --acds-key for ACDS server verification.
 * Used by: client, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_acds_security_group(options_builder_t *b);

/**
 * @brief Add media file streaming options to a builder
 *
 * Adds --file and --loop options for media file playback.
 * Used by: client, mirror, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_media_group(options_builder_t *b);

/**
 * @brief Add audio streaming options to a builder
 *
 * Adds --audio, --microphone-index, --speakers-index, and volume options.
 * Used by: client, discovery modes
 *
 * @param b Builder to add options to
 */
void options_builder_add_audio_group(options_builder_t *b);
