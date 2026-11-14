/**
 * @file environment.c
 * @brief ENVIRONMENT section generator for man pages
 * @ingroup options_manpage
 */

#include <ascii-chat/options/manpage/content/environment.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <libflags/flags.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *manpage_content_generate_environment(const options_config_t *config) {
  if (!config || config->num_descriptors == 0) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Generate explicit and derived environment variables from config.
  // Manual variables are preserved from the template
  size_t buffer_capacity = 32768;
  char *buffer = SAFE_MALLOC(buffer_capacity, char *);
  size_t offset = 0;

  // Iterate through all option descriptors
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];

    char derived_name[256];
    const char *env_name = desc->env_var_name;
    if (!env_name && config->environment_prefix && desc->long_name && flags_derive_environment_name(config->environment_prefix, desc->long_name, derived_name, sizeof(derived_name)))
      env_name = derived_name;
    if (!env_name) {
      continue;
    }

    // Ensure buffer is large enough
    if (offset + 512 >= buffer_capacity) {
      buffer_capacity *= 2;
      buffer = SAFE_REALLOC(buffer, buffer_capacity, char *);
    }

    log_debug("[ENVIRONMENT] env=%s, desc=%s", env_name, desc->help_text ? desc->help_text : "NULL");

    // Generate .TP tagged paragraph for this environment variable
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".TP\n");
    offset += safe_snprintf(buffer + offset, buffer_capacity - offset, ".B %s\n", env_name);

    if (desc->help_text) {
      offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n", desc->help_text);
    }
  }

  log_debug("Generated ENVIRONMENT ASCII_CHAT_* section (%zu bytes)", offset);
  return buffer;
}

/**
 * @brief Environment variable entry for manpage generation
 * @brief Environment variable entry for manpage generation
 */
typedef struct {
  const char *name;             ///< Environment variable name
  const char *description;      ///< Description text
  const char *option_long_name; ///< Long option name (for "(see --option)" suffix), or NULL for manual
} env_var_entry_t;

// Comparison function for qsort
static int env_var_compare(const void *a, const void *b) {
  const env_var_entry_t *var_a = (const env_var_entry_t *)a;
  const env_var_entry_t *var_b = (const env_var_entry_t *)b;
  return strcmp(var_a->name, var_b->name);
}

char *manpage_content_generate_environment_with_manual(const options_config_t *config, const char **manual_vars, size_t manual_count, const char **manual_descs) {

  if (!config) {
    char *buffer = SAFE_MALLOC(1, char *);
    buffer[0] = '\0';
    return buffer;
  }

  // Count explicit and derived variables.
  size_t num_auto_vars = 0;
  for (size_t i = 0; i < config->num_descriptors; i++) {
    if (config->descriptors[i].env_var_name || (config->environment_prefix && config->descriptors[i].long_name)) {
      num_auto_vars++;
    }
  }

  // Allocate array for all variables
  size_t total_vars = num_auto_vars + manual_count;
  env_var_entry_t *all_vars = SAFE_MALLOC(total_vars * sizeof(env_var_entry_t), env_var_entry_t *);
  char **owned_names = SAFE_CALLOC(num_auto_vars ? num_auto_vars : 1, sizeof(char *), char **);
  size_t var_idx = 0;

  // Copy auto-generated variables from config
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->env_var_name || (config->environment_prefix && desc->long_name)) {
      const char *name = desc->env_var_name;
      if (!name) {
        char derived_name[256];
        if (!flags_derive_environment_name(config->environment_prefix, desc->long_name, derived_name, sizeof(derived_name)))
          continue;
        owned_names[var_idx] = SAFE_MALLOC(strlen(derived_name) + 1, char *);
        SAFE_STRNCPY(owned_names[var_idx], derived_name, strlen(derived_name) + 1);
        name = owned_names[var_idx];
      }
      all_vars[var_idx].name = name;
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
      if (all_vars[i].option_long_name) {
        // For auto-generated variables, just reference the flag
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "See: \\fB\\-\\-%s\\fR\n", all_vars[i].option_long_name);
      } else {
        // For manual variables, show full description
        offset += safe_snprintf(buffer + offset, buffer_capacity - offset, "%s\n", all_vars[i].description);
      }
    }
  }

  SAFE_FREE(all_vars);
  for (size_t i = 0; i < num_auto_vars; ++i)
    SAFE_FREE(owned_names[i]);
  SAFE_FREE(owned_names);

  log_debug("Generated ENVIRONMENT section (%zu manual + %zu auto = %zu total, %zu bytes)", manual_count, num_auto_vars, total_vars, offset);
  return buffer;
}

void manpage_content_free_environment(char *content) {
  if (content) {
    SAFE_FREE(content);
  }
}
