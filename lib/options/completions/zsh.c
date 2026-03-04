/**
 * @file zsh.c
 * @brief Zsh shell completion script generator
 * @ingroup options
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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
      size_t pos = 0;
      pos += safe_snprintf(completion_spec + pos, sizeof(completion_spec) - pos, ":(");
      for (size_t i = 0; meta->enum_values[i] != NULL && pos < sizeof(completion_spec) - 1; i++) {
        if (i > 0)
          pos += safe_snprintf(completion_spec + pos, sizeof(completion_spec) - pos, " ");
        pos += safe_snprintf(completion_spec + pos, sizeof(completion_spec) - pos, "%s", meta->enum_values[i]);
      }
      safe_snprintf(completion_spec + pos, sizeof(completion_spec) - pos, ")");
    } else if (meta->input_type == OPTION_INPUT_FILEPATH) {
      // File path completion
      SAFE_STRNCPY(completion_spec, ":_files", sizeof(completion_spec));
    } else if (meta->examples && meta->examples[0] != NULL) {
      // Examples: "(example1 example2)" - practical values, higher priority than calculated ranges
      size_t pos = 0;
      pos += safe_snprintf(completion_spec + pos, sizeof(completion_spec) - pos, ":(");
      for (size_t i = 0; meta->examples[i] != NULL && pos < sizeof(completion_spec) - 1; i++) {
        if (i > 0)
          pos += safe_snprintf(completion_spec + pos, sizeof(completion_spec) - pos, " ");
        pos += safe_snprintf(completion_spec + pos, sizeof(completion_spec) - pos, "%s", meta->examples[i]);
      }
      safe_snprintf(completion_spec + pos, sizeof(completion_spec) - pos, ")");
    } else if (meta->input_type == OPTION_INPUT_NUMERIC) {
      // Numeric completion with range - only if no examples
      if (meta->numeric_range.min > 0 || meta->numeric_range.max > 0) {
        safe_snprintf(completion_spec, sizeof(completion_spec), ":(numeric %d-%d)", meta->numeric_range.min,
                      meta->numeric_range.max);
      } else {
        SAFE_STRNCPY(completion_spec, ":(numeric)", sizeof(completion_spec));
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

/**
 * Collect unique group names from options and sort them
 */
static const char **zsh_collect_groups(const option_descriptor_t *opts, size_t count, size_t *out_group_count) {
  if (!opts || count == 0) {
    *out_group_count = 0;
    return NULL;
  }

  // Allocate array for unique groups (worst case: count groups)
  const char **groups = SAFE_MALLOC(count * sizeof(const char *), const char **);
  size_t group_count = 0;

  // Collect unique groups
  for (size_t i = 0; i < count; i++) {
    const char *group = opts[i].group;
    if (!group) continue;

    // Check if we already have this group
    bool found = false;
    for (size_t j = 0; j < group_count; j++) {
      if (strcmp(groups[j], group) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      groups[group_count++] = group;
    }
  }

  // Sort groups alphabetically for consistent ordering
  for (size_t i = 0; i < group_count; i++) {
    for (size_t j = i + 1; j < group_count; j++) {
      if (strcmp(groups[i], groups[j]) > 0) {
        const char *tmp = groups[i];
        groups[i] = groups[j];
        groups[j] = tmp;
      }
    }
  }

  *out_group_count = group_count;
  return groups;
}

/**
 * Write grouped options with category headers
 */
static void zsh_write_options_grouped(FILE *output, const option_descriptor_t *opts, size_t count) {
  if (!opts || count == 0) return;

  size_t group_count = 0;
  const char **groups = zsh_collect_groups(opts, count, &group_count);

  // Write options grouped by category
  for (size_t g = 0; g < group_count; g++) {
    const char *group = groups[g];

    // Write group header as a comment
    fprintf(output, "    # %s\n", group);

    // Write all options in this group
    for (size_t i = 0; i < count; i++) {
      if (opts[i].group && strcmp(opts[i].group, group) == 0) {
        zsh_write_option(output, &opts[i]);
      }
    }
  }

  SAFE_FREE(groups);
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

  /* Binary options - grouped by category */
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  if (binary_opts) {
    zsh_write_options_grouped(output, binary_opts, binary_count);
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

  /* Server options - grouped by category */
  size_t server_count = 0;
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);

  if (server_opts) {
    zsh_write_options_grouped(output, server_opts, server_count);
    SAFE_FREE(server_opts);
  }

  fprintf(output, "      && return 0\n"
                  "    ;;\n"
                  "  client)\n"
                  "    _arguments \\\n");

  /* Client options - grouped by category */
  size_t client_count = 0;
  const option_descriptor_t *client_opts = options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  if (client_opts) {
    zsh_write_options_grouped(output, client_opts, client_count);
    SAFE_FREE(client_opts);
  }

  fprintf(output, "      && return 0\n"
                  "    ;;\n"
                  "  mirror)\n"
                  "    _arguments \\\n");

  /* Mirror options - grouped by category */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts = options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  if (mirror_opts) {
    zsh_write_options_grouped(output, mirror_opts, mirror_count);
    SAFE_FREE(mirror_opts);
  }

  fprintf(output, "      && return 0\n"
                  "    ;;\n"
                  "  discovery-service)\n"
                  "    _arguments \\\n");

  /* Discovery-service options - grouped by category */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVICE, false, &discovery_svc_count);

  if (discovery_svc_opts) {
    zsh_write_options_grouped(output, discovery_svc_opts, discovery_svc_count);
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
