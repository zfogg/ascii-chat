/**
 * @file discovery.c
 * @ingroup options
 * @brief Discovery mode option parsing and help text
 *
 * Discovery-specific command-line argument parsing for session-based video chat
 * with automatic host negotiation. This mode allows participants to join a session
 * and dynamically become the host based on NAT quality.
 *
 * Supported features:
 * - Session discovery via ACDS
 * - Dynamic host negotiation
 * - Local webcam capture
 * - ASCII art rendering
 * - Audio streaming
 * - Terminal color modes
 * - Palette customization
 */

#include "options/discovery.h"
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
// Discovery Option Parsing
// ============================================================================

asciichat_error_t parse_discovery_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config =
      options_preset_discovery("ascii-chat", "P2P video chat with automatic host negotiation");
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

  // Session string is optional:
  // - If provided: join existing session
  // - If not provided: start new session (ACDS will generate session string)
  if (remaining_argc > 0) {
    SAFE_SNPRINTF(opts->session_string, sizeof(opts->session_string), "%s", remaining_argv[0]);

    if (remaining_argc > 1) {
      (void)fprintf(stderr, "Error: Unexpected arguments after session string:\n");
      for (int i = 1; i < remaining_argc; i++) {
        (void)fprintf(stderr, "  %s\n", remaining_argv[i]);
      }
      options_config_destroy(config);
      return option_error_invalid();
    }
  }
  // No session string = start new session (opts->session_string remains empty)

  options_config_destroy(config);
  return ASCIICHAT_OK;
}

// ============================================================================
// Discovery Usage Text
// ============================================================================

void usage_discovery(FILE *desc) {
  const options_config_t *config =
      options_preset_discovery("ascii-chat", "P2P video chat with automatic host negotiation");
  if (!config) {
    (void)fprintf(desc, "Error: Failed to create options config\n");
    return;
  }

  // For discovery mode, show BINARY options FIRST, then mode-specific options
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

  // Print binary-level options first for discovery mode
  options_print_help_for_mode(config, (asciichat_mode_t)-1, config->program_name, config->description, desc);

  // Clean up
  options_config_destroy(config);
}
