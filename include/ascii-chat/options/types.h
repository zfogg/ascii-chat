#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <ascii-chat/options/options.h>

/** @brief Origin type used by the ascii-chat registry before conversion to libflags. */
typedef enum {
  OPTION_TYPE_BOOL,
  OPTION_TYPE_INT,
  OPTION_TYPE_STRING,
  OPTION_TYPE_DOUBLE,
  OPTION_TYPE_CALLBACK,
  OPTION_TYPE_ACTION,
} option_type_t;

/** @brief Completion input classification retained for ascii-chat UI metadata. */
typedef enum {
  OPTION_INPUT_NONE,
  OPTION_INPUT_ENUM,
  OPTION_INPUT_NUMERIC,
  OPTION_INPUT_STRING,
  OPTION_INPUT_FILEPATH,
  OPTION_INPUT_CHOICE,
} option_input_type_t;

/** @brief Registry metadata used to enrich libflags completion descriptors. */
typedef struct {
  const char **enum_values;
  const char **enum_descriptions;
  const int *enum_integer_values;
  struct {
    int min;
    int max;
    int step;
  } numeric_range;
  const char **examples;
  const char *default_value;
  option_input_type_t input_type;
  bool is_list;
  const char *validation_pattern;
} option_metadata_t;

/** @brief Dependency relationship retained for ascii-chat policy declarations. */
typedef enum {
  DEPENDENCY_REQUIRES,
  DEPENDENCY_CONFLICTS,
  DEPENDENCY_IMPLIES,
} dependency_type_t;

/** @brief Legacy registry view used only by ascii-chat completion adapters. */
typedef struct {
  const char *long_name;
  char short_name;
  option_type_t type;
  size_t offset;
  const char *help_text;
  const char *group;
  const char *arg_placeholder;
  bool hide_from_mode_help;
  bool hide_from_binary_help;
  const void *default_value;
  bool required;
  const char *env_var_name;
  bool (*validate)(const void *options_struct);
  bool (*parse_fn)(const char *arg, void *dest, char **error_msg);
  void (*action_fn)(void);
  bool owns_memory;
  bool optional_arg;
  option_mode_bitmask_t mode_bitmask;
  option_metadata_t metadata;
  const void *(*mode_default_getter)(asciichat_mode_t mode);
} option_descriptor_t;

