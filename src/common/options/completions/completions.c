/**
 * @file completions.c
 * @brief Implementation of shell completion generation from options registry
 * @ingroup options
 */

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define OPTIONS_COMPLETIONS_LOWER_BUFFER_SIZE 32

#include <ascii-chat/options/enums.h>
#include <string.h>
#include <ctype.h>
#include <ascii-chat/options/completions/completions.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/config.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
#include <libflags/builder.h>
#include <libflags/completions/completions.h>

/* Forward declarations for format-specific generators */
asciichat_error_t completions_generate_bash(FILE *output);
asciichat_error_t completions_generate_fish(FILE *output);
asciichat_error_t completions_generate_zsh(FILE *output);
asciichat_error_t completions_generate_powershell(FILE *output);

/**
 * Generate the portable shell formats from the shared libflags schema.
 *
 * ascii-chat owns the registry and its mode-specific metadata; libflags owns
 * the shell syntax and escaping for the common option list.  PowerShell is
 * and libflags owns the shell syntax and escaping for the common option list.
 */
static asciichat_error_t completions_generate_with_libflags(completion_format_t format, FILE *output) {
  options_config_t *config = options_preset_unified("ascii-chat", "Terminal video chat");
  if (!config)
    return SET_ERRNO(ERROR_CONFIG, "Failed to create completion schema");

  flags_schema_builder_t *builder = options_config_build_flags_schema(OPTION_MODE_ALL);
  if (!builder) {
    options_config_destroy(config);
    return SET_ERRNO(ERROR_CONFIG, "Failed to build completion schema");
  }

  flags_completion_shell_t shell = format == COMPLETION_FORMAT_BASH ? FLAGS_COMPLETION_BASH : format == COMPLETION_FORMAT_FISH ? FLAGS_COMPLETION_FISH : format == COMPLETION_FORMAT_ZSH ? FLAGS_COMPLETION_ZSH : FLAGS_COMPLETION_POWERSHELL;
  bool written = flags_completion_write(flags_schema_builder_schema(builder), "ascii-chat", shell, output);
  flags_schema_builder_destroy(builder);
  options_config_destroy(config);
  return written ? ASCIICHAT_OK : SET_ERRNO(ERROR_FILE_OPERATION, "Failed to write completion script");
}

asciichat_error_t completions_generate_for_shell(completion_format_t format, FILE *output) {
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  switch (format) {
  case COMPLETION_FORMAT_BASH:
  case COMPLETION_FORMAT_FISH:
  case COMPLETION_FORMAT_ZSH:
  case COMPLETION_FORMAT_POWERSHELL:
    return completions_generate_with_libflags(format, output);
  default:
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unknown completion format: %d", format);
  }
}

const char *completions_get_shell_name(completion_format_t format) {
  switch (format) {
  case COMPLETION_FORMAT_BASH:
    return OPT_COMPLETION_SHELL_BASH;
  case COMPLETION_FORMAT_FISH:
    return OPT_COMPLETION_SHELL_FISH;
  case COMPLETION_FORMAT_ZSH:
    return OPT_COMPLETION_SHELL_ZSH;
  case COMPLETION_FORMAT_POWERSHELL:
    return OPT_COMPLETION_SHELL_POWERSHELL;
  default:
    return "unknown";
  }
}

completion_format_t completions_parse_shell_name(const char *shell_name) {
  if (!shell_name) {
    return COMPLETION_FORMAT_UNKNOWN;
  }

  /* Convert to lowercase for case-insensitive matching */
  char lower[OPTIONS_COMPLETIONS_LOWER_BUFFER_SIZE] = {0};
  size_t len = strlen(shell_name);
  if (len >= sizeof(lower)) {
    return COMPLETION_FORMAT_UNKNOWN;
  }

  for (size_t i = 0; i < len; i++) {
    lower[i] = tolower((unsigned char)shell_name[i]);
  }

  if (strcmp(lower, OPT_COMPLETION_SHELL_BASH) == 0) {
    return COMPLETION_FORMAT_BASH;
  } else if (strcmp(lower, OPT_COMPLETION_SHELL_FISH) == 0) {
    return COMPLETION_FORMAT_FISH;
  } else if (strcmp(lower, OPT_COMPLETION_SHELL_ZSH) == 0) {
    return COMPLETION_FORMAT_ZSH;
  } else if (strcmp(lower, OPT_COMPLETION_SHELL_POWERSHELL) == 0 || strcmp(lower, "ps") == 0) {
    return COMPLETION_FORMAT_POWERSHELL;
  }

  return COMPLETION_FORMAT_UNKNOWN;
}

/**
 * @brief Collect options from all modes with deduplication
 *
 * Iterates through all completion modes (MODE_DISCOVERY, MODE_SERVER, MODE_CLIENT,
 * MODE_MIRROR, MODE_DISCOVERY_SERVICE) and collects unique options by long_name.
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
  asciichat_mode_t modes[] = {MODE_DISCOVERY, MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVICE, MODE_MCP_SERVER};
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
          option_descriptor_t *temp = (option_descriptor_t *)SAFE_REALLOC(combined_opts, combined_count * sizeof(option_descriptor_t), option_descriptor_t *);
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
