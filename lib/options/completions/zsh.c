/**
 * @file zsh.c
 * @brief Zsh shell completion script generator with category grouping
 * @ingroup options
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ascii-chat/options/completions/zsh.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/registry/mode_defaults.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/utf8.h>

/**
 * Escape special characters in completion descriptions for zsh
 */
static void zsh_escape_desc(FILE *output, const char *text) {
  if (!text) {
    return;
  }

  for (const char *p = text; *p; p++) {
    switch (*p) {
    case '"':
      // Escape double quotes for zsh double-quoted strings
      fprintf(output, "\\\"");
      break;
    case '\\':
      // Escape backslashes for zsh double-quoted strings
      fprintf(output, "\\\\");
      break;
    case '$':
      // Escape dollar signs to prevent variable expansion
      fprintf(output, "\\$");
      break;
    case '`':
      // Escape backticks to prevent command substitution
      fprintf(output, "\\`");
      break;
    case ':':
      // Colons don't need escaping in descriptions - they only have special meaning
      // as field separators before the description, which has already been written
      fputc(':', output);
      break;
    case '\n':
    case '\t':
      fprintf(output, " ");
      break;
    default:
      fputc(*p, output);
    }
  }
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
 * Format a single option for zsh _arguments command
 */
static void zsh_format_option_for_arguments(FILE *output, const option_descriptor_t *opt, bool has_more) {
  if (!opt) return;

  // Check if this is an enum option with values to complete
  size_t entry_count = 0;
  const enum_to_string_entry_t *entries = NULL;
  if (options_is_enum_option(opt->long_name)) {
    entries = options_get_enum_entries(opt->long_name, &entry_count);
  }

  // For enum options, use _arguments format matching rg's style
  if (entries && entry_count > 0) {
    fprintf(output, "    '--%s=[", opt->long_name);
    // Escape the help text for the option description
    for (const char *p = opt->help_text; p && *p; p++) {
      if (*p == '\'') {
        fprintf(output, "'\\''");
      } else if (*p == '\n' || *p == '\t') {
        fprintf(output, " ");
      } else {
        fputc(*p, output);
      }
    }
    fprintf(output, "]:values:((\n");
    // Write each value with its description on its own line
    for (size_t v = 0; v < entry_count; v++) {
      fprintf(output, "      %s\\:\\\"%s\\\"", entries[v].string, entries[v].desc ? entries[v].desc : "");
      if (v < entry_count - 1) {
        fprintf(output, "\n");
      }
    }
    fprintf(output, "\n    ))'");
  } else {
    // For non-enum options, use simple description format
    fprintf(output, "    '--%s:", opt->long_name);
    // Escape help text for single quotes
    for (const char *p = opt->help_text; p && *p; p++) {
      if (*p == '\'') {
        fprintf(output, "'\\''");
      } else if (*p == '\n' || *p == '\t') {
        fprintf(output, " ");
      } else {
        fputc(*p, output);
      }
    }
    fprintf(output, "'");
  }

  if (has_more) {
    fprintf(output, " \\\n");
  } else {
    fprintf(output, "\n");
  }
}

/**
 * Write options grouped by category using _arguments for proper value completion
 */
static void zsh_write_options_grouped(FILE *output, const option_descriptor_t *opts, size_t count,
                                       const char *func_prefix) {
  if (!opts || count == 0) return;

  // Set up proper context for zsh completion with mode-specific scope
  fprintf(output, "  local curcontext=\"${curcontext%%%%:*}:%s\"\n", func_prefix);
  // Use _arguments for proper enum value completion support
  fprintf(output, "  _arguments -s \\\n");

  // Write all options for _arguments (which handles value completion properly)
  for (size_t i = 0; i < count; i++) {
    // Check if this is an enum option with values to complete
    size_t entry_count = 0;
    const enum_to_string_entry_t *entries = NULL;
    if (options_is_enum_option(opts[i].long_name)) {
      entries = options_get_enum_entries(opts[i].long_name, &entry_count);
    }

    // For enum options, use _arguments format matching rg's style
    if (entries && entry_count > 0) {
      fprintf(output, "    '--");
      fprintf(output, "%s=[", opts[i].long_name);
      // Escape the help text for the option description
      for (const char *p = opts[i].help_text; p && *p; p++) {
        if (*p == '\'') {
          fprintf(output, "'\\''");
        } else if (*p == '\n' || *p == '\t') {
          fprintf(output, " ");
        } else {
          fputc(*p, output);
        }
      }
      fprintf(output, "]:values:((\n");
      // Write each value with its description on its own line
      for (size_t v = 0; v < entry_count; v++) {
        fprintf(output, "      %s\\:\"", entries[v].string);
        // Escape the description
        if (entries[v].desc) {
          for (const char *p = entries[v].desc; *p; p++) {
            if (*p == '\'') {
              fprintf(output, "'\\''");
            } else {
              fputc(*p, output);
            }
          }
        }
        fprintf(output, "\"\n");
      }
      fprintf(output, "    ))'");
    } else {
      // For non-enum options, use simple description format
      fprintf(output, "    '--");
      fprintf(output, "%s", opts[i].long_name);
      fprintf(output, ":");
      // Escape help text for single quotes
      for (const char *p = opts[i].help_text; p && *p; p++) {
        if (*p == '\'') {
          fprintf(output, "'\\''");
        } else if (*p == '\n' || *p == '\t') {
          fprintf(output, " ");
        } else {
          fputc(*p, output);
        }
      }
      fprintf(output, "'");
    }

    if (i < count - 1) {
      fprintf(output, " \\\n");
    } else {
      fprintf(output, "\n");
    }
  }
}

asciichat_error_t completions_generate_zsh(FILE *output) {
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  fprintf(output, "#compdef _ascii_chat ascii-chat\n"
                  "# Zsh completion script for ascii-chat\n"
                  "# Generated from options registry - DO NOT EDIT MANUALLY\n"
                  "\n"
                  "_ascii_chat() {\n"
                  "  local curcontext=\"$curcontext\"\n"
                  "  \n"
                  "  # Check if second word is an option (starts with -) - default to discovery\n"
                  "  if [[ \"${words[2]}\" == -* ]]; then\n"
                  "    curcontext=\"${curcontext%%%%:*}:discovery\"\n");

  /* Discovery mode options (includes binary-level options) */
  size_t discovery_count = 0;
  const option_descriptor_t *discovery_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &discovery_count);
  if (discovery_opts) {
    fprintf(output, "    _arguments -s \\\n");
    for (size_t i = 0; i < discovery_count; i++) {
      zsh_format_option_for_arguments(output, &discovery_opts[i], i < discovery_count - 1);
    }
    fprintf(output, "\n");
    SAFE_FREE(discovery_opts);
  }

  fprintf(output, "    return 0\n"
                  "  fi\n"
                  "\n"
                  "  # Route to mode-specific options\n"
                  "  case \"${words[2]}\" in\n"
                  "    server)\n"
                  "      curcontext=\"${curcontext%%%%:*}:server\"\n");

  /* Server options */
  size_t server_count = 0;
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);
  if (server_opts) {
    fprintf(output, "      _arguments -s \\\n");
    for (size_t i = 0; i < server_count; i++) {
      zsh_format_option_for_arguments(output, &server_opts[i], i < server_count - 1);
    }
    fprintf(output, "\n");
    SAFE_FREE(server_opts);
  }

  fprintf(output, "      return 0\n"
                  "      ;;\n"
                  "    discovery-service)\n"
                  "      curcontext=\"${curcontext%%%%:*}:discovery-service\"\n");

  /* Discovery-service options */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVICE, false, &discovery_svc_count);
  if (discovery_svc_opts) {
    fprintf(output, "      _arguments -s \\\n");
    for (size_t i = 0; i < discovery_svc_count; i++) {
      zsh_format_option_for_arguments(output, &discovery_svc_opts[i], i < discovery_svc_count - 1);
    }
    fprintf(output, "\n");
    SAFE_FREE(discovery_svc_opts);
  }

  fprintf(output, "      return 0\n"
                  "      ;;\n"
                  "    client)\n"
                  "      curcontext=\"${curcontext%%%%:*}:client\"\n");

  /* Client options */
  size_t client_count = 0;
  const option_descriptor_t *client_opts = options_registry_get_for_display(MODE_CLIENT, false, &client_count);
  if (client_opts) {
    fprintf(output, "      _arguments -s \\\n");
    for (size_t i = 0; i < client_count; i++) {
      zsh_format_option_for_arguments(output, &client_opts[i], i < client_count - 1);
    }
    fprintf(output, "\n");
    SAFE_FREE(client_opts);
  }

  fprintf(output, "      return 0\n"
                  "      ;;\n"
                  "    mirror)\n"
                  "      curcontext=\"${curcontext%%%%:*}:mirror\"\n");

  /* Mirror options */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts = options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);
  if (mirror_opts) {
    fprintf(output, "      _arguments -s \\\n");
    for (size_t i = 0; i < mirror_count; i++) {
      zsh_format_option_for_arguments(output, &mirror_opts[i], i < mirror_count - 1);
    }
    fprintf(output, "\n");
    SAFE_FREE(mirror_opts);
  }

  fprintf(output, "      return 0\n"
                  "      ;;\n"
                  "    discovery)\n"
                  "      curcontext=\"${curcontext%%%%:*}:discovery\"\n");

  /* Discovery mode options again for explicit discovery command */
  size_t discovery_count2 = 0;
  const option_descriptor_t *discovery_opts2 = options_registry_get_for_display(MODE_DISCOVERY, true, &discovery_count2);
  if (discovery_opts2) {
    fprintf(output, "      _arguments -s \\\n");
    for (size_t i = 0; i < discovery_count2; i++) {
      zsh_format_option_for_arguments(output, &discovery_opts2[i], i < discovery_count2 - 1);
    }
    fprintf(output, "\n");
    SAFE_FREE(discovery_opts2);
  }

  fprintf(output, "      return 0\n"
                  "      ;;\n"
                  "  esac\n"
                  "\n"
                  "  # No mode specified - show mode selection\n"
                  "  local state line modes\n"
                  "  _arguments -s \\\n"
                  "    '1: :->mode' && return\n"
                  "  [[ -n \"$state\" ]] || return 1\n"
                  "  case $state in\n"
                  "    mode)\n"
                  "      local -a server_modes client_modes\n"
                  "      server_modes=(\n");

  /* Generate server-like modes from registry */
  size_t mode_server_count = 0;
  const mode_descriptor_t *mode_server_descs = get_modes_by_group("server-like", &mode_server_count);
  if (mode_server_descs) {
    for (size_t i = 0; i < mode_server_count; i++) {
      fprintf(output, "        '%s:%s'\n", mode_server_descs[i].name, mode_server_descs[i].description);
    }
    SAFE_FREE(mode_server_descs);
  }

  fprintf(output, "      )\n"
                  "      client_modes=(\n");

  /* Generate client-like modes from registry */
  size_t mode_client_count = 0;
  const mode_descriptor_t *mode_client_descs = get_modes_by_group("client-like", &mode_client_count);
  if (mode_client_descs) {
    for (size_t i = 0; i < mode_client_count; i++) {
      if (strcmp(mode_client_descs[i].name, "discovery") == 0) {
        continue;
      }
      fprintf(output, "        '%s:%s'\n", mode_client_descs[i].name, mode_client_descs[i].description);
    }
    SAFE_FREE(mode_client_descs);
  }

  fprintf(output, "      )\n"
                  "      _describe -t server-modes 'server-like modes' server_modes\n"
                  "      _describe -t client-modes 'client-like modes' client_modes\n"
                  "      ;;\n"
                  "  esac\n"
                  "}\n");

  return ASCIICHAT_OK;
}
