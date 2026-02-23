/**
 * @file help.c
 * @brief Help text generation and formatting for options
 * @ingroup options
 *
 * Implements all help/usage text generation functions including
 * programmatic section printers and unified help output.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/options/builder/internal.h>
#include <ascii-chat/options/builder.h>
#include <ascii-chat/options/common.h>
#include <ascii-chat/options/layout.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/log/logging.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// Programmatic Section Printers for Help Output
// ============================================================================

/**
 * @brief Get mode name from mode bitmask
 *
 * Converts an OPTION_MODE_* bitmask to a mode name string.
 * For example: OPTION_MODE_SERVER -> "server", OPTION_MODE_CLIENT -> "client"
 *
 * Special cases:
 * - Binary-level examples (OPTION_MODE_BINARY) don't get a mode name
 * - Discovery mode examples don't render mode name (treated like binary)
 *
 * @param mode_bitmask The mode bitmask (OPTION_MODE_SERVER, OPTION_MODE_CLIENT, etc.)
 * @return Mode name string (e.g., "server", "client", "mirror"), or NULL for binary/discovery
 */
static const char *get_mode_name_from_bitmask(uint32_t mode_bitmask) {
  // Skip binary-only examples (OPTION_MODE_BINARY = 0x100)
  if (mode_bitmask & OPTION_MODE_BINARY && !(mode_bitmask & 0x1F)) {
    return NULL; // Binary-level example, no mode name to prepend
  }

  // Skip discovery mode (renders like binary app with flags, no mode prefix)
  if (mode_bitmask == OPTION_MODE_DISCOVERY) {
    return NULL;
  }

  // Map individual mode bits to mode names (checked in priority order)
  if (mode_bitmask & OPTION_MODE_SERVER) {
    return "server";
  }
  if (mode_bitmask & OPTION_MODE_CLIENT) {
    return "client";
  }
  if (mode_bitmask & OPTION_MODE_MIRROR) {
    return "mirror";
  }
  if (mode_bitmask & OPTION_MODE_DISCOVERY_SVC) {
    return "discovery-service";
  }

  return NULL; // Unknown or no specific mode
}

/**
 * @brief Calculate global max column width across all help sections
 *
 * Calculates the maximum width needed for proper alignment across
 * USAGE, EXAMPLES, OPTIONS, and MODES sections.
 */
int options_config_calculate_max_col_width(const options_config_t *config) {
  if (!config)
    return 0;

  const char *binary_name = PLATFORM_BINARY_NAME;

  int max_col_width = 0;
  char temp_buf[BUFFER_SIZE_MEDIUM];

  // Check USAGE entries (capped at 45 chars for max first column)
  for (size_t i = 0; i < config->num_usage_lines; i++) {
    const usage_descriptor_t *usage = &config->usage_lines[i];
    int len = 0;

    len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);

    if (usage->mode) {
      const char *colored_mode = colored_string(LOG_COLOR_FATAL, usage->mode);
      len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_mode);
    }

    if (usage->positional) {
      const char *colored_pos = colored_string(LOG_COLOR_INFO, usage->positional);
      len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_pos);
    }

    if (usage->show_options) {
      const char *options_text =
          (usage->mode && strcmp(usage->mode, "<mode>") == 0) ? "[mode-options...]" : "[options...]";
      const char *colored_opts = colored_string(LOG_COLOR_WARN, options_text);
      len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_opts);
    }

    int w = utf8_display_width(temp_buf);
    if (w > LAYOUT_COLUMN_WIDTH) {
      w = LAYOUT_COLUMN_WIDTH;
    }
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check EXAMPLES entries
  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];
    int len = 0;

    // Only prepend program name if this is not a utility command
    if (!example->is_utility_command) {
      len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);

      // Programmatically add mode name based on mode_bitmask
      const char *mode_name = get_mode_name_from_bitmask(example->mode_bitmask);
      if (mode_name) {
        len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", mode_name);
      }
    }

    if (example->args) {
      const char *colored_args = colored_string(LOG_COLOR_INFO, example->args);
      len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", colored_args);
    }

    int w = utf8_display_width(temp_buf);
    if (w > LAYOUT_COLUMN_WIDTH) {
      w = LAYOUT_COLUMN_WIDTH;
    }
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check MODES entries (capped at 45 chars)
  for (size_t i = 0; i < config->num_modes; i++) {
    const char *colored_name = colored_string(LOG_COLOR_FATAL, config->modes[i].name);
    int w = utf8_display_width(colored_name);
    if (w > LAYOUT_COLUMN_WIDTH) {
      w = LAYOUT_COLUMN_WIDTH;
    }
    if (w > max_col_width)
      max_col_width = w;
  }

  // Check OPTIONS entries (from descriptors)
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->hide_from_mode_help || desc->hide_from_binary_help || !desc->group)
      continue;

    // Build option display string with separate coloring for short and long flags
    char opts_buf[BUFFER_SIZE_SMALL];
    if (desc->short_name && desc->short_name != '\0') {
      char short_flag[16];
      safe_snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
      char long_flag[BUFFER_SIZE_SMALL];
      safe_snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
      // Color short flag, add comma, color long flag
      safe_snprintf(opts_buf, sizeof(opts_buf), "%s, %s", colored_string(LOG_COLOR_WARN, short_flag),
                    colored_string(LOG_COLOR_WARN, long_flag));
    } else {
      char long_flag[BUFFER_SIZE_SMALL];
      safe_snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
      safe_snprintf(opts_buf, sizeof(opts_buf), "%s", colored_string(LOG_COLOR_WARN, long_flag));
    }
    const char *colored_opts = opts_buf;

    int w = utf8_display_width(colored_opts);
    if (w > LAYOUT_COLUMN_WIDTH) {
      w = LAYOUT_COLUMN_WIDTH;
    }
    if (w > max_col_width)
      max_col_width = w;
  }

  // Enforce maximum column width of 45 characters
  if (max_col_width > 45) {
    max_col_width = 45;
  }

  return max_col_width;
}

/* ============================================================================
 * Per-Section Column Width Calculation (Abstract)
 * ============================================================================ */

