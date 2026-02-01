/**
 * @file options.c
 * @brief OPTIONS section generator for man pages
 * @ingroup options_manpage
 */

#include "options.h"
#include "../../../log/logging.h"
#include "../../../common.h"
#include "options/manpage.h"
#include "options/builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

char *manpage_content_generate_options(const options_config_t *config) {
  if (!config || config->num_descriptors == 0) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Use a growing buffer for dynamic content
  size_t buffer_capacity = 8192;
  char *buffer = SAFE_MALLOC(buffer_capacity, char *);
  size_t offset = 0;

  // Build list of unique groups in order of first appearance
  const char **unique_groups = SAFE_MALLOC(config->num_descriptors * sizeof(const char *), const char **);
  size_t num_unique_groups = 0;

  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];

    // Skip hidden options
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
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".SS %s\n", current_group);

    // Print all options in this group
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];

      // Skip if not in current group or if hidden
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

      // Ensure buffer is large enough
      if (offset + 512 >= buffer_capacity) {
        buffer_capacity *= 2;
        buffer = SAFE_REALLOC(buffer, buffer_capacity, char *);
      }

      // Start option item
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");

      // Write option flags
      if (desc->short_name && desc->short_name != '\0') {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B \\-%c, \\-\\-%s", desc->short_name,
                                desc->long_name);
      } else {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B \\-\\-%s", desc->long_name);
      }

      // Add argument placeholder for value-taking options
      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        // Check for custom placeholder first, fall back to type-based placeholder
        const char *placeholder =
            desc->arg_placeholder ? desc->arg_placeholder : options_get_type_placeholder(desc->type);
        if (placeholder && *placeholder) {
          offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " \\fI%s\\fR", placeholder);
        }
      }

      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");

      // Write help text
      if (desc->help_text) {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s", escape_groff_special(desc->help_text));
        if (!desc->default_value) {
          offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");
        } else {
          offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " ");
        }
      } else if (desc->default_value) {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " ");
      }

      // Add default value if present
      if (desc->default_value) {
        char default_buf[256];
        int n = options_format_default_value(desc->type, desc->default_value, default_buf, sizeof(default_buf));
        if (n > 0) {
          offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "(default: ");
          if (desc->type == OPTION_TYPE_STRING) {
            offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s", escape_groff_special(default_buf));
          } else {
            offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s", default_buf);
          }
          offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ")\n");
        }
      }

      // Add mode information if applicable
      const char *mode_str = format_mode_names(desc->mode_bitmask);
      if (mode_str && strcmp(mode_str, "all modes") != 0) {
        if (strcmp(mode_str, "global") == 0) {
          offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "(mode: %s)\n", mode_str);
        } else {
          offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "(modes: %s)\n", mode_str);
        }
      }

      // Add environment variable note if present
      if (desc->env_var_name) {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "(env: \\fB%s\\fR)\n", desc->env_var_name);
      }

      // Add REQUIRED note if applicable
      if (desc->required) {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "[REQUIRED]\n");
      }
    }
  }

  SAFE_FREE(unique_groups);
  offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");

  log_debug("Generated OPTIONS section (%zu bytes)", offset);
  return buffer;
}

void manpage_content_free_options(char *content) {
  if (content) {
    SAFE_FREE(content);
  }
}
