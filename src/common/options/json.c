#include <ascii-chat/options/json.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/options/builder/internal.h>
#include <math.h>
#include <string.h>

static const char *json_type(const registry_entry_t *entry) {
  if (entry->type == OPTION_TYPE_BOOL)
    return "boolean";
  if (entry->type == OPTION_TYPE_INT || !strcmp(entry->long_name, "zalgo"))
    return "integer";
  if (entry->type == OPTION_TYPE_DOUBLE || (entry->type == OPTION_TYPE_CALLBACK && !entry->metadata.enum_values &&
                                            entry->metadata.numeric_range.max && strcmp(entry->long_name, "seek")))
    return "number";
  return "string";
}
static asciichat_error_t validate_value(const registry_entry_t *e, yyjson_val *v) {
  if (!strcmp(e->long_name, "test-pattern")) {
    if (yyjson_is_bool(v) || (yyjson_is_int(v) && yyjson_get_sint(v) >= 0 && yyjson_get_sint(v) <= 1) ||
        (yyjson_is_str(v) && (!strcmp(yyjson_get_str(v), "0") || !strcmp(yyjson_get_str(v), "1"))))
      return ASCIICHAT_OK;
    return SET_ERRNO(ERROR_INVALID_PARAM, "test-pattern must be boolean, 0 or 1");
  }
  const char *type = json_type(e);
  bool ok = (!strcmp(type, "boolean") && yyjson_is_bool(v)) || (!strcmp(type, "integer") && yyjson_is_int(v)) ||
            (!strcmp(type, "number") && yyjson_is_num(v)) || (!strcmp(type, "string") && yyjson_is_str(v)) ||
            (!strcmp(e->long_name, "seek") && yyjson_is_num(v));
  if (!ok)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Wrong type for %s", e->long_name);
  if (yyjson_is_str(v) && (yyjson_get_len(v) > 4096 || strlen(yyjson_get_str(v)) != yyjson_get_len(v)))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid or oversized option %s", e->long_name);
  if (yyjson_is_num(v) && !isfinite(yyjson_get_num(v)))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Non-finite option");
  return ASCIICHAT_OK;
}

yyjson_mut_val *options_json_schema_for_option(yyjson_mut_doc *d, const char *name) {
  if (!d || !name) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Missing option schema argument");
    return NULL;
  }
  const registry_entry_t *e = registry_find_entry_by_name(name);
  if (!e || e->type == OPTION_TYPE_ACTION) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Unknown value option: %s", name);
    return NULL;
  }
  yyjson_mut_val *v = yyjson_mut_obj(d);
  yyjson_mut_obj_add_str(d, v, "type", json_type(e));
  yyjson_mut_obj_add_str(d, v, "description", e->help_text);
  if (e->metadata.enum_values) {
    yyjson_mut_val *values = yyjson_mut_arr(d);
    for (size_t i = 0; e->metadata.enum_values[i]; i++)
      yyjson_mut_arr_add_str(d, values, e->metadata.enum_values[i]);
    yyjson_mut_obj_add_val(d, v, "enum", values);
  }
  if (!strcmp(name, "test-pattern")) {
    yyjson_mut_obj_remove_str(v, "type");
    yyjson_mut_obj_remove_str(v, "enum");
    yyjson_mut_val *values = yyjson_mut_arr(d);
    yyjson_mut_arr_add_bool(d, values, true);
    yyjson_mut_arr_add_bool(d, values, false);
    yyjson_mut_arr_add_int(d, values, 0);
    yyjson_mut_arr_add_int(d, values, 1);
    yyjson_mut_arr_add_str(d, values, "0");
    yyjson_mut_arr_add_str(d, values, "1");
    yyjson_mut_obj_add_val(d, v, "enum", values);
  }
  if (!strcmp(name, "seek")) {
    yyjson_mut_obj_remove_str(v, "type");
    yyjson_mut_val *types = yyjson_mut_arr(d);
    yyjson_mut_arr_add_str(d, types, "string");
    yyjson_mut_arr_add_str(d, types, "number");
    yyjson_mut_obj_add_val(d, v, "type", types);
  }
  return v;
}

asciichat_error_t options_json_parse_value(const char *name, yyjson_val *v, char *output, size_t output_size,
                                           bool *omitted) {
  if (!name || !v || !output || !output_size || !omitted)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing JSON option argument");
  *omitted = false;
  const registry_entry_t *e = registry_find_entry_by_name(name);
  if (!e || e->type < 0 || e->type >= NUM_OPTION_TYPES || e->type == OPTION_TYPE_ACTION || e->owns_memory)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported value option: %s", name);
  asciichat_error_t err = validate_value(e, v);
  if (err != ASCIICHAT_OK)
    return err;
  if (yyjson_is_str(v)) {
    size_t len = yyjson_get_len(v);
    if (len >= output_size || (e->type == OPTION_TYPE_STRING && e->default_value_size && len >= e->default_value_size))
      return SET_ERRNO(ERROR_INVALID_PARAM, "Option value too long: %s", name);
    SAFE_STRNCPY(output, yyjson_get_str(v), output_size);
  } else if (yyjson_is_bool(v)) {
    SAFE_SNPRINTF(output, output_size, "%s", yyjson_get_bool(v) ? "true" : "false");
  } else {
    SAFE_SNPRINTF(output, output_size, "%.17g", yyjson_get_num(v));
  }
  if (!strcmp(name, "test-pattern") && yyjson_is_bool(v)) {
    if (!yyjson_get_bool(v)) {
      *omitted = true;
      return ASCIICHAT_OK;
    }
    SAFE_SNPRINTF(output, output_size, "0");
  }
  // Use the same registry descriptor and handlers as options_config_parse().
  option_descriptor_t descriptor = registry_entry_to_descriptor(e);
  options_t *scratch = SAFE_CALLOC(1, sizeof(*scratch), options_t *);
  err = g_builder_handlers[descriptor.type].apply_cli((char *)scratch + descriptor.offset, output, &descriptor);
  SAFE_FREE(scratch);
  return err;
}
