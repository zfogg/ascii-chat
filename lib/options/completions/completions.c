/**
 * @file completions.c
 * @brief Implementation of shell completion generation from options registry
 * @ingroup options
 */

#include <string.h>
#include <ctype.h>
#include "options/completions/completions.h"
#include "options/registry.h"
#include "common.h"
#include "log/logging.h"

/* Forward declarations for format-specific generators */
asciichat_error_t completions_generate_bash(FILE *output);
asciichat_error_t completions_generate_fish(FILE *output);
asciichat_error_t completions_generate_zsh(FILE *output);
asciichat_error_t completions_generate_powershell(FILE *output);

asciichat_error_t completions_generate_for_shell(completion_format_t format, FILE *output) {
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  switch (format) {
  case COMPLETION_FORMAT_BASH:
    return completions_generate_bash(output);
  case COMPLETION_FORMAT_FISH:
    return completions_generate_fish(output);
  case COMPLETION_FORMAT_ZSH:
    return completions_generate_zsh(output);
  case COMPLETION_FORMAT_POWERSHELL:
    return completions_generate_powershell(output);
  default:
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unknown completion format: %d", format);
  }
}

const char *completions_get_shell_name(completion_format_t format) {
  switch (format) {
  case COMPLETION_FORMAT_BASH:
    return "bash";
  case COMPLETION_FORMAT_FISH:
    return "fish";
  case COMPLETION_FORMAT_ZSH:
    return "zsh";
  case COMPLETION_FORMAT_POWERSHELL:
    return "powershell";
  default:
    return "unknown";
  }
}

completion_format_t completions_parse_shell_name(const char *shell_name) {
  if (!shell_name) {
    return COMPLETION_FORMAT_UNKNOWN;
  }

  /* Convert to lowercase for case-insensitive matching */
  char lower[32] = {0};
  size_t len = strlen(shell_name);
  if (len >= sizeof(lower)) {
    return COMPLETION_FORMAT_UNKNOWN;
  }

  for (size_t i = 0; i < len; i++) {
    lower[i] = tolower((unsigned char)shell_name[i]);
  }

  if (strcmp(lower, "bash") == 0) {
    return COMPLETION_FORMAT_BASH;
  } else if (strcmp(lower, "fish") == 0) {
    return COMPLETION_FORMAT_FISH;
  } else if (strcmp(lower, "zsh") == 0) {
    return COMPLETION_FORMAT_ZSH;
  } else if (strcmp(lower, "powershell") == 0 || strcmp(lower, "ps") == 0) {
    return COMPLETION_FORMAT_POWERSHELL;
  }

  return COMPLETION_FORMAT_UNKNOWN;
}

/**
 * @brief Collect options from all modes with deduplication
 *
 * Iterates through all completion modes (MODE_DISCOVERY, MODE_SERVER, MODE_CLIENT,
 * MODE_MIRROR, MODE_DISCOVERY_SERVER) and collects unique options by long_name.
 * Useful for generators that need to show completions for options across multiple modes.
 *
 * @param[out] count Pointer to receive the count of unique options
 * @return Dynamically allocated array of option_descriptor_t, must be freed by caller.
 *         Returns NULL if no options found.
 *
 * @note The caller must free the returned pointer with SAFE_FREE()
 */
option_descriptor_t *completions_collect_all_modes_unique(size_t *count) {
  if (!count) {
    return NULL;
  }

  option_descriptor_t *combined_opts = NULL;
  size_t combined_count = 0;

  /* All completion modes to iterate through */
  mode_t modes[] = {MODE_DISCOVERY, MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVER};
  const size_t modes_len = sizeof(modes) / sizeof(modes[0]);

  for (size_t m = 0; m < modes_len; m++) {
    size_t mode_count = 0;
    const option_descriptor_t *mode_opts = options_registry_get_for_mode(modes[m], &mode_count);
    if (mode_opts) {
      /* Collect unique options by long_name */
      for (size_t i = 0; i < mode_count; i++) {
        bool already_has = false;
        for (size_t j = 0; j < combined_count; j++) {
          if (strcmp(combined_opts[j].long_name, mode_opts[i].long_name) == 0) {
            already_has = true;
            break;
          }
        }
        if (!already_has) {
          combined_count++;
          option_descriptor_t *temp = (option_descriptor_t *)SAFE_REALLOC(
              combined_opts, combined_count * sizeof(option_descriptor_t), option_descriptor_t *);
          if (temp) {
            combined_opts = temp;
            combined_opts[combined_count - 1] = mode_opts[i];
          }
        }
      }
      SAFE_FREE(mode_opts);
    }
  }

  *count = combined_count;
  return combined_opts;
}
