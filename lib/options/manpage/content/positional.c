/**
 * @file positional.c
 * @brief POSITIONAL ARGUMENTS section generator for man pages
 * @ingroup options_manpage
 */

#include "positional.h"
#include "../../../log/logging.h"
#include "../../../common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *escape_groff_special(const char *str);

char *manpage_content_generate_positional(const options_config_t *config) {
  if (!config || config->num_positional_args == 0) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Allocate growing buffer for positional section
  size_t buffer_capacity = 4096;
  char *buffer = SAFE_MALLOC(buffer_capacity, char *);
  size_t offset = 0;

  for (size_t i = 0; i < config->num_positional_args; i++) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[i];

    // Ensure buffer is large enough
    if (offset + 512 >= buffer_capacity) {
      buffer_capacity *= 2;
      buffer = SAFE_REALLOC(buffer, buffer_capacity, char *);
    }

    offset += snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += snprintf(buffer + offset, buffer_capacity - offset, ".B %s", pos_arg->name);
    if (!pos_arg->required) {
      offset += snprintf(buffer + offset, buffer_capacity - offset, " (optional)");
    }
    offset += snprintf(buffer + offset, buffer_capacity - offset, "\n");

    if (pos_arg->help_text) {
      offset += snprintf(buffer + offset, buffer_capacity - offset, "%s\n", escape_groff_special(pos_arg->help_text));
    }

    // Add examples if present
    if (pos_arg->num_examples > 0) {
      offset += snprintf(buffer + offset, buffer_capacity - offset, ".RS\n");
      offset += snprintf(buffer + offset, buffer_capacity - offset, ".PP\n");
      offset += snprintf(buffer + offset, buffer_capacity - offset, ".B Examples:\n");
      offset += snprintf(buffer + offset, buffer_capacity - offset, ".RS\n");
      for (size_t j = 0; j < pos_arg->num_examples; j++) {
        offset +=
            snprintf(buffer + offset, buffer_capacity - offset, "%s\n", escape_groff_special(pos_arg->examples[j]));
        // Add .PP between examples to force line breaks in HTML rendering
        if (j < pos_arg->num_examples - 1) {
          offset += snprintf(buffer + offset, buffer_capacity - offset, ".PP\n");
        }
      }
      offset += snprintf(buffer + offset, buffer_capacity - offset, ".RE\n");
      offset += snprintf(buffer + offset, buffer_capacity - offset, ".RE\n");
    }
  }

  offset += snprintf(buffer + offset, buffer_capacity - offset, "\n");

  log_debug("Generated POSITIONAL ARGUMENTS section (%zu bytes)", offset);
  return buffer;
}

void manpage_content_free_positional(char *content) {
  if (content) {
    SAFE_FREE(content);
  }
}
