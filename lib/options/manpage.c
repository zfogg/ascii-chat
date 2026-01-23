/**
 * @file manpage.c
 * @brief Implementation of man page template generation from options builder
 * @ingroup options
 */

#include "manpage.h"
#include "common.h"
#include "log/logging.h"
#include "util/string.h"
#include "version.h"
#include "options/options.h"    // For asciichat_mode_t and option_mode_bitmask_t
#include "embedded_resources.h" // For embedded documentation resources
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

// ============================================================================
// Forward Declarations
// ============================================================================

static parsed_section_t *parse_sections_from_file(FILE *f, size_t *num_sections);
#ifdef NDEBUG
static parsed_section_t *parse_manpage_sections_from_memory(const char *content, size_t content_len,
                                                            size_t *num_sections);
#endif

// ============================================================================
// Helper Functions for Groff/Troff Formatting
// ============================================================================

// ============================================================================
// Section Parsing and Merge Functions
// ============================================================================

/**
 * @brief Check if line is a section marker comment
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

  // Check for groff comment: .\" or .\"
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
 * @brief Escape special characters in groff/troff format
 * Characters that need escaping: backslash, period at line start, hyphen in some contexts
 */
static const char *escape_groff_special(const char *str) {
  // For simplicity, we'll just return the string as-is
  // In a more robust implementation, we'd escape special characters
  // But for man page content, most strings don't have problematic characters
  return str ? str : "";
}

/**
 * @brief Write groff header and title section
 */
static void write_header(FILE *f, const char *program_name, const char *mode_name, const char *brief_description) {
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char date_str[32];
  strftime(date_str, sizeof(date_str), "%B %Y", tm_info);

  // Build full program name (e.g., "ascii-chat-server" or just "ascii-chat")
  char full_name[256];
  if (mode_name) {
    snprintf(full_name, sizeof(full_name), "%s-%s", program_name, mode_name);
  } else {
    snprintf(full_name, sizeof(full_name), "%s", program_name);
  }

  // .TH NAME SECTION DATE SOURCE MANUAL
  // Section 1 = user commands, 5 = file formats
  fprintf(f, ".TH %s 1 \"%s\" \"%s\" \"User Commands\"\n", full_name, date_str, program_name);
  fprintf(f, ".SH NAME\n");
  fprintf(f, ".B %s\n", full_name);
  fprintf(f, "\\- %s\n", escape_groff_special(brief_description));
  fprintf(f, "\n");
}

/**
 * @brief Write SYNOPSIS section
 */
static void write_synopsis(FILE *f, const char *mode_name) {
  fprintf(f, ".SH SYNOPSIS\n");

  if (mode_name) {
    // Mode-specific synopsis
    fprintf(f, ".B ascii-chat\n");
    fprintf(f, ".I %s\n", mode_name);
    fprintf(f, "[\\fIoptions\\fR]\n");
  } else {
    // Binary-level synopsis - show main modes
    fprintf(f, ".B ascii-chat\n");
    fprintf(f, "[\\fIoptions\\fR] [\\fBserver\\fR | \\fBclient\\fR | \\fBmirror\\fR | \\fBdiscovery-service\\fR] "
               "[\\fImode-options\\fR]\n");
    fprintf(f, "\n");
    fprintf(f, ".B ascii-chat\n");
    fprintf(f, "[\\fIoptions\\fR] \\fI<session-string>\\fR\n");
  }
  fprintf(f, "\n");
}

/**
 * @brief Write USAGE section from usage descriptors
 * @param f Output file
 * @param config Options config (can be NULL to write all modes)
 */
static void write_usage_section(FILE *f, const options_config_t *config) {
  fprintf(f, ".SH USAGE\n");

  // If config is provided, use its usage lines
  if (config && config->num_usage_lines > 0) {
    for (size_t i = 0; i < config->num_usage_lines; i++) {
      const usage_descriptor_t *usage = &config->usage_lines[i];

      fprintf(f, ".TP\n");
      fprintf(f, ".B ascii-chat");
      if (usage->mode) {
        fprintf(f, " %s", usage->mode);
      }
      if (usage->positional) {
        fprintf(f, " %s", usage->positional);
      }
      if (usage->show_options) {
        fprintf(f, " [options...]");
      }
      fprintf(f, "\n");

      if (usage->description) {
        fprintf(f, "%s\n", escape_groff_special(usage->description));
      }
    }
  } else {
    // Generate complete USAGE section from all modes
    // Binary-level usage (from binary preset) - only include lines where mode is NULL
    const options_config_t *binary_config = options_preset_unified(NULL, NULL);
    if (binary_config && binary_config->num_usage_lines > 0) {
      for (size_t i = 0; i < binary_config->num_usage_lines; i++) {
        const usage_descriptor_t *usage = &binary_config->usage_lines[i];
        // Only include binary-level usage (no mode specified)
        if (usage->mode == NULL) {
          fprintf(f, ".TP\n");
          fprintf(f, ".B ascii-chat");
          if (usage->positional) {
            fprintf(f, " %s", usage->positional);
          }
          if (usage->show_options) {
            fprintf(f, " [options...]");
          }
          fprintf(f, "\n");
          if (usage->description) {
            fprintf(f, "%s\n", escape_groff_special(usage->description));
          }
        }
      }
    }
    if (binary_config) {
      options_config_destroy(binary_config);
    }

    // Mode-specific usage (server, client, mirror, discovery-service, discovery)
    const char *modes[] = {"server", "client", "mirror", "discovery-service", "discovery", NULL};
    const options_config_t *unified_config = options_preset_unified(NULL, NULL);

    for (const char **mode_ptr = modes; *mode_ptr && unified_config; mode_ptr++) {
      const char *mode = *mode_ptr;

      // Validate mode name
      if (strcmp(mode, "server") != 0 && strcmp(mode, "client") != 0 && strcmp(mode, "mirror") != 0 &&
          strcmp(mode, "discovery-service") != 0 && strcmp(mode, "discovery") != 0) {
        continue;
      }

      // Print usage lines for this mode
      if (unified_config->num_usage_lines > 0) {
        for (size_t i = 0; i < unified_config->num_usage_lines; i++) {
          const usage_descriptor_t *usage = &unified_config->usage_lines[i];

          // Filter by mode
          if (usage->mode && strcmp(usage->mode, mode) == 0) {
            fprintf(f, ".TP\n");
            fprintf(f, ".B ascii-chat");
            if (usage->mode) {
              fprintf(f, " %s", usage->mode);
            }
            if (usage->positional) {
              fprintf(f, " %s", usage->positional);
            }
            if (usage->show_options) {
              fprintf(f, " [mode-options...]");
            }
            fprintf(f, "\n");
            if (usage->description) {
              fprintf(f, "%s\n", escape_groff_special(usage->description));
            }
          }
        }
      }
    }

    if (unified_config) {
      options_config_destroy(unified_config);
    }
  }

  fprintf(f, "\n");
}

/**
 * @brief Format mode names from a mode_bitmask
 * Returns a string like "server, client" or "all modes" or "global" or NULL if no specific mode
 * Maps MODE_DISCOVERY to "binary" (the unified binary visible to users)
 * Binary-level options (OPTION_MODE_BINARY without mode bits) show as "global"
 */
