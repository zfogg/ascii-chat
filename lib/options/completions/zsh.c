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
    case '\'':
      // Escape single quotes: end quote, escaped quote, start quote
      fprintf(output, "'\\''");
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
 * Write value completion case blocks for enum, boolean, and device index options
 *
 * Generates case arms that intercept when completing after an option:
 *   --color-mode) _values 'color mode' auto none 16 256 truecolor; return ;;
 *   --webcam-index) _ascii_chat_webcam_indices; return ;;
 *   --audio) _values 'value' true false; return ;;
 */
static void zsh_write_value_cases(FILE *output, const option_descriptor_t *opts, size_t count) {
  if (!opts || count == 0) return;

  // Case statement for option name matching
  fprintf(output, "  case \"$prev\" in\n");

  for (size_t i = 0; i < count; i++) {
    // Skip action options (they take no value)
    if (opts[i].type == OPTION_TYPE_ACTION) continue;

    // Device index options: call helper functions
    if (strcmp(opts[i].long_name, "webcam-index") == 0) {
      fprintf(output, "    --%-30s) _ascii_chat_webcam_indices; return ;;\n", opts[i].long_name);
    }
    else if (strcmp(opts[i].long_name, "microphone-index") == 0) {
      fprintf(output, "    --%-30s) _ascii_chat_microphone_indices; return ;;\n", opts[i].long_name);
    }
    else if (strcmp(opts[i].long_name, "speakers-index") == 0) {
      fprintf(output, "    --%-30s) _ascii_chat_speakers_indices; return ;;\n", opts[i].long_name);
    }
    // Enum options: emit value completion
    else if (opts[i].metadata.input_type == OPTION_INPUT_ENUM && opts[i].metadata.enum_values) {
      fprintf(output, "    --%-30s) _values '%s'", opts[i].long_name, opts[i].long_name);

      for (size_t j = 0; opts[i].metadata.enum_values[j] != NULL; j++) {
        fprintf(output, " %s", opts[i].metadata.enum_values[j]);
      }
      fprintf(output, "; return ;;\n");
    }
    // Boolean options: emit true/false completion
    else if (opts[i].type == OPTION_TYPE_BOOL) {
      fprintf(output, "    --%-30s) _values 'value' true false; return ;;\n", opts[i].long_name);
    }
  }

  fprintf(output, "  esac\n\n");

  // Also handle --option=VALUE form (device indices don't support this form)
  fprintf(output, "  case \"${words[CURRENT]}\" in\n");

  for (size_t i = 0; i < count; i++) {
    if (opts[i].type == OPTION_TYPE_ACTION) continue;

    // Skip device index options (they don't support =VALUE form)
    if (strcmp(opts[i].long_name, "webcam-index") == 0 ||
        strcmp(opts[i].long_name, "microphone-index") == 0 ||
        strcmp(opts[i].long_name, "speakers-index") == 0) {
      continue;
    }

    if (opts[i].metadata.input_type == OPTION_INPUT_ENUM && opts[i].metadata.enum_values) {
      fprintf(output, "    '--%-30s=*') _values '%s'", opts[i].long_name, opts[i].long_name);

      for (size_t j = 0; opts[i].metadata.enum_values[j] != NULL; j++) {
        fprintf(output, " %s", opts[i].metadata.enum_values[j]);
      }
      fprintf(output, "; return ;;\n");
    }
  }

  fprintf(output, "  esac\n\n");
}

/**
 * Write options grouped by category using _describe for proper group headers
 */
static void zsh_write_options_grouped(FILE *output, const option_descriptor_t *opts, size_t count,
                                       const char *func_prefix) {
  if (!opts || count == 0) return;

  size_t group_count = 0;
  const char **groups = zsh_collect_groups(opts, count, &group_count);

  // First pass: declare arrays for each group
  for (size_t g = 0; g < group_count; g++) {
    const char *group = groups[g];
    fprintf(output, "  local -a %s_%s_opts=(\n", func_prefix, group);

    // Write all options in this group (long options only to avoid "corrections" duplicates)
    for (size_t i = 0; i < count; i++) {
      if (!opts[i].group || strcmp(opts[i].group, group) != 0) continue;

      // Long option only (short options are less discoverable via TAB)
      fprintf(output, "    '--%s:", opts[i].long_name);
      zsh_escape_desc(output, opts[i].help_text);
      fprintf(output, "'\n");
    }

    fprintf(output, "  )\n");
  }

  // Second pass: call _describe for each group (with lowercase display)
  for (size_t g = 0; g < group_count; g++) {
    const char *group = groups[g];
    fprintf(output, "  _describe -t %s '", group);
    utf8_write_lowercase(output, group);
    fprintf(output, " options' %s_%s_opts\n", func_prefix, group);
  }

  SAFE_FREE(groups);
}