/**
 * @brief Calculate max column width for a specific section type
 *
 * Abstract function that calculates the maximum width needed for items
 * in a specific section (USAGE, EXAMPLES, MODES, OPTIONS, POSITIONAL_ARGUMENTS).
 * Capped at 75 characters.
 *
 * @param config Options configuration
 * @param section_type Type of section: "usage", "examples", "modes", "options", or "positional"
 * @param mode Current mode (used for filtering)
 * @param for_binary_help Whether this is for binary-level help
 * @return Maximum width needed for this section (minimum 20, maximum 75)
 */
static int calculate_section_max_col_width(const options_config_t *config, const char *section_type,
                                           asciichat_mode_t mode, bool for_binary_help) {
  if (!config || !section_type) {
    return 20; // Minimum width
  }

  int max_width = 0;
  const char *binary_name = PLATFORM_BINARY_NAME;
  char temp_buf[BUFFER_SIZE_MEDIUM];

  if (strcmp(section_type, "usage") == 0) {
    // Calculate max width for USAGE section (use plain text, no ANSI codes)
    if (config->num_usage_lines == 0)
      return 20;

    // Get mode name for filtering usage lines (same logic as options_print_help_for_mode)
    const char *mode_name = NULL;
    if (!for_binary_help) {
      switch (mode) {
      case MODE_SERVER:
        mode_name = "server";
        break;
      case MODE_CLIENT:
        mode_name = "client";
        break;
      case MODE_MIRROR:
        mode_name = "mirror";
        break;
      case MODE_DISCOVERY_SERVICE:
        mode_name = "discovery-service";
        break;
      case MODE_DISCOVERY:
        mode_name = NULL; // Binary help uses MODE_DISCOVERY but shows all usage lines
        break;
      default:
        mode_name = NULL;
        break;
      }
    }

    for (size_t i = 0; i < config->num_usage_lines; i++) {
      const usage_descriptor_t *usage = &config->usage_lines[i];

      // Filter usage lines by mode (same logic as options_print_help_for_mode)
      if (!for_binary_help) {
        // For mode-specific help, show ONLY the current mode's usage line
        // Don't show generic binary-level or placeholder lines
        if (!usage->mode || strcmp(usage->mode, mode_name) != 0) {
          continue;
        }
      }

      int len = 0;

      // Build plain text version for width calculation (no ANSI codes)
      len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);

      if (usage->mode) {
        len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", usage->mode);
      }

      if (usage->positional) {
        len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", usage->positional);
      }

      if (usage->show_options) {
        const char *options_text =
            (usage->mode && strcmp(usage->mode, "<mode>") == 0) ? "[mode-options...]" : "[options...]";
        len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", options_text);
      }

      int w = utf8_display_width_n(temp_buf, len);
      if (w > max_width)
        max_width = w;
    }

    // Cap USAGE section at 50 chars max (usage lines are inherently shorter)
    if (max_width > 50)
      max_width = 50;
  } else if (strcmp(section_type, "examples") == 0) {
    // Calculate max width for EXAMPLES section
    if (config->num_examples == 0)
      return 20;

    for (size_t i = 0; i < config->num_examples; i++) {
      const example_descriptor_t *example = &config->examples[i];

      // Filter examples by mode using bitmask
      if (for_binary_help) {
        if (!(example->mode_bitmask & OPTION_MODE_BINARY))
          continue;
      } else {
        uint32_t mode_bitmask = (1 << mode);
        if (!(example->mode_bitmask & mode_bitmask))
          continue;
      }

      int len = 0;
      len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, "%s", binary_name);

      // Include mode name in width calculation (using plain text, not colored)
      if (!example->is_utility_command) {
        const char *mode_name = get_mode_name_from_bitmask(example->mode_bitmask);
        if (mode_name) {
          len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", mode_name);
        }
      }

      if (example->args) {
        len += safe_snprintf(temp_buf + len, sizeof(temp_buf) - len, " %s", example->args);
      }

      int w = utf8_display_width_n(temp_buf, len);
      if (w > max_width)
        max_width = w;
    }

    // Allow examples to expand wider (no hard cap) - layout function will handle based on terminal width
    // Previously capped at 75, but long URLs and other content may exceed this
  } else if (strcmp(section_type, "modes") == 0) {
    // Calculate max width for MODES section
    if (config->num_modes == 0)
      return 20;

    for (size_t i = 0; i < config->num_modes; i++) {
      // Use plain text for width calculation (no ANSI codes)
      int w = utf8_display_width(config->modes[i].name);
      if (w > max_width)
        max_width = w;
    }

    // Cap MODES section at 30 chars max (mode names are short)
    if (max_width > 30)
      max_width = 30;
  } else if (strcmp(section_type, "options") == 0) {
    // Calculate max width for OPTIONS section
    if (config->num_descriptors == 0)
      return 20;

    char option_str[BUFFER_SIZE_MEDIUM];

    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];

      // Filter by mode and visibility
      if (!option_applies_to_mode(desc, mode, for_binary_help) || !desc->group || desc->hide_from_mode_help ||
          desc->hide_from_binary_help) {
        continue;
      }

      int option_len = 0;

      if (desc->short_name) {
        char short_flag[16];
        safe_snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
        char long_flag[BUFFER_SIZE_SMALL];
        safe_snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        option_len +=
            safe_snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s, %s",
                          colored_string(LOG_COLOR_WARN, short_flag), colored_string(LOG_COLOR_WARN, long_flag));
      } else {
        char long_flag[BUFFER_SIZE_SMALL];
        safe_snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        option_len += safe_snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                    colored_string(LOG_COLOR_WARN, long_flag));
      }

      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        option_len += safe_snprintf(option_str + option_len, sizeof(option_str) - option_len, " ");
        const char *placeholder = get_option_help_placeholder_str(desc);
        if (placeholder[0] != '\0') {
          option_len += safe_snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                      colored_string(LOG_COLOR_INFO, placeholder));
        }
      }

      int w = utf8_display_width(option_str);
      if (w > max_width)
        max_width = w;
    }
  } else if (strcmp(section_type, "positional") == 0) {
    // Calculate max width for POSITIONAL ARGUMENTS section
    if (config->num_positional_args == 0)
      return 20;

    option_mode_bitmask_t current_mode_bitmask = 1U << mode;

    for (size_t pa_idx = 0; pa_idx < config->num_positional_args; pa_idx++) {
      const positional_arg_descriptor_t *pos_arg = &config->positional_args[pa_idx];

      // Filter by mode_bitmask
      if (pos_arg->mode_bitmask != 0 && !(pos_arg->mode_bitmask & current_mode_bitmask)) {
        continue;
      }

      if (pos_arg->examples) {
        for (size_t i = 0; i < pos_arg->num_examples; i++) {
          const char *example = pos_arg->examples[i];
          const char *p = example;

          // Skip leading spaces
          while (*p == ' ')
            p++;
          const char *first_part = p;

          // Find double-space delimiter
          while (*p && !(*p == ' ' && *(p + 1) == ' '))
            p++;
          int first_len_bytes = (int)(p - first_part);

          int w = utf8_display_width_n(first_part, first_len_bytes);
          if (w > max_width)
            max_width = w;
        }
      }
    }
  }

  // Cap at 75 characters
  if (max_width > 75)
    max_width = 75;

  return max_width > 20 ? max_width : 20;
}

