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

asciichat_error_t completions_generate_for_shell(completion_format_t format, FILE *output)
{
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

const char* completions_get_shell_name(completion_format_t format)
{
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

completion_format_t completions_parse_shell_name(const char *shell_name)
{
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
