/**
 * @file parser.c
 * @brief Man page template parsing and section extraction implementation
 * @ingroup options_manpage
 *
 * Parses man page templates to extract sections and detect their types
 * (AUTO-generated, MANUAL, or MERGE sections).
 */

#include "parser.h"
#include "../../log/logging.h"
#include "../../common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// Helper Functions
// ============================================================================

/**
 * @brief Check if line is a section marker comment
 *
 * Detects lines like:
 * - .\" AUTO-START: ENVIRONMENT
 * - .\" MANUAL-END: DESCRIPTION
 * - .\" MERGE-START: OPTIONS
 */
static bool is_marker_line(const char *line, const char **type_out, char **section_out) {
  if (!line) {
    return false;
  }

  // Skip leading whitespace
  const char *p = line;
  while (isspace((unsigned char)*p)) {
    p++;
  }

  // Check for groff comment: .\ " or ."
  if (strncmp(p, ".\\\"", 3) != 0 && strncmp(p, ".\"", 2) != 0) {
    return false;
  }

  // Skip comment marker
  if (p[0] == '.' && p[1] == '\\' && p[2] == '"') {
    p += 3;
  } else if (p[0] == '.' && p[1] == '"') {
    p += 2;
  }

  // Skip whitespace after comment marker
  while (isspace((unsigned char)*p)) {
    p++;
  }

  // Check for marker pattern: TYPE-START: SECTION or TYPE-END: SECTION
  const char *type = NULL;
  const char *section_start = NULL;

  if (strncmp(p, "AUTO-START:", 11) == 0) {
    type = "AUTO";
    section_start = p + 11;
  } else if (strncmp(p, "AUTO-END:", 9) == 0) {
    type = "AUTO";
    section_start = p + 9;
  } else if (strncmp(p, "MANUAL-START:", 13) == 0) {
    type = "MANUAL";
    section_start = p + 13;
  } else if (strncmp(p, "MANUAL-END:", 11) == 0) {
    type = "MANUAL";
    section_start = p + 11;
  } else if (strncmp(p, "MERGE-START:", 12) == 0) {
    type = "MERGE";
    section_start = p + 12;
  } else if (strncmp(p, "MERGE-END:", 10) == 0) {
    type = "MERGE";
    section_start = p + 10;
  } else {
    return false;
  }

  // Skip whitespace before section name
  while (isspace((unsigned char)*section_start)) {
    section_start++;
  }

  // Extract section name (until end of line)
  const char *section_end = section_start;
  while (*section_end && *section_end != '\n' && *section_end != '\r') {
    section_end++;
  }

  if (section_end > section_start) {
    if (type_out) {
      *type_out = type;
    }
    if (section_out) {
      size_t len = section_end - section_start;
      *section_out = SAFE_MALLOC(len + 1, char *);
      memcpy(*section_out, section_start, len);
      (*section_out)[len] = '\0';
      // Trim trailing whitespace
      char *s = *section_out + len - 1;
      while (s >= *section_out && isspace((unsigned char)*s)) {
        *s-- = '\0';
      }
    }
    return true;
  }

  return false;
}

/**
 * @brief Check if line is a section header (.SH directive)
 */
static bool is_section_header(const char *line, char **section_name_out) {
  if (!line) {
    return false;
  }

  // Skip leading whitespace
  const char *p = line;
  while (isspace((unsigned char)*p)) {
    p++;
  }

  // Check for .SH directive
  if (strncmp(p, ".SH", 3) != 0) {
    return false;
  }

  p += 3;

  // Skip whitespace
  while (isspace((unsigned char)*p)) {
    p++;
  }

  // Extract section name (until end of line)
  const char *name_start = p;
  const char *name_end = name_start;
  while (*name_end && *name_end != '\n' && *name_end != '\r') {
    name_end++;
  }

  if (name_end > name_start) {
    size_t len = name_end - name_start;
    *section_name_out = SAFE_MALLOC(len + 1, char *);
    memcpy(*section_name_out, name_start, len);
    (*section_name_out)[len] = '\0';
    // Trim trailing whitespace
    char *s = *section_name_out + len - 1;
    while (s >= *section_name_out && isspace((unsigned char)*s)) {
      *s-- = '\0';
    }
    // Remove quotes if present
    len = strlen(*section_name_out);
    if (len >= 2 && (*section_name_out)[0] == '"' && (*section_name_out)[len - 1] == '"') {
      memmove(*section_name_out, *section_name_out + 1, len - 2);
      (*section_name_out)[len - 2] = '\0';
    }
    return true;
  }

  return false;
}

