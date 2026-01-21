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
#include "options/layout.h"

#include "asciichat_errno.h"
#include "common.h"
#include "log/logging.h"
#include "options/options.h"
#include "options/validation.h"
#include "platform/terminal.h"
#include "util/ip.h"
#include "util/parsing.h"
#include "util/password.h"
#include "util/string.h"
#include "util/utf8.h"
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
  const options_config_t *config =
      options_preset_server("ascii-chat server", "host a server mixing video and audio for ascii-chat clients");
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
// Server Usage Text
// ============================================================================

void usage_server(FILE *desc) {
  // Get config with program name and description
  const options_config_t *config =
      options_preset_server("ascii-chat server", "host a server mixing video and audio for ascii-chat clients");
  if (!config) {
    (void)fprintf(desc, "Error: Failed to create options config\n");
    return;
  }

  // Print program name and description
  (void)fprintf(desc, "%s - %s\n\n", config->program_name, config->description);

  // Detect terminal width for layout
  int term_width = 80;
  terminal_size_t term_size;
  if (terminal_get_size(&term_size) == ASCIICHAT_OK && term_size.cols > 40) {
    term_width = term_size.cols;
  } else {
    // Fallback to COLUMNS environment variable if terminal detection fails
    const char *cols_env = SAFE_GETENV("COLUMNS");
    if (cols_env) {
      int cols = atoi(cols_env);
      if (cols > 40)
        term_width = cols;
    }
  }

  // Calculate global max column width across all sections (USAGE, BIND ADDRESS FORMATS, OPTIONS)
  int global_max_col_width = 0;

  // Include USAGE line widths in max calculation
  if (config->num_usage_lines > 0) {
    for (size_t i = 0; i < config->num_usage_lines; i++) {
      const usage_descriptor_t *usage = &config->usage_lines[i];
      // Estimate width of usage line (binary name + mode + positional + options suffix)
      int est_width = strlen("ascii-chat");  // binary name
      if (usage->mode)
        est_width += strlen(usage->mode) + 1;
      if (usage->positional)
        est_width += strlen(usage->positional) + 1;
      if (usage->show_options)
        est_width += 20;  // "[options...]" or "[mode-options...]"

      if (est_width > global_max_col_width)
        global_max_col_width = est_width;
    }
  }

  // Include positional argument examples (BIND ADDRESS FORMATS) widths in max calculation
  if (config->num_positional_args > 0) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[0];
    if (pos_arg->examples) {
      for (size_t i = 0; i < pos_arg->num_examples; i++) {
        const char *example = pos_arg->examples[i];
        // Find the first part (before multiple spaces)
        const char *p = example;
        while (*p == ' ')
          p++;
        const char *first_part = p;
        while (*p && !(*p == ' ' && *(p + 1) == ' '))
          p++;
        int first_len_bytes = (int)(p - first_part);
        int col_width = utf8_display_width_n(first_part, first_len_bytes);
        if (col_width > global_max_col_width)
          global_max_col_width = col_width;
      }
    }
  }

  // Include option descriptor widths in max calculation
  int options_max_col_width = options_config_calculate_max_col_width(config);
  if (options_max_col_width > global_max_col_width)
    global_max_col_width = options_max_col_width;

  // Ensure minimum width
  if (global_max_col_width < 20)
    global_max_col_width = 20;

  // Print USAGE section first (with global max width - we'll fake this by printing USAGE manually)
  fprintf(desc, "%s\n", colored_string(LOG_COLOR_DEBUG, "USAGE:"));
  if (config->num_usage_lines > 0) {
    for (size_t i = 0; i < config->num_usage_lines; i++) {
      const usage_descriptor_t *usage = &config->usage_lines[i];
      char usage_buf[512];
      int len = 0;

      // Start with binary name
      len += snprintf(usage_buf + len, sizeof(usage_buf) - len, "ascii-chat");

      // Add mode if present
      if (usage->mode) {
        len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_FATAL, usage->mode));
      }

      // Add positional args if present
      if (usage->positional) {
        len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_INFO, usage->positional));
      }

      // Add options suffix if requested
      if (usage->show_options) {
        const char *options_text = (usage->mode && strcmp(usage->mode, "<mode>") == 0)
                                   ? "[mode-options...]"
                                   : "[options...]";
        len += snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_WARN, options_text));
      }

      layout_print_two_column_row(desc, usage_buf, usage->description, global_max_col_width, term_width);
    }
  }
  fprintf(desc, "\n");

  // Print positional argument examples (BIND ADDRESS FORMATS) after USAGE section
  if (config->num_positional_args > 0) {
    const positional_arg_descriptor_t *pos_arg = &config->positional_args[0];
    if (pos_arg->section_heading && pos_arg->examples && pos_arg->num_examples > 0) {
      // Print section heading using colored_string for consistency
      (void)fprintf(desc, "%s\n", colored_string(LOG_COLOR_DEBUG, pos_arg->section_heading));

      // Print each example using layout function with global max width for consistent alignment
      for (size_t i = 0; i < pos_arg->num_examples; i++) {
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
        char colored_result[512];
        snprintf(colored_result, sizeof(colored_result), "%s",
                 colored_string(LOG_COLOR_INFO, colored_first_part));

        // Use layout function with global max_col_width for consistent alignment
        layout_print_two_column_row(desc, colored_result,
                                    desc_start ? desc_start : "",
                                    global_max_col_width, term_width);
      }
      (void)fprintf(desc, "\n");
    }
  }

  // Print everything after USAGE (MODES, MODE-OPTIONS, EXAMPLES, OPTIONS) with global max width
  options_config_print_options_sections_with_width(config, desc, global_max_col_width);

  // Print project links
  print_project_links(desc);

  // Clean up the config
  options_config_destroy(config);
}
