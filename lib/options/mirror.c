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
#include "util/string.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Mirror Option Parsing
// ============================================================================

asciichat_error_t parse_mirror_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config = options_preset_mirror("ascii-chat mirror", "render ascii on localhost with no network or audio");
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
// Mirror Usage Text
// ============================================================================

void usage_mirror(FILE *desc) {
  // Get config with program name and description
  const options_config_t *config = options_preset_mirror("ascii-chat mirror", "render ascii on localhost with no network or audio");
  if (!config) {
    (void)fprintf(desc, "Error: Failed to create options config\n");
    return;
  }

  // Print program name and description (color mode name magenta)
  const char *space = strchr(config->program_name, ' ');
  if (space) {
    int binary_len = space - config->program_name;
    (void)fprintf(desc, "%.*s %s - %s\n\n", binary_len, config->program_name,
                  colored_string(LOG_COLOR_FATAL, space + 1), config->description);
  } else {
    (void)fprintf(desc, "%s - %s\n\n", config->program_name, config->description);
  }

  // Print project links
  print_project_links(desc);
  (void)fprintf(desc, "\n");

  // Print USAGE section first
  options_config_print_usage_section(config, desc);

  // Print everything after USAGE (EXAMPLES, OPTIONS) with global max width
  // Note: MODE-OPTIONS only appears in binary-level help, not mode-specific help
  options_config_print_options_sections_with_width(config, desc, 0);

  // Clean up the config
  options_config_destroy(config);
}
