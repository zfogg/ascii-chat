/**
 * @file discovery_service.c
 * @ingroup options
 * @brief Discovery service mode option parsing and help text
 *
 * Discovery service-specific command-line argument parsing with support for:
 * - Dual-stack binding (0-2 IPv4/IPv6 addresses)
 * - Database configuration
 * - Identity key management
 * - Logging configuration
 */

#include "options/discovery_service.h"
#include "options/builder.h"
#include "options/common.h"
#include "options/layout.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/validation.h"
#include "util/ip.h"
#include "util/path.h"
#include "util/string.h"
#include "util/utf8.h"
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

asciichat_error_t parse_discovery_service_options(int argc, char **argv, options_t *opts) {
  const options_config_t *config = options_preset_acds("ascii-chat discovery-service", "secure p2p session signalling");
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
  if (opts->discovery_database_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      options_config_destroy(config);
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for database path");
    }
    snprintf(opts->discovery_database_path, sizeof(opts->discovery_database_path), "%sdiscovery.db", config_dir);
    SAFE_FREE(config_dir);
  }

  if (opts->discovery_key_path[0] == '\0') {
    char *config_dir = get_config_dir();
    if (!config_dir) {
      options_config_destroy(config);
      return SET_ERRNO(ERROR_CONFIG, "Failed to get config directory for identity key path");
    }
    snprintf(opts->discovery_key_path, sizeof(opts->discovery_key_path), "%sdiscovery_identity", config_dir);
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
  const options_config_t *config = options_preset_acds("ascii-chat discovery-service", "secure p2p session signalling");
  if (!config) {
    (void)fprintf(desc, "Error: Failed to create options config\n");
    return;
  }

  // Print program name and description
  (void)fprintf(desc, "%s - %s\n\n", config->program_name, config->description);

  // Detect terminal width for layout
  int term_width = 80;
  const char *cols_env = getenv("COLUMNS");
  if (cols_env) {
    int cols = atoi(cols_env);
    if (cols > 40)
      term_width = cols;
  }

  // Print positional argument examples with layout formatting
  if (config->num_positional_args > 0) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[0];
    if (pos_arg->section_heading && pos_arg->examples && pos_arg->num_examples > 0) {
      // Print section heading using colored_string for consistency
      (void)fprintf(desc, "%s\n", colored_string(LOG_COLOR_DEBUG, pos_arg->section_heading));

      for (size_t i = 0; i < pos_arg->num_examples; i++) {
        // Print bind address examples with proper alignment
        const char *example = pos_arg->examples[i];
        // Find the first part (before multiple spaces) and description (after multiple spaces)
        const char *p = example;
        const char *desc_start = NULL;

        // Skip leading spaces
        while (*p == ' ')
          p++;
        const char *first_part = p;

        // Find end of first part (look for 2+ spaces)
        while (*p && !(*p == ' ' && *(p + 1) == ' '))
          p++;
        int first_len_bytes = (int)(p - first_part);

        // Skip spaces to find description
        while (*p == ' ')
          p++;
        if (*p) {
          desc_start = p;
        }

        // Build the first part with colored_string for consistency
        char colored_first_part[256];
        snprintf(colored_first_part, sizeof(colored_first_part), "%.*s", first_len_bytes, first_part);
        fprintf(desc, "  %s", colored_string(LOG_COLOR_INFO, colored_first_part));
        // Calculate display width of first part (accounts for UTF-8 multi-byte chars)
        int first_display_width = utf8_display_width_n(first_part, first_len_bytes);
        // Pad to column 30 for description alignment
        int padding = LAYOUT_DESCRIPTION_START_COL - (2 + first_display_width);
        if (padding > 0) {
          for (int j = 0; j < padding; j++)
            fprintf(desc, " ");
        } else {
          fprintf(desc, " ");
        }
        // Print description with wrapping using layout function
        if (desc_start) {
          layout_print_wrapped_description(desc, desc_start, LAYOUT_DESCRIPTION_START_COL, term_width);
        }
        fprintf(desc, "\n");
      }
      (void)fprintf(desc, "\n");
    }
  }

  // Generate options from builder configuration
  options_config_print_usage(config, desc);

  // Print project links
  print_project_links(desc);

  // Clean up the config
  options_config_destroy(config);
}