/**
 * @brief Convert type string to enum
 */
static section_type_t type_string_to_enum(const char *type_str) {
  if (!type_str) {
    return SECTION_TYPE_UNMARKED;
  }
  if (strcmp(type_str, "AUTO") == 0) {
    return SECTION_TYPE_AUTO;
  }
  if (strcmp(type_str, "MANUAL") == 0) {
    return SECTION_TYPE_MANUAL;
  }
  if (strcmp(type_str, "MERGE") == 0) {
    return SECTION_TYPE_MERGE;
  }
  return SECTION_TYPE_UNMARKED;
}

/**
 * @brief Parse sections from FILE handle (common implementation)
 */
static asciichat_error_t parse_sections_internal(FILE *f, parsed_section_t **out_sections, size_t *out_count) {
  if (!f || !out_sections || !out_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_sections_internal");
  }

  parsed_section_t *sections = NULL;
  size_t capacity = 16;
  size_t count = 0;
  sections = SAFE_MALLOC(capacity * sizeof(parsed_section_t), parsed_section_t *);

  char *line = NULL;
  size_t line_len = 0;
  size_t line_num = 0;

  parsed_section_t *current_section = NULL;
  char *current_content = NULL;
  size_t current_content_capacity = 4096;
  size_t current_content_len = 0;
  current_content = SAFE_MALLOC(current_content_capacity, char *);

  // Track markers for current section
  const char *current_type = NULL;
  char *current_marker_section = NULL;
  bool in_marked_section = false;

  while (getline(&line, &line_len, f) != -1) {
    line_num++;

    const char *marker_type = NULL;
    char *marker_section = NULL;
    bool is_marker = is_marker_line(line, &marker_type, &marker_section);

    char *section_header_name = NULL;
    bool is_header = is_section_header(line, &section_header_name);

    if (is_marker) {
      // Check if this is a START marker
      if (strstr(line, "-START:") != NULL) {
        // Start of a marked section
        current_type = marker_type;
        if (current_marker_section) {
          SAFE_FREE(current_marker_section);
        }
        current_marker_section = marker_section;
        in_marked_section = true;
        marker_section = NULL; // Ownership transferred
      } else if (strstr(line, "-END:") != NULL) {
        // End of marked section
        if (marker_section) {
          SAFE_FREE(marker_section);
        }
        in_marked_section = false;
        current_type = NULL;
        if (current_marker_section) {
          SAFE_FREE(current_marker_section);
          current_marker_section = NULL;
        }
      } else {
        if (marker_section) {
          SAFE_FREE(marker_section);
        }
      }
    }

    if (is_header) {
      // Finalize previous section if exists
      if (current_section) {
        if (current_content && current_content_len > 0) {
          current_section->content = current_content;
          current_section->content_len = current_content_len;
          current_content = NULL;
          current_content_capacity = 4096;
          current_content_len = 0;
          current_content = SAFE_MALLOC(current_content_capacity, char *);
        } else {
          SAFE_FREE(current_content);
          current_content = NULL;
          current_content_capacity = 4096;
          current_content_len = 0;
          current_content = SAFE_MALLOC(current_content_capacity, char *);
        }
      }

      // Start new section
      if (count >= capacity) {
        capacity *= 2;
        sections = SAFE_REALLOC(sections, capacity * sizeof(parsed_section_t), parsed_section_t *);
      }

      current_section = &sections[count++];
      memset(current_section, 0, sizeof(parsed_section_t));
      current_section->section_name = section_header_name;
      current_section->start_line = line_num;
      current_section->end_line = line_num;
      current_section->type = in_marked_section ? type_string_to_enum(current_type) : SECTION_TYPE_UNMARKED;
      current_section->has_markers = in_marked_section;

      // Append header line to content
      size_t line_strlen = strlen(line);
      if (current_content_len + line_strlen + 1 >= current_content_capacity) {
        current_content_capacity = (current_content_len + line_strlen + 1) * 2;
        current_content = SAFE_REALLOC(current_content, current_content_capacity, char *);
      }
      memcpy(current_content + current_content_len, line, line_strlen);
      current_content_len += line_strlen;
      current_content[current_content_len] = '\0';
    } else if (current_section) {
      // Append line to current section content
      size_t line_strlen = strlen(line);
      if (current_content_len + line_strlen + 1 >= current_content_capacity) {
        current_content_capacity = (current_content_len + line_strlen + 1) * 2;
        current_content = SAFE_REALLOC(current_content, current_content_capacity, char *);
      }
      memcpy(current_content + current_content_len, line, line_strlen);
      current_content_len += line_strlen;
      current_content[current_content_len] = '\0';
      current_section->end_line = line_num;
    }
  }

  // Finalize last section
  if (current_section && current_content && current_content_len > 0) {
    current_section->content = current_content;
    current_section->content_len = current_content_len;
  } else if (current_content) {
    SAFE_FREE(current_content);
  }

  if (current_marker_section) {
    SAFE_FREE(current_marker_section);
  }
  if (line) {
    free(line);
  }

  *out_sections = sections;
  *out_count = count;
  log_debug("Parsed %zu sections from file", count);
  return ASCIICHAT_OK;
}

