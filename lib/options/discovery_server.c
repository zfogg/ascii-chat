/**
 * @file discovery_server.c
 * @ingroup options
 * @brief Discovery server mode option parsing and help text
 *
 * Discovery server-specific command-line argument parsing with support for:
 * - Dual-stack binding (0-2 IPv4/IPv6 addresses)
 * - Database configuration
 * - Identity key management
 * - Logging configuration
 */

#include "options/discovery_server.h"
#include "options/builder.h"
#include "options/common.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/validation.h"
#include "util/ip.h"
#include "util/path.h"
#include "version.h"

#ifdef _WIN32
#include "platform/windows/getopt.h"
#else
#include <getopt.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// ACDS Option Parsing
// ============================================================================

asciichat_error_t parse_discovery_server_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config = options_preset_acds("ascii-chat discovery-server", "ascii-chat discovery service");
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

  // Check for unexpected remaining arguments
  if (remaining_argc > 0) {
    (void)fprintf(stderr, "Error: Unexpected arguments after options:\n");
    for (int i = 0; i < remaining_argc; i++) {
      (void)fprintf(stderr, "  %s\n", remaining_argv[i]);
    }
    options_config_destroy(config);
    return option_error_invalid();
  }

  // Set default paths if not specified
  if (opts->acds_database_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      options_config_destroy(config);
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for database path");
    }
    snprintf(opts->acds_database_path, sizeof(opts->acds_database_path), "%sacds.db", config_dir);
    SAFE_FREE(config_dir);
  }

  if (opts->acds_key_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      options_config_destroy(config);
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for identity key path");
    }
    snprintf(opts->acds_key_path, sizeof(opts->acds_key_path), "%sacds_identity", config_dir);
    SAFE_FREE(config_dir);
  }

  options_config_destroy(config);
  return ASCIICHAT_OK;
}

// ============================================================================
// ACDS Usage/Help Output
// ============================================================================

void usage_acds(FILE *desc) {
  // Get config with program name and description
  const options_config_t *config = options_preset_acds("ascii-chat discovery-server", "ascii-chat discovery service");
  if (!config) {
    (void)fprintf(desc, "Error: Failed to create options config\n");
    return;
  }

  (void)fprintf(desc, "%s - %s\n\n", config->program_name, config->description);
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  %s [options...]\n\n", config->program_name);

  // Print positional argument examples programmatically if they exist
  if (config->num_positional_args > 0) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[0];
    if (pos_arg->section_heading && pos_arg->examples && pos_arg->num_examples > 0) {
      (void)fprintf(desc, "%s:\n", pos_arg->section_heading);
      for (size_t i = 0; i < pos_arg->num_examples; i++) {
        (void)fprintf(desc, "  %s\n", pos_arg->examples[i]);
      }
      (void)fprintf(desc, "\n");
    }
  }

  // Generate options from builder configuration
  options_config_print_usage(config, desc);

  // Clean up the config
  options_config_destroy(config);
}