/**
 * @brief Print USAGE section programmatically
 *
 * Builds colored usage lines from components:
 * - mode: magenta (using LOG_COLOR_FATAL)
 * - positional: green (using LOG_COLOR_INFO)
 * - options: yellow (using LOG_COLOR_WARN)
 */
static void print_usage_section(const options_config_t *config, FILE *stream, int term_width, int max_col_width) {
  if (!config || !stream || config->num_usage_lines == 0) {
    return;
  }

  const char *binary_name = PLATFORM_BINARY_NAME;

  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "USAGE"));

  // Build colored syntax strings using colored_string() for all components
  for (size_t i = 0; i < config->num_usage_lines; i++) {
    const usage_descriptor_t *usage = &config->usage_lines[i];
    char usage_buf[BUFFER_SIZE_MEDIUM];
    int len = 0;

    // Start with binary name
    len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, "%s", binary_name);

    // Add mode if present (magenta color)
    if (usage->mode) {
      len +=
          safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_FATAL, usage->mode));
    }

    // Add positional args if present (green color)
    if (usage->positional) {
      len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s",
                           colored_string(LOG_COLOR_INFO, usage->positional));
    }

    // Add options suffix if requested (yellow color)
    if (usage->show_options) {
      const char *options_text =
          (usage->mode && strcmp(usage->mode, "<mode>") == 0) ? "[mode-options...]" : "[options...]";
      len +=
          safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_WARN, options_text));
    }

    // Print with layout function using global column width
    layout_print_two_column_row(stream, usage_buf, usage->description, max_col_width, term_width, 0);
  }
  fprintf(stream, "\n");
}

/**
 * @brief Print EXAMPLES section programmatically
 *
 * Builds colored example commands from components:
 * - mode: magenta (using LOG_COLOR_FATAL)
 * - args/flags: yellow (using LOG_COLOR_WARN)
 */
static void print_examples_section(const options_config_t *config, FILE *stream, int term_width, int max_col_width,
                                   asciichat_mode_t mode, bool for_binary_help) {
  if (!config || !stream || config->num_examples == 0) {
    return;
  }

  const char *binary_name = PLATFORM_BINARY_NAME;

  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "EXAMPLES"));

  // Build colored command strings using colored_string() for all components
  for (size_t i = 0; i < config->num_examples; i++) {
    const example_descriptor_t *example = &config->examples[i];

    // Filter examples based on mode bitmask
    if (for_binary_help) {
      // Binary help shows only examples with OPTION_MODE_BINARY flag
      if (!(example->mode_bitmask & OPTION_MODE_BINARY)) {
        continue;
      }
    } else {
      // For mode-specific help, show examples with matching mode
      // Convert mode enum to bitmask
      uint32_t mode_bitmask = (1 << mode);
      if (!(example->mode_bitmask & mode_bitmask)) {
        continue;
      }
    }

    char cmd_buf[BUFFER_SIZE_MEDIUM];
    int len = 0;

    // Only add binary name if this is not a utility command
    if (!example->is_utility_command) {
      len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s", binary_name);

      // Programmatically add mode name based on current mode being displayed
      // When example applies to multiple modes (via OR'd bitmask), we show the
      // current mode's name, not the first mode in the bitmask
      uint32_t current_mode_bitmask = for_binary_help ? OPTION_MODE_BINARY : (1 << mode);
      const char *mode_name = get_mode_name_from_bitmask(current_mode_bitmask);
      if (mode_name) {
        len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, " %s", colored_string(LOG_COLOR_FATAL, mode_name));
      }
    }

    // Add args/flags if present (flags=yellow, arguments=green, utility programs=white)
    if (example->args) {
      // Only add space if we've already added something (binary name)
      if (len > 0) {
        len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, " ");
      }

      // For utility commands, color everything white except flags (yellow)
      // For regular examples, color flags yellow and arguments green
      if (example->is_utility_command) {
        // Utility command: only flags get yellow, everything else is white
        const char *p = example->args;
        char current_token[BUFFER_SIZE_SMALL];
        int token_len = 0;

        while (*p) {
          if (*p == ' ' || *p == '|' || *p == '>' || *p == '<') {
            // Flush current token if any
            if (token_len > 0) {
              current_token[token_len] = '\0';
              // Flags (start with -) are yellow, everything else is white
              if (current_token[0] == '-') {
                len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                                     colored_string(LOG_COLOR_WARN, current_token));
              } else {
                len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                                     colored_string(LOG_COLOR_RESET, current_token));
              }
              token_len = 0;
            }
            // Add the separator (space, pipe, redirect, etc) in white
            if (*p != ' ') {
              len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s ",
                                   colored_string(LOG_COLOR_RESET, (*p == '|')   ? "|"
                                                                   : (*p == '>') ? ">"
                                                                                 : "<"));
            } else {
              len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, " ");
            }
            p++;
            // Skip multiple spaces
            while (*p == ' ')
              p++;
          } else {
            current_token[token_len++] = *p;
            p++;
          }
        }

        // Flush last token
        if (token_len > 0) {
          current_token[token_len] = '\0';
          if (current_token[0] == '-') {
            len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                                 colored_string(LOG_COLOR_WARN, current_token));
          } else {
            len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                                 colored_string(LOG_COLOR_RESET, current_token));
          }
        }
      } else {
        // Regular example: flags=yellow, arguments=green
        const char *p = example->args;
        char current_token[BUFFER_SIZE_SMALL];
        int token_len = 0;

        while (*p) {
          if (*p == ' ') {
            // Flush current token if any
            if (token_len > 0) {
              current_token[token_len] = '\0';
              // Color flags (start with -) yellow, arguments green
              if (current_token[0] == '-') {
                len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s ",
                                     colored_string(LOG_COLOR_WARN, current_token));
              } else {
                len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s ",
                                     colored_string(LOG_COLOR_INFO, current_token));
              }
              token_len = 0;
            }
            p++;
            // Skip multiple spaces
            while (*p == ' ')
              p++;
          } else {
            current_token[token_len++] = *p;
            p++;
          }
        }

        // Flush last token
        if (token_len > 0) {
          current_token[token_len] = '\0';
          if (current_token[0] == '-') {
            len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                                 colored_string(LOG_COLOR_WARN, current_token));
          } else {
            len += safe_snprintf(cmd_buf + len, sizeof(cmd_buf) - len, "%s",
                                 colored_string(LOG_COLOR_INFO, current_token));
          }
        }
      }

      // Remove trailing space if added
      if (len > 0 && cmd_buf[len - 1] == ' ') {
        len--;
      }
    }

    // Print with layout function using global column width
    layout_print_two_column_row(stream, cmd_buf, example->description, max_col_width, term_width, 0);
  }

  fprintf(stream, "\n");
}