static const char *format_mode_names(option_mode_bitmask_t mode_bitmask) {
  if (mode_bitmask == 0) {
    return NULL; // No mode restrictions
  }

  // Check if this is a binary-level option (parsed before mode detection)
  if ((mode_bitmask & OPTION_MODE_BINARY) && !(mode_bitmask & 0x1F)) {
    return "global"; // Binary-only option with no mode restrictions
  }

  // Check if it's all user-facing modes (server, client, mirror, discovery-service)
  // MODE_DISCOVERY is internal and not shown to users
  option_mode_bitmask_t user_modes_mask =
      (1 << MODE_SERVER) | (1 << MODE_CLIENT) | (1 << MODE_MIRROR) | (1 << MODE_DISCOVERY_SERVER);
  if ((mode_bitmask & user_modes_mask) == user_modes_mask) {
    return "all modes";
  }

  // Build a list of user-facing modes in consistent order:
  // client, server, mirror, discovery-service
  // (Note: MODE_DISCOVERY is internal and not displayed to users)
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

/**
 * @brief Write OPTIONS section from option descriptors
 */
static void write_options_section(FILE *f, const options_config_t *config) {
  if (config->num_descriptors == 0) {
    return;
  }

  fprintf(f, ".SH OPTIONS\n");

  // Build list of unique groups in order of first appearance
  const char **unique_groups = SAFE_MALLOC(config->num_descriptors * sizeof(const char *), const char **);
  size_t num_unique_groups = 0;

  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];

    // Skip hidden options (including options hidden from binary help, like --create-man-page)
    if (desc->hide_from_mode_help || desc->hide_from_binary_help || !desc->group) {
      continue;
    }

    // Check if this group is already in the list
    bool group_exists = false;
    for (size_t j = 0; j < num_unique_groups; j++) {
      if (unique_groups[j] && strcmp(unique_groups[j], desc->group) == 0) {
        group_exists = true;
        break;
      }
    }

    // Add new group to list
    if (!group_exists && num_unique_groups < config->num_descriptors) {
      unique_groups[num_unique_groups++] = desc->group;
    }
  }

  // Print options grouped by category
  for (size_t g = 0; g < num_unique_groups; g++) {
    const char *current_group = unique_groups[g];

    // Add section heading for each group
    fprintf(f, ".SS %s\n", current_group);

    // Print all options in this group
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];

      // Skip if not in current group or if hidden
      // Filter by mode_bitmask instead of hide flags
      bool is_binary_option = (desc->mode_bitmask & OPTION_MODE_BINARY) != 0;
      bool applies_to_mode = false;
      if (is_binary_option) {
        applies_to_mode = !desc->hide_from_binary_help;
      } else {
        applies_to_mode = !desc->hide_from_mode_help;
      }
      if (!applies_to_mode || !desc->group || strcmp(desc->group, current_group) != 0) {
        continue;
      }

      // Start option item
      fprintf(f, ".TP\n");

      // Write option flags
      if (desc->short_name && desc->short_name != '\0') {
        fprintf(f, ".B \\-%c", desc->short_name);
        fprintf(f, ", \\-\\-%s", desc->long_name);
      } else {
        fprintf(f, ".B \\-\\-%s", desc->long_name);
      }

      // Add argument placeholder for value-taking options
      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        const char *placeholder = options_get_type_placeholder(desc->type);
        if (placeholder && *placeholder) {
          fprintf(f, " \\fI%s\\fR", placeholder);
        }
      }

      fprintf(f, "\n");

      // Write help text
      if (desc->help_text) {
        fprintf(f, "%s", escape_groff_special(desc->help_text));
        // Only add newline if no default value follows
        if (!desc->default_value) {
          fprintf(f, "\n");
        } else {
          fprintf(f, " ");
        }
      } else if (desc->default_value) {
        // No help text but has default - add space before default
        fprintf(f, " ");
      }

      // Add default value if present
      if (desc->default_value) {
        char default_buf[256];
        int n = options_format_default_value(desc->type, desc->default_value, default_buf, sizeof(default_buf));
        if (n > 0) {
          fprintf(f, "(default: ");

          // Apply groff escaping for STRING type only
          if (desc->type == OPTION_TYPE_STRING) {
            fprintf(f, "%s", escape_groff_special(default_buf));
          } else {
            fprintf(f, "%s", default_buf);
          }

          fprintf(f, ")");
          fprintf(f, "\n");
        }
      }

      // Add mode information if applicable
      const char *mode_str = format_mode_names(desc->mode_bitmask);
      if (mode_str && strcmp(mode_str, "all modes") != 0) {
        // Use "(mode: ...)" for global, "(modes: ...)" for mode-specific
        if (strcmp(mode_str, "global") == 0) {
          fprintf(f, "(mode: %s)\n", mode_str);
        } else {
          fprintf(f, "(modes: %s)\n", mode_str);
        }
      }

      // Add environment variable note if present
      if (desc->env_var_name) {
        fprintf(f, "(env: \\fB%s\\fR)\n", desc->env_var_name);
      }

      // Add REQUIRED note if applicable
      if (desc->required) {
        fprintf(f, "[REQUIRED]\n");
      }
    }
  }

  SAFE_FREE(unique_groups);
  fprintf(f, "\n");
}

/**
 * @brief Write POSITIONAL ARGUMENTS section
 */
static void write_positional_section(FILE *f, const options_config_t *config) {
  if (config->num_positional_args == 0) {
    return;
  }

  fprintf(f, ".SH POSITIONAL ARGUMENTS\n");

  for (size_t i = 0; i < config->num_positional_args; i++) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[i];

    fprintf(f, ".TP\n");
    fprintf(f, ".B %s", pos_arg->name);
    if (!pos_arg->required) {
      fprintf(f, " (optional)");
    }
    fprintf(f, "\n");

    if (pos_arg->help_text) {
      fprintf(f, "%s\n", escape_groff_special(pos_arg->help_text));
    }

    // Add examples if present
    if (pos_arg->num_examples > 0) {
      fprintf(f, ".RS\n");
      fprintf(f, ".B Examples:\n");
      fprintf(f, ".RS\n");
      for (size_t j = 0; j < pos_arg->num_examples; j++) {
        fprintf(f, ".IP \"%s\"\n", escape_groff_special(pos_arg->examples[j]));
      }
      fprintf(f, ".RE\n");
      fprintf(f, ".RE\n");
    }
  }

  fprintf(f, "\n");
}

/**
 * @brief Write EXAMPLES section
 */
static void write_examples_section(FILE *f, const options_config_t *config) {
  if (config->num_examples == 0) {
    return;
  }

  fprintf(f, ".SH EXAMPLES\n");

  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];

    fprintf(f, ".TP\n");
    fprintf(f, ".B ascii-chat");
    if (example->mode) {
      fprintf(f, " %s", example->mode);
    }
    if (example->args) {
      fprintf(f, " %s", example->args);
    }
    fprintf(f, "\n");

    if (example->description) {
      fprintf(f, "%s\n", escape_groff_special(example->description));
    }
  }

  fprintf(f, "\n");
}

/**
 * @brief Write EXAMPLES section from all modes (for merged generation)
 */
static void write_examples_section_all_modes(FILE *f) {
  fprintf(f, ".SH EXAMPLES\n");

  // Binary-level examples (discovery mode - no mode prefix) - only show examples where mode is NULL
  const options_config_t *binary_config = options_preset_unified(NULL, NULL);
  if (binary_config && binary_config->num_examples > 0) {
    for (size_t i = 0; i < binary_config->num_examples; i++) {
      const example_descriptor_t *example = &binary_config->examples[i];
      // Only include binary-level examples (no mode specified)
      if (example->mode == NULL) {
        fprintf(f, ".TP\n");
        fprintf(f, ".B ascii-chat");
        if (example->args) {
          fprintf(f, " %s", example->args);
        }
        fprintf(f, "\n");

        if (example->description) {
          fprintf(f, "%s\n", escape_groff_special(example->description));
        }
      }
    }
  }
  if (binary_config) {
    options_config_destroy(binary_config);
  }

  // Mode-specific examples (server, client, mirror, discovery-service, discovery)
  const char *modes[] = {"server", "client", "mirror", "discovery-service", "discovery", NULL};
  const options_config_t *unified_config = options_preset_unified(NULL, NULL);

  for (const char **mode_ptr = modes; *mode_ptr && unified_config; mode_ptr++) {
    const char *mode = *mode_ptr;

    if (unified_config->num_examples > 0) {
      for (size_t i = 0; i < unified_config->num_examples; i++) {
        const example_descriptor_t *example = &unified_config->examples[i];

        // Filter by mode
        if (example->mode && strcmp(example->mode, mode) == 0) {
          fprintf(f, ".TP\n");
          fprintf(f, ".B ascii-chat");
          if (example->mode) {
            fprintf(f, " %s", example->mode);
          }
          if (example->args) {
            fprintf(f, " %s", example->args);
          }
          fprintf(f, "\n");

          if (example->description) {
            fprintf(f, "%s\n", escape_groff_special(example->description));
          }
        }
      }
    }
  }

  if (unified_config) {
    options_config_destroy(unified_config);
  }

  fprintf(f, "\n");
}

