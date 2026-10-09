/**
 * @file zsh.c
 * @brief Zsh shell completion script generator with described values
 * @ingroup options
 */

#include <string.h>
#include <stdio.h>
#include <ascii-chat/options/completions/zsh.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/registry/mode_defaults.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/common.h>

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
    case ':':
    case '|':
    case '[':
    case ']':
      // Escape special zsh characters that conflict with completion syntax
      fprintf(output, "\\%c", *p);
      break;
    case '\\':
      // Escape backslashes
      fprintf(output, "\\\\");
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

/** Emit option specifications from the same descriptors used by help and parsing. */
static void zsh_write_combined_args(FILE *output, const option_descriptor_t *opts, size_t count, bool modes) {
  if (!opts || count == 0)
    return;

  fprintf(output, "  local -a args=(\n");

  for (size_t i = 0; i < count; i++) {
    for (int spelling = 0; spelling < (opts[i].short_name ? 2 : 1); ++spelling) {
      fprintf(output, "    '");
      char name[256];
      if (spelling == 0)
        SAFE_SNPRINTF(name, sizeof(name), "--%s", opts[i].long_name);
      else
        SAFE_SNPRINTF(name, sizeof(name), "-%c", opts[i].short_name);

      // Device index options: call helper functions
      if (strcmp(opts[i].long_name, "webcam-index") == 0) {
        fprintf(output, "%s=[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]:device index:_ascii_chat_webcam_indices'\n");
      } else if (strcmp(opts[i].long_name, "microphone-index") == 0) {
        fprintf(output, "%s=[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]:device index:_ascii_chat_microphone_indices'\n");
      } else if (strcmp(opts[i].long_name, "speakers-index") == 0) {
        fprintf(output, "%s=[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]:device index:_ascii_chat_speakers_indices'\n");
      }
      // Enum options: full spec with values and descriptions
      else if (opts[i].metadata.input_type == OPTION_INPUT_ENUM && opts[i].metadata.enum_values) {
        fprintf(output, "%s=[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]%s%s:((\\\n", opts[i].optional_arg ? "::" : ":", opts[i].long_name);

        for (size_t j = 0; opts[i].metadata.enum_values[j] != NULL; j++) {
          fprintf(output, "      %s", opts[i].metadata.enum_values[j]);
          if (opts[i].metadata.enum_descriptions && opts[i].metadata.enum_descriptions[j]) {
            fprintf(output, "\\:\"");
            zsh_escape_desc(output, opts[i].metadata.enum_descriptions[j]);
            fprintf(output, "\"");
          }
          fprintf(output, "\\\n");
        }
        fprintf(output, "    ))'\n");
      }
      // Boolean options with values
      else if (opts[i].type == OPTION_TYPE_BOOL) {
        fprintf(output, "%s=[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]::value:((" OPT_VALUE_TRUE "\\:\"enable\" " OPT_VALUE_FALSE "\\:\"disable\"))'\n");
      }
      // Action options (no value)
      else if (opts[i].type == OPTION_TYPE_ACTION) {
        fprintf(output, "%s[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]'\n");
      } else if (opts[i].metadata.input_type == OPTION_INPUT_FILEPATH ||
                 (opts[i].arg_placeholder && strstr(opts[i].arg_placeholder, "FILE"))) {
        fprintf(output, "%s=[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]:file:_files'\n");
      } else if (opts[i].metadata.examples) {
        fprintf(output, "%s=[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]%svalue:(", opts[i].optional_arg ? "::" : ":");
        for (size_t j = 0; opts[i].metadata.examples[j]; ++j) {
          if (j)
            fputc(' ', output);
          zsh_escape_desc(output, opts[i].metadata.examples[j]);
        }
        fprintf(output, ")'\n");
      }
      // Other options
      else {
        fprintf(output, "%s=[", name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "]%svalue:'\n", opts[i].optional_arg ? "::" : ":");
      }
    }
  }
  fprintf(output, "  )\n\n");
  fprintf(output, "  _arguments -C -s -S \"${args[@]}\" ");
  if (modes)
    fprintf(output, "'1:mode:_ascii_chat_modes' ");
  fprintf(output, "'*:argument:'\n\n");
}

