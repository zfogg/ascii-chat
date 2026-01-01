/**
 * @file acds.c
 * @ingroup options
 * @brief ACDS mode option parsing and help text
 *
 * ACDS-specific command-line argument parsing with support for:
 * - Dual-stack binding (0-2 IPv4/IPv6 addresses)
 * - Database configuration
 * - Identity key management
 * - Logging configuration
 */

#include "options/acds.h"
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
// ACDS Option Globals
// ============================================================================

int opt_acds_port = 27225;
char opt_acds_database_path[OPTIONS_BUFF_SIZE] = "";
char opt_acds_key_path[OPTIONS_BUFF_SIZE] = "";

// ============================================================================
// ACDS Option Parsing
// ============================================================================

asciichat_error_t parse_acds_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config = options_preset_acds();
  int remaining_argc;
  char **remaining_argv;

  asciichat_error_t result = options_config_parse(config, argc, argv, opts, &remaining_argc, &remaining_argv);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  // Check for unexpected remaining arguments
  if (remaining_argc > 0) {
    (void)fprintf(stderr, "Error: Unexpected arguments after options:\n");
    for (int i = 0; i < remaining_argc; i++) {
      (void)fprintf(stderr, "  %s\n", remaining_argv[i]);
    }
    return option_error_invalid();
  }

  // Set default paths for ACDS-specific globals if not specified
  if (opt_acds_database_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory");
    }
    snprintf(opt_acds_database_path, sizeof(opt_acds_database_path), "%sacds.db", config_dir);
    free(config_dir);
  }

  if (opt_acds_key_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory");
    }
    snprintf(opt_acds_key_path, sizeof(opt_acds_key_path), "%sacds_identity", config_dir);
    free(config_dir);
  }

  return ASCIICHAT_OK;
}
