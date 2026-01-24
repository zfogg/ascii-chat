/**
 * @file manpage.c
 * @brief Man page generation using modular architecture
 * @ingroup options
 *
 * Refactored to use modular layers:
 * - Resources: Loading embedded/filesystem templates
 * - Parser: Parsing existing man page sections
 * - Formatter: Groff/troff formatting utilities
 * - Content Generators: OPTIONS, ENVIRONMENT, USAGE, EXAMPLES, POSITIONAL
 * - Merger: Intelligently merging auto-generated with manual content
 */

#include "manpage.h"
#include "manpage/resources.h"
#include "manpage/parser.h"
#include "manpage/formatter.h"
#include "manpage/merger.h"
#include "manpage/content/options.h"
#include "manpage/content/environment.h"
#include "manpage/content/usage.h"
#include "manpage/content/examples.h"
#include "manpage/content/positional.h"
#include "builder.h"
#include "common.h"
#include "log/logging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================================
// Helper Functions (exported for content generators)
// =============================================================================

const char *escape_groff_special(const char *str) {
  return str ? str : "";
}

const char *format_mode_names(option_mode_bitmask_t mode_bitmask) {
  if (mode_bitmask == 0) {
    return NULL;
  }

  if ((mode_bitmask & OPTION_MODE_BINARY) && !(mode_bitmask & 0x1F)) {
    return "global";
  }

  option_mode_bitmask_t user_modes_mask =
      (1 << MODE_SERVER) | (1 << MODE_CLIENT) | (1 << MODE_MIRROR) | (1 << MODE_DISCOVERY_SERVER);
  if ((mode_bitmask & user_modes_mask) == user_modes_mask) {
    return "all modes";
  }

  static char mode_str[256];
  mode_str[0] = '\0';
  int pos = 0;

  if (mode_bitmask & (1 << MODE_CLIENT)) {
    pos += snprintf(mode_str + pos, sizeof(mode_str) - pos, "%sclient", pos > 0 ? ", " : "");
  }
  if (mode_bitmask & (1 << MODE_SERVER)) {
    pos += snprintf(mode_str + pos, sizeof(mode_str) - pos, "%sserver", pos > 0 ? ", " : "");
  }
  if (mode_bitmask & (1 << MODE_MIRROR)) {
    pos += snprintf(mode_str + pos, sizeof(mode_str) - pos, "%smirror", pos > 0 ? ", " : "");
  }
  if (mode_bitmask & (1 << MODE_DISCOVERY_SERVER)) {
    pos += snprintf(mode_str + pos, sizeof(mode_str) - pos, "%sdiscovery-service", pos > 0 ? ", " : "");
  }

  return pos > 0 ? mode_str : NULL;
}

// =============================================================================
// Main Public API Functions
// =============================================================================

