/**
 * @file usage.c
 * @brief USAGE section generator for man pages
 * @ingroup options_manpage
 */

#include "usage.h"
#include "../../../log/logging.h"
#include "../../../common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *escape_groff_special(const char *str);

char *manpage_content_generate_usage(const options_config_t *config) {
  if (!config || config->num_usage_lines == 0) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Allocate growing buffer for usage section
  size_t buffer_capacity = 4096;
  char *buffer = SAFE_MALLOC(buffer_capacity, char *);
  size_t offset = 0;

  for (size_t i = 0; i < config->num_usage_lines; i++) {
    const usage_descriptor_t *usage = &config->usage_lines[i];

    // Ensure buffer is large enough
    if (offset + 512 >= buffer_capacity) {
      buffer_capacity *= 2;
      buffer = SAFE_REALLOC(buffer, buffer_capacity, char *);
    }

    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B ascii-chat");
    if (usage->mode) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " %s", usage->mode);
    }
    if (usage->positional) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " %s", usage->positional);
    }
    if (usage->show_options) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " [options...]");
    }
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");

    if (usage->description) {
      offset +=
          safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n", escape_groff_special(usage->description));
    }
  }

  offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");

  log_debug("Generated USAGE section (%zu bytes)", offset);
  return buffer;
}

void manpage_content_free_usage(char *content) {
  if (content) {
    SAFE_FREE(content);
  }
}
