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
#include "platform/question.h"
#include "platform/stat.h"
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

  option_mode_bitmask_t user_modes_mask = (1 << MODE_SERVER) | (1 << MODE_CLIENT) | (1 << MODE_MIRROR) |
                                          (1 << MODE_DISCOVERY_SERVICE) | (1 << MODE_DISCOVERY);
  if ((mode_bitmask & user_modes_mask) == user_modes_mask) {
    // If all user modes are set, check if binary-level is also set
    if (mode_bitmask & OPTION_MODE_BINARY) {
      return "global, client, server, mirror, discovery-service";
    }
    return "all modes";
  }

  static char mode_str[256];
  mode_str[0] = '\0';
  int pos = 0;

  // Add global if binary-level is set
  if (mode_bitmask & OPTION_MODE_BINARY) {
    pos += safe_snprintf(mode_str + pos, sizeof(mode_str) - pos, "global");
  }

  if (mode_bitmask & (1 << MODE_DISCOVERY)) {
    pos += safe_snprintf(mode_str + pos, sizeof(mode_str) - pos, "%sascii-chat", pos > 0 ? ", " : "");
  }
  if (mode_bitmask & (1 << MODE_CLIENT)) {
    pos += safe_snprintf(mode_str + pos, sizeof(mode_str) - pos, "%sclient", pos > 0 ? ", " : "");
  }
  if (mode_bitmask & (1 << MODE_SERVER)) {
    pos += safe_snprintf(mode_str + pos, sizeof(mode_str) - pos, "%sserver", pos > 0 ? ", " : "");
  }
  if (mode_bitmask & (1 << MODE_MIRROR)) {
    pos += safe_snprintf(mode_str + pos, sizeof(mode_str) - pos, "%smirror", pos > 0 ? ", " : "");
  }
  if (mode_bitmask & (1 << MODE_DISCOVERY_SERVICE)) {
    pos += safe_snprintf(mode_str + pos, sizeof(mode_str) - pos, "%sdiscovery-service", pos > 0 ? ", " : "");
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
    fprintf(f, "%s", synopsis_content);
    manpage_merger_free_content(synopsis_content);
  }

  // Write POSITIONAL ARGUMENTS if present
  if (config->num_positional_args > 0) {
    char *pos_content = manpage_content_generate_positional(config);
    if (pos_content && *pos_content != '\0') {
      fprintf(f, "%s", pos_content);
    }
    manpage_content_free_positional(pos_content);
  }

  // Write DESCRIPTION
  manpage_fmt_write_section(f, "DESCRIPTION");
  fprintf(f, ".B ascii-chat\nis a terminal-based video chat application that converts webcam video to ASCII\n");
  fprintf(f, "art in real-time. It enables video chat directly in your terminal, whether you're\n");
  fprintf(f, "using a local console, a remote SSH session, or any terminal emulator.\n");
  manpage_fmt_write_blank_line(f);

  // Write USAGE
  char *usage_content = NULL;
  size_t usage_len = 0;
  err = manpage_merger_generate_usage(config, &usage_content, &usage_len);
  if (err == ASCIICHAT_OK && usage_content && usage_len > 0) {
    fprintf(f, ".SH USAGE\n%s", usage_content);
    manpage_merger_free_content(usage_content);
  }

  // Write OPTIONS
  if (config->num_descriptors > 0) {
    char *options_content = manpage_content_generate_options(config);
    if (options_content && *options_content != '\0') {
      fprintf(f, "%s", options_content);
    }
    manpage_content_free_options(options_content);
  }

  // Write EXAMPLES if present
  if (config->num_examples > 0) {
    char *examples_content = manpage_content_generate_examples(config);
    if (examples_content && *examples_content != '\0') {
      fprintf(f, "%s", examples_content);
    }
    manpage_content_free_examples(examples_content);
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
    char *env_content = manpage_content_generate_environment(config);
    if (env_content && *env_content != '\0') {
      fprintf(f, "%s", env_content);
    }
    manpage_content_free_environment(env_content);
  }

  // Write placeholder manual sections
  manpage_fmt_write_section(f, "FILES");
  fprintf(f, ".I ~/.ascii-chat/config.toml\n");
  fprintf(f, "User configuration file\n");
  manpage_fmt_write_blank_line(f);

  manpage_fmt_write_section(f, "NOTES");
  fprintf(f, "For more information and examples, visit the project repository.\n");
  manpage_fmt_write_blank_line(f);

  manpage_fmt_write_section(f, "BUGS");
  fprintf(f, "Report bugs at the project issue tracker.\n");
  manpage_fmt_write_blank_line(f);

  manpage_fmt_write_section(f, "AUTHOR");
  fprintf(f, "Contributed by the ascii-chat community.\n");
  manpage_fmt_write_blank_line(f);

  manpage_fmt_write_section(f, "SEE ALSO");
  fprintf(f, ".B man(1),\n");
  fprintf(f, ".B groff_man(7)\n");
  manpage_fmt_write_blank_line(f);

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

  if (output_path && strlen(output_path) > 0 && strcmp(output_path, "-") != 0) {
    // Check if file already exists and prompt for confirmation
    struct stat st;
    if (stat(output_path, &st) == 0) {
      // File exists - ask user if they want to overwrite
      log_plain("Man page file already exists: %s", output_path);

      bool overwrite = platform_prompt_yes_no("Overwrite", false); // Default to No
      if (!overwrite) {
        log_plain("Man page generation cancelled.");
        return SET_ERRNO(ERROR_FILE_OPERATION, "User cancelled overwrite");
      }

      log_plain("Overwriting existing man page file...");
    }

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

  // Track MERGE sections
  bool in_merge_section = false;
  bool merge_content_generated = false;
  char current_merge_section[64] = {0};

  // For ENVIRONMENT MERGE section: collect manual variables
  const char **manual_env_vars = NULL;
  const char **manual_env_descs = NULL;
  size_t manual_env_count = 0;
  size_t manual_env_capacity = 0;

  while (*p) {
    // Find next line
    const char *line_end = strchr(p, '\n');
    if (!line_end) {
      // Last line without newline
      if (!in_auto_section && !in_merge_section) {
        fputs(p, f);
      }
      break;
    }

    size_t line_len = (size_t)(line_end - p);

    // Check for MERGE-START marker (only in current line)
    bool has_merge_start = false;
    if (strstr(p, "MERGE-START:") != NULL && strstr(p, "MERGE-START:") < line_end) {
      has_merge_start = true;
    }

    if (has_merge_start) {
      in_merge_section = true;
      merge_content_generated = false;
      manual_env_count = 0; // Reset manual variable collection
      manual_env_capacity = 0;

      // Extract section name (e.g., "ENVIRONMENT" from "MERGE-START: ENVIRONMENT")
      const char *section_start = strstr(p, "MERGE-START:");
      if (section_start && section_start < line_end) {
        section_start += strlen("MERGE-START:");
        while (*section_start && section_start < line_end && isspace(*section_start))
          section_start++;

        const char *section_name_end = section_start;
        while (section_name_end < line_end && *section_name_end && *section_name_end != '\n') {
          section_name_end++;
        }

        while (section_name_end > section_start && isspace(*(section_name_end - 1))) {
          section_name_end--;
        }

        size_t section_name_len = (size_t)(section_name_end - section_start);
        if (section_name_len > 0 && section_name_len < sizeof(current_merge_section)) {
          strncpy(current_merge_section, section_start, section_name_len);
          current_merge_section[section_name_len] = '\0';
        }
      }

      // Do NOT write the MERGE-START marker line - it's an internal control marker
      // The content will be generated when MERGE-END is encountered

      // For ENVIRONMENT MERGE sections, find and preserve the .SH header
      // (skipping over any marker comment lines that might be in between)
      if (strcmp(current_merge_section, "ENVIRONMENT") == 0) {
        const char *search_line = line_end + 1;
        bool found_header = false;
        while (*search_line && !found_header) {
          const char *search_line_end = strchr(search_line, '\n');
          if (!search_line_end)
            break;

          // Check if this is the .SH header
          if (strncmp(search_line, ".SH ", 4) == 0) {
            // Write the .SH header line
            size_t header_len = (size_t)(search_line_end - search_line);
            fwrite(search_line, 1, header_len + 1, f);
            p = search_line_end + 1;
            found_header = true;
            break;
          }

          // Skip marker comment lines (.\" MANUAL-*, .\" MERGE-*, etc)
          if (strncmp(search_line, ".\\\" ", 4) == 0) {
            search_line = search_line_end + 1;
            continue; // Keep searching
          }

          // If we hit a non-marker, non-.SH line, stop searching
          break;
        }
        if (found_header) {
          continue; // Skip the "Skip to next line" at line 359
        }
      }

      // Skip to next line
      p = line_end + 1;
      continue;

    } else if (in_merge_section && strstr(p, "MERGE-END:") != NULL && strstr(p, "MERGE-END:") < line_end) {
      // Before writing MERGE-END, generate content if not already done
      if (!merge_content_generated) {
        if (strcmp(current_merge_section, "ENVIRONMENT") == 0) {
          log_debug("[MANPAGE] Generating ENVIRONMENT with %zu manual + %zu auto variables", manual_env_count,
                    config->num_descriptors);
          char *env_content = manpage_content_generate_environment_with_manual(config, manual_env_vars,
                                                                               manual_env_count, manual_env_descs);
          if (env_content && *env_content != '\0') {
            log_debug("[MANPAGE] Writing ENVIRONMENT content: %zu bytes", strlen(env_content));
            fprintf(f, "%s", env_content);
          } else {
            log_warn("[MANPAGE] ENVIRONMENT content is empty!");
          }
          manpage_content_free_environment(env_content);
        }
        merge_content_generated = true;
      }

      in_merge_section = false;
      // Do NOT write the MERGE-END marker line - it's an internal control marker
      memset(current_merge_section, 0, sizeof(current_merge_section));

      // Free collected manual variables (strings first, then arrays)
      for (size_t i = 0; i < manual_env_count; i++) {
        if (manual_env_vars && manual_env_vars[i]) {
          char *var_name = (char *)manual_env_vars[i];
          SAFE_FREE(var_name);
        }
        if (manual_env_descs && manual_env_descs[i]) {
          char *var_desc = (char *)manual_env_descs[i];
          SAFE_FREE(var_desc);
        }
      }
      if (manual_env_vars) {
        SAFE_FREE(manual_env_vars);
        manual_env_vars = NULL;
      }
      if (manual_env_descs) {
        SAFE_FREE(manual_env_descs);
        manual_env_descs = NULL;
      }
      manual_env_count = 0;
      manual_env_capacity = 0;

      p = line_end + 1;
      continue;

    } else if (in_merge_section) {
      // Within MERGE section: collect manual environment variables for ENVIRONMENT section
      if (strcmp(current_merge_section, "ENVIRONMENT") == 0) {
        // Check if line starts with ".TP" or ".B " (groff markers)
        bool is_tp_marker = (line_len >= 3 && strncmp(p, ".TP", 3) == 0 && (line_len == 3 || isspace(p[3])));
        bool is_b_marker = (line_len >= 3 && strncmp(p, ".B ", 3) == 0);

        if (is_b_marker) {
          // Extract variable name after ".B "
          const char *var_start = p + 3;
          while (*var_start && var_start < line_end && isspace(*var_start))
            var_start++;

          const char *var_end = var_start;
          while (var_end < line_end && *var_end && *var_end != '\n') {
            var_end++;
          }

          size_t var_len = (size_t)(var_end - var_start);
          if (var_len > 0) {
            char *var_name = SAFE_MALLOC(var_len + 1, char *);
            strncpy(var_name, var_start, var_len);
            var_name[var_len] = '\0';

            // Trim trailing whitespace
            while (var_len > 0 && isspace(var_name[var_len - 1])) {
              var_name[--var_len] = '\0';
            }

            // Store the variable name
            if (manual_env_count >= manual_env_capacity) {
              manual_env_capacity = manual_env_capacity == 0 ? 16 : manual_env_capacity * 2;
              manual_env_vars =
                  SAFE_REALLOC(manual_env_vars, manual_env_capacity * sizeof(const char *), const char **);
              manual_env_descs =
                  SAFE_REALLOC(manual_env_descs, manual_env_capacity * sizeof(const char *), const char **);
            }

            manual_env_vars[manual_env_count] = var_name;
            manual_env_descs[manual_env_count] = NULL; // Will be set from next line(s)
            manual_env_count++;
          }
        } else if (manual_env_count > 0 && !is_tp_marker && !is_b_marker) {
          // This is likely a description line following a ".B var_name" line
          // Set description for the last collected variable
          const char *desc_start = p;
          while (*desc_start && desc_start < line_end && isspace(*desc_start))
            desc_start++;

          size_t desc_len = (size_t)(line_end - desc_start);
          if (desc_len > 0 && manual_env_descs[manual_env_count - 1] == NULL) {
            char *desc = SAFE_MALLOC(desc_len + 1, char *);
            strncpy(desc, desc_start, desc_len);
            desc[desc_len] = '\0';

            // Trim trailing whitespace
            while (desc_len > 0 && isspace(desc[desc_len - 1])) {
              desc[--desc_len] = '\0';
            }

            if (desc_len > 0) {
              manual_env_descs[manual_env_count - 1] = desc;
            } else {
              SAFE_FREE(desc);
            }
          }
        }
        // For ENVIRONMENT MERGE sections, do NOT write template content
        // All content will be generated at MERGE-END
        p = line_end + 1;
        continue;
      } else {
        // For other MERGE sections (if any), write template content as-is
        fwrite(p, 1, line_len + 1, f);
        p = line_end + 1;
        continue;
      }
    }

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

      // Don't write the AUTO-START marker line - it's an internal control comment

    } else if (strstr(p, "AUTO-END:") != NULL && strstr(p, "AUTO-END:") < line_end) {
      in_auto_section = false;
      // Don't write the AUTO-END marker line - it's an internal control comment
      memset(current_auto_section, 0, sizeof(current_auto_section));
      found_section_header = false;

    } else if (in_auto_section) {
      // Preserve comment lines that come before the section header
      if (!found_section_header && strstr(p, ".\\\"") != NULL && strstr(p, ".\\\"") < line_end) {
        // Skip comment lines in AUTO sections that contain "auto-generated" text
        if (strstr(p, "auto-generated") == NULL) {
          // Non-marker comment lines can be preserved
          fwrite(p, 1, line_len + 1, f);
        }
        // Skip comment lines with "auto-generated" marker text
      } else if (!found_section_header && strstr(p, ".SH ") != NULL && strstr(p, ".SH ") < line_end) {
        // If this is the section header line (.SH ...), write it and generate content
        fwrite(p, 1, line_len + 1, f);
        found_section_header = true;

        // Generate content for this AUTO section
        if (strcmp(current_auto_section, "SYNOPSIS") == 0) {
          log_debug("[MANPAGE] Generating SYNOPSIS section");
          char *synopsis_content = NULL;
          size_t synopsis_len = 0;
          asciichat_error_t gen_err = manpage_merger_generate_synopsis(NULL, &synopsis_content, &synopsis_len);
          log_debug("[MANPAGE] SYNOPSIS: err=%d, len=%zu", gen_err, synopsis_len);
          if (gen_err == ASCIICHAT_OK && synopsis_content && synopsis_len > 0) {
            fprintf(f, "%s", synopsis_content);
            manpage_merger_free_content(synopsis_content);
          }
        } else if (strcmp(current_auto_section, "POSITIONAL ARGUMENTS") == 0) {
          log_debug("[MANPAGE] Generating POSITIONAL ARGUMENTS (config has %zu args)", config->num_positional_args);
          char *pos_content = manpage_content_generate_positional(config);
          if (pos_content) {
            size_t pos_len = strlen(pos_content);
            log_debug("[MANPAGE] POSITIONAL ARGUMENTS: %zu bytes", pos_len);
            if (*pos_content != '\0') {
              fprintf(f, "%s", pos_content);
            }
          }
          manpage_content_free_positional(pos_content);
        } else if (strcmp(current_auto_section, "USAGE") == 0) {
          log_debug("[MANPAGE] Generating USAGE (config has %zu usage lines)", config->num_usage_lines);
          char *usage_content = NULL;
          size_t usage_len = 0;
          asciichat_error_t usage_err = manpage_merger_generate_usage(config, &usage_content, &usage_len);
          log_debug("[MANPAGE] USAGE: err=%d, len=%zu", usage_err, usage_len);
          if (usage_err == ASCIICHAT_OK && usage_content && usage_len > 0) {
            fprintf(f, "%s", usage_content);
            manpage_merger_free_content(usage_content);
          }
        } else if (strcmp(current_auto_section, "EXAMPLES") == 0) {
          log_debug("[MANPAGE] Generating EXAMPLES (config has %zu examples)", config->num_examples);
          char *examples_content = manpage_content_generate_examples(config);
          if (examples_content) {
            size_t ex_len = strlen(examples_content);
            log_debug("[MANPAGE] EXAMPLES: %zu bytes", ex_len);
            if (*examples_content != '\0') {
              fprintf(f, "%s", examples_content);
            }
          }
          manpage_content_free_examples(examples_content);
        } else if (strcmp(current_auto_section, "OPTIONS") == 0) {
          log_debug("[MANPAGE] Generating OPTIONS (config has %zu descriptors)", config->num_descriptors);
          char *options_content = manpage_content_generate_options(config);
          if (options_content) {
            size_t opt_len = strlen(options_content);
            log_debug("[MANPAGE] OPTIONS: %zu bytes", opt_len);
            if (*options_content != '\0') {
              fprintf(f, "%s", options_content);
            }
          }
          manpage_content_free_options(options_content);
        }
      }
      // Otherwise skip old content between section header and AUTO-END
    } else {
      // Write all manual content (not in AUTO section)
      // Skip marker comment lines: .\" AUTO-*, .\" MANUAL-*, .\" MERGE-*
      // These are internal build-time control comments
      bool is_marker_comment = (strstr(p, ".\\\" AUTO-") != NULL && strstr(p, ".\\\" AUTO-") < line_end) ||
                               (strstr(p, ".\\\" MANUAL-") != NULL && strstr(p, ".\\\" MANUAL-") < line_end) ||
                               (strstr(p, ".\\\" MERGE-") != NULL && strstr(p, ".\\\" MERGE-") < line_end);
      if (!is_marker_comment) {
        fwrite(p, 1, line_len + 1, f);
      }
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
