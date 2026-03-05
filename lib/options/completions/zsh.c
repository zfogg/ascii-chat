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
#include <ascii-chat/common.h>
#include <ascii-chat/util/utf8.h>

/**
 * Escape special characters in completion descriptions for zsh
 * Keep descriptions under 60 characters to avoid zsh parser issues
 */
static void zsh_escape_desc(FILE *output, const char *text) {
  if (!text) {
    return;
  }

  size_t chars = 0;
  const size_t MAX_DESC = 60;

  for (const char *p = text; *p && chars < MAX_DESC; p++) {
    switch (*p) {
    case '\'':
      fprintf(output, "'\\''");
      chars++;
      break;
    case '\n':
    case '\t':
      fprintf(output, " ");
      chars++;
      break;
    default:
      fputc(*p, output);
      chars++;
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

    // Write all options in this group
    for (size_t i = 0; i < count; i++) {
      if (!opts[i].group || strcmp(opts[i].group, group) != 0) continue;

      // Short option if present
      if (opts[i].short_name != '\0') {
        fprintf(output, "    '-%c:", opts[i].short_name);
        zsh_escape_desc(output, opts[i].help_text);
        fprintf(output, "'\n");
      }

      // Long option
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

  fprintf(output, "#compdef _ascii_chat ascii-chat\n"
#ifndef NDEBUG
                  "#compdef _ascii_chat build/bin/ascii-chat ./build/bin/ascii-chat\n"
#endif
                  "# Zsh completion script for ascii-chat\n"
                  "# Generated from options registry - DO NOT EDIT MANUALLY\n"
                  "\n"
                  "_ascii_chat() {\n"
                  "  # If first arg is a flag (starts with -), go straight to args completion\n"
                  "  if [[ ${words[2]} == -* ]]; then\n"
                  "    _ascii_chat_args\n"
                  "    return\n"
                  "  fi\n"
                  "  local curcontext=\"$curcontext\" state line modes\n"
                  "  _arguments \\\n"
                  "    '1:mode:->mode' \\\n"
                  "    '*::args:_ascii_chat_args'\n"
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

  /* Generate client-like modes from registry (exclude discovery since it's the default mode) */
  size_t mode_client_count = 0;
  const mode_descriptor_t *mode_client_descs = get_modes_by_group("client-like", &mode_client_count);
  if (mode_client_descs) {
    for (size_t i = 0; i < mode_client_count; i++) {
      /* Skip discovery mode - it's the default when no mode is specified */
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
                  "}\n"
                  "\n"
                  "_ascii_chat_args() {\n"
                  "  case $words[1] in\n"
                  "    # Server-like modes\n"
                  "    server)\n"
                  "      _ascii_chat_server\n"
                  "      ;;\n"
                  "    discovery-service)\n"
                  "      _ascii_chat_discovery_service\n"
                  "      ;;\n"
                  "\n"
                  "    # Client-like modes\n"
                  "    client)\n"
                  "      _ascii_chat_client\n"
                  "      ;;\n"
                  "    mirror)\n"
                  "      _ascii_chat_mirror\n"
                  "      ;;\n"
                  "\n"
                  "    *)\n"
                  "      # Default mode (discovery) + binary-level options\n"
                  "      _ascii_chat_discovery\n"
                  "      # Also show binary-level options\n");

  /* Binary options - grouped by category */
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  if (binary_opts) {
    zsh_write_options_grouped(output, binary_opts, binary_count, "binary");
    SAFE_FREE(binary_opts);
  }

  fprintf(output, "      ;;\n"
                  "  esac\n"
                  "}\n"
                  "\n"
                  "# Server-like modes: handle incoming connections and stream management\n"
                  "_ascii_chat_server() {\n");

  /* Server options - grouped by category */
  size_t server_count = 0;
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);

  if (server_opts) {
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
    zsh_write_options_grouped(output, mirror_opts, mirror_count, "mirror");
    SAFE_FREE(mirror_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "# Discovery mode: find sessions via discovery service (default mode when no mode specified)\n"
                  "_ascii_chat_discovery() {\n");

  /* Discovery options - grouped by category */
  size_t discovery_count = 0;
  const option_descriptor_t *discovery_opts = options_registry_get_for_display(MODE_DISCOVERY, false, &discovery_count);

  if (discovery_opts) {
    zsh_write_options_grouped(output, discovery_opts, discovery_count, "discovery");
    SAFE_FREE(discovery_opts);
  }

  fprintf(output, "}\n"
                  "\n"
                  "_ascii_chat \"$@\"\n");

  return ASCIICHAT_OK;
}
