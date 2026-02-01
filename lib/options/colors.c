/**
 * @file lib/options/colors.c
 * @brief Early color scheme loading before logging initialization
 * @ingroup options
 *
 * Loads color schemes from CLI arguments and config files BEFORE logging
 * is initialized, so that logging colors are applied from the start.
 *
 * Priority: --color-scheme CLI > ~/.config/ascii-chat/colors.toml > built-in default
 */

#include "common.h"
#include "platform/filesystem.h"
#include "ui/colors.h"
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Early Color Scheme Loading (called from main() before log_init)
 * ============================================================================ */

/**
 * @brief Scan argv for --color-scheme option (quick parse, no validation)
 * @param argc Argument count
 * @param argv Argument array
 * @return Color scheme name if found, NULL otherwise
 *
 * This is a simple linear scan that doesn't do full option parsing.
 * It's only used to find --color-scheme before logging is initialized.
 */
static const char *find_cli_color_scheme(int argc, const char *const argv[]) {
  for (int i = 1; i < argc - 1; i++) {
    if (strcmp(argv[i], "--color-scheme") == 0) {
      return argv[i + 1];
    }
  }
  return NULL;
}

/**
 * @brief Load color scheme from config files (checks multiple locations)
 * @param scheme Pointer to store loaded scheme
 * @return ASCIICHAT_OK if loaded from any location, ERROR_NOT_FOUND if none found
 *
 * Attempts to load user's custom color scheme from TOML config files using the
 * unified platform config search API. Searches standard locations in priority order:
 * 1. ~/.config/ascii-chat/colors.toml (highest priority, user config)
 * 2. /opt/homebrew/etc/ascii-chat/colors.toml (macOS Homebrew)
 * 3. /usr/local/etc/ascii-chat/colors.toml (Unix/Linux local)
 * 4. /etc/ascii-chat/colors.toml (system-wide, lowest priority)
 *
 * Uses override semantics: returns first match (highest priority).
 * Built-in defaults are used if no config file found.
 */
static asciichat_error_t load_config_color_scheme(color_scheme_t *scheme) {
  if (!scheme) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "scheme pointer is NULL");
  }

  /* Use platform abstraction to find colors.toml across standard locations */
  config_file_list_t config_files = {0};
  asciichat_error_t search_result = platform_find_config_file("colors.toml", &config_files);

  if (search_result != ASCIICHAT_OK) {
    /* Platform search failed - non-fatal, will use built-in defaults */
    return ERROR_NOT_FOUND;
  }

  /* Use first match (highest priority) - override semantics */
  asciichat_error_t load_result = ERROR_NOT_FOUND;
  if (config_files.count > 0) {
    load_result = colors_load_from_file(config_files.files[0].path, scheme);
  }

  /* Clean up search results */
  config_file_list_free(&config_files);

  return load_result;
}

/**
 * @brief Initialize color scheme early (before logging)
 * @param argc Argument count
 * @param argv Argument array
 * @return ASCIICHAT_OK on success, error code on failure (non-fatal)
 *
 * This function is called from main() BEFORE log_init() to apply color scheme
 * to logging before any log messages are printed.
 *
 * Priority order:
 * 1. --color-scheme CLI argument (highest priority)
 * 2. ~/.config/ascii-chat/colors.toml config file
 * 3. Built-in "pastel" default scheme (lowest priority)
 */
asciichat_error_t options_colors_init_early(int argc, const char *const argv[]) {
  /* Initialize the color system with defaults */
  asciichat_error_t result = colors_init();
  if (result != ASCIICHAT_OK) {
    /* Non-fatal: use built-in defaults */
    return ASCIICHAT_OK;
  }

  /* Step 1: Try to load from config file (~/.config/ascii-chat/colors.toml) */
  color_scheme_t config_scheme = {0};
  asciichat_error_t config_result = load_config_color_scheme(&config_scheme);
  if (config_result == ASCIICHAT_OK) {
    /* Config file loaded successfully, apply it */
    colors_set_active_scheme(config_scheme.name);
  }

  /* Step 2: CLI --color-scheme overrides config file */
  const char *cli_scheme = find_cli_color_scheme(argc, argv);
  if (cli_scheme) {
    asciichat_error_t cli_result = colors_set_active_scheme(cli_scheme);
    if (cli_result != ASCIICHAT_OK) {
      /* Invalid scheme name from CLI, continue with current scheme */
      return cli_result;
    }
  }

  return ASCIICHAT_OK;
}