/**
 * @brief Print MODES section programmatically
 */
static void print_modes_section(const options_config_t *config, FILE *stream, int term_width, int max_col_width) {
  if (!config || !stream || config->num_modes == 0) {
    return;
  }

  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "MODES"));

  // Print each mode with colored name using colored_string() and global column width
  for (size_t i = 0; i < config->num_modes; i++) {
    char mode_buf[BUFFER_SIZE_SMALL];
    safe_snprintf(mode_buf, sizeof(mode_buf), "%s", colored_string(LOG_COLOR_FATAL, config->modes[i].name));
    layout_print_two_column_row(stream, mode_buf, config->modes[i].description, max_col_width, term_width, 0);
  }

  fprintf(stream, "\n");
}

/**
 * @brief Print MODE-OPTIONS section programmatically
 */
static void print_mode_options_section(FILE *stream, int term_width, int max_col_width) {
  const char *binary_name = PLATFORM_BINARY_NAME;

  // Print section header with colored "MODE-OPTIONS:" label
  fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, "MODE-OPTIONS"));

  // Build colored command with components
  char usage_buf[512];
  int len = 0;

  // Binary name (no color)
  len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, "%s ", binary_name);

  // Mode placeholder (magenta)
  len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, "%s", colored_string(LOG_COLOR_FATAL, "<mode>"));

  // Space and help option (yellow)
  len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s", colored_string(LOG_COLOR_WARN, "--help"));

  layout_print_two_column_row(stream, usage_buf, "Show options for a mode", max_col_width, term_width, 0);

  fprintf(stream, "\n");
}

void options_config_print_usage(const options_config_t *config, FILE *stream) {
  if (!config || !stream)
    return;

  // Detect terminal width from COLUMNS env var or use default
  int term_width = 80;
  const char *cols_env = SAFE_GETENV("COLUMNS");
  if (cols_env) {
    int cols = atoi(cols_env);
    if (cols > 40)
      term_width = cols;
  }

  // Binary-level help uses MODE_DISCOVERY internally
  asciichat_mode_t mode = MODE_DISCOVERY;
  bool for_binary_help = true;

  // Calculate per-section column widths (each section is independent, capped at 75)
  int usage_max_col_width = calculate_section_max_col_width(config, "usage", mode, for_binary_help);
  int modes_max_col_width = calculate_section_max_col_width(config, "modes", mode, for_binary_help);
  int examples_max_col_width = calculate_section_max_col_width(config, "examples", mode, for_binary_help);
  int options_max_col_width = calculate_section_max_col_width(config, "options", mode, for_binary_help);

  // Print programmatically generated sections (USAGE, MODES, MODE-OPTIONS, EXAMPLES)
  print_usage_section(config, stream, term_width, usage_max_col_width);
  print_modes_section(config, stream, term_width, modes_max_col_width);
  print_mode_options_section(stream, term_width, 40); // Keep reasonable width for mode-options
  print_examples_section(config, stream, term_width, examples_max_col_width, MODE_SERVER, true);

  // Build list of unique groups in order of first appearance

  const char **unique_groups = SAFE_MALLOC(config->num_descriptors * sizeof(const char *), const char **);
  size_t num_unique_groups = 0;

  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    // Filter by mode_bitmask - for binary help, show binary options
    if (!option_applies_to_mode(desc, MODE_SERVER, for_binary_help) || !desc->group) {
      continue;
    }

    // Check if this group is already in the list
    bool group_exists = false;
    for (size_t j = 0; j < num_unique_groups; j++) {
      if (unique_groups[j] && strcmp(unique_groups[j], desc->group) == 0) {
        group_exists = true;
        break;
      }
    }

    // Add new group to list
    if (!group_exists && num_unique_groups < config->num_descriptors) {
      unique_groups[num_unique_groups++] = desc->group;
    }
  }

  // Print options grouped by group name
  for (size_t g = 0; g < num_unique_groups; g++) {
    const char *current_group = unique_groups[g];
    // Only add leading newline for groups after the first one
    if (g > 0) {
      fprintf(stream, "\n");
    }
    fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, current_group));

    // Print all options in this group
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];

      // Skip if not in current group or if doesn't apply to mode
      if (!option_applies_to_mode(desc, MODE_SERVER, for_binary_help) || !desc->group ||
          strcmp(desc->group, current_group) != 0) {
        continue;
      }

      // Build option string with separate coloring for short and long flags
      char option_str[BUFFER_SIZE_MEDIUM] = "";
      int option_len = 0;

      // Short name and long name with separate coloring
      if (desc->short_name) {
        char short_flag[16];
        safe_snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
        char long_flag[BUFFER_SIZE_SMALL];
        safe_snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        // Color short flag, plain comma-space, color long flag
        option_len +=
            safe_snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s, %s",
                          colored_string(LOG_COLOR_WARN, short_flag), colored_string(LOG_COLOR_WARN, long_flag));
      } else {
        char long_flag[BUFFER_SIZE_SMALL];
        safe_snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        option_len += safe_snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                    colored_string(LOG_COLOR_WARN, long_flag));
      }

      // Value placeholder (colored green)
      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        option_len += safe_snprintf(option_str + option_len, sizeof(option_str) - option_len, " ");
        const char *placeholder = get_option_help_placeholder_str(desc);
        if (placeholder[0] != '\0') {
          option_len += safe_snprintf(option_str + option_len, sizeof(option_str) - option_len, "%s",
                                      colored_string(LOG_COLOR_INFO, placeholder));
        }
      }

      // Build description string (plain text, colors applied when printing)
      char desc_str[BUFFER_SIZE_MEDIUM] = "";
      int desc_len = 0;

      if (desc->help_text) {
        desc_len += safe_snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s", desc->help_text);
      }

      // Skip adding default if the description already mentions it
      bool description_has_default =
          desc->help_text && (strstr(desc->help_text, "(default:") || strstr(desc->help_text, "=default)"));

      if (desc->default_value && !description_has_default) {
        char default_buf[256];
        int default_len = format_option_default_value_str(desc, default_buf, sizeof(default_buf));
        if (default_len > 0) {
          desc_len +=
              safe_snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s %s)",
                            colored_string(LOG_COLOR_FATAL, "default:"), colored_string(LOG_COLOR_FATAL, default_buf));
        }
      }

      if (desc->required) {
        desc_len += safe_snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " [REQUIRED]");
      }

      // Use layout function with section-specific column width for consistent alignment
      // option_str already contains colored_string() results, so pass it directly
      layout_print_two_column_row(stream, option_str, desc_str, options_max_col_width, term_width, 2);
    }
  }

  // Cleanup
  SAFE_FREE(unique_groups);

  fprintf(stream, "\n");
}

