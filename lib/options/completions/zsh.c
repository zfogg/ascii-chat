/**
 * @file zsh.c
 * @brief Zsh shell completion script generator
 * @ingroup options
 */

#include <string.h>
#include <stdio.h>
#include <ascii-chat/options/completions/zsh.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/common.h>

/**
 * Escape help text for zsh's _arguments command
 * Zsh interprets square brackets as special syntax, so they need to be escaped
 */
static void zsh_escape_help(FILE *output, const char *text) {
  if (!text) {
    return;
  }

  for (const char *p = text; *p; p++) {
    switch (*p) {
    case '[':
    case ']':
      // Escape square brackets for zsh _arguments
      fprintf(output, "\\%c", *p);
      break;
    case '\'':
      // Escape single quotes
      fprintf(output, "'\\''");
      break;
    case '\n':
      // Convert newlines to spaces in help text
      fprintf(output, " ");
      break;
    case '\t':
      // Convert tabs to spaces in help text
      fprintf(output, " ");
      break;
    default:
      fputc(*p, output);
    }
  }
}

static void zsh_write_option(FILE *output, const option_descriptor_t *opt) {
  if (!opt) {
    return;
  }

  // Get completion metadata for this option
  const option_metadata_t *meta = options_registry_get_metadata(opt->long_name);

  // Build completion spec based on metadata
  char completion_spec[512] = "";
  if (meta) {
    if (meta->input_type == OPTION_INPUT_ENUM && meta->enum_values && meta->enum_values[0] != NULL) {
      // Enum completion: "(value1 value2 value3)"
      strcpy(completion_spec, ":(");
      for (size_t i = 0; meta->enum_values[i] != NULL; i++) {
        if (i > 0)
          strcat(completion_spec, " ");
        strcat(completion_spec, meta->enum_values[i]);
      }
      strcat(completion_spec, ")");
    } else if (meta->input_type == OPTION_INPUT_FILEPATH) {
      // File path completion
      strcpy(completion_spec, ":_files");
    } else if (meta->examples && meta->examples[0] != NULL) {
      // Examples: "(example1 example2)" - practical values, higher priority than calculated ranges
      strcpy(completion_spec, ":(");
      for (size_t i = 0; meta->examples[i] != NULL; i++) {
        if (i > 0)
          strcat(completion_spec, " ");
        strcat(completion_spec, meta->examples[i]);
      }
      strcat(completion_spec, ")");
    } else if (meta->input_type == OPTION_INPUT_NUMERIC) {
      // Numeric completion with range - only if no examples
      if (meta->numeric_range.min > 0 || meta->numeric_range.max > 0) {
        safe_snprintf(completion_spec, sizeof(completion_spec), ":(numeric %d-%d)", meta->numeric_range.min,
                      meta->numeric_range.max);
      } else {
        strcpy(completion_spec, ":(numeric)");
      }
    }
  }

  // Write short option if present
  if (opt->short_name != '\0') {
    fprintf(output, "    '-%c[", opt->short_name);
    zsh_escape_help(output, opt->help_text);
    fprintf(output, "]%s' \\\n", completion_spec);
  }

  // Write long option
  fprintf(output, "    '--%s[", opt->long_name);
  zsh_escape_help(output, opt->help_text);
  fprintf(output, "]%s' \\\n", completion_spec);
}

asciichat_error_t completions_generate_zsh(FILE *output) {
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  fprintf(output, "#compdef _ascii_chat ascii-chat\n"
#ifndef NDEBUG
                  "#compdef _ascii_chat build/bin/ascii-chat ./build/bin/ascii-chat\n"
#endif
                  "# Zsh completion script for ascii-chat\n"
                  "# Generated from options registry - DO NOT EDIT MANUALLY\n"
                  "\n"
                  "_ascii_chat() {\n"
                  "  local curcontext=\"$curcontext\" state line\n"
                  "  _arguments -C \\\n");

  /* Binary options - use unified display API matching help system */
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  if (binary_opts) {
    for (size_t i = 0; i < binary_count; i++) {
      zsh_write_option(output, &binary_opts[i]);
    }
    SAFE_FREE(binary_opts);
  }

  fprintf(output, "    '1:mode:(server client mirror discovery-service)' \\\n"
                  "    '*::mode args:_ascii_chat_subcommand'\n"
                  "}\n"
                  "\n"
                  "_ascii_chat_subcommand() {\n"
                  "  case $line[1] in\n"
                  "  server)\n"
                  "    _arguments \\\n");

  /* Server options - use unified display API matching help system */
  size_t server_count = 0;
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);

  if (server_opts) {
    for (size_t i = 0; i < server_count; i++) {
      zsh_write_option(output, &server_opts[i]);
    }
    SAFE_FREE(server_opts);
  }

  fprintf(output, "      && return 0\n"
                  "    ;;\n"
                  "  client)\n"
                  "    _arguments \\\n");

  /* Client options - use unified display API matching help system */
  size_t client_count = 0;
  const option_descriptor_t *client_opts = options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  if (client_opts) {
    for (size_t i = 0; i < client_count; i++) {
      zsh_write_option(output, &client_opts[i]);
    }
    SAFE_FREE(client_opts);
  }

  fprintf(output, "      && return 0\n"
                  "    ;;\n"
                  "  mirror)\n"
                  "    _arguments \\\n");

  /* Mirror options - use unified display API matching help system */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts = options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  if (mirror_opts) {
    for (size_t i = 0; i < mirror_count; i++) {
      zsh_write_option(output, &mirror_opts[i]);
    }
    SAFE_FREE(mirror_opts);
  }

  fprintf(output, "      && return 0\n"
                  "    ;;\n"
                  "  discovery-service)\n"
                  "    _arguments \\\n");

  /* Discovery-service options */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVICE, false, &discovery_svc_count);

  if (discovery_svc_opts) {
    for (size_t i = 0; i < discovery_svc_count; i++) {
      zsh_write_option(output, &discovery_svc_opts[i]);
    }
    SAFE_FREE(discovery_svc_opts);
  }

  fprintf(output, "      && return 0\n"
                  "    ;;\n"
                  "  esac\n"
                  "}\n"
                  "\n"
                  "_ascii_chat \"$@\"\n");

  return ASCIICHAT_OK;
}
