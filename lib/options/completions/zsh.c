/**
 * @file zsh.c
 * @brief Zsh shell completion script generator
 * @ingroup options
 */

#include <string.h>
#include <stdio.h>
#include "options/completions/zsh.h"
#include "options/registry.h"
#include "common.h"

static void zsh_write_option(FILE *output, const option_descriptor_t *opt)
{
  if (!opt) {
    return;
  }

  if (opt->short_name != '\0') {
    fprintf(output, "    '-%c[%s]' \\\n", opt->short_name, opt->help_text);
  }
  fprintf(output, "    '--%s[%s]' \\\n", opt->long_name, opt->help_text);
}

asciichat_error_t completions_generate_zsh(FILE *output)
{
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  fprintf(output,
    "#compdef _ascii_chat ascii-chat\n"
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
  const option_descriptor_t *binary_opts =
      options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  if (binary_opts) {
    for (size_t i = 0; i < binary_count; i++) {
      zsh_write_option(output, &binary_opts[i]);
    }
    SAFE_FREE(binary_opts);
  }

  fprintf(output,
    "    '1:mode:(server client mirror discovery-service)' \\\n"
    "    '*::mode args:_ascii_chat_subcommand'\n"
    "}\n"
    "\n"
    "_ascii_chat_subcommand() {\n"
    "  case $line[1] in\n"
    "  server)\n"
    "    _arguments \\\n");

  /* Server options - use unified display API matching help system */
  size_t server_count = 0;
  const option_descriptor_t *server_opts =
      options_registry_get_for_display(MODE_SERVER, false, &server_count);

  if (server_opts) {
    for (size_t i = 0; i < server_count; i++) {
      zsh_write_option(output, &server_opts[i]);
    }
    SAFE_FREE(server_opts);
  }

  fprintf(output,
    "      && return 0\n"
    "    ;;\n"
    "  client)\n"
    "    _arguments \\\n");

  /* Client options - use unified display API matching help system */
  size_t client_count = 0;
  const option_descriptor_t *client_opts =
      options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  if (client_opts) {
    for (size_t i = 0; i < client_count; i++) {
      zsh_write_option(output, &client_opts[i]);
    }
    SAFE_FREE(client_opts);
  }

  fprintf(output,
    "      && return 0\n"
    "    ;;\n"
    "  mirror)\n"
    "    _arguments \\\n");

  /* Mirror options - use unified display API matching help system */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts =
      options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  if (mirror_opts) {
    for (size_t i = 0; i < mirror_count; i++) {
      zsh_write_option(output, &mirror_opts[i]);
    }
    SAFE_FREE(mirror_opts);
  }

  fprintf(output,
    "      && return 0\n"
    "    ;;\n"
    "  discovery-service)\n"
    "    _arguments \\\n");

  /* Discovery-service options */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVER, false, &discovery_svc_count);

  if (discovery_svc_opts) {
    for (size_t i = 0; i < discovery_svc_count; i++) {
      zsh_write_option(output, &discovery_svc_opts[i]);
    }
    SAFE_FREE(discovery_svc_opts);
  }

  fprintf(output,
    "      && return 0\n"
    "    ;;\n"
    "  esac\n"
    "}\n"
    "\n"
    "_ascii_chat \"$@\"\n");

  return ASCIICHAT_OK;
}
