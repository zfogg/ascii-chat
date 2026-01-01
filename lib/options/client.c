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
#include "options/common.h"
#include "options/presets.h"

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
  (void)fprintf(desc, "ascii-chat - client options\n\n");
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
  (void)fprintf(desc, "OPTIONS:\n");
  (void)fprintf(desc, USAGE_HELP_LINE);
  (void)fprintf(desc, USAGE_PORT_CLIENT_LINE);
  (void)fprintf(desc, USAGE_RECONNECT_LINE);
  (void)fprintf(desc, USAGE_WIDTH_LINE);
  (void)fprintf(desc, USAGE_HEIGHT_LINE);
  (void)fprintf(desc, USAGE_WEBCAM_INDEX_LINE);
  (void)fprintf(desc, USAGE_LIST_WEBCAMS_LINE);
  (void)fprintf(desc, USAGE_LIST_MICROPHONES_LINE);
  (void)fprintf(desc, USAGE_LIST_SPEAKERS_LINE);
  (void)fprintf(desc, USAGE_MICROPHONE_INDEX_LINE);
  (void)fprintf(desc, USAGE_SPEAKERS_INDEX_LINE);
  (void)fprintf(desc, USAGE_WEBCAM_FLIP_LINE);
  (void)fprintf(desc, USAGE_TEST_PATTERN_CLIENT_LINE);
#ifdef _WIN32
  (void)fprintf(desc, USAGE_FPS_WIN_LINE);
#else
  (void)fprintf(desc, USAGE_FPS_UNIX_LINE);
#endif
  (void)fprintf(desc, USAGE_COLOR_MODE_LINE);
  (void)fprintf(desc, USAGE_SHOW_CAPABILITIES_LINE);
  (void)fprintf(desc, USAGE_UTF8_LINE);
  (void)fprintf(desc, USAGE_RENDER_MODE_LINE);
  (void)fprintf(desc, USAGE_PALETTE_LINE);
  (void)fprintf(desc, USAGE_PALETTE_CHARS_LINE);
  (void)fprintf(desc, USAGE_AUDIO_LINE);
  (void)fprintf(desc, USAGE_AUDIO_ANALYSIS_LINE);
  (void)fprintf(desc, USAGE_NO_AUDIO_PLAYBACK_LINE);
  (void)fprintf(desc, USAGE_STRETCH_LINE);
  (void)fprintf(desc, USAGE_SNAPSHOT_LINE);
  (void)fprintf(desc, USAGE_SNAPSHOT_DELAY_LINE, (double)SNAPSHOT_DELAY_DEFAULT);
  (void)fprintf(desc, USAGE_STRIP_ANSI_LINE);
  (void)fprintf(desc, USAGE_ENCRYPT_LINE);
  (void)fprintf(desc, USAGE_KEY_CLIENT_LINE);
  (void)fprintf(desc, USAGE_PASSWORD_LINE);
  (void)fprintf(desc, USAGE_KEYFILE_LINE);
  (void)fprintf(desc, USAGE_NO_ENCRYPT_LINE);
  (void)fprintf(desc, USAGE_SERVER_KEY_LINE);
}