asciichat_error_t completions_generate_zsh(FILE *output) {
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  /* Load binary-level and discovery options for binary-level completion.
   * Binary-level completion (options before mode name like --color-mode) uses
   * _arguments with -n -S flags to establish proper zsh completion context.
   * Without _arguments, completion functions fail with "can only be called from
   * completion function" error, causing zsh to display "corrections (errors: N)"
   * messages. The -n flag disables defaults and -S disables short option
   * processing, allowing safe context establishment. */
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  size_t discovery_count = 0;
  const option_descriptor_t *discovery_opts = options_registry_get_for_display(MODE_DISCOVERY, false, &discovery_count);

  fprintf(output, "compdef _ascii_chat ascii-chat\n"
                  "# Zsh completion script for ascii-chat\n"
                  "# Generated from options registry - DO NOT EDIT MANUALLY\n"
                  "\n"
                  "# Enable approximate completion for ascii-chat - suggests close matches\n"
                  "# when the typed string doesn't match exactly (e.g., --colr-mode -> --color-mode)\n"
                  "zstyle ':completion:*:ascii-chat:*' completer _complete _approximate\n"
                  "zstyle ':completion:*:ascii-chat:*:approximate:*' max-errors 2 numeric\n"
                  "\n"
                  "# Device index completion helpers\n"
                  "_ascii_chat_webcam_indices() {\n"
                  "  local -a indices\n"
                  "  local output\n"
                  "  output=$(\"${words[1]}\" --list-webcams 2>/dev/null) || return\n"
                  "  while IFS= read -r line; do\n"
                  "    [[ \"$line\" =~ ^[[:space:]]*([0-9]+)[[:space:]]+(.+)$ ]] && \\\n"
                  "      indices+=(\"${match[1]}:${match[2]}\")\n"
                  "  done <<< \"$output\"\n"
                  "  _describe 'webcam' indices\n"
                  "}\n"
                  "\n"
                  "_ascii_chat_microphone_indices() {\n"
                  "  local -a indices\n"
                  "  local output\n"
                  "  output=$(\"${words[1]}\" --list-microphones 2>/dev/null) || return\n"
                  "  while IFS= read -r line; do\n"
                  "    [[ \"$line\" =~ ^[[:space:]]*([0-9-]+)[[:space:]]+(.+)$ ]] && \\\n"
                  "      indices+=(\"${match[1]}:${match[2]}\")\n"
                  "  done <<< \"$output\"\n"
                  "  _describe 'microphone' indices\n"
                  "}\n"
                  "\n"
                  "_ascii_chat_speakers_indices() {\n"
                  "  local -a indices\n"
                  "  local output\n"
                  "  output=$(\"${words[1]}\" --list-speakers 2>/dev/null) || return\n"
                  "  while IFS= read -r line; do\n"
                  "    [[ \"$line\" =~ ^[[:space:]]*([0-9-]+)[[:space:]]+(.+)$ ]] && \\\n"
                  "      indices+=(\"${match[1]}:${match[2]}\")\n"
                  "  done <<< \"$output\"\n"
                  "  _describe 'speakers' indices\n"
                  "}\n"
                  "\n"
                  "_ascii_chat_binary() {\n"
                  "  local -a binary_opts=(\n");

  /* Add discovery options to binary-level completion */
  if (discovery_opts) {
    for (size_t i = 0; i < discovery_count; i++) {
      fprintf(output, "    '--%s:", discovery_opts[i].long_name);
      zsh_escape_desc(output, discovery_opts[i].help_text);
      fprintf(output, "'\n");
    }
  }

  fprintf(output, "  )\n"
                  "  _values 'option' \"${binary_opts[@]}\"\n"
                  "}\n"
                  "\n"
                  "_ascii_chat_binary_grouped() {\n");

  /* Write binary-level options in grouped format */
  if (binary_opts) {
    zsh_write_value_cases(output, binary_opts, binary_count);
    zsh_write_options_grouped(output, binary_opts, binary_count, "binary");
  }

  fprintf(output, "}\n"
                  "\n"
                  "_ascii_chat() {\n"
                  "  if [[ ${words[2]} == -* ]]; then\n"
                  "    # Binary-level options: show both binary and discovery mode options\n"
                  "    # (since discovery is the default mode that runs at binary level)\n"
                  "    _ascii_chat_binary_grouped\n"
                  "    _ascii_chat_discovery\n"
                  "    return\n"
                  "  fi\n"
                  "\n"
                  "  # Mode routing: check words[2] to determine which mode function to call\n"
                  "  case $words[2] in\n"
                  "    # Server-like modes\n"
                  "    server)\n"
                  "      _ascii_chat_server\n"
                  "      return\n"
                  "      ;;\n"
                  "    discovery-service)\n"
                  "      _ascii_chat_discovery_service\n"
                  "      return\n"
                  "      ;;\n"
                  "\n"
                  "    # Client-like modes\n"
                  "    client)\n"
                  "      _ascii_chat_client\n"
                  "      return\n"
                  "      ;;\n"
                  "    mirror)\n"
                  "      _ascii_chat_mirror\n"
                  "      return\n"
                  "      ;;\n"
                  "\n"
                  "    # Default mode (discovery) or mode selection\n"
                  "    *)\n"
                  "      if [[ $CURRENT -eq 2 ]]; then\n"
                  "        # Completing the mode name itself - show available modes\n"
                  "        local -a server_modes client_modes\n"
                  "        server_modes=(\n");

  /* Generate server-like modes from registry */
  size_t mode_server_count = 0;
  const mode_descriptor_t *mode_server_descs = get_modes_by_group("server-like", &mode_server_count);
  if (mode_server_descs) {
    for (size_t i = 0; i < mode_server_count; i++) {
      fprintf(output, "          '%s:%s'\n", mode_server_descs[i].name, mode_server_descs[i].description);
    }
    SAFE_FREE(mode_server_descs);
  }

  fprintf(output, "        )\n"
                  "        client_modes=(\n");

  /* Generate client-like modes from registry (exclude discovery since it's the default mode) */
  size_t mode_client_count = 0;
  const mode_descriptor_t *mode_client_descs = get_modes_by_group("client-like", &mode_client_count);
  if (mode_client_descs) {
    for (size_t i = 0; i < mode_client_count; i++) {
      /* Skip discovery mode - it's the default when no mode is specified */
      if (strcmp(mode_client_descs[i].name, "discovery") == 0) {
        continue;
      }
      fprintf(output, "          '%s:%s'\n", mode_client_descs[i].name, mode_client_descs[i].description);
    }
    SAFE_FREE(mode_client_descs);
  }

  fprintf(output, "        )\n"
                  "        _describe -t server-modes 'server-like modes' server_modes\n"
                  "        _describe -t client-modes 'client-like modes' client_modes\n"
                  "      else\n"
                  "        # Default mode (discovery) - complete its options\n"
                  "        _ascii_chat_discovery\n"
                  "      fi\n"
                  "      ;;\n"
                  "  esac\n"
                  "}\n"
                  "\n"
                  "# Server-like modes: handle incoming connections and stream management\n"
                  "_ascii_chat_server() {\n");

  /* Server options - grouped by category */
  size_t server_count = 0;
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);

  if (server_opts) {
    zsh_write_value_cases(output, server_opts, server_count);
    zsh_write_options_grouped(output, server_opts, server_count, "server");
    SAFE_FREE(server_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "_ascii_chat_discovery_service() {\n");

  /* Discovery-service options - grouped by category */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVICE, false, &discovery_svc_count);

  if (discovery_svc_opts) {
    zsh_write_value_cases(output, discovery_svc_opts, discovery_svc_count);
    zsh_write_options_grouped(output, discovery_svc_opts, discovery_svc_count, "discovery_service");
    SAFE_FREE(discovery_svc_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "# Client-like modes: connect to servers or render local media\n"
                  "_ascii_chat_client() {\n");

  /* Client options - grouped by category */
  size_t client_count = 0;
  const option_descriptor_t *client_opts = options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  if (client_opts) {
    zsh_write_value_cases(output, client_opts, client_count);
    zsh_write_options_grouped(output, client_opts, client_count, "client");
    SAFE_FREE(client_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "_ascii_chat_mirror() {\n");

  /* Mirror options - grouped by category */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts = options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  if (mirror_opts) {
    zsh_write_value_cases(output, mirror_opts, mirror_count);
    zsh_write_options_grouped(output, mirror_opts, mirror_count, "mirror");
    SAFE_FREE(mirror_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "# Discovery mode: find sessions via discovery service (default mode when no mode specified)\n"
                  "_ascii_chat_discovery() {\n");

  /* Discovery options - grouped by category (reuse already-loaded options) */
  if (discovery_opts) {
    zsh_write_value_cases(output, discovery_opts, discovery_count);
    zsh_write_options_grouped(output, discovery_opts, discovery_count, "discovery");
  }

  fprintf(output, "}\n"
                  "\n"
                  "_ascii_chat \"$@\"\n");

  SAFE_FREE(binary_opts);
  SAFE_FREE(discovery_opts);

  return ASCIICHAT_OK;
}