asciichat_error_t options_config_generate_manpage_template(const options_config_t *config, const char *program_name,
                                                           const char *mode_name, const char *output_path,
                                                           const char *brief_description) {
  if (!config || !program_name || !brief_description) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing required parameters for man page generation");
  }

  FILE *f = NULL;
  bool should_close = false;

  if (output_path) {
    f = fopen(output_path, "w");
    if (!f) {
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open output file: %s", output_path);
    }
    should_close = true;
  } else {
    f = stdout;
  }

  // Write title/header
  manpage_fmt_write_title(f, program_name, mode_name, brief_description);

  // Write SYNOPSIS
  char *synopsis_content = NULL;
  size_t synopsis_len = 0;
  asciichat_error_t err = manpage_merger_generate_synopsis(mode_name, &synopsis_content, &synopsis_len);
  if (err == ASCIICHAT_OK && synopsis_content && synopsis_len > 0) {
    manpage_fmt_write_marker(f, "AUTO", "SYNOPSIS", true);
    fprintf(f, "%s", synopsis_content);
    manpage_fmt_write_marker(f, "AUTO", "SYNOPSIS", false);
    manpage_merger_free_content(synopsis_content);
  }

  // Write POSITIONAL ARGUMENTS if present
  if (config->num_positional_args > 0) {
    manpage_fmt_write_marker(f, "AUTO", "POSITIONAL ARGUMENTS", true);
    char *pos_content = manpage_content_generate_positional(config);
    if (pos_content && *pos_content != '\0') {
      fprintf(f, "%s", pos_content);
    }
    manpage_fmt_write_marker(f, "AUTO", "POSITIONAL ARGUMENTS", false);
    manpage_content_free_positional(pos_content);
  }

  // Write DESCRIPTION
  manpage_fmt_write_marker(f, "MANUAL", "DESCRIPTION", true);
  manpage_fmt_write_section(f, "DESCRIPTION");
  fprintf(f, ".B ascii-chat\nis a terminal-based video chat application that converts webcam video to ASCII\n");
  fprintf(f, "art in real-time. It enables video chat directly in your terminal, whether you're\n");
  fprintf(f, "using a local console, a remote SSH session, or any terminal emulator.\n");
  manpage_fmt_write_blank_line(f);
  manpage_fmt_write_marker(f, "MANUAL", "DESCRIPTION", false);

  // Write USAGE
  manpage_fmt_write_marker(f, "AUTO", "USAGE", true);
  char *usage_content = NULL;
  size_t usage_len = 0;
  err = manpage_merger_generate_usage(config, &usage_content, &usage_len);
  if (err == ASCIICHAT_OK && usage_content && usage_len > 0) {
    fprintf(f, ".SH USAGE\n%s", usage_content);
    manpage_merger_free_content(usage_content);
  }
  manpage_fmt_write_marker(f, "AUTO", "USAGE", false);

  // Write OPTIONS
  if (config->num_descriptors > 0) {
    manpage_fmt_write_marker(f, "AUTO", "OPTIONS", true);
    char *options_content = manpage_content_generate_options(config);
    if (options_content && *options_content != '\0') {
      fprintf(f, "%s", options_content);
    }
    manpage_content_free_options(options_content);
    manpage_fmt_write_marker(f, "AUTO", "OPTIONS", false);
  }

  // Write EXAMPLES if present
  if (config->num_examples > 0) {
    manpage_fmt_write_marker(f, "AUTO", "EXAMPLES", true);
    char *examples_content = manpage_content_generate_examples(config);
    if (examples_content && *examples_content != '\0') {
      fprintf(f, "%s", examples_content);
    }
    manpage_content_free_examples(examples_content);
    manpage_fmt_write_marker(f, "AUTO", "EXAMPLES", false);
  }

  // Write ENVIRONMENT if present
  bool has_env_vars = false;
  for (size_t i = 0; i < config->num_descriptors; i++) {
    if (config->descriptors[i].env_var_name) {
      has_env_vars = true;
      break;
    }
  }

  if (has_env_vars) {
    manpage_fmt_write_marker(f, "MERGE", "ENVIRONMENT", true);
    char *env_content = manpage_content_generate_environment(config);
    if (env_content && *env_content != '\0') {
      fprintf(f, "%s", env_content);
    }
    manpage_content_free_environment(env_content);
    manpage_fmt_write_marker(f, "MERGE", "ENVIRONMENT", false);
  }

  // Write placeholder manual sections
  manpage_fmt_write_marker(f, "MANUAL", "FILES", true);
  manpage_fmt_write_section(f, "FILES");
  fprintf(f, ".I ~/.ascii-chat/config.toml\n");
  fprintf(f, "User configuration file\n");
  manpage_fmt_write_blank_line(f);
  manpage_fmt_write_marker(f, "MANUAL", "FILES", false);

  manpage_fmt_write_marker(f, "MANUAL", "NOTES", true);
  manpage_fmt_write_section(f, "NOTES");
  fprintf(f, "For more information and examples, visit the project repository.\n");
  manpage_fmt_write_blank_line(f);
  manpage_fmt_write_marker(f, "MANUAL", "NOTES", false);

  manpage_fmt_write_marker(f, "MANUAL", "BUGS", true);
  manpage_fmt_write_section(f, "BUGS");
  fprintf(f, "Report bugs at the project issue tracker.\n");
  manpage_fmt_write_blank_line(f);
  manpage_fmt_write_marker(f, "MANUAL", "BUGS", false);

  manpage_fmt_write_marker(f, "MANUAL", "AUTHOR", true);
  manpage_fmt_write_section(f, "AUTHOR");
  fprintf(f, "Contributed by the ascii-chat community.\n");
  manpage_fmt_write_blank_line(f);
  manpage_fmt_write_marker(f, "MANUAL", "AUTHOR", false);

  manpage_fmt_write_marker(f, "MANUAL", "SEE ALSO", true);
  manpage_fmt_write_section(f, "SEE ALSO");
  fprintf(f, ".B man(1),\n");
  fprintf(f, ".B groff_man(7)\n");
  manpage_fmt_write_blank_line(f);
  manpage_fmt_write_marker(f, "MANUAL", "SEE ALSO", false);

  if (should_close) {
    fclose(f);
  }

  log_debug("Generated man page template to %s", output_path ? output_path : "stdout");
  return ASCIICHAT_OK;
}