/**
 * @brief Write ENVIRONMENT VARIABLES section
 */
static void write_environment_section(FILE *f, const options_config_t *config) {
  // Collect all options with environment variables
  bool has_env_vars = false;
  for (size_t i = 0; i < config->num_descriptors; i++) {
    if (config->descriptors[i].env_var_name) {
      has_env_vars = true;
      break;
    }
  }

  if (!has_env_vars) {
    return;
  }

  fprintf(f, ".SH ENVIRONMENT VARIABLES\n");

  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];

    if (!desc->env_var_name) {
      continue;
    }

    fprintf(f, ".TP\n");
    fprintf(f, ".B %s\n", desc->env_var_name);
    fprintf(f, "Set to override ");
    fprintf(f, ".B \\-\\-%s", desc->long_name);

    if (desc->help_text) {
      fprintf(f, " (%s)", desc->help_text);
    }

    fprintf(f, "\n");
  }

  fprintf(f, "\n");
}

/**
 * @brief Write manual section placeholders
 */
static void write_manual_sections(FILE *f) {
  fprintf(f, ".SH DESCRIPTION\n");
  fprintf(f, "[MANUAL: Add detailed description of the program here]\n");
  fprintf(f, "\n");

  fprintf(f, ".SH FILES\n");
  fprintf(f, ".TP\n");
  fprintf(f, ".B $HOME/.ascii-chat/\n");
  fprintf(f, "[MANUAL: Add configuration file paths and descriptions]\n");
  fprintf(f, "\n");

  fprintf(f, ".SH NOTES\n");
  fprintf(f, "[MANUAL: Add additional notes about the program]\n");
  fprintf(f, "\n");

  fprintf(f, ".SH BUGS\n");
  fprintf(f, "[MANUAL: Report bugs at https://github.com/anthropics/ascii-chat/issues]\n");
  fprintf(f, "\n");

  fprintf(f, ".SH AUTHOR\n");
  fprintf(f, "[MANUAL: Add author information]\n");
  fprintf(f, "\n");

  fprintf(f, ".SH SEE ALSO\n");
  fprintf(f, "[MANUAL: Add related commands and tools]\n");
  fprintf(f, "\n");
}

// ============================================================================
// Section Parsing Implementation
// ============================================================================

parsed_section_t *parse_manpage_sections(const char *filepath, size_t *num_sections) {
  if (!filepath || !num_sections) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_manpage_sections");
    return NULL;
  }

  FILE *f = fopen(filepath, "r");
  if (!f) {
    SET_ERRNO(ERROR_CONFIG, "Failed to open man page template: %s", filepath);
    return NULL;
  }

  // Use common parsing function
  parsed_section_t *sections = parse_sections_from_file(f, num_sections);
  fclose(f);
  return sections;
}

void free_parsed_sections(parsed_section_t *sections, size_t num_sections) {
  if (!sections) {
    return;
  }

  for (size_t i = 0; i < num_sections; i++) {
    if (sections[i].section_name) {
      SAFE_FREE(sections[i].section_name);
    }
    if (sections[i].content) {
      SAFE_FREE(sections[i].content);
    }
  }

  SAFE_FREE(sections);
}

const parsed_section_t *find_section(const parsed_section_t *sections, size_t num_sections, const char *section_name) {
  if (!sections || !section_name) {
    return NULL;
  }

  for (size_t i = 0; i < num_sections; i++) {
    if (sections[i].section_name && strcmp(sections[i].section_name, section_name) == 0) {
      return &sections[i];
    }
  }

  return NULL;
}

/**
 * @brief Extract environment variable names from ENVIRONMENT section content
 */
