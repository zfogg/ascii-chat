/**
 * @file fish.c
 * @brief Fish shell completion script generator
 * @ingroup options
 */

#include <string.h>
#include <stdio.h>
#include <ascii-chat/options/completions/fish.h>
#include <ascii-chat/options/registry.h>
#include <ascii-chat/options/enums.h>
#include <ascii-chat/common.h>

/**
 * Escape help text for fish shell completions
 * Fish uses single quotes for the help text, so single quotes need to be escaped
 */
static void fish_escape_help(FILE *output, const char *text) {
  if (!text) {
    return;
  }

  for (const char *p = text; *p; p++) {
    if (*p == '\'') {
      // Escape single quotes by ending quote, adding escaped quote, starting quote again
      fprintf(output, "'\\''");
    } else if (*p == '\n' || *p == '\t') {
      // Convert newlines and tabs to spaces
      fprintf(output, " ");
    } else {
      fputc(*p, output);
    }
  }
}

static void fish_write_option(FILE *output, const option_descriptor_t *opt, const char *condition) {
  if (!opt) {
    return;
  }

  // Get completion metadata for this option
  const option_metadata_t *meta = options_registry_get_metadata(opt->long_name);

  // Determine if we should exclude file completion
  bool exclusive = false;

  if (meta) {
    if (meta->input_type == OPTION_INPUT_ENUM && meta->enum_values && meta->enum_count > 0) {
      exclusive = true;
      // Enum values as completions
      // Output short option once with first value
      if (opt->short_name != '\0') {
        fprintf(output, "complete -c ascii-chat %s -s %c -x -a '%s' -d '", condition, opt->short_name,
                meta->enum_values[0]);
        fish_escape_help(output, opt->help_text);
        fprintf(output, "'\n");
      }
      // Output long option with all values
      for (size_t i = 0; i < meta->enum_count; i++) {
        fprintf(output, "complete -c ascii-chat %s -l %s -x -a '%s' -d '", condition, opt->long_name,
                meta->enum_values[i]);
        fish_escape_help(output, opt->help_text);
        fprintf(output, "'\n");
      }
      return;
    } else if (meta->input_type == OPTION_INPUT_FILEPATH) {
      // File paths use default fish file completion
      exclusive = false;
    } else if (meta->examples && meta->examples[0] != NULL) {
      exclusive = true;
      // Example values as completions (practical values, higher priority than calculated ranges)
      // Output short option once with first example
      if (opt->short_name != '\0') {
        fprintf(output, "complete -c ascii-chat %s -s %c -x -a '%s' -d '", condition, opt->short_name,
                meta->examples[0]);
        fish_escape_help(output, opt->help_text);
        fprintf(output, "'\n");
      }
      // Output long option with all examples
      for (size_t i = 0; meta->examples[i] != NULL; i++) {
        fprintf(output, "complete -c ascii-chat %s -l %s -x -a '%s' -d '", condition, opt->long_name,
                meta->examples[i]);
        fish_escape_help(output, opt->help_text);
        fprintf(output, "'\n");
      }
      return;
    } else if (meta->input_type == OPTION_INPUT_NUMERIC) {
      exclusive = true;
      // Numeric range - suggest min, middle, max values
      if (opt->short_name != '\0') {
        fprintf(output, "complete -c ascii-chat %s -s %c -x -a '%d' -d 'numeric (%d-%d)'\n", condition, opt->short_name,
                meta->numeric_range.min, meta->numeric_range.min, meta->numeric_range.max);
      }
      fprintf(output, "complete -c ascii-chat %s -l %s -x -a '%d' -d 'numeric (%d-%d)'\n", condition, opt->long_name,
              meta->numeric_range.min, meta->numeric_range.min, meta->numeric_range.max);
      if (meta->numeric_range.max > meta->numeric_range.min) {
        int middle = (meta->numeric_range.min + meta->numeric_range.max) / 2;
        fprintf(output, "complete -c ascii-chat %s -l %s -x -a '%d' -d 'numeric (middle)'\n", condition, opt->long_name,
                middle);
        fprintf(output, "complete -c ascii-chat %s -l %s -x -a '%d' -d 'numeric (max)'\n", condition, opt->long_name,
                meta->numeric_range.max);
      }
      return;
    }
  }

  // Basic completion without values
  if (opt->short_name != '\0') {
    if (exclusive) {
      fprintf(output, "complete -c ascii-chat %s -s %c -x -d '", condition, opt->short_name);
      fish_escape_help(output, opt->help_text);
      fprintf(output, "'\n");
    } else {
      fprintf(output, "complete -c ascii-chat %s -s %c -d '", condition, opt->short_name);
      fish_escape_help(output, opt->help_text);
      fprintf(output, "'\n");
    }
  }
  if (exclusive) {
    fprintf(output, "complete -c ascii-chat %s -l %s -x -d '", condition, opt->long_name);
    fish_escape_help(output, opt->help_text);
    fprintf(output, "'\n");
  } else {
    fprintf(output, "complete -c ascii-chat %s -l %s -d '", condition, opt->long_name);
    fish_escape_help(output, opt->help_text);
    fprintf(output, "'\n");
  }
}

asciichat_error_t completions_generate_fish(FILE *output) {
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
  const option_descriptor_t *binary_opts = options_registry_get_for_display(MODE_DISCOVERY, true, &binary_count);

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
  const option_descriptor_t *server_opts = options_registry_get_for_display(MODE_SERVER, false, &server_count);

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
  const option_descriptor_t *client_opts = options_registry_get_for_display(MODE_CLIENT, false, &client_count);

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
  const option_descriptor_t *mirror_opts = options_registry_get_for_display(MODE_MIRROR, false, &mirror_count);

  fprintf(output, "# Mirror options (same as 'ascii-chat mirror --help')\n");
  if (mirror_opts) {
    for (size_t i = 0; i < mirror_count; i++) {
      fish_write_option(output, &mirror_opts[i], "-n '__fish_seen_subcommand_from mirror'");
    }
    SAFE_FREE(mirror_opts);
  }
  fprintf(output, "\n");

  /* Discovery-service options */
  size_t discovery_svc_count = 0;
  const option_descriptor_t *discovery_svc_opts =
      options_registry_get_for_display(MODE_DISCOVERY_SERVICE, false, &discovery_svc_count);

  fprintf(output, "# Discovery-service options (same as 'ascii-chat discovery-service --help')\n");
  if (discovery_svc_opts) {
    for (size_t i = 0; i < discovery_svc_count; i++) {
      fish_write_option(output, &discovery_svc_opts[i], "-n '__fish_seen_subcommand_from discovery-service'");
    }
    SAFE_FREE(discovery_svc_opts);
  }

  return ASCIICHAT_OK;
}
