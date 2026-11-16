/**
 * @file enums.c
 * @brief Enum views backed by the option registry metadata.
 */
#include <ascii-chat/options/enums.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/common.h>

const enum_to_string_entry_t *options_get_enum_entries(const char *option_name, size_t *entry_count) {
  if (!entry_count)
    return NULL;
  *entry_count = 0;
  if (!option_name)
    return NULL;
  const option_metadata_t *meta = options_registry_get_metadata(option_name);
  if (!meta || !meta->enum_values)
    return NULL;
  while (meta->enum_values[*entry_count])
    ++*entry_count;
  enum_to_string_entry_t *entries = SAFE_CALLOC(*entry_count + 1, sizeof(*entries), enum_to_string_entry_t *);
  for (size_t i = 0; i < *entry_count; ++i) {
    entries[i].enum_value = meta->enum_integer_values ? meta->enum_integer_values[i] : (int)i;
    entries[i].string = meta->enum_values[i];
    entries[i].desc = meta->enum_descriptions ? meta->enum_descriptions[i] : NULL;
  }
  entries[*entry_count].enum_value = -1;
  return entries;
}

const char **options_get_enum_values(const char *option_name, size_t *value_count) {
  if (!value_count)
    return NULL;
  *value_count = 0;
  if (!option_name)
    return NULL;
  const char **values = options_registry_get_enum_values(option_name, NULL, value_count);
  if (!values)
    return NULL;
  const char **copy = SAFE_MALLOC((*value_count + 1) * sizeof(*copy), const char **);
  memcpy(copy, values, (*value_count + 1) * sizeof(*copy));
  return copy;
}

bool options_is_enum_option(const char *option_name) {
  if (!option_name)
    return false;
  size_t count = 0;
  return options_registry_get_enum_values(option_name, NULL, &count) != NULL;
}
