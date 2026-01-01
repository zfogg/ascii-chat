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

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/validation.h"
#include "util/ip.h"
#include "util/parsing.h"
#include "util/password.h"
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
  const options_config_t *config = options_preset_server();
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

  return ASCIICHAT_OK;
}

// ============================================================================
// Server Usage Text
// ============================================================================

void usage_server(FILE *desc) {
  // Print custom bind address help first
  (void)fprintf(desc, "ascii-chat server - Run as multi-client video chat server\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat server [bind-address] [bind-address6] [options...]\n\n");
  (void)fprintf(desc, "BIND ADDRESS FORMATS:\n");
  (void)fprintf(desc, "  (none)                     bind to 127.0.0.1 and ::1 (localhost)\n");
  (void)fprintf(desc, "  192.168.1.100              bind to IPv4 address only\n");
  (void)fprintf(desc, "  ::                         bind to all IPv6 addresses\n");
  (void)fprintf(desc, "  0.0.0.0                    bind to all IPv4 addresses\n");
  (void)fprintf(desc, "  192.168.1.100 ::           bind to IPv4 and IPv6 (dual-stack)\n\n");

  // Generate options from builder configuration
  const options_config_t *config = options_preset_server();
  if (config) {
    options_config_print_usage(config, desc);
  }
}
