/**
 * @file environment.c
 * @brief ENVIRONMENT section generator for man pages
 * @ingroup options_manpage
 */

#include "environment.h"
#include "../../../log/logging.h"
#include "../../../common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *manpage_content_generate_environment(const options_config_t *config) {
  if (!config || config->num_descriptors == 0) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Generate only ASCII_CHAT_* environment variables from config
  // Manual variables are preserved from the template
  size_t buffer_capacity = 32768;
  char *buffer = SAFE_MALLOC(buffer_capacity, char *);
  size_t offset = 0;

  // Iterate through all option descriptors
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];

    // Only include descriptors that have environment variable names
    if (!desc->env_var_name) {
      continue;
    }

    // Ensure buffer is large enough
    if (offset + 512 >= buffer_capacity) {
      buffer_capacity *= 2;
      buffer = SAFE_REALLOC(buffer, buffer_capacity, char *);
    }

    log_debug("[ENVIRONMENT] env=%s, desc=%s", desc->env_var_name ? desc->env_var_name : "NULL",
              desc->help_text ? desc->help_text : "NULL");

    // Generate .TP tagged paragraph for this environment variable
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B %s\n", desc->env_var_name);

    if (desc->help_text) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n", desc->help_text);
    }
  }

  log_debug("Generated ENVIRONMENT ASCII_CHAT_* section (%zu bytes)", offset);
  return buffer;
}

// Structure to hold environment variable info for sorting
typedef struct {
  const char *name;
  const char *description;
  const char *option_long_name; // Long option name (for "(see --option)" suffix), or NULL for manual
} env_var_entry_t;

// Comparison function for qsort
static int env_var_compare(const void *a, const void *b) {
  const env_var_entry_t *var_a = (const env_var_entry_t *)a;
  const env_var_entry_t *var_b = (const env_var_entry_t *)b;
  return strcmp(var_a->name, var_b->name);
}

char *manpage_content_generate_environment_with_manual(const options_config_t *config, const char **manual_vars,
                                                       size_t manual_count, const char **manual_descs) {

  if (!config) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Count how many ASCII_CHAT_* variables we have
  size_t num_auto_vars = 0;
  for (size_t i = 0; i < config->num_descriptors; i++) {
    if (config->descriptors[i].env_var_name) {
      num_auto_vars++;
    }
  }

  // Allocate array for all variables
  size_t total_vars = num_auto_vars + manual_count;
  env_var_entry_t *all_vars = SAFE_MALLOC(total_vars * sizeof(env_var_entry_t), env_var_entry_t *);
  size_t var_idx = 0;

  // Copy auto-generated variables from config
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->env_var_name) {
      all_vars[var_idx].name = desc->env_var_name;
      all_vars[var_idx].description = desc->help_text ? desc->help_text : "";
      all_vars[var_idx].option_long_name = desc->long_name; // Store for reference
      var_idx++;
    }
  }

  // Add manual variables
  for (size_t i = 0; i < manual_count && var_idx < total_vars; i++) {
    all_vars[var_idx].name = manual_vars[i];
    all_vars[var_idx].description = (manual_descs && manual_descs[i]) ? manual_descs[i] : "";
    all_vars[var_idx].option_long_name = NULL; // Manual variables have no option reference
    var_idx++;
  }

  // Sort all variables alphabetically
  qsort(all_vars, total_vars, sizeof(env_var_entry_t), env_var_compare);

  // Generate output buffer with all environment variables
  size_t buffer_capacity = 32768;
  char *buffer = SAFE_MALLOC(buffer_capacity, char *);
  size_t offset = 0;

  // Write all environment variables in sorted order
  for (size_t i = 0; i < total_vars; i++) {
    if (offset + 512 >= buffer_capacity) {
      buffer_capacity *= 2;
      buffer = SAFE_REALLOC(buffer, buffer_capacity, char *);
    }

    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B %s\n", all_vars[i].name);
    if (*all_vars[i].description) {
      // For auto-generated variables, add "(see --option-name)" reference
      if (all_vars[i].option_long_name) {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s (see \\fB\\-\\-%s\\fR)\n",
                                all_vars[i].description, all_vars[i].option_long_name);
      } else {
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n", all_vars[i].description);
      }
    }
  }

  SAFE_FREE(all_vars);

  log_debug("Generated ENVIRONMENT section (%zu manual + %zu auto = %zu total, %zu bytes)", manual_count, num_auto_vars,
            total_vars, offset);
  return buffer;
}

void manpage_content_free_environment(char *content) {
  if (content) {
    SAFE_FREE(content);
  }
}
