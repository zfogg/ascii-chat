/**
 * @file fish.c
 * @brief Fish shell completion script generator
 * @ingroup options
 */

#include <string.h>
#include <stdio.h>
#include "options/completions/fish.h"
#include "options/registry.h"
#include "options/enums.h"
#include "common.h"

static void fish_write_option(FILE *output, const option_descriptor_t *opt, const char *condition)
{
  if (!opt) {
    return;
  }

  if (opt->short_name != '\0') {
    fprintf(output, "complete -c ascii-chat %s -s %c -d '%s'\n", condition, opt->short_name, opt->help_text);
  }
  fprintf(output, "complete -c ascii-chat %s -l %s -d '%s'\n", condition, opt->long_name, opt->help_text);
}

asciichat_error_t completions_generate_fish(FILE *output)
{
  if (!output) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Output stream cannot be NULL");
  }

  fprintf(output,
    "# Fish completion script for ascii-chat\n"
    "# Generated from options registry - DO NOT EDIT MANUALLY\n"
    "\n"
    "complete -c ascii-chat -f\n"
    "\n"
    "function __fish_ascii_chat_using_mode\n"
    "    set -l cmd (commandline -opc)\n"
    "    for arg in $cmd\n"
    "        if contains -- $arg server client mirror\n"
    "            echo $arg\n"
    "            return 0\n"
    "        end\n"
    "    end\n"
    "    return 1\n"
    "end\n"
    "\n"
    "function __fish_ascii_chat_mode_is\n"
    "    test (string match -q $argv[1] (command __fish_ascii_chat_using_mode))\n"
    "end\n"
    "\n"
    "function __fish_ascii_chat_no_mode\n"
    "    not __fish_ascii_chat_using_mode > /dev/null\n"
    "end\n"
    "\n"
    "# Modes\n"
    "complete -c ascii-chat -n __fish_ascii_chat_no_mode -a server -d 'Run a video chat server'\n"
    "complete -c ascii-chat -n __fish_ascii_chat_no_mode -a client -d 'Connect to a video chat server'\n"
    "complete -c ascii-chat -n __fish_ascii_chat_no_mode -a mirror -d 'View webcam locally without network'\n"
    "\n");

  /* Binary options - use unified display API matching help system */
  size_t binary_count = 0;
  const option_descriptor_t *binary_opts =
      options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

  fprintf(output, "# Binary-level options (same as 'ascii-chat --help')\n");
  if (binary_opts) {
    for (size_t i = 0; i < binary_count; i++) {
      fish_write_option(output, &binary_opts[i], "");
    }
    SAFE_FREE(binary_opts);
  }
  fprintf(output, "\n");

  /* Server options - use unified display API matching help system */
  size_t server_count = 0;
  const option_descriptor_t *server_opts =
      options_registry_get_for_display(MODE_SERVER, false, &server_count);

  fprintf(output, "# Server options (same as 'ascii-chat server --help')\n");
  if (server_opts) {
    for (size_t i = 0; i < server_count; i++) {
      fish_write_option(output, &server_opts[i], "-n '__fish_seen_subcommand_from server'");
    }
    SAFE_FREE(server_opts);
  }
  fprintf(output, "\n");

  /* Client options - use unified display API matching help system */
  size_t client_count = 0;
  const option_descriptor_t *client_opts =
      options_registry_get_for_display(MODE_CLIENT, false, &client_count);

  fprintf(output, "# Client options (same as 'ascii-chat client --help')\n");
  if (client_opts) {
    for (size_t i = 0; i < client_count; i++) {
      fish_write_option(output, &client_opts[i], "-n '__fish_seen_subcommand_from client'");
    }
    SAFE_FREE(client_opts);
  }
  fprintf(output, "\n");

  /* Mirror options - use unified display API matching help system */
  size_t mirror_count = 0;
  const option_descriptor_t *mirror_opts =
      options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  fprintf(output, "# Mirror options (same as 'ascii-chat mirror --help')\n");
  if (mirror_opts) {
    for (size_t i = 0; i < mirror_count; i++) {
      fish_write_option(output, &mirror_opts[i], "-n '__fish_seen_subcommand_from mirror'");
    }
    SAFE_FREE(mirror_opts);
  }

  return ASCIICHAT_OK;
}
