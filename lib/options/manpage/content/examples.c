/**
 * @file examples.c
 * @brief EXAMPLES section generator for man pages
 * @ingroup options_manpage
 */

#include "examples.h"
#include "../../../log/logging.h"
#include "../../../common.h"
#include "options/manpage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Get mode name from mode bitmask (internal helper)
 * Same logic as options_get_mode_name_from_bitmask() in builder.c
 */
static const char *get_mode_name_for_manpage(uint32_t mode_bitmask) {
  // Skip binary-only examples (OPTION_MODE_BINARY = 0x100)
  if (mode_bitmask & 0x100 && !(mode_bitmask & 0x1F)) {
    return NULL; // Binary-level example
  }

  // Skip discovery mode (renders without mode prefix)
  if (mode_bitmask == (1 << 4)) { // MODE_DISCOVERY = 4
    return NULL;
  }

  // Map individual mode bits to mode names
  if (mode_bitmask & (1 << 0)) { // MODE_SERVER = 0
    return "server";
  }
  if (mode_bitmask & (1 << 1)) { // MODE_CLIENT = 1
    return "client";
  }
  if (mode_bitmask & (1 << 2)) { // MODE_MIRROR = 2
    return "mirror";
  }
  if (mode_bitmask & (1 << 3)) { // MODE_DISCOVERY_SERVICE = 3
    return "discovery-service";
  }

  return NULL;
}

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

    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B ascii-chat");

    // Programmatically add mode name based on mode_bitmask
    const char *mode_name = get_mode_name_for_manpage(example->mode_bitmask);
    if (mode_name) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " %s", mode_name);
    }

    if (example->args) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " %s", example->args);
    }
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");

    if (example->description) {
      offset +=
          safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n", escape_groff_special(example->description));
    }
  }

  offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");

  return buffer;
}

void manpage_content_free_examples(char *content) {
  if (content) {
    SAFE_FREE(content);
  }
}