static asciichat_error_t extract_env_var_names_from_content(const char *content, char ***var_names, size_t *num_vars) {
  if (!content || !var_names || !num_vars) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  *var_names = NULL;
  *num_vars = 0;

  size_t capacity = 16;
  *var_names = SAFE_MALLOC(capacity * sizeof(char *), char **);

  // Parse content line by line
  // Content should be null-terminated (we create a copy before calling this)
  const char *p = content;
  const char *content_limit = content + strlen(content);
  bool in_tp_block = false;
  char current_var[256] = {0};
  size_t var_len = 0;

  while (p < content_limit && *p) {
    // Check for .TP directive (start of env var entry)
    if (p + 3 <= content_limit && strncmp(p, ".TP", 3) == 0 &&
        (p + 3 >= content_limit || p[3] == '\n' || p[3] == '\r' || p[3] == '\0' || isspace((unsigned char)p[3]))) {
      in_tp_block = true;
      var_len = 0;
      p += 3;
      // Skip to next line
      while (p < content_limit && *p != '\n') {
        p++;
      }
      if (p < content_limit && *p == '\n') {
        p++;
      }
      continue;
    }

    // Check for .B directive (bold text, likely env var name)
    // Must be immediately after .TP (within same or next line)
    if (in_tp_block && p + 3 <= content_limit && strncmp(p, ".B ", 3) == 0) {
      p += 3;
      // Skip whitespace
      while (p < content_limit && isspace((unsigned char)*p) && *p != '\n') {
        p++;
      }
      // Extract variable name (until whitespace or newline)
      var_len = 0;
      while (p < content_limit && !isspace((unsigned char)*p) && *p != '\n' && var_len < sizeof(current_var) - 1) {
        current_var[var_len++] = *p++;
      }
      current_var[var_len] = '\0';

      if (var_len > 0) {
        // Add to array (deduplicate if already present)
        bool already_exists = false;
        for (size_t k = 0; k < *num_vars; k++) {
          if ((*var_names)[k] && strcmp((*var_names)[k], current_var) == 0) {
            already_exists = true;
            break;
          }
        }
        if (!already_exists) {
          if (*num_vars >= capacity) {
            capacity *= 2;
            *var_names = SAFE_REALLOC(*var_names, capacity * sizeof(char *), char **);
          }
          (*var_names)[*num_vars] = SAFE_MALLOC(var_len + 1, char *);
          memcpy((*var_names)[*num_vars], current_var, var_len + 1);
          (*num_vars)++;
        }
      }

      in_tp_block = false;
      // Skip to end of line
      while (p < content_limit && *p != '\n') {
        p++;
      }
      if (p < content_limit && *p == '\n') {
        p++;
      }
      continue;
    }

    // Reset in_tp_block if we hit another .TP or .SH before finding .B
    if (in_tp_block) {
      if ((p + 3 <= content_limit && strncmp(p, ".TP", 3) == 0) ||
          (p + 3 <= content_limit && strncmp(p, ".SH", 3) == 0)) {
        in_tp_block = false;
      }
    }

    // Check for next .SH (end of section)
    if (p + 3 <= content_limit && strncmp(p, ".SH", 3) == 0) {
      break;
    }

    p++;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Write section marker comments
 */
static void write_section_marker(FILE *f, const char *type, const char *section_name, bool is_start) {
  fprintf(f, ".\\\" %s-%s: %s\n", type, is_start ? "START" : "END", section_name);
  if (is_start) {
    if (strcmp(type, "AUTO") == 0) {
      fprintf(f, ".\\\" This section is auto-generated. Manual edits will be lost.\n");
    } else if (strcmp(type, "MERGE") == 0) {
      fprintf(f, ".\\\" This section merges auto-generated and manual content.\n");
    }
  }
}

/**
 * @brief Environment variable entry for sorting
 */
typedef struct {
  char *name;        ///< Variable name
  char *description; ///< Full description including formatting
  bool is_manual;    ///< True if from manual content, false if auto-generated
} env_var_entry_t;

/**
 * @brief Comparison function for qsort
 */
static int compare_env_vars(const void *a, const void *b) {
  const env_var_entry_t *ea = (const env_var_entry_t *)a;
  const env_var_entry_t *eb = (const env_var_entry_t *)b;
  return strcmp(ea->name, eb->name);
}

/**
 * @brief Write merged ENVIRONMENT section with all variables sorted alphabetically
 */
static void write_environment_section_merged(FILE *f, const options_config_t *config,
                                             const parsed_section_t *existing_section) {
  write_section_marker(f, "MERGE", "ENVIRONMENT", true);
  fprintf(f, ".SH ENVIRONMENT\n");

  // Collect all environment variables (both manual and auto-generated)
  env_var_entry_t *all_vars = NULL;
  size_t num_all_vars = 0;
  size_t capacity = 32;
  all_vars = SAFE_MALLOC(capacity * sizeof(env_var_entry_t), env_var_entry_t *);

  // Extract and collect manual environment variables from content
  if (existing_section && existing_section->content) {
    const char *content = existing_section->content;
    const char *content_limit = content + existing_section->content_len;
    const char *p = content;

    // Parse environment variable entries from manual content
    // Looking for .TP followed by .B <VAR_NAME>
    while (p < content_limit) {
      if (p + 3 <= content_limit && strncmp(p, ".TP", 3) == 0 &&
          (p + 3 >= content_limit || p[3] == '\n' || isspace((unsigned char)p[3]))) {
        // Found .TP, look for following .B with variable name
        const char *tp_start = p;
        p += 3;
        while (p < content_limit && (*p == '\n' || isspace((unsigned char)*p))) {
          p++;
        }

        // Look for .B directive
        if (p + 3 <= content_limit && strncmp(p, ".B ", 3) == 0) {
          p += 3;
          while (p < content_limit && isspace((unsigned char)*p) && *p != '\n') {
            p++;
          }

          // Extract variable name
          const char *var_start = p;
          while (p < content_limit && !isspace((unsigned char)*p) && *p != '\n') {
            p++;
          }
          const char *var_end = p;

          if (var_end > var_start) {
            size_t var_len = var_end - var_start;
            char var_name[256];
            if (var_len < sizeof(var_name)) {
              memcpy(var_name, var_start, var_len);
              var_name[var_len] = '\0';

              // Collect the full .TP block until next .TP or end
              const char *block_start = tp_start;
              const char *block_end = p;
              while (block_end < content_limit) {
                if (block_end + 3 <= content_limit && strncmp(block_end, ".TP", 3) == 0 &&
                    (block_end + 3 >= content_limit || block_end[3] == '\n' || isspace((unsigned char)block_end[3]))) {
                  break;
                }
                block_end++;
              }

              // Add to array
              if (num_all_vars >= capacity) {
                capacity *= 2;
                all_vars = SAFE_REALLOC(all_vars, capacity * sizeof(env_var_entry_t), env_var_entry_t *);
              }

              size_t block_len = block_end - block_start;
              all_vars[num_all_vars].name = SAFE_MALLOC(var_len + 1, char *);
              memcpy(all_vars[num_all_vars].name, var_name, var_len + 1);
              all_vars[num_all_vars].description = SAFE_MALLOC(block_len + 1, char *);
              memcpy(all_vars[num_all_vars].description, block_start, block_len);
              all_vars[num_all_vars].description[block_len] = '\0';
              all_vars[num_all_vars].is_manual = true;
              num_all_vars++;

              p = block_end;
              continue;
            }
          }
        }
      }
      p++;
    }
  }

  // Add auto-generated environment variables from options
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];

    if (!desc->env_var_name) {
      continue;
    }

    // Check if already in manual list
    bool already_exists = false;
    for (size_t j = 0; j < num_all_vars; j++) {
      if (strcmp(all_vars[j].name, desc->env_var_name) == 0) {
        already_exists = true;
        break;
      }
    }

    if (!already_exists) {
      if (num_all_vars >= capacity) {
        capacity *= 2;
        all_vars = SAFE_REALLOC(all_vars, capacity * sizeof(env_var_entry_t), env_var_entry_t *);
      }

      all_vars[num_all_vars].name = SAFE_MALLOC(strlen(desc->env_var_name) + 1, char *);
      strcpy(all_vars[num_all_vars].name, desc->env_var_name);

      // Generate description for auto-generated variable
      size_t desc_len = strlen(".TP\n.B ") + strlen(desc->env_var_name) + 1 + strlen("Set to override .B \\-\\--") +
                        strlen(desc->long_name) + 20;
      all_vars[num_all_vars].description = SAFE_MALLOC(desc_len, char *);
      snprintf(all_vars[num_all_vars].description, desc_len, ".TP\n.B %s\nSet to override .B \\-\\-%s",
               desc->env_var_name, desc->long_name);

      all_vars[num_all_vars].is_manual = false;
      num_all_vars++;
    }
  }

  // Sort all variables alphabetically
  qsort(all_vars, num_all_vars, sizeof(env_var_entry_t), compare_env_vars);

  // Write sorted variables
  for (size_t i = 0; i < num_all_vars; i++) {
    fprintf(f, "%s", all_vars[i].description);
    if (all_vars[i].description[strlen(all_vars[i].description) - 1] != '\n') {
      fprintf(f, "\n");
    }
  }

  // Free all variables
  for (size_t i = 0; i < num_all_vars; i++) {
    if (all_vars[i].name) {
      SAFE_FREE(all_vars[i].name);
    }
    if (all_vars[i].description) {
      SAFE_FREE(all_vars[i].description);
    }
  }
  SAFE_FREE(all_vars);

  fprintf(f, "\n");
  write_section_marker(f, "MERGE", "ENVIRONMENT", false);
}

// ============================================================================
// Public API
// ============================================================================

asciichat_error_t options_config_generate_manpage_template(const options_config_t *config, const char *program_name,
                                                           const char *mode_name, const char *output_path,
                                                           const char *brief_description) {
  if (!config || !program_name || !brief_description) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing required parameters for man page generation");
  }

  FILE *f = NULL;
  bool should_close = false;

  if (output_path) {
    // Write to file
    f = fopen(output_path, "w");
    if (!f) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to open man page template file: %s", output_path);
    }
    should_close = true;
  } else {
    // Write to stdout
    f = stdout;
    should_close = false;
  }

  // Write all sections
  write_header(f, program_name, mode_name, brief_description);
  write_synopsis(f, mode_name);
  write_usage_section(f, config);
  write_options_section(f, config);
  write_positional_section(f, config);
  write_examples_section(f, config);
  write_environment_section(f, config);
  write_manual_sections(f);

  // Footer note
  fprintf(f, ".SH TEMPLATE NOTE\n");
  fprintf(f, "This man page template was auto-generated. The sections marked [MANUAL: ...]\n");
  fprintf(f, "should be filled in with appropriate content. The OPTIONS, EXAMPLES, and other\n");
  fprintf(f, "auto-generated sections are populated from the command-line options builder and\n");
  fprintf(f, "can be regenerated if options change.\n");

  if (should_close) {
    fclose(f);
    log_info("Generated man page template: %s", output_path);
  } else {
    fflush(f);
  }

  return ASCIICHAT_OK;
}