asciichat_error_t options_builder_generate_manpage_template(options_builder_t *builder, const char *program_name,
                                                            const char *mode_name, const char *output_path,
                                                            const char *brief_description) {
  if (!builder || !program_name || !brief_description) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing required parameters");
  }

  const options_config_t *config = options_builder_build(builder);
  if (!config) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to build options configuration");
  }

  return options_config_generate_manpage_template(config, program_name, mode_name, output_path, brief_description);
}

asciichat_error_t options_config_generate_manpage_merged(const options_config_t *config, const char *program_name,
                                                         const char *mode_name, const char *output_path,
                                                         const char *brief_description) {
  (void)mode_name;         // Not used
  (void)program_name;      // Not used
  (void)brief_description; // Not used

  if (!config) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "config is required for man page generation");
  }

  FILE *f = NULL;
  bool should_close = false;

  if (output_path) {
    f = fopen(output_path, "w");
    if (!f) {
      return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open output file: %s", output_path);
    }
    should_close = true;
  } else {
    f = stdout;
  }

  // Load template resources
  manpage_resources_t resources;
  memset(&resources, 0, sizeof(resources));
  asciichat_error_t err = manpage_resources_load(&resources);
  if (err != ASCIICHAT_OK) {
    if (should_close)
      fclose(f);
    return err;
  }

  if (!manpage_resources_is_valid(&resources)) {
    if (should_close)
      fclose(f);
    manpage_resources_cleanup(&resources);
    return SET_ERRNO(ERROR_CONFIG, "Man page resources are not valid");
  }

  // Process template and merge AUTO sections with generated content
  const char *template_content = resources.template_content;
  const char *p = template_content;

  bool in_auto_section = false;
  char current_auto_section[128] = "";
  bool found_section_header = false;

  while (*p) {
    // Find next line
    const char *line_end = strchr(p, '\n');
    if (!line_end) {
      // Last line without newline
      if (!in_auto_section) {
        fputs(p, f);
      }
      break;
    }

    size_t line_len = (size_t)(line_end - p);

    // Check for AUTO-START marker (only in current line)
    bool has_auto_start = false;
    const char *temp = p;
    while (temp < line_end) {
      if (strstr(temp, "AUTO-START:") != NULL) {
        const char *found = strstr(temp, "AUTO-START:");
        if (found < line_end) {
          has_auto_start = true;
          break;
        }
        // strstr() found it but it's beyond this line, so keep looking
        temp = found + 1;
      } else {
        break;
      }
    }

    if (has_auto_start) {
      in_auto_section = true;
      found_section_header = false;

      // Extract section name (e.g., "SYNOPSIS" from "AUTO-START: SYNOPSIS")
      const char *section_start = strstr(p, "AUTO-START:");
      if (section_start && section_start < line_end) {
        section_start += strlen("AUTO-START:");
        while (*section_start && section_start < line_end && isspace(*section_start))
          section_start++;

        const char *section_name_end = section_start;
        // Extract section name: everything until end of line, then trim trailing whitespace
        while (section_name_end < line_end && *section_name_end && *section_name_end != '\n') {
          section_name_end++;
        }

        // Now trim trailing whitespace from the section name
        while (section_name_end > section_start && isspace(*(section_name_end - 1))) {
          section_name_end--;
        }

        size_t section_name_len = (size_t)(section_name_end - section_start);
        if (section_name_len > 0 && section_name_len < sizeof(current_auto_section)) {
          strncpy(current_auto_section, section_start, section_name_len);
          current_auto_section[section_name_len] = '\0';
        }
      }

      // Write the AUTO-START marker line
      fwrite(p, 1, line_len + 1, f);

    } else if (strstr(p, "AUTO-END:") != NULL && strstr(p, "AUTO-END:") < line_end) {
      in_auto_section = false;
      // Write the AUTO-END marker line
      fwrite(p, 1, line_len + 1, f);
      memset(current_auto_section, 0, sizeof(current_auto_section));
      found_section_header = false;

    } else if (in_auto_section) {
      // If this is the section header line (.SH ...), write it and generate content
      if (!found_section_header && strstr(p, ".SH ") != NULL && strstr(p, ".SH ") < line_end) {
        fwrite(p, 1, line_len + 1, f);
        found_section_header = true;

        // Generate content for this AUTO section
        if (strcmp(current_auto_section, "SYNOPSIS") == 0) {
          char *synopsis_content = NULL;
          size_t synopsis_len = 0;
          asciichat_error_t gen_err = manpage_merger_generate_synopsis(NULL, &synopsis_content, &synopsis_len);
          if (gen_err == ASCIICHAT_OK && synopsis_content && synopsis_len > 0) {
            fprintf(f, "%s", synopsis_content);
            manpage_merger_free_content(synopsis_content);
          }
        } else if (strcmp(current_auto_section, "POSITIONAL ARGUMENTS") == 0) {
          char *pos_content = manpage_content_generate_positional(config);
          if (pos_content && *pos_content != '\0') {
            fprintf(f, "%s", pos_content);
          }
          manpage_content_free_positional(pos_content);
        } else if (strcmp(current_auto_section, "USAGE") == 0) {
          char *usage_content = NULL;
          size_t usage_len = 0;
          if (manpage_merger_generate_usage(config, &usage_content, &usage_len) == ASCIICHAT_OK && usage_content &&
              usage_len > 0) {
            fprintf(f, "%s", usage_content);
            manpage_merger_free_content(usage_content);
          }
        } else if (strcmp(current_auto_section, "EXAMPLES") == 0) {
          char *examples_content = manpage_content_generate_examples(config);
          if (examples_content && *examples_content != '\0') {
            fprintf(f, "%s", examples_content);
          }
          manpage_content_free_examples(examples_content);
        } else if (strcmp(current_auto_section, "OPTIONS") == 0) {
          char *options_content = manpage_content_generate_options(config);
          if (options_content && *options_content != '\0') {
            fprintf(f, "%s", options_content);
          }
          manpage_content_free_options(options_content);
        }
      }
      // Otherwise skip old content between section header and AUTO-END
    } else {
      // Write all manual content (not in AUTO section)
      fwrite(p, 1, line_len + 1, f);
    }

    // Move to next line
    p = line_end + 1;
  }

  manpage_resources_cleanup(&resources);

  if (should_close) {
    fclose(f);
  }

  fflush(f);
  log_debug("Generated merged man page to %s", output_path ? output_path : "stdout");
  return ASCIICHAT_OK;
}

parsed_section_t *parse_manpage_sections(const char *filepath, size_t *num_sections) {
  if (!filepath || !num_sections) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return NULL;
  }

  FILE *f = fopen(filepath, "r");
  if (!f) {
    SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open file: %s", filepath);
    return NULL;
  }

  parsed_section_t *sections = NULL;
  asciichat_error_t err = manpage_parser_parse_file(f, &sections, num_sections);
  fclose(f);

  if (err != ASCIICHAT_OK) {
    return NULL;
  }

  return sections;
}

void free_parsed_sections(parsed_section_t *sections, size_t num_sections) {
  manpage_parser_free_sections(sections, num_sections);
}

const parsed_section_t *find_section(const parsed_section_t *sections, size_t num_sections, const char *section_name) {
  return manpage_parser_find_section(sections, num_sections, section_name);
}

#ifndef NDEBUG
asciichat_error_t options_config_generate_final_manpage(const char *template_path, const char *output_path,
                                                        const char *version_string, const char *content_file_path) {
  (void)template_path;
  (void)output_path;
  (void)version_string;
  (void)content_file_path;
  return SET_ERRNO(ERROR_CONFIG, "Not implemented in refactored version");
}
#endif

/** @} */
