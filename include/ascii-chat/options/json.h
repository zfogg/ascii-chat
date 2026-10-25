/** @file json.h
 * @brief Registry-backed JSON schemas and conversion through CLI option handlers.
 */
#pragma once
#include <ascii-chat/asciichat_errno.h>
#include <yyjson.h>
#include <stdbool.h>
#include <stddef.h>

/** Generate a JSON property from a registered option's metadata; document owns the result. */
yyjson_mut_val *options_json_schema_for_option(yyjson_mut_doc *doc, const char *name);
/** Convert and parse a JSON value with the CLI handler, without publishing global options.
 * omitted is set for an explicitly disabled optional flag such as test-pattern=false.
 * Full cross-option validation remains in the normal CLI worker initialization.
 */
asciichat_error_t options_json_parse_value(const char *name, yyjson_val *value, char *output, size_t output_size,
                                           bool *omitted);
