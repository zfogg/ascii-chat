/**
 * @file examples.c
 * @brief EXAMPLES section generator for man pages
 * @ingroup options_manpage
 */

#include "examples.h"
#include "../../../log/logging.h"
#include "../../../common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const char *escape_groff_special(const char *str);

char *manpage_content_generate_examples(const options_config_t *config) {
  if (!config || config->num_examples == 0) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Allocate growing buffer for examples section
  size_t buffer_capacity = 8192;
  char *buffer = SAFE_MALLOC(buffer_capacity, char *);
  size_t offset = 0;

  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];

    // Ensure buffer is large enough
    if (offset + 512 >= buffer_capacity) {
      buffer_capacity *= 2;
      buffer = SAFE_REALLOC(buffer, buffer_capacity, char *);
    }

    offset += snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += snprintf(buffer + offset, buffer_capacity - offset, ".B ascii-chat");
    if (example->args) {
      offset += snprintf(buffer + offset, buffer_capacity - offset, " %s", example->args);
    }
    offset += snprintf(buffer + offset, buffer_capacity - offset, "\n");

    if (example->description) {
      offset += snprintf(buffer + offset, buffer_capacity - offset, "%s\n", escape_groff_special(example->description));
    }
  }

  offset += snprintf(buffer + offset, buffer_capacity - offset, "\n");

  return buffer;
}

void manpage_content_free_examples(char *content) {
  if (content) {
    SAFE_FREE(content);
  }
}
