/**
 * @file internal.h
 * @brief Internal declarations for builder implementation
 * @ingroup options
 *
 * Shared structures and forward declarations used across builder modules.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#pragma once

#include <ascii-chat/options/builder.h>
#include <ascii-chat/common.h>
#include <stdbool.h>
#include <stddef.h>

// Initial capacities for dynamic arrays
#define INITIAL_DESCRIPTOR_CAPACITY 32
#define INITIAL_DEPENDENCY_CAPACITY 16
#define INITIAL_POSITIONAL_ARG_CAPACITY 8
#define INITIAL_OWNED_STRINGS_CAPACITY 32

/**
 * @brief Type handler for builder operations
 */
typedef struct {
  // Check if option value differs from default
  bool (*is_set)(const void *field, const option_descriptor_t *desc);
  // Apply environment variable and set defaults
  void (*apply_env)(void *field, const char *env_value, const option_descriptor_t *desc);
  // Parse CLI argument value
  asciichat_error_t (*apply_cli)(void *field, const char *opt_value, const option_descriptor_t *desc);
  // Format value for help text placeholder (e.g., "NUM", "STR")
  void (*format_help_placeholder)(char *buf, size_t bufsize);
} option_builder_handler_t;

// Handler registry - indexed by option_type_t (defined in handlers.c)
extern const option_builder_handler_t g_builder_handlers[];
#define NUM_OPTION_TYPES 6 // BOOL, INT, STRING, DOUBLE, CALLBACK, ACTION

// Helper functions from various modules (can be called across modules)
const char *get_option_help_placeholder_str(const option_descriptor_t *desc);
int format_option_default_value_str(const option_descriptor_t *desc, char *buf, size_t bufsize);
bool option_applies_to_mode(const option_descriptor_t *desc, asciichat_mode_t mode, bool for_binary_help);

// Capacity management (in builder.c)
void ensure_descriptor_capacity(options_builder_t *builder);
void ensure_dependency_capacity(options_builder_t *builder);
void ensure_positional_arg_capacity(options_builder_t *builder);
void ensure_owned_strings_capacity(options_builder_t *builder);

// Internal lookup (in lifecycle.c)
const option_descriptor_t *find_option(const options_config_t *config, const char *long_name);
bool is_option_set(const options_config_t *config, const void *options_struct, const char *option_name);
