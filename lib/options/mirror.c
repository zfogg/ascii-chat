/**
 * @file mirror.c
 * @ingroup options
 * @brief Mirror mode option parsing and help text
 *
 * Mirror-specific command-line argument parsing for standalone local webcam display.
 * This is a subset of client options with no networking, audio, or encryption.
 *
 * Supported features:
 * - Local webcam capture
 * - ASCII art rendering
 * - Terminal color modes
 * - Palette customization
 * - Snapshot mode
 */

#include "options/mirror.h"
#include "options/builder.h"
#include "options/common.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/validation.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Mirror Option Parsing
// ============================================================================

asciichat_error_t parse_mirror_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config = options_preset_mirror();
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
// Mirror Usage Text
// ============================================================================

void usage_mirror(FILE *desc) {
  (void)fprintf(desc, "ascii-chat mirror - View local webcam as ASCII art (no server)\n\n");
  (void)fprintf(desc, "USAGE:\n");
  (void)fprintf(desc, "  ascii-chat mirror [options...]\n\n");

  // Generate options from builder configuration
  const options_config_t *config = options_preset_mirror();
  if (config) {
    options_config_print_usage(config, desc);
  }
}
