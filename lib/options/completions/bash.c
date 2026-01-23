/**
 * @file bash.c
 * @brief Bash shell completion script generator
 * @ingroup options
 *
 * Generates bash completions dynamically from the options registry.
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "options/completions/bash.h"
#include "options/registry.h"
#include "options/enums.h"
#include "common.h"

/**
 * Escape shell special characters in help text
 */
static void bash_escape_help(FILE *output, const char *text)
{
  if (!text) {
    return;
  }

  for (const char *p = text; *p; p++) {
    switch (*p) {
      case '\'':
        fprintf(output, "'\\''");
        break;
      case '\n':
        fprintf(output, "\\n");
        break;
      case '\t':
        fprintf(output, "\\t");
        break;
      default:
        fputc(*p, output);
    }
  }
}

/**
 * Write a single bash option (short and long names with help text)
 */
static void bash_write_option(FILE *output, const option_descriptor_t *opt)
{
  if (!opt) {
    return;
  }

  if (opt->short_name != '\0') {
    fprintf(output, "    '-%c' '", opt->short_name);
    bash_escape_help(output, opt->help_text);
    fprintf(output, "'\n");
  }

  fprintf(output, "    '--%s' '", opt->long_name);
  bash_escape_help(output, opt->help_text);
  fprintf(output, "'\n");
}

/**
 * Write bash header
 */
static void bash_write_header(FILE *output)
{
  fprintf(output,
    "# Bash completion script for ascii-chat\n"
    "# Generated from options registry - DO NOT EDIT MANUALLY\n"
    "# Usage: eval \"$(ascii-chat --completions bash)\"\n"
    "\n"
    "_ascii_chat() {\n"
    "  local cur prev words cword\n"
    "  _init_completion || return\n"
    "\n");
}

/**
 * Write bash footer
 */
static void bash_write_footer(FILE *output)
{
  fprintf(output,
    "}\n"
    "\n"
    "complete -F _ascii_chat ascii-chat\n");
}

/**
 * Generate all option arrays from registry
 *
 * Uses options_registry_get_for_display() to ensure completions match
 * the help system's filtering logic exactly.
 */
static asciichat_error_t bash_write_all_options(FILE *output)
{
  /* Binary-level options - use MODE_DISCOVERY (the default mode) with for_binary_help=true
   * This matches how options_print_help_for_mode() filters options for '--help' display */
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts =
      options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  fprintf(output, "  # Binary-level options (same as 'ascii-chat --help')\n  local -a binary_opts=(\n");
  if (binary_opts) {
    for (size_t i = 0; i < binary_count; i++) {
      bash_write_option(output, &binary_opts[i]);
    }
    SAFE_FREE(binary_opts);
  }
  fprintf(output, "  )\n\n");

  /* Server options - mode-specific with for_binary_help=false
   * This matches how options_print_help_for_mode() filters for 'ascii-chat server --help' */
  size_t server_count = 0;
  const option_descriptor_t *server_opts =
      options_registry_get_for_display(MODE_SERVER, false, &server_count);

  fprintf(output, "  # Server-mode options (same as 'ascii-chat server --help')\n  local -a server_opts=(\n");
  if (server_opts) {
    for (size_t i = 0; i < server_count; i++) {
      bash_write_option(output, &server_opts[i]);
    }
    SAFE_FREE(server_opts);
  }
  fprintf(output, "  )\n\n");

  /* Client options - mode-specific with for_binary_help=false */
  size_t client_count = 0;
  const option_descriptor_t *client_opts =
      options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  fprintf(output, "  # Client-mode options (same as 'ascii-chat client --help')\n  local -a client_opts=(\n");
  if (client_opts) {
    for (size_t i = 0; i < client_count; i++) {
      bash_write_option(output, &client_opts[i]);
    }
    SAFE_FREE(client_opts);
  }
  fprintf(output, "  )\n\n");

  /* Mirror options - mode-specific with for_binary_help=false */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts =
      options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  fprintf(output, "  # Mirror-mode options (same as 'ascii-chat mirror --help')\n  local -a mirror_opts=(\n");
  if (mirror_opts) {
    for (size_t i = 0; i < mirror_count; i++) {
      bash_write_option(output, &mirror_opts[i]);
    }
    SAFE_FREE(mirror_opts);
  }
  fprintf(output, "  )\n\n");

  return ASCIICHAT_OK;
}

/**
 * Generate enum completion cases from registry
 */