// ============================================================================
// Public API
// ============================================================================

asciichat_error_t manpage_parser_parse_file(FILE *f, parsed_section_t **out_sections, size_t *out_count) {
  if (!f || !out_sections || !out_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for manpage_parser_parse_file");
  }

  return parse_sections_internal(f, out_sections, out_count);
}

asciichat_error_t manpage_parser_parse_memory(const char *content, size_t content_len, parsed_section_t **out_sections,
                                              size_t *out_count) {
  if (!content || content_len == 0 || !out_sections || !out_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for manpage_parser_parse_memory");
  }

  // Create temporary file from memory
  FILE *tmp = tmpfile();
  if (!tmp) {
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to create temporary file for memory parsing");
  }

  // Write content to temporary file
  // CRITICAL: Write content_len + 1 to include the null terminator!
  // The embedded string is content_len + 1 bytes (content_len chars + null terminator)
  // getline() needs the null terminator to properly handle EOF
  size_t bytes_to_write = content_len + 1;
  size_t written = fwrite(content, 1, bytes_to_write, tmp);
  if (written != bytes_to_write) {
    fclose(tmp);
    return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to write complete content to temporary file");
  }

  // Rewind to beginning for reading
  rewind(tmp);

  // Parse using common function
  asciichat_error_t err = parse_sections_internal(tmp, out_sections, out_count);
  fclose(tmp);

  return err;
}

void manpage_parser_free_sections(parsed_section_t *sections, size_t count) {
  if (!sections) {
    return;
  }

  for (size_t i = 0; i < count; i++) {
    if (sections[i].section_name) {
      SAFE_FREE(sections[i].section_name);
    }
    if (sections[i].content) {
      SAFE_FREE(sections[i].content);
    }
  }

  SAFE_FREE(sections);
}

const parsed_section_t *manpage_parser_find_section(const parsed_section_t *sections, size_t count,
                                                    const char *section_name) {
  if (!sections || !section_name) {
    return NULL;
  }

  for (size_t i = 0; i < count; i++) {
    if (sections[i].section_name && strcmp(sections[i].section_name, section_name) == 0) {
      return &sections[i];
    }
  }

  return NULL;
}
