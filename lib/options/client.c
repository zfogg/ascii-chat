/**
 * @file client.c
 * @ingroup options
 * @brief Client mode option parsing and help text
 *
 * Client-specific command-line argument parsing with support for:
 * - Server connection (positional address[:port])
 * - Webcam configuration
 * - Audio capture/playback
 * - Display settings
 * - Cryptographic authentication
 * - Network compression
 * - Reconnection behavior
 */

#include "options/client.h"
#include "options/builder.h"
#include "options/common.h"

#include "asciichat_errno.h"
#include "audio/audio.h"
#include "common.h"
#include "crypto/crypto.h"
#include "log/logging.h"
#include "options/levenshtein.h"
#include "options/options.h"
#include "options/validation.h"
#include "platform/system.h"
#include "util/ip.h"
#include "util/parsing.h"
#include "util/password.h"
#include "video/ascii.h"
#include "video/webcam/webcam.h"

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
// Client Option Parsing
// ============================================================================

asciichat_error_t parse_client_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config = options_preset_client();
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
// Client Usage Text
// ============================================================================

void usage_client(FILE *desc) {
  // Print custom address format help first
  (void)fprintf(desc, "ascii-chat client - Connect to ascii-chat server\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat client [address][:port] [options...]\n\n");
  (void)fprintf(desc, "ADDRESS FORMATS:\n");
  (void)fprintf(desc, "  (none)                     connect to localhost:27224\n");
  (void)fprintf(desc, "  hostname                   connect to hostname:27224\n");
  (void)fprintf(desc, "  hostname:port              connect to hostname:port\n");
  (void)fprintf(desc, "  192.168.1.1                connect to IPv4:27224\n");
  (void)fprintf(desc, "  192.168.1.1:8080           connect to IPv4:port\n");
  (void)fprintf(desc, "  ::1                        connect to IPv6:27224\n");
  (void)fprintf(desc, "  [::1]:8080                 connect to IPv6:port (brackets required with port)\n\n");

  // Generate options from builder configuration
  const options_config_t *config = options_preset_client();
  if (config) {
    options_config_print_usage(config, desc);
  }
}