asciichat_error_t completions_generate_zsh(FILE *output) {
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  // Before a mode, expose the complete registry; after a mode, use its own options.
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  fprintf(output, "# Zsh completion script for ascii-chat\n"
                  "# Generated from options registry - DO NOT EDIT MANUALLY\n"
                  "\n"
                  "# Configure completion for ascii-chat\n"
                  "# Use only _complete (no corrections or approximate matching)\n"
                  "zstyle ':completion:*:ascii-chat:*' completer _complete\n"
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
                  "_ascii_chat_binary_grouped() {\n"
                  "  local curcontext=$curcontext\n"
                  "  local -a context line state_descr args\n"
                  "  local state\n"
                  "  local -A opt_args\n\n");

  /* Write binary-level options in grouped format */
  if (binary_opts) {
    zsh_write_combined_args(output, binary_opts, binary_count, true);
  }

  fprintf(output, "}\n\n_ascii_chat() {\n"
                  "  local i mode=''\n"
                  "  for (( i=2; i<CURRENT; ++i )); do\n"
                  "    case $words[i] in\n"
                  "      --) break ;;\n"
                  "      --*=*) continue ;;\n");
  // Skip required option values so a file called 'mirror' is not a mode.
  for (size_t i = 0; i < binary_count; ++i) {
    const option_descriptor_t *opt = &binary_opts[i];
    if (opt->type == OPTION_TYPE_ACTION || opt->type == OPTION_TYPE_BOOL || opt->optional_arg)
      continue;
    fprintf(output, "      --%s", opt->long_name);
    if (opt->short_name)
      fprintf(output, "|-%c", opt->short_name);
    fprintf(output, ") (( ++i )) ;;\n");
  }
  fprintf(output, "      server|client|mirror|discovery-service|acds)\n"
                  "        mode=$words[i]\n"
                  "        words=(\"$words[1]\" \"${(@)words[i+1,-1]}\")\n"
                  "        (( CURRENT -= i - 1 ))\n"
                  "        break ;;\n"
                  "    esac\n"
                  "  done\n"
                  "  case $mode in\n"
                  "    # Server-like modes\n"
                  "    server)\n"
                  "      _ascii_chat_server\n"
                  "      return\n"
                  "      ;;\n"
                  "    discovery-service|acds)\n"
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
                  "    *) _ascii_chat_binary_grouped ;;\n"
                  "  esac\n"
                  "}\n\n"
                  "_ascii_chat_modes() {\n"
                  "  local -a server_modes client_modes\n"
                  "  server_modes=(\n");

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
                  "}\n\n"
                  "# Server-like modes: handle incoming connections and stream management\n"
                  "_ascii_chat_server() {\n"
                  "  local curcontext=$curcontext\n"
                  "  local -a context line state_descr args\n"
                  "  local state\n"
                  "  local -A opt_args\n\n");

  /* Server options - grouped by category */
  size_t server_count = 0;
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);

  if (server_opts) {
    zsh_write_combined_args(output, server_opts, server_count, false);
    SAFE_FREE(server_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "_ascii_chat_discovery_service() {\n"
                  "  local curcontext=$curcontext\n"
                  "  local -a context line state_descr args\n"
                  "  local state\n"
                  "  local -A opt_args\n\n");

  /* Discovery-service options - grouped by category */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVICE, false, &discovery_svc_count);

  if (discovery_svc_opts) {
    zsh_write_combined_args(output, discovery_svc_opts, discovery_svc_count, false);
    SAFE_FREE(discovery_svc_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "# Client-like modes: connect to servers or render local media\n"
                  "_ascii_chat_client() {\n"
                  "  local curcontext=$curcontext\n"
                  "  local -a context line state_descr args\n"
                  "  local state\n"
                  "  local -A opt_args\n\n");

  /* Client options - grouped by category */
  size_t client_count = 0;
  const option_descriptor_t *client_opts = options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  if (client_opts) {
    zsh_write_combined_args(output, client_opts, client_count, false);
    SAFE_FREE(client_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "_ascii_chat_mirror() {\n"
                  "  local curcontext=$curcontext\n"
                  "  local -a context line state_descr args\n"
                  "  local state\n"
                  "  local -A opt_args\n\n");

  /* Mirror options - grouped by category */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts = options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  if (mirror_opts) {
    zsh_write_combined_args(output, mirror_opts, mirror_count, false);
    SAFE_FREE(mirror_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "# Register completion functions (must come after definitions)\n"
                  "compdef _ascii_chat ascii-chat\n"
                  "compdef _ascii_chat 'build/bin/ascii-chat'\n");

  SAFE_FREE(binary_opts);

  return ASCIICHAT_OK;
}