asciichat_error_t options_builder_generate_manpage_template(options_builder_t *builder, const char *program_name,
                                                            const char *mode_name, const char *output_path,
                                                            const char *brief_description) {
  if (!builder) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Options builder is NULL");
  }

  // Build config from builder
  options_config_t *config = options_builder_build(builder);
  if (!config) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to build options config");
  }

  // Generate man page template
  asciichat_error_t result =
      options_config_generate_manpage_template(config, program_name, mode_name, output_path, brief_description);

  // Clean up
  options_config_destroy(config);

  return result;
}

// ============================================================================
// Helper: Parse manpage sections from FILE*
// ============================================================================

/**
 * @brief Parse man page sections from an open FILE*
 *
 * This is the core parsing logic extracted from parse_manpage_sections.
 * Takes an open FILE* and parses sections from it.
 *
 * @param f Open FILE* to read from
 * @param num_sections Output: number of sections parsed
 * @return Array of parsed sections (caller must free), or NULL on error
 */
static parsed_section_t *parse_sections_from_file(FILE *f, size_t *num_sections) {
  if (!f || !num_sections) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_sections_from_file");
    return NULL;
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

  *num_sections = count;
  return sections;
}

#ifdef NDEBUG
/**
 * @brief Parse man page sections from memory buffer
 *
 * Creates a temporary FILE* from memory and parses sections.
 * Uses platform-specific methods for efficiency:
 * - Unix/macOS: fmemopen() for zero-copy (if available)
 * - Windows: tmpfile() + fwrite (no fmemopen)
 *
 * @param content Memory buffer containing man page content
 * @param content_len Length of buffer (excluding null terminator)
 * @param num_sections Output: number of sections parsed
 * @return Array of parsed sections (caller must free), or NULL on error
 */
static parsed_section_t *parse_manpage_sections_from_memory(const char *content, size_t content_len,
                                                            size_t *num_sections) {
  if (!content || !num_sections) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_manpage_sections_from_memory");
    return NULL;
  }

#ifdef _WIN32
  // Windows: Use tmpfile() since fmemopen() is not available
  FILE *tmp = tmpfile();
  if (!tmp) {
    SET_ERRNO(ERROR_CONFIG, "Failed to create temporary file for memory parsing");
    return NULL;
  }

  // Write content to temporary file
  // CRITICAL: Write content_len + 1 to include the null terminator!
  // The embedded string is content_len + 1 bytes (content_len chars + null terminator)
  // getline() needs the null terminator to properly handle EOF
  size_t bytes_to_write = content_len + 1; // Include null terminator
  if (fwrite(content, 1, bytes_to_write, tmp) != bytes_to_write) {
    fclose(tmp);
    SET_ERRNO(ERROR_CONFIG, "Failed to write complete content to temporary file");
    return NULL;
  }

  // Rewind to beginning for reading
  rewind(tmp);

  // Parse using common function
  parsed_section_t *sections = parse_sections_from_file(tmp, num_sections);
  fclose(tmp);
  return sections;

#else // Unix/macOS: Use tmpfile() (same as Windows for consistency)
  // Use tmpfile instead of fmemopen for better portability and consistency
  FILE *tmp = tmpfile();
  if (!tmp) {
    SET_ERRNO(ERROR_CONFIG, "Failed to create temporary file for memory parsing");
    return NULL;
  }

  // Write content to temporary file
  // CRITICAL: Write content_len + 1 to include the null terminator!
  // The embedded string is content_len + 1 bytes (content_len chars + null terminator)
  // getline() needs the null terminator to properly handle EOF
  size_t bytes_to_write = content_len + 1; // Include null terminator
  size_t written = fwrite(content, 1, bytes_to_write, tmp);
  if (written != bytes_to_write) {
    fclose(tmp);
    SET_ERRNO(ERROR_CONFIG, "Failed to write complete content to temporary file");
    return NULL;
  }

  // Rewind to beginning for reading
  rewind(tmp);

  // Parse using common function
  parsed_section_t *sections = parse_sections_from_file(tmp, num_sections);
  fclose(tmp);
  return sections;
#endif
}
#endif // NDEBUG

asciichat_error_t options_config_generate_manpage_merged(const options_config_t *config, const char *program_name,
                                                         const char *mode_name, const char *output_path,
                                                         const char *brief_description) {
  if (!config || !program_name || !brief_description) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing required parameters for man page generation");
  }

  // Load man page resources from embedded or filesystem based on build type
  FILE *template_file = NULL;
  FILE *content_file = NULL;
  const char *template_str = NULL;
  const char *content_str = NULL;
  size_t template_len = 0;
  size_t content_len = 0;

  // Get template source (embedded in production, filesystem in development)
  if (get_manpage_template(&template_file, &template_str, &template_len) != 0) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to load man page template");
  }

  // Get content source (embedded in production, filesystem in development)
  if (get_manpage_content(&content_file, &content_str, &content_len) != 0) {
    release_manpage_resources(template_file);
    return SET_ERRNO(ERROR_CONFIG, "Failed to load man page content");
  }

  // Parse template and content sections
  parsed_section_t *existing_sections = NULL;
  size_t num_existing_sections = 0;

#ifdef NDEBUG
  // Release: Parse from embedded memory
  existing_sections = parse_manpage_sections_from_memory(template_str, template_len, &num_existing_sections);
#else
  // Debug: Parse from filesystem path
  existing_sections = parse_manpage_sections("share/man/man1/ascii-chat.1.in", &num_existing_sections);
#endif

  if (!existing_sections && num_existing_sections > 0) {
    log_warn("Failed to parse man page template, generating fresh template");
  }

  // Parse content sections
  parsed_section_t *content_sections = NULL;
  size_t num_content_sections = 0;

#ifdef NDEBUG
  // Release: Parse from embedded memory
  content_sections = parse_manpage_sections_from_memory(content_str, content_len, &num_content_sections);
#else
  // Debug: Parse from filesystem path
  content_sections = parse_manpage_sections("share/man/man1/ascii-chat.1.content", &num_content_sections);
