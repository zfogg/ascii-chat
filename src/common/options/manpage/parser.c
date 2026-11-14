/**
 * @file parser.c
 * @brief ascii-chat compatibility adapters for the libflags manpage parser.
 */

#include <ascii-chat/options/manpage/parser.h>
#include <libflags/manpage/parser.h>

#include <stdlib.h>
#include <string.h>

static section_type_t to_ascii_type(flags_manpage_section_type_t type) {
  switch (type) {
  case FLAGS_MANPAGE_SECTION_AUTO: return SECTION_TYPE_AUTO;
  case FLAGS_MANPAGE_SECTION_MANUAL: return SECTION_TYPE_MANUAL;
  case FLAGS_MANPAGE_SECTION_MERGE: return SECTION_TYPE_MERGE;
  default: return SECTION_TYPE_UNMARKED;
  }
}

static bool convert_sections(flags_manpage_section_t *source, size_t count, parsed_section_t **output) {
  parsed_section_t *converted = calloc(count ? count : 1, sizeof(*converted));
  if (!converted) return false;
  for (size_t i = 0; i < count; ++i) {
    converted[i].section_name = source[i].name;
    converted[i].type = to_ascii_type(source[i].type);
    converted[i].content = source[i].content;
    converted[i].content_len = source[i].content_length;
    converted[i].start_line = source[i].start_line;
    converted[i].end_line = source[i].end_line;
    converted[i].has_markers = source[i].has_markers;
    source[i].name = NULL;
    source[i].content = NULL;
  }
  *output = converted;
  return true;
}

asciichat_error_t manpage_parser_parse_file(FILE *f, parsed_section_t **out_sections, size_t *out_count) {
  if (!f || !out_sections || !out_count) return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  flags_manpage_section_t *source = NULL;
  size_t count = 0;
  if (!flags_manpage_parser_parse(f, &source, &count) || !convert_sections(source, count, out_sections)) {
    flags_manpage_parser_free(source, count);
    return SET_ERRNO(ERROR_CONFIG, "Failed to parse manpage sections");
  }
  flags_manpage_parser_free(source, count);
  *out_count = count;
  return ASCIICHAT_OK;
}

asciichat_error_t manpage_parser_parse_memory(const char *content, size_t content_len, parsed_section_t **out_sections, size_t *out_count) {
  if (!content || !content_len || !out_sections || !out_count) return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  flags_manpage_section_t *source = NULL;
  size_t count = 0;
  if (!flags_manpage_parser_parse_memory(content, content_len, &source, &count) || !convert_sections(source, count, out_sections)) {
    flags_manpage_parser_free(source, count);
    return SET_ERRNO(ERROR_CONFIG, "Failed to parse manpage sections");
  }
  flags_manpage_parser_free(source, count);
  *out_count = count;
  return ASCIICHAT_OK;
}

void manpage_parser_free_sections(parsed_section_t *sections, size_t count) {
  if (!sections) return;
  for (size_t i = 0; i < count; ++i) {
    free(sections[i].section_name);
    free(sections[i].content);
  }
  free(sections);
}

const parsed_section_t *manpage_parser_find_section(const parsed_section_t *sections, size_t count, const char *section_name) {
  if (!sections || !section_name) return NULL;
  for (size_t i = 0; i < count; ++i)
    if (sections[i].section_name && strcmp(sections[i].section_name, section_name) == 0) return &sections[i];
  return NULL;
}