/**
 * @brief Print only the USAGE section
 *
 * Splits usage printing from other sections to allow custom content
 * (like positional argument examples) to be inserted between USAGE and other sections.
 */
void options_config_print_usage_section(const options_config_t *config, FILE *stream) {
  if (!config || !stream)
    return;

  // Detect terminal width from COLUMNS env var or use default
  int term_width = 80;
  const char *cols_env = SAFE_GETENV("COLUMNS");
  if (cols_env) {
    int cols = atoi(cols_env);
    if (cols > 40)
      term_width = cols;
  }

  // Calculate global max column width across all sections for consistent alignment
  int max_col_width = options_config_calculate_max_col_width(config);

  // Print only USAGE section
  print_usage_section(config, stream, term_width, max_col_width);
}

/**
 * @brief Print everything except the USAGE section
 *
 * Prints MODES, MODE-OPTIONS, EXAMPLES, and OPTIONS sections.
 * Used with options_config_print_usage_section to allow custom content in between.
 */
void options_config_print_options_sections_with_width(const options_config_t *config, FILE *stream, int max_col_width,
                                                      asciichat_mode_t mode) {
  if (!config || !stream) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Config or stream is NULL");
    return;
  }

  // Detect terminal width - try actual terminal size first, fallback to COLUMNS env var
  int term_width = 80;
  terminal_size_t term_size;
  if (terminal_get_size(&term_size) == ASCIICHAT_OK && term_size.cols > 40) {
    term_width = term_size.cols;
  } else {
    const char *cols_env = SAFE_GETENV("COLUMNS");
    if (cols_env) {
      int cols = atoi(cols_env);
      if (cols > 40)
        term_width = cols;
    }
  }

  // Calculate column width if not provided
  if (max_col_width <= 0) {
    max_col_width = options_config_calculate_max_col_width(config);
  }

  // CAP max_col_width: 86 if terminal wide, otherwise 45 for narrow first column
  int max_col_cap = (term_width > 170) ? 86 : 45;
  if (max_col_width > max_col_cap) {
    max_col_width = max_col_cap;
  }

  // Determine if this is binary-level help
  // Discovery mode IS the binary-level help (shows binary options + discovery options)
  // Other modes show only mode-specific options
  bool for_binary_help = (mode == MODE_DISCOVERY);
  // Build list of unique groups in order of first appearance
  const char **unique_groups = SAFE_MALLOC(config->num_descriptors * sizeof(const char *), const char **);
  size_t num_unique_groups = 0;

  // For binary-level help, we now use a two-pass system to order groups,
  // ensuring binary-level option groups appear before discovery-mode groups.
  if (for_binary_help) {
    // Pass 1: Add GENERAL first, then LOGGING if present
    bool general_added = false;
    bool logging_added = false;
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      if (desc->group) {
        if (!general_added && strcmp(desc->group, "GENERAL") == 0) {
          unique_groups[num_unique_groups++] = "GENERAL";
          general_added = true;
        }
        if (!logging_added && strcmp(desc->group, "LOGGING") == 0) {
          unique_groups[num_unique_groups++] = "LOGGING";
          logging_added = true;
        }
        if (general_added && logging_added) {
          break;
        }
      }
    }

    // Pass 2: Collect all other groups that apply for binary help (all modes), if not already added.
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      // An option applies if option_applies_to_mode says so for binary help (which checks OPTION_MODE_ALL)
      if (option_applies_to_mode(desc, mode, for_binary_help) && desc->group) {
        // Skip GENERAL and LOGGING groups if we already added them (for binary help)
        if (strcmp(desc->group, "GENERAL") == 0 || strcmp(desc->group, "LOGGING") == 0) {
          continue;
        }

        bool group_exists = false;
        for (size_t j = 0; j < num_unique_groups; j++) {
          if (strcmp(unique_groups[j], desc->group) == 0) {
            group_exists = true;
            break;
          }
        }
        if (!group_exists) {
          unique_groups[num_unique_groups++] = desc->group;
        }
      } else if (desc->group) {
      }
    }
  } else {
    // Original logic for other modes
    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      if (option_applies_to_mode(desc, mode, for_binary_help) && desc->group) {
        bool group_exists = false;
        for (size_t j = 0; j < num_unique_groups; j++) {
          if (strcmp(unique_groups[j], desc->group) == 0) {
            group_exists = true;
            break;
          }
        }
        if (!group_exists) {
          unique_groups[num_unique_groups++] = desc->group;
        }
      }
    }
  }

  // Print options grouped by category
  for (size_t gi = 0; gi < num_unique_groups; gi++) {
    const char *current_group = unique_groups[gi];
    // Add newline before each group except the first
    if (gi > 0) {
      fprintf(stream, "\n");
    }
    fprintf(stream, "%s\n", colored_string(LOG_COLOR_DEBUG, current_group));

    for (size_t i = 0; i < config->num_descriptors; i++) {
      const option_descriptor_t *desc = &config->descriptors[i];
      if (!option_applies_to_mode(desc, mode, for_binary_help) || !desc->group ||
          strcmp(desc->group, current_group) != 0) {
        continue;
      }

      // Build option string (flag part) with separate coloring for short and long flags
      char colored_option_str[BUFFER_SIZE_MEDIUM] = "";
      int colored_len = 0;

      // Short name and long name with separate coloring
      if (desc->short_name) {
        char short_flag[16];
        safe_snprintf(short_flag, sizeof(short_flag), "-%c", desc->short_name);
        char long_flag[BUFFER_SIZE_SMALL];
        safe_snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        // Color short flag, plain comma-space, color long flag
        colored_len +=
            safe_snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s, %s",
                          colored_string(LOG_COLOR_WARN, short_flag), colored_string(LOG_COLOR_WARN, long_flag));
      } else {
        char long_flag[BUFFER_SIZE_SMALL];
        safe_snprintf(long_flag, sizeof(long_flag), "--%s", desc->long_name);
        colored_len += safe_snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                     colored_string(LOG_COLOR_WARN, long_flag));
      }

      // Value placeholder (colored green)
      if (desc->type != OPTION_TYPE_BOOL && desc->type != OPTION_TYPE_ACTION) {
        colored_len += safe_snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, " ");
        const char *placeholder = get_option_help_placeholder_str(desc);
        if (placeholder[0] != '\0') {
          colored_len += safe_snprintf(colored_option_str + colored_len, sizeof(colored_option_str) - colored_len, "%s",
                                       colored_string(LOG_COLOR_INFO, placeholder));
        }
      }

      // Build description with defaults and env vars
      char desc_str[1024] = "";
      int desc_len = 0;

      if (desc->help_text) {
        desc_len += safe_snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, "%s", desc->help_text);
      }

      // Skip adding default if the description already mentions it
      bool description_has_default =
          desc->help_text && (strstr(desc->help_text, "(default:") || strstr(desc->help_text, "=default)"));

      if (desc->default_value && !description_has_default) {
        char default_buf[256];
        int default_len = format_option_default_value_str(desc, default_buf, sizeof(default_buf));
        if (default_len > 0) {
          desc_len +=
              safe_snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " (%s %s)",
                            colored_string(LOG_COLOR_FATAL, "default:"), colored_string(LOG_COLOR_FATAL, default_buf));
        }
      }

      if (desc->required) {
        desc_len += safe_snprintf(desc_str + desc_len, sizeof(desc_str) - desc_len, " [REQUIRED]");
      }

      layout_print_two_column_row(stream, colored_option_str, desc_str, max_col_width, term_width, 2);
    }
  }

  // Cleanup
  SAFE_FREE(unique_groups);
}