#endif

  if (!content_sections && num_content_sections > 0) {
    log_warn("Failed to parse man page content, proceeding without it");
  }

  // Release resource file handles (no-op for embedded resources)
  release_manpage_resources(template_file);
  release_manpage_resources(content_file);

  FILE *f = NULL;
  bool should_close = false;

  if (output_path) {
    // Write to file
    f = fopen(output_path, "w");
    if (!f) {
      if (existing_sections) {
        free_parsed_sections(existing_sections, num_existing_sections);
      }
      return SET_ERRNO(ERROR_CONFIG, "Failed to open man page template file: %s", output_path);
    }
    should_close = true;
  } else {
    // Write to stdout
    f = stdout;
    should_close = false;
  }

  // Write header (always manual or unmarked, preserve if exists)
  // Check content file first, then existing template
  const parsed_section_t *name_section = NULL;
  if (content_sections) {
    name_section = find_section(content_sections, num_content_sections, "NAME");
  }
  if (!name_section && existing_sections) {
    name_section = find_section(existing_sections, num_existing_sections, "NAME");
  }

  if (name_section && name_section->type == SECTION_TYPE_MANUAL) {
    // Preserve manual NAME section
    fwrite(name_section->content, 1, name_section->content_len, f);
  } else {
    // Generate new NAME section
    write_header(f, program_name, mode_name, brief_description);
  }

  // Write SYNOPSIS (AUTO if marked, otherwise generate)
  const parsed_section_t *synopsis_section = NULL;
  if (existing_sections) {
    synopsis_section = find_section(existing_sections, num_existing_sections, "SYNOPSIS");
  }

  if (synopsis_section && synopsis_section->type == SECTION_TYPE_MANUAL) {
    write_section_marker(f, "MANUAL", "SYNOPSIS", true);
    fwrite(synopsis_section->content, 1, synopsis_section->content_len, f);
    write_section_marker(f, "MANUAL", "SYNOPSIS", false);
  } else {
    write_section_marker(f, "AUTO", "SYNOPSIS", true);
    write_synopsis(f, mode_name);
    write_section_marker(f, "AUTO", "SYNOPSIS", false);
  }

  // Write POSITIONAL ARGUMENTS (AUTO) - right after SYNOPSIS, before DESCRIPTION
  if (config->num_positional_args > 0) {
    write_section_marker(f, "AUTO", "POSITIONAL ARGUMENTS", true);
    write_positional_section(f, config);
    write_section_marker(f, "AUTO", "POSITIONAL ARGUMENTS", false);
  }

  // Write DESCRIPTION (MANUAL) - after POSITIONAL ARGUMENTS, before USAGE/OPTIONS
  // Check content file first, then existing template
  const parsed_section_t *description_section = NULL;
  if (content_sections) {
    description_section = find_section(content_sections, num_content_sections, "DESCRIPTION");
  }
  if (!description_section && existing_sections) {
    description_section = find_section(existing_sections, num_existing_sections, "DESCRIPTION");
  }
  if (description_section &&
      (description_section->type == SECTION_TYPE_MANUAL || description_section->type == SECTION_TYPE_UNMARKED)) {
    if (!description_section->has_markers) {
      write_section_marker(f, "MANUAL", "DESCRIPTION", true);
    }
    fwrite(description_section->content, 1, description_section->content_len, f);
    if (!description_section->has_markers) {
      write_section_marker(f, "MANUAL", "DESCRIPTION", false);
    }
  }

  // Write USAGE (AUTO) - generate complete usage from all modes
  write_section_marker(f, "AUTO", "USAGE", true);
  write_usage_section(f, NULL); // Pass NULL to generate all modes
  write_section_marker(f, "AUTO", "USAGE", false);

  // Write EXAMPLES (MERGE if marked, otherwise AUTO - generate all modes)
  const parsed_section_t *examples_section = NULL;
  if (existing_sections) {
    examples_section = find_section(existing_sections, num_existing_sections, "EXAMPLES");
  }

  if (examples_section && examples_section->type == SECTION_TYPE_MERGE) {
    // Merge: write manual examples, then append builder examples from all modes
    write_section_marker(f, "MERGE", "EXAMPLES", true);
    // Write manual content (but skip .SH line)
    const char *content = examples_section->content;
    const char *content_limit = content + examples_section->content_len;
    const char *p = content;
    // Skip .SH EXAMPLES line
    while (p < content_limit && strncmp(p, ".SH", 3) != 0) {
      p++;
    }
    if (p < content_limit) {
      p += 3;
      while (p < content_limit && *p != '\n') {
        p++;
      }
      if (p < content_limit && *p == '\n') {
        p++;
      }
    }
    // Write rest until next .SH
    const char *content_end = p;
    while (content_end < content_limit && strncmp(content_end, ".SH", 3) != 0) {
      content_end++;
    }
    if (content_end > p) {
      fwrite(p, 1, content_end - p, f);
    }
    // Append builder examples from all modes
    write_examples_section_all_modes(f);
    write_section_marker(f, "MERGE", "EXAMPLES", false);
  } else if (examples_section && examples_section->type == SECTION_TYPE_MANUAL) {
    write_section_marker(f, "MANUAL", "EXAMPLES", true);
    fwrite(examples_section->content, 1, examples_section->content_len, f);
    write_section_marker(f, "MANUAL", "EXAMPLES", false);
  } else {
    write_section_marker(f, "AUTO", "EXAMPLES", true);
    write_examples_section_all_modes(f);
    write_section_marker(f, "AUTO", "EXAMPLES", false);
  }

  // Write OPTIONS (AUTO)
  const parsed_section_t *options_section = NULL;
  if (existing_sections) {
    options_section = find_section(existing_sections, num_existing_sections, "OPTIONS");
  }

  if (options_section && options_section->type == SECTION_TYPE_MANUAL) {
    write_section_marker(f, "MANUAL", "OPTIONS", true);
    fwrite(options_section->content, 1, options_section->content_len, f);
    write_section_marker(f, "MANUAL", "OPTIONS", false);
  } else {
    write_section_marker(f, "AUTO", "OPTIONS", true);
    write_options_section(f, config);
    write_section_marker(f, "AUTO", "OPTIONS", false);
  }

  // Write PALETTES and RENDER MODES (MANUAL) - right after EXAMPLES, before ENVIRONMENT
  // Check content file first, then existing template
  const char *display_section_names[] = {"PALETTES", "RENDER MODES", NULL};
  for (const char **section_name = display_section_names; *section_name; section_name++) {
    const parsed_section_t *section = NULL;
    if (content_sections) {
      section = find_section(content_sections, num_content_sections, *section_name);
    }
    if (!section && existing_sections) {
      section = find_section(existing_sections, num_existing_sections, *section_name);
    }
    if (section && (section->type == SECTION_TYPE_MANUAL || section->type == SECTION_TYPE_UNMARKED)) {
      if (!section->has_markers) {
        write_section_marker(f, "MANUAL", *section_name, true);
      }
      fwrite(section->content, 1, section->content_len, f);
      if (!section->has_markers) {
        write_section_marker(f, "MANUAL", *section_name, false);
      }
    }
  }

  // Write remaining manual sections in original order: CONFIGURATION, LIMITS, then ENVIRONMENT
  // Check content file first, then existing template
  const char *config_section_names[] = {"CONFIGURATION", "LIMITS", NULL};
  for (const char **section_name = config_section_names; *section_name; section_name++) {
    const parsed_section_t *section = NULL;
    if (content_sections) {
      section = find_section(content_sections, num_content_sections, *section_name);
    }
    if (!section && existing_sections) {
      section = find_section(existing_sections, num_existing_sections, *section_name);
    }
    if (section && (section->type == SECTION_TYPE_MANUAL || section->type == SECTION_TYPE_UNMARKED)) {
      if (!section->has_markers) {
        write_section_marker(f, "MANUAL", *section_name, true);
      }
      fwrite(section->content, 1, section->content_len, f);
      if (!section->has_markers) {
        write_section_marker(f, "MANUAL", *section_name, false);
      }
    }
  }

  // Write ENVIRONMENT (MERGE) - after LIMITS, before FILES
  // Check content file first, then existing template
  const parsed_section_t *env_section = NULL;
  if (content_sections) {
    env_section = find_section(content_sections, num_content_sections, "ENVIRONMENT");
  }
  if (!env_section && existing_sections) {
    env_section = find_section(existing_sections, num_existing_sections, "ENVIRONMENT");
  }

  if (env_section && env_section->type == SECTION_TYPE_MANUAL) {
    write_section_marker(f, "MANUAL", "ENVIRONMENT", true);
    fwrite(env_section->content, 1, env_section->content_len, f);
    write_section_marker(f, "MANUAL", "ENVIRONMENT", false);
  } else {
    write_environment_section_merged(f, config, env_section);
  }

  // Write remaining manual sections: FILES, EXIT STATUS, SECURITY, NOTES, BUGS, AUTHOR, SEE ALSO
  // Check content file first, then existing template
  const char *manual_section_names[] = {"FILES", "EXIT STATUS", "SECURITY", "NOTES",
                                        "BUGS",  "AUTHOR",      "SEE ALSO", NULL};

  for (const char **section_name = manual_section_names; *section_name; section_name++) {
    const parsed_section_t *section = NULL;
    if (content_sections) {
      section = find_section(content_sections, num_content_sections, *section_name);
    }
    if (!section && existing_sections) {
      section = find_section(existing_sections, num_existing_sections, *section_name);
    }
    if (section) {
      if (section->type == SECTION_TYPE_MANUAL || section->type == SECTION_TYPE_UNMARKED) {
        // Preserve manual or unmarked sections
        if (!section->has_markers) {
          // Add markers if missing
          write_section_marker(f, "MANUAL", *section_name, true);
        }
        // Write content, but trim trailing markers that belong to next section
        const char *content = section->content;
        const char *content_limit = content + section->content_len;
        const char *write_end = content_limit;

        // Simple approach: if content ends with a START marker line, trim it
        // Look backwards from the end for ".\" MANUAL-START:", ".\" AUTO-START:", or ".\" MERGE-START:"
        const char *p = content_limit;
        // Go back to find the last newline
        while (p > content && *(p - 1) != '\n') {
          p--;
        }
        // Now p points to start of last line (or beginning of content)
        // Check if this line is a START marker
        if (p < content_limit) {
          // Skip whitespace
          const char *line_start = p;
          while (line_start < content_limit && (*line_start == ' ' || *line_start == '\t')) {
            line_start++;
          }
          // Check if it's a START marker
          if ((line_start + 16 <= content_limit && line_start[0] == '.' && line_start[1] == '\\' &&
               line_start[2] == '"' && line_start[3] == ' ' && strncmp(line_start + 4, "MANUAL-START:", 13) == 0) ||
              (line_start + 14 <= content_limit && line_start[0] == '.' && line_start[1] == '\\' &&
               line_start[2] == '"' && line_start[3] == ' ' && strncmp(line_start + 4, "AUTO-START:", 11) == 0) ||
              (line_start + 15 <= content_limit && line_start[0] == '.' && line_start[1] == '\\' &&
               line_start[2] == '"' && line_start[3] == ' ' && strncmp(line_start + 4, "MERGE-START:", 12) == 0)) {
            // This is a START marker - trim it (go back to before this line)
            write_end = p;
          }
        }

        // Also check for END markers followed by START markers (original logic)
        p = content;
        while (p < content_limit) {
          // Look for END markers (may have ".\" " prefix or be standalone)
          bool found_end = false;
          const char *end_marker_start = NULL;
          if (p + 12 <= content_limit && strncmp(p, "MANUAL-END:", 12) == 0) {
            found_end = true;
            end_marker_start = p;
          } else if (p + 16 <= content_limit && p[0] == '.' && p[1] == '\\' && p[2] == '"' && p[3] == ' ' &&
                     strncmp(p + 4, "MANUAL-END:", 12) == 0) {
            found_end = true;
            end_marker_start = p + 4; // Point to actual "MANUAL-END:" text
          }
          if (found_end) {
            // Find end of this line (including newline)
            // Start from the beginning of the line (could be ".\" " prefix)
            const char *line_start = (end_marker_start > p) ? p : end_marker_start;
            const char *line_end = line_start;
            while (line_end < content_limit && *line_end != '\n') {
              line_end++;
            }
            if (line_end < content_limit) {
              line_end++; // Include the newline
            }
            // Check if next line is a START marker (could be on same line or next line)
            const char *check_pos = line_end;
            // Skip any whitespace/newlines
            while (check_pos < content_limit &&
                   (*check_pos == ' ' || *check_pos == '\t' || *check_pos == '\n' || *check_pos == '\r')) {
              check_pos++;
            }
            // Check if it's a START marker (pattern is: .\" MANUAL-START:)
            // In file: period, backslash, double-quote, space
            if ((check_pos + 16 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "MANUAL-START:", 13) == 0) ||
                (check_pos + 14 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "AUTO-START:", 11) == 0) ||
                (check_pos + 15 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "MERGE-START:", 12) == 0)) {
              // This START marker belongs to next section - stop writing at end of END marker line
              write_end = line_end;
              break;
            }
            p = line_end;
            continue;
          }
          // Check for AUTO-END: (with or without prefix)
          found_end = false;
          end_marker_start = NULL;
          if (p + 10 <= content_limit && strncmp(p, "AUTO-END:", 10) == 0) {
            found_end = true;
            end_marker_start = p;
          } else if (p + 14 <= content_limit && p[0] == '.' && p[1] == '\\' && p[2] == '"' && p[3] == ' ' &&
                     strncmp(p + 4, "AUTO-END:", 10) == 0) {
            found_end = true;
            end_marker_start = p + 4;
          }
          if (found_end) {
            const char *line_start = (end_marker_start > p) ? p : end_marker_start;
            const char *line_end = line_start;
            while (line_end < content_limit && *line_end != '\n') {
              line_end++;
            }
            if (line_end < content_limit) {
              line_end++;
            }
            const char *check_pos = line_end;
            while (check_pos < content_limit &&
                   (*check_pos == ' ' || *check_pos == '\t' || *check_pos == '\n' || *check_pos == '\r')) {
              check_pos++;
            }
            if ((check_pos + 16 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "MANUAL-START:", 13) == 0) ||
                (check_pos + 14 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "AUTO-START:", 11) == 0) ||
                (check_pos + 15 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "MERGE-START:", 12) == 0)) {
              write_end = line_end;
              break;
            }
            p = line_end;
            continue;
          }
          // Check for MERGE-END: (with or without prefix)
          found_end = false;
          end_marker_start = NULL;
          if (p + 11 <= content_limit && strncmp(p, "MERGE-END:", 11) == 0) {
            found_end = true;
            end_marker_start = p;
          } else if (p + 15 <= content_limit && p[0] == '.' && p[1] == '\\' && p[2] == '"' && p[3] == ' ' &&
                     strncmp(p + 4, "MERGE-END:", 11) == 0) {
            found_end = true;
            end_marker_start = p + 4;
          }
          if (found_end) {
            const char *line_start = (end_marker_start > p) ? p : end_marker_start;
            const char *line_end = line_start;
            while (line_end < content_limit && *line_end != '\n') {
              line_end++;
            }
            if (line_end < content_limit) {
              line_end++;
            }
            const char *check_pos = line_end;
            while (check_pos < content_limit &&
                   (*check_pos == ' ' || *check_pos == '\t' || *check_pos == '\n' || *check_pos == '\r')) {
              check_pos++;
            }
            if ((check_pos + 16 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "MANUAL-START:", 13) == 0) ||
                (check_pos + 14 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "AUTO-START:", 11) == 0) ||
                (check_pos + 15 <= content_limit && check_pos[0] == '.' && check_pos[1] == '\\' &&
                 check_pos[2] == '"' && check_pos[3] == ' ' && strncmp(check_pos + 4, "MERGE-START:", 12) == 0)) {
              write_end = line_end;
              break;
            }
            p = line_end;
            continue;
          }
          p++;
        }

        // Write content up to write_end
        if (write_end > content) {
          fwrite(content, 1, write_end - content, f);
        }
        if (!section->has_markers) {
          write_section_marker(f, "MANUAL", *section_name, false);
        }
      } else if (section->type == SECTION_TYPE_AUTO) {
        // Skip AUTO sections (already generated above)
      }
    }
  }

  // If no existing template, write placeholder manual sections
  if (!existing_sections) {
    write_manual_sections(f);
  }

  // Add any sections from content file that weren't already written
  // Sections that are already written: NAME, SYNOPSIS, DESCRIPTION, USAGE, EXAMPLES, OPTIONS,
  // PALETTES, RENDER MODES, CONFIGURATION, LIMITS, ENVIRONMENT, FILES, EXIT STATUS, SECURITY, NOTES, BUGS, AUTHOR, SEE
  // ALSO
  if (content_sections) {
    const char *written_section_names[] = {
        "NAME",         "SYNOPSIS",      "DESCRIPTION", "USAGE",       "EXAMPLES", "OPTIONS",     "PALETTES",
        "RENDER MODES", "CONFIGURATION", "LIMITS",      "ENVIRONMENT", "FILES",    "EXIT STATUS", "SECURITY",
        "NOTES",        "BUGS",          "AUTHOR",      "SEE ALSO",    NULL};

    for (size_t i = 0; i < num_content_sections; i++) {
      const parsed_section_t *content_section = &content_sections[i];
      // Only add if content exists and section name exists
      if (content_section->content && content_section->section_name) {
        // Check if this section was already written
        bool already_written = false;
        for (const char **written_name = written_section_names; *written_name; written_name++) {
          if (strcmp(content_section->section_name, *written_name) == 0) {
            already_written = true;
            break;
          }
        }
        if (!already_written) {
          // New section from content file - append it
          fprintf(f, "\n");
          if (!content_section->has_markers) {
            write_section_marker(f, "MANUAL", content_section->section_name, true);
          }
          fwrite(content_section->content, 1, content_section->content_len, f);
          if (!content_section->has_markers) {
            write_section_marker(f, "MANUAL", content_section->section_name, false);
          }
        }
      }
    }
  }

  if (should_close) {
    fclose(f);
  } else {
    fflush(f);
  }

  if (existing_sections) {
    free_parsed_sections(existing_sections, num_existing_sections);
  }
  if (content_sections) {
    free_parsed_sections(content_sections, num_content_sections);
  }

  if (output_path) {
    log_info("Generated merged man page template: %s", output_path);
  }
  return ASCIICHAT_OK;
}

