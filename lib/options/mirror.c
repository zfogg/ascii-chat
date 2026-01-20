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
  const options_config_t *config = options_preset_mirror("ascii-chat mirror", "view local webcam as ascii art");
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

  options_config_destroy(config);
  return ASCIICHAT_OK;
}

// ============================================================================
// Mirror Usage Text
// ============================================================================

void usage_mirror(FILE *desc) {
  // Get config with program name and description
  const options_config_t *config = options_preset_mirror("ascii-chat mirror", "view local webcam as ascii art");
  if (!config) {
    (void)fprintf(desc, "Error: Failed to create options config\n");
    return;
  }

  // Print program name and description
  (void)fprintf(desc, "%s - %s\n\n", config->program_name, config->description);

  // Print USAGE header with color
  (void)fprintf(desc, "%sUSAGE:%s\n", log_level_color(LOG_COLOR_DEBUG), log_level_color(LOG_COLOR_RESET));

  // Get color codes once to avoid rotating buffer issues
  const char *magenta = log_level_color(LOG_COLOR_FATAL);
  const char *yellow = log_level_color(LOG_COLOR_WARN);
  const char *reset_color = log_level_color(LOG_COLOR_RESET);

  // Print USAGE line with colored components: binary (default), mode (magenta), options (yellow)
  (void)fprintf(desc, "  ascii-chat %s%s%s %s[options...]%s\n\n",
      magenta, "mirror", reset_color,    // mode in magenta
      yellow, reset_color);               // [options...] in yellow

  // Generate options from builder configuration
  options_config_print_usage(config, desc);

  // Clean up the config
  options_config_destroy(config);
}