/**
 * @brief Print everything except the USAGE section (backward compatibility wrapper)
 *
 * Calls options_config_print_options_sections_with_width with auto-calculation.
 */
void options_config_print_options_sections(const options_config_t *config, FILE *stream, asciichat_mode_t mode) {
  options_config_print_options_sections_with_width(config, stream, 0, mode);
}

// ============================================================================
// Unified Help Printing Function
// ============================================================================

/**
 * @brief Print help for a specific mode or binary level
 *
 * This is the single unified function for all help output (binary level and all modes).
 * It handles common layout logic, terminal detection, and section printing.
 *
 * @param config Options config with all options (binary + all modes)
 * @param mode Mode to show help for (use -1 for binary-level help)
 * @param program_name Full program name with mode (e.g., "ascii-chat server")
 * @param description Brief description of the mode/binary
 * @param desc Output file stream (usually stdout)
 */
void options_print_help_for_mode(const options_config_t *config, asciichat_mode_t mode, const char *program_name,
                                 const char *description, FILE *desc) {
  if (!config || !desc) {
    if (desc) {
      fprintf(desc, "Error: Failed to create options config\n");
    } else {
      SET_ERRNO(ERROR_INVALID_PARAM, "Config or desc is NULL");
    }
    return;
  }

  // Detect terminal width early so we can decide whether to show ASCII art
  int term_width = 80;
  terminal_size_t term_size;
  if (terminal_get_size(&term_size) == ASCIICHAT_OK && term_size.cols > 40) {
    term_width = term_size.cols;
  } else {
    const char *cols_env = SAFE_GETENV("COLUMNS");
    if (cols_env) {
      int cols = atoi(cols_env);
      if (cols > 40)
        term_width = cols;
    }
  }

  // Print ASCII art logo only if terminal is wide enough (ASCII art is ~52 chars wide)
  if (term_width >= 60) {
    (void)fprintf(desc, "  __ _ ___  ___(_|_)       ___| |__   __ _| |_ \n");
    (void)fprintf(desc, " / _` / __|/ __| | |_____ / __| '_ \\ / _` | __|\n");
    (void)fprintf(desc, "| (_| \\__ \\ (__| | |_____| (__| | | | (_| | |_ \n");
    (void)fprintf(desc, " \\__,_|___/\\___|_|_|      \\___|_| |_|\\__,_|\\__|\n");
    (void)fprintf(desc, "\n");
  }

  // Print program name and description (color mode name magenta if it's a mode-specific help)
  if (program_name) {
    const char *space = strchr(program_name, ' ');
    if (space && mode >= 0) {
      // Mode-specific help: color the mode name
      int binary_len = space - program_name;
      (void)fprintf(desc, "%.*s %s - %s\n\n", binary_len, program_name, colored_string(LOG_COLOR_FATAL, space + 1),
                    description);
    } else {
      // Binary-level help: use colored_string for the program name too
      (void)fprintf(desc, "%s - %s\n\n", colored_string(LOG_COLOR_FATAL, program_name), description);
    }
  }

  // Print project links
  print_project_links(desc);
  (void)fprintf(desc, "\n");

  // Determine if this is binary-level help (called for 'ascii-chat --help')
  // Binary help uses MODE_DISCOVERY as the mode value
  bool for_binary_help = (mode == MODE_DISCOVERY);

  // Calculate column widths for USAGE upfront (before printing) for alignment
  int usage_max_col_width = calculate_section_max_col_width(config, "usage", mode, for_binary_help);
  // Use USAGE width for both USAGE and EXAMPLES sections for consistent alignment
  // (don't let long examples push column too far right)
  int usage_examples_col_width = usage_max_col_width;

  // Print USAGE section (with section-specific column width and mode filtering)
  fprintf(desc, "%s\n", colored_string(LOG_COLOR_DEBUG, "USAGE"));
  if (config->num_usage_lines > 0) {
    // Get mode name for filtering usage lines
    const char *mode_name = NULL;
    switch (mode) {
    case MODE_SERVER:
      mode_name = "server";
      break;
    case MODE_CLIENT:
      mode_name = "client";
      break;
    case MODE_MIRROR:
      mode_name = "mirror";
      break;
    case MODE_DISCOVERY_SERVICE:
      mode_name = "discovery-service";
      break;
    case MODE_DISCOVERY:
      mode_name = NULL; // Binary help shows all usage lines
      break;
    default:
      mode_name = NULL;
      break;
    }

    for (size_t i = 0; i < config->num_usage_lines; i++) {
      const usage_descriptor_t *usage = &config->usage_lines[i];

      // Filter usage lines by mode
      if (!for_binary_help) {
        // For mode-specific help, show the current mode's usage line and the generic [mode] --help line
        bool is_current_mode = (usage->mode && mode_name && strcmp(usage->mode, mode_name) == 0);
        bool is_generic_help_line = (!usage->mode && usage->positional && strcmp(usage->positional, "[mode] --help") == 0);

        if (!is_current_mode && !is_generic_help_line) {
          continue;
        }
      }

      char usage_buf[BUFFER_SIZE_MEDIUM];
      int len = 0;

      len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, "ascii-chat");

      // For mode-specific help, use [mode] placeholder when showing --help usage
      if (usage->mode) {
        if (!for_binary_help && usage->positional && strcmp(usage->positional, "--help") == 0) {
          // Mode-specific help: show [mode] --help as generic placeholder
          len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s",
                               colored_string(LOG_COLOR_FATAL, "[mode]"));
        } else {
          // Binary help or non-help usage: show actual mode name
          len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s",
                               colored_string(LOG_COLOR_FATAL, usage->mode));
        }
      }

      if (usage->positional) {
        len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s",
                             colored_string(LOG_COLOR_INFO, usage->positional));
      }

      if (usage->show_options) {
        const char *options_text =
            (usage->mode && strcmp(usage->mode, "<mode>") == 0) ? "[mode-options...]" : "[options...]";
        len += safe_snprintf(usage_buf + len, sizeof(usage_buf) - len, " %s",
                             colored_string(LOG_COLOR_WARN, options_text));
      }

      layout_print_two_column_row(desc, usage_buf, usage->description, usage_examples_col_width, term_width, 0);
    }
  }
  fprintf(desc, "\n");

  // Print MODES section (only for binary-level help)
  if (for_binary_help && config->num_modes > 0) {
    int modes_max_col_width = calculate_section_max_col_width(config, "modes", mode, for_binary_help);
    print_modes_section(config, desc, term_width, modes_max_col_width);
  }

  // Print positional argument examples (with mode filtering and section-specific column width)
  if (config->num_positional_args > 0) {
    // First, check if any positional args apply to this mode
    option_mode_bitmask_t current_mode_bitmask = 1U << mode;
    bool has_applicable_positional_args = false;

    for (size_t pa_idx = 0; pa_idx < config->num_positional_args; pa_idx++) {
      const positional_arg_descriptor_t *pos_arg = &config->positional_args[pa_idx];

      // Filter by mode_bitmask (matching the parsing code logic)
      if (pos_arg->mode_bitmask != 0 && !(pos_arg->mode_bitmask & current_mode_bitmask)) {
        continue;
      }

      if (pos_arg->section_heading && pos_arg->examples && pos_arg->num_examples > 0) {
        has_applicable_positional_args = true;
        break;
      }
    }

    // Only print the section if there are applicable positional args
    if (has_applicable_positional_args) {
      int positional_max_col_width = calculate_section_max_col_width(config, "positional", mode, false);

      for (size_t pa_idx = 0; pa_idx < config->num_positional_args; pa_idx++) {
        const positional_arg_descriptor_t *pos_arg = &config->positional_args[pa_idx];

        // Filter by mode_bitmask (matching the parsing code logic)
        if (pos_arg->mode_bitmask != 0 && !(pos_arg->mode_bitmask & current_mode_bitmask)) {
          continue;
        }

        if (pos_arg->section_heading && pos_arg->examples && pos_arg->num_examples > 0) {
          (void)fprintf(desc, "%s\n", colored_string(LOG_COLOR_DEBUG, pos_arg->section_heading));

          for (size_t i = 0; i < pos_arg->num_examples; i++) {
            const char *example = pos_arg->examples[i];
            const char *p = example;
            const char *desc_start = NULL;

            while (*p == ' ')
              p++;
            const char *first_part = p;

            while (*p && !(*p == ' ' && *(p + 1) == ' '))
              p++;
            int first_len_bytes = (int)(p - first_part);

            while (*p == ' ')
              p++;
            if (*p) {
              desc_start = p;
            }

            char colored_first_part[256];
            safe_snprintf(colored_first_part, sizeof(colored_first_part), "%.*s", first_len_bytes, first_part);
            char colored_result[512];
            safe_snprintf(colored_result, sizeof(colored_result), "%s",
                          colored_string(LOG_COLOR_INFO, colored_first_part));

            layout_print_two_column_row(desc, colored_result, desc_start ? desc_start : "", positional_max_col_width,
                                        term_width, 0);
          }
          (void)fprintf(desc, "\n");
        }
      }
    }
  }

  // Print EXAMPLES section (using USAGE column width for alignment)
  print_examples_section(config, desc, term_width, usage_examples_col_width, mode, for_binary_help);

  // Print custom sections (after EXAMPLES, before OPTIONS)
  if (config->num_custom_sections > 0) {
    option_mode_bitmask_t current_mode_bitmask = 1U << mode;
    for (size_t i = 0; i < config->num_custom_sections; i++) {
      const custom_section_descriptor_t *section = &config->custom_sections[i];

      // Filter by mode_bitmask
      if (section->mode_bitmask != 0 && !(section->mode_bitmask & current_mode_bitmask)) {
        continue;
      }

      if (section->heading) {
        fprintf(desc, "%s\n", colored_string(LOG_COLOR_DEBUG, section->heading));
      }

      if (section->content) {
        // Special handling for KEYBINDINGS section: colorize keybindings and wrap to 70 chars
        if (section->heading && strcmp(section->heading, "KEYBINDINGS") == 0) {
          // Build colored version with proper keybinding colorization
          char colored_output[2048] = "";
          const char *src = section->content;
          char *dst = colored_output;
          size_t remaining = sizeof(colored_output) - 1;

          while (*src && remaining > 0) {
            // Colorize ? that don't end sentences
            if (*src == '?' && *(src + 1) != '\n' && *(src + 1) != '\0') {
              const char *colored = colored_string(LOG_COLOR_FATAL, "?");
              size_t len = strlen(colored);
              if (len <= remaining) {
                memcpy(dst, colored, len);
                dst += len;
                remaining -= len;
                src += 1;
              } else {
                break;
              }
            } else if (strncmp(src, "Space", 5) == 0 &&
                       (src > section->content && (*(src - 1) == ',' || *(src - 1) == ' '))) {
              const char *colored = colored_string(LOG_COLOR_FATAL, "Space");
              size_t len = strlen(colored);
              if (len <= remaining) {
                memcpy(dst, colored, len);
                dst += len;
                remaining -= len;
                src += 5;
              } else {
                break;
              }
            } else if (strncmp(src, "arrows", 6) == 0 &&
                       (src > section->content && (*(src - 1) == ',' || *(src - 1) == ' '))) {
              const char *colored = colored_string(LOG_COLOR_FATAL, "arrows");
              size_t len = strlen(colored);
              if (len <= remaining) {
                memcpy(dst, colored, len);
                dst += len;
                remaining -= len;
                src += 6;
              } else {
                break;
              }
            } else if (*src == 'm' && (src > section->content && (*(src - 1) == ',' || *(src - 1) == ' ')) &&
                       (*(src + 1) == ',' || *(src + 1) == ')')) {
              const char *colored = colored_string(LOG_COLOR_FATAL, "m");
              size_t len = strlen(colored);
              if (len <= remaining) {
                memcpy(dst, colored, len);
                dst += len;
                remaining -= len;
                src += 1;
              } else {
                break;
              }
            } else if (*src == 'c' && (src > section->content && (*(src - 1) == ',' || *(src - 1) == ' ')) &&
                       (*(src + 1) == ',' || *(src + 1) == ')')) {
              const char *colored = colored_string(LOG_COLOR_FATAL, "c");
              size_t len = strlen(colored);
              if (len <= remaining) {
                memcpy(dst, colored, len);
                dst += len;
                remaining -= len;
                src += 1;
              } else {
                break;
              }
            } else if (*src == 'f' && (src > section->content && (*(src - 1) == ',' || *(src - 1) == ' ')) &&
                       (*(src + 1) == ',' || *(src + 1) == ')')) {
              const char *colored = colored_string(LOG_COLOR_FATAL, "f");
              size_t len = strlen(colored);
              if (len <= remaining) {
                memcpy(dst, colored, len);
                dst += len;
                remaining -= len;
                src += 1;
              } else {
                break;
              }
            } else if (*src == 'r' && (src > section->content && (*(src - 1) == ',' || *(src - 1) == ' ')) &&
                       (*(src + 1) == ')')) {
              const char *colored = colored_string(LOG_COLOR_FATAL, "r");
              size_t len = strlen(colored);
              if (len <= remaining) {
                memcpy(dst, colored, len);
                dst += len;
                remaining -= len;
                src += 1;
              } else {
                break;
              }
            } else {
              *dst++ = *src++;
              remaining--;
            }
          }
          *dst = '\0';
          // Print with 2-space indent, wrapping at min(terminal width, 90)
          fprintf(desc, "  ");
          int keybindings_wrap_width = term_width < 90 ? term_width : 90;
          layout_print_wrapped_description(desc, colored_output, 2, keybindings_wrap_width, 0);
          fprintf(desc, "\n");
        } else {
          fprintf(desc, "%s\n", section->content);
        }
      }

      fprintf(desc, "\n");
    }
  }

  // Print options sections (with section-specific column width for options)
  int options_max_col_width = calculate_section_max_col_width(config, "options", mode, for_binary_help);
  options_config_print_options_sections_with_width(config, desc, options_max_col_width, mode);
}

void options_struct_destroy(const options_config_t *config, void *options_struct) {
  if (!config || !options_struct)
    return;

  // Free all owned strings
  for (size_t i = 0; i < config->num_owned_strings; i++) {
    free(config->owned_strings[i]);
  }

  // Reset owned strings tracking
  ((options_config_t *)config)->num_owned_strings = 0;

  // NULL out string fields
  char *base = (char *)options_struct;
  for (size_t i = 0; i < config->num_descriptors; i++) {
    const option_descriptor_t *desc = &config->descriptors[i];
    if (desc->type == OPTION_TYPE_STRING && desc->owns_memory) {
      char **field = (char **)(base + desc->offset);
      *field = NULL;
    }
  }
}
