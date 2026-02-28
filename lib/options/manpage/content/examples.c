/**
 * @file examples.c
 * @brief EXAMPLES section generator for man pages
 * @ingroup options_manpage
 */

#include <ascii-chat/options/manpage/content/examples.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/manpage.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * @brief Mode name mapping for bitmask extraction
 */
typedef struct {
  uint32_t bit;
  const char *name;
} mode_bit_name_t;

static const mode_bit_name_t mode_bit_names[] = {
    {(1 << 0), "server"},            // MODE_SERVER
    {(1 << 1), "client"},            // MODE_CLIENT
    {(1 << 2), "mirror"},            // MODE_MIRROR
    {(1 << 3), "discovery-service"}, // MODE_DISCOVERY_SERVICE
    {(1 << 4), NULL},                // MODE_DISCOVERY (no mode prefix)
    {0, NULL}                        // Terminator
};

/**
 * @brief Get all mode names from mode bitmask
 *
 * Extracts all mode bits from a bitmask and returns an array of mode names.
 * The caller must free the returned array.
 *
 * @param mode_bitmask Bitmask with one or more mode bits set
 * @param[out] count Number of mode names returned
 * @return Array of mode name strings (NULL-terminated), or NULL if no modes
 */
static const char **get_all_mode_names_from_bitmask(uint32_t mode_bitmask, size_t *count) {
  // Skip binary-only examples (OPTION_MODE_BINARY = 0x100)
  if (mode_bitmask & 0x100 && !(mode_bitmask & 0x1F)) {
    *count = 0;
    return NULL; // Binary-level example, no mode name
  }

  // Count how many mode bits are set
  size_t num_modes = 0;
  for (size_t i = 0; mode_bit_names[i].bit != 0; i++) {
    if ((mode_bitmask & mode_bit_names[i].bit) && mode_bit_names[i].name) {
      num_modes++;
    }
  }

  if (num_modes == 0) {
    *count = 0;
    return NULL;
  }

  // Allocate array for mode names (plus NULL terminator)
  const char **names = SAFE_MALLOC((num_modes + 1) * sizeof(char *), const char **);

  // Fill in mode names
  size_t idx = 0;
  for (size_t i = 0; mode_bit_names[i].bit != 0; i++) {
    if ((mode_bitmask & mode_bit_names[i].bit) && mode_bit_names[i].name) {
      names[idx++] = mode_bit_names[i].name;
    }
  }
  names[idx] = NULL;

  *count = num_modes;
  return names;
}

char *manpage_content_generate_examples(const options_config_t *config) {
  if (!config || config->num_examples == 0) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Seed random number generator for mode selection
  // Use config pointer address for deterministic-but-varied seed
  srand((unsigned int)((uintptr_t)config));

  // Allocate growing buffer for examples section
  size_t buffer_capacity = 8192;
  char *buffer = SAFE_MALLOC(buffer_capacity, char *);
  size_t offset = 0;

  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];

    // Get all mode names from bitmask (examples can apply to multiple modes)
    // For man pages, we only show each example once, randomly selecting one mode
    size_t num_modes = 0;
    const char **mode_names = get_all_mode_names_from_bitmask(example->mode_bitmask, &num_modes);

    // Ensure buffer is large enough
    if (offset + 512 >= buffer_capacity) {
      buffer_capacity *= 2;
      buffer = SAFE_REALLOC(buffer, buffer_capacity, char *);
    }

    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B ascii-chat");

    // Add mode name if applicable (randomly select one mode for multi-mode examples)
    if (num_modes > 0) {
      size_t selected_mode = (size_t)rand() % num_modes;
      if (mode_names[selected_mode]) {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " %s", mode_names[selected_mode]);
      }
    }

    if (example->args) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, " %s", example->args);
    }
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "\n");

    if (example->description) {
      offset +=
          safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n", escape_groff_special(example->description));
    }

    // Free mode names array
    if (mode_names) {
      SAFE_FREE(mode_names);
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
