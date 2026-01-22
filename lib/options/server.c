/**
 * @file server.c
 * @ingroup options
 * @brief Server mode option parsing and help text
 *
 * Server-specific command-line argument parsing with support for:
 * - Dual-stack binding (0-2 IPv4/IPv6 addresses)
 * - Client authentication and access control
 * - Connection limits
 * - Network compression
 * - Audio mixer control
 */

#include "options/server.h"
#include "options/builder.h"
#include "options/common.h"
#include "options/layout.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/validation.h"
#include "platform/terminal.h"
#include "util/ip.h"
#include "util/parsing.h"
#include "util/password.h"
#include "util/string.h"
#include "util/utf8.h"
#include "video/ascii.h"

#ifdef _WIN32
#include "platform/windows/getopt.h"
#else
#include <getopt.h>
#endif

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Server Option Parsing
// ============================================================================

asciichat_error_t parse_server_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config =
      options_preset_server("ascii-chat server", "host a server mixing video and audio for ascii-chat clients");
  if (!config) {
    return SET_ERRNO(ERROR_CONFIG, "Failed to create options configuration");
  }
  int remaining_argc;
  char **remaining_argv;

  // Apply defaults from preset before parsing command-line args
  asciichat_error_t defaults_result = options_config_set_defaults(config, opts);
  if (defaults_result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return defaults_result;
  }

  asciichat_error_t result = options_config_parse(config, argc, argv, opts, &remaining_argc, &remaining_argv);
  if (result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return result;
  }

  // Validate options (check dependencies, conflicts, etc.)
  result = validate_options_and_report(config, opts);
  if (result != ASCIICHAT_OK) {
    options_config_destroy(config);
    return result;
  }

  // Check for unexpected remaining arguments
  if (remaining_argc > 0) {
    (void)fprintf(stderr, "Error: Unexpected arguments after options:\n");
    for (int i = 0; i < remaining_argc; i++) {
      (void)fprintf(stderr, "  %s\n", remaining_argv[i]);
    }
    options_config_destroy(config);
    return option_error_invalid();
  }

  options_config_destroy(config);
  return ASCIICHAT_OK;
}

// ============================================================================
// Server Usage Text
// ============================================================================

void usage_server(FILE *desc) {
  // Get config with program name and description
  const options_config_t *config =
      options_preset_server("ascii-chat server", "host a server mixing video and audio for ascii-chat clients");
  if (!config) {
    (void)fprintf(desc, "Error: Failed to create options config\n");
    return;
  }

  // Use unified help printing function
  options_print_help_for_mode(config, MODE_SERVER, config->program_name, config->description, desc);

  // Clean up the config
  options_config_destroy(config);
}
