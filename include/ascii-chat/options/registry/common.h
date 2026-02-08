/**
 * @file common.h
 * @brief Shared structures and macros for registry implementation
 * @ingroup options
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/options.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Mode-aware default value getter function type
 *
 * Returns the default value for an option based on the current mode.
 * Used for options that have different defaults per mode (e.g., log-file, port, websocket-port).
 *
 * @param mode The mode to get the default for
 * @return Pointer to the default value for this mode (must remain valid)
 */
typedef const void *(*mode_default_getter_fn)(asciichat_mode_t mode);

/**
 * @brief Registry entry - stores option definition with mode bitmask and metadata
 */
typedef struct {
  const char *long_name;
  char short_name;
  option_type_t type;
  size_t offset;
  const void *default_value; ///< Default value (single value for all modes, or NULL if mode_default_getter is set)
  size_t default_value_size;
  const char *help_text;
  const char *group;
  const char *arg_placeholder; ///< Custom argument placeholder (e.g., "SHELL [FILE]" instead of "STR")
  bool required;
  const char *env_var_name;
  bool (*validate_fn)(const void *options_struct, char **error_msg);
  bool (*parse_fn)(const char *arg, void *dest, char **error_msg);
  bool owns_memory;
  bool optional_arg;
  option_mode_bitmask_t mode_bitmask;
  option_metadata_t metadata; ///< Enum values, numeric ranges, examples
  mode_default_getter_fn
      mode_default_getter; ///< Mode-aware default getter (NULL if using default_value) - LAST for backward compat
} registry_entry_t;

/**
 * @brief Category builder structure - maps categories to their entry arrays
 */
typedef struct {
  const registry_entry_t *entries;
  const char *name;
} category_builder_t;

/**
 * @brief Initialize a terminator entry (sentinel value for array end)
 * Uses designated initializers to properly initialize all fields to zero/NULL
 */
#define REGISTRY_TERMINATOR()                                                                                          \
  {.long_name = NULL,                                                                                                  \
   .short_name = '\0',                                                                                                 \
   .type = OPTION_TYPE_BOOL,                                                                                           \
   .offset = 0,                                                                                                        \
   .default_value = NULL,                                                                                              \
   .mode_default_getter = NULL,                                                                                        \
   .default_value_size = 0,                                                                                            \
   .help_text = NULL,                                                                                                  \
   .group = NULL,                                                                                                      \
   .arg_placeholder = NULL,                                                                                            \
   .required = false,                                                                                                  \
   .env_var_name = NULL,                                                                                               \
   .validate_fn = NULL,                                                                                                \
   .parse_fn = NULL,                                                                                                   \
   .owns_memory = false,                                                                                               \
   .optional_arg = false,                                                                                              \
   .mode_bitmask = OPTION_MODE_NONE,                                                                                   \
   .metadata = {0}}