static void bash_write_enum_cases(FILE *output)
{
  /* Enum option names to check */
  const char *enum_opts[] = {
    "log-level", "color-mode", "palette", "render-mode", "reconnect", NULL
  };

  for (size_t i = 0; enum_opts[i] != NULL; i++) {
    const char *opt_name = enum_opts[i];
    size_t enum_count = 0;
    const char **enum_values = options_get_enum_values(opt_name, &enum_count);

    if (!enum_values) {
      continue;
    }

    /* Get option descriptor for short name */
    const option_descriptor_t *opt = options_registry_find_by_name(opt_name);
    if (!opt) {
      continue;
    }

    /* Write case statement */
    if (opt->short_name != '\0') {
      fprintf(output, "  -%c | ", opt->short_name);
    }
    fprintf(output, "--%-25s)\n", opt_name);
    fprintf(output, "    COMPREPLY=($(compgen -W \"");

    /* Write enum values */
    for (size_t j = 0; j < enum_count; j++) {
      fprintf(output, "%s", enum_values[j]);
      if (j < enum_count - 1) {
        fprintf(output, " ");
      }
    }

    fprintf(output, "\" -- \"$cur\"))\n");
    fprintf(output, "    return\n");
    fprintf(output, "    ;;\n");
  }

  /* Numeric compression level (not an enum, just a range) */
  fprintf(output, "  --compression-level)\n");
  fprintf(output, "    COMPREPLY=($(compgen -W \"1 2 3 4 5 6 7 8 9\" -- \"$cur\"))\n");
  fprintf(output, "    return\n");
  fprintf(output, "    ;;\n");
}

/**
 * Write completion logic
 */
static void bash_write_completion_logic(FILE *output)
{
  fprintf(output,
    "  # Modes\n"
    "  local modes=\"server client mirror\"\n"
    "\n"
    "  # Detect which mode we're in\n"
    "  local mode=\"\"\n"
    "  local i\n"
    "  for ((i = 1; i < cword; i++)); do\n"
    "    case \"${words[i]}\" in\n"
    "    server | client | mirror)\n"
    "      mode=\"${words[i]}\"\n"
    "      break\n"
    "      ;;\n"
    "    esac\n"
    "  done\n"
    "\n"
    "  case \"$prev\" in\n"
    "  # Options that take file paths\n"
    "  -L | --log-file | -K | --key | -F | --keyfile | --client-keys | --server-key | --config | -k)\n"
    "    _filedir\n"
    "    return\n"
    "    ;;\n");

  /* Write enum cases */
  bash_write_enum_cases(output);

  fprintf(output,
    "  esac\n"
    "\n"
    "  # If current word starts with -, complete options\n"
    "  if [[ \"$cur\" == -* ]]; then\n"
    "    local -a opts_to_complete\n"
    "\n"
    "    case \"$mode\" in\n"
    "    client)\n"
    "      opts_to_complete=(\"${binary_opts[@]}\" \"${client_opts[@]}\")\n"
    "      ;;\n"
    "    server)\n"
    "      opts_to_complete=(\"${binary_opts[@]}\" \"${server_opts[@]}\")\n"
    "      ;;\n"
    "    mirror)\n"
    "      opts_to_complete=(\"${binary_opts[@]}\" \"${mirror_opts[@]}\")\n"
    "      ;;\n"
    "    *)\n"
    "      opts_to_complete=(\"${binary_opts[@]}\")\n"
    "      ;;\n"
    "    esac\n"
    "\n"
    "    # Generate completions with help text\n"
    "    local -a completions\n"
    "    for ((i = 0; i < ${#opts_to_complete[@]}; i += 2)); do\n"
    "      if [[ \"${opts_to_complete[i]}\" == \"$cur\"* ]]; then\n"
    "        completions+=(\"${opts_to_complete[i]}\")\n"
    "      fi\n"
    "    done\n"
    "\n"
    "    if [[ ${#completions[@]} -gt 0 ]]; then\n"
    "      if compopt &>/dev/null 2>&1; then\n"
    "        compopt -o nosort 2>/dev/null || true\n"
    "        COMPREPLY=()\n"
    "        for opt in \"${completions[@]}\"; do\n"
    "          for ((i = 0; i < ${#opts_to_complete[@]}; i += 2)); do\n"
    "            if [[ \"${opts_to_complete[i]}\" == \"$opt\" ]]; then\n"
    "              COMPREPLY+=(\"$opt	${opts_to_complete[i+1]}\")\n"
    "              break\n"
    "            fi\n"
    "          done\n"
    "        done\n"
    "      else\n"
    "        COMPREPLY=($(compgen -W \"${completions[*]}\" -- \"$cur\"))\n"
    "      fi\n"
    "    fi\n"
    "    return\n"
    "  fi\n"
    "\n"
    "  # If no mode specified yet, suggest modes\n"
    "  if [[ -z \"$mode\" ]]; then\n"
    "    COMPREPLY=($(compgen -W \"$modes\" -- \"$cur\"))\n"
    "    return\n"
    "  fi\n");
}

asciichat_error_t completions_generate_bash(FILE *output)
{
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  bash_write_header(output);
  asciichat_error_t err = bash_write_all_options(output);
  if (err != ASCIICHAT_OK) {
    return err;
  }
  bash_write_completion_logic(output);
  bash_write_footer(output);

  return ASCIICHAT_OK;
}
