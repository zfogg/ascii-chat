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
#include "options/common.h"
#include "options/presets.h"

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
  (void)fprintf(desc, "ascii-chat - server options\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat server [address1] [address2] [options...]\n\n");
  (void)fprintf(desc, "BIND ADDRESSES (Positional Arguments):\n");
  (void)fprintf(desc, "  0 arguments: Bind to 127.0.0.1 and ::1 (localhost dual-stack)\n");
  (void)fprintf(desc, "  1 argument:  Bind only to this IPv4 OR IPv6 address\n");
  (void)fprintf(desc, "  2 arguments: Bind to both (must be one IPv4 AND one IPv6, order-independent)\n\n");
  (void)fprintf(desc, "EXAMPLES:\n");
  (void)fprintf(desc, "  ascii-chat server                    # Localhost only (127.0.0.1 + ::1)\n");
  (void)fprintf(desc, "  ascii-chat server 0.0.0.0            # All IPv4 interfaces\n");
  (void)fprintf(desc, "  ascii-chat server ::                 # All IPv6 interfaces\n");
  (void)fprintf(desc, "  ascii-chat server 0.0.0.0 ::         # All interfaces (dual-stack)\n");
  (void)fprintf(desc, "  ascii-chat server 192.168.1.100 ::1  # Specific IPv4 + localhost IPv6\n\n");
  (void)fprintf(desc, "OPTIONS:\n");
  (void)fprintf(desc, USAGE_HELP_LINE);
  (void)fprintf(desc, USAGE_PORT_LINE, 27224);
  (void)fprintf(desc, USAGE_MAX_CLIENTS_LINE);
  (void)fprintf(desc, USAGE_ENCRYPT_LINE);
  (void)fprintf(desc, USAGE_KEY_SERVER_LINE);
  (void)fprintf(desc, USAGE_PASSWORD_LINE);
  (void)fprintf(desc, USAGE_KEYFILE_LINE);
  (void)fprintf(desc, USAGE_NO_ENCRYPT_LINE);
  (void)fprintf(desc, USAGE_CLIENT_KEYS_LINE);
  (void)fprintf(desc, USAGE_COMPRESSION_LEVEL_LINE);
  (void)fprintf(desc, USAGE_NO_COMPRESS_LINE);
  (void)fprintf(desc, USAGE_ENCODE_AUDIO_LINE);
  (void)fprintf(desc, USAGE_NO_ENCODE_AUDIO_LINE);
  (void)fprintf(desc, USAGE_NO_AUDIO_MIXER_LINE);
}
