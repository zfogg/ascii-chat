/**
 * @file positional.c
 * @brief POSITIONAL ARGUMENTS section generator for man pages
 * @ingroup options_manpage
 */

#include <ascii-chat/options/manpage/content/positional.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/manpage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B %s\n", pos_arg->name);

    if (pos_arg->help_text) {
      offset +=
          safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n", escape_groff_special(pos_arg->help_text));
    }

    // Add examples if present
    if (pos_arg->num_examples > 0) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".RS\n");
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B Examples:\n");
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".RS\n");
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".nf\n");
      for (size_t j = 0; j < pos_arg->num_examples; j++) {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n",
                                escape_groff_special(pos_arg->examples[j]));
      }
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".fi\n");
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".RE\n");
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".RE\n");
    }
  }

  offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");

  log_debug("Generated POSITIONAL ARGUMENTS section (%zu bytes)", offset);
  return buffer;
}

void manpage_content_free_positional(char *content) {
  if (content) {
    SAFE_FREE(content);
  }
}