#ifndef NDEBUG
/**
 * @brief Generate final man page (.1) from template (.1.in) with version substitution and optional content file
 */
asciichat_error_t options_config_generate_final_manpage(const char *template_path, const char *output_path,
                                                        const char *version_string, const char *content_file_path) {
  if (!template_path || !output_path || !version_string) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing required parameters for final man page generation");
  }

  // Parse content file if provided
  parsed_section_t *content_sections = NULL;
  size_t num_content_sections = 0;
  if (content_file_path) {
    content_sections = parse_manpage_sections(content_file_path, &num_content_sections);
    if (!content_sections && num_content_sections > 0) {
      log_warn("Failed to parse content file, proceeding without it");
    }
  }

  FILE *template_file = fopen(template_path, "r");
  if (!template_file) {
    if (content_sections) {
      free_parsed_sections(content_sections, num_content_sections);
    }
    return SET_ERRNO(ERROR_CONFIG, "Failed to open template file: %s", template_path);
  }

  FILE *output_file = fopen(output_path, "w");
  if (!output_file) {
    fclose(template_file);
    if (content_sections) {
      free_parsed_sections(content_sections, num_content_sections);
    }
    return SET_ERRNO(ERROR_CONFIG, "Failed to open output file: %s", output_path);
  }

  // Parse template to understand its structure (for merging content file sections)
  size_t num_template_sections = 0;
  parsed_section_t *template_sections = parse_manpage_sections(template_path, &num_template_sections);

  // Read template line by line, substitute version, and merge content from file if provided
  char *line = NULL;
  size_t line_capacity = 0;
  ssize_t line_len;
  const char *current_section_name = NULL;
  bool wrote_content_for_current_section = false;

  while ((line_len = getline(&line, &line_capacity, template_file)) != -1) {
    // Detect section headers
    if (strncmp(line, ".SH ", 4) == 0) {
      // Extract section name
      char section_name[256] = {0};
      const char *section_start = line + 4;
      size_t name_len = 0;
      while (section_start[name_len] && section_start[name_len] != '\n' && name_len < sizeof(section_name) - 1) {
        section_name[name_len] = section_start[name_len];
        name_len++;
      }
      section_name[name_len] = '\0';
      current_section_name = NULL;
      wrote_content_for_current_section = false;

      // Check if we have content for this section from the file
      if (content_sections) {
        const parsed_section_t *content_section = find_section(content_sections, num_content_sections, section_name);
        if (content_section) {
          current_section_name = section_name;
          // Check if this is a MERGE section in template
          if (template_sections) {
            const parsed_section_t *template_section =
                find_section(template_sections, num_template_sections, section_name);
            if (template_section && template_section->type == SECTION_TYPE_MERGE) {
            }
          }
        }
      }
    }

    // Check for MERGE section markers
    if (strstr(line, "MERGE-START:") != NULL) {
      // MERGE-START detected (for reference, no action needed)
    } else if (strstr(line, "MERGE-END:") != NULL) {
      // Before MERGE-END, insert content from file if we have it
      if (current_section_name && content_sections && !wrote_content_for_current_section) {
        const parsed_section_t *content_section =
            find_section(content_sections, num_content_sections, current_section_name);
        if (content_section && content_section->content) {
          // Write content (skip .SH header and markers)
          const char *content = content_section->content;
          const char *content_limit = content + content_section->content_len;
          const char *p = content;

          // Skip .SH line if present
          if (strncmp(p, ".SH", 3) == 0) {
            while (p < content_limit && *p != '\n') {
              p++;
            }
            if (p < content_limit && *p == '\n') {
              p++;
            }
          }
          // Skip marker comments
          while (p < content_limit) {
            if ((p + 3 <= content_limit && strncmp(p, ".\\\"", 3) == 0) ||
                (p + 2 <= content_limit && strncmp(p, ".\"", 2) == 0)) {
              while (p < content_limit && *p != '\n') {
                p++;
              }
              if (p < content_limit && *p == '\n') {
                p++;
              }
              continue;
            }
            break;
          }
          // Write remaining content
          if (p < content_limit) {
            fwrite(p, 1, content_limit - p, output_file);
          }
          wrote_content_for_current_section = true;
        }
      }
    }

    // Substitute @PROJECT_VERSION@ with version_string
    const char *version_marker = "@PROJECT_VERSION@";
    const char *pos = line;
    const char *marker_pos;

    while ((marker_pos = strstr(pos, version_marker)) != NULL) {
      // Write everything before the marker
      size_t before_len = marker_pos - pos;
      if (before_len > 0) {
        fwrite(pos, 1, before_len, output_file);
      }
      // Write version string
      fwrite(version_string, 1, strlen(version_string), output_file);
      // Move past the marker
      pos = marker_pos + strlen(version_marker);
    }
    // Write the rest of the line
    if (*pos) {
      fwrite(pos, 1, strlen(pos), output_file);
    }
  }

  // Add any sections from content file that weren't in template
  if (content_sections) {
    for (size_t i = 0; i < num_content_sections; i++) {
      const parsed_section_t *content_section = &content_sections[i];
      const parsed_section_t *template_section =
          find_section(template_sections, num_template_sections, content_section->section_name);
      if (!template_section && content_section->content) {
        // New section - append it
        fprintf(output_file, "\n");
        fwrite(content_section->content, 1, content_section->content_len, output_file);
      }
    }
  }

  free(line);
  fclose(template_file);
  fclose(output_file);

  if (template_sections) {
    free_parsed_sections(template_sections, num_template_sections);
  }
  if (content_sections) {
    free_parsed_sections(content_sections, num_content_sections);
  }

  log_info("Generated final man page: %s (from template: %s%s)", output_path, template_path,
           content_file_path ? ", content file merged" : "");
  return ASCIICHAT_OK;
}
#endif
