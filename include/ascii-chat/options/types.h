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


