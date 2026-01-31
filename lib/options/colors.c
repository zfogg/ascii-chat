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
#include "platform/path.h"
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
 * @brief Load color scheme from config file (~/.config/ascii-chat/colors.toml)
 * @param scheme Pointer to store loaded scheme
 * @return ASCIICHAT_OK if loaded, error code if file not found or invalid
 *
 * Attempts to load user's custom color scheme from TOML config file.
 * Returns ERROR_NOT_FOUND if file doesn't exist (not an error condition).
 */
static asciichat_error_t load_config_color_scheme(color_scheme_t *scheme) {
  if (!scheme) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "scheme pointer is NULL");
  }

  /* Build config path: ~/.config/ascii-chat/colors.toml */
  char config_path[PLATFORM_MAX_PATH_LENGTH];

  /* Get config directory (e.g., ~/.config/ascii-chat/) */
  char *config_dir = platform_get_config_dir();
  if (!config_dir) {
    return ERROR_NOT_FOUND; /* No config directory */
  }

  /* Build full path to colors.toml */
  snprintf(config_path, sizeof(config_path), "%scolors.toml", config_dir);
  SAFE_FREE(config_dir);

  /* Check if file exists and is a regular file */
  if (!platform_is_regular_file(config_path)) {
    return ERROR_NOT_FOUND; /* File doesn't exist or not a regular file, not an error */
  }

  /* Load from TOML file */
  return colors_load_from_file(config_path, scheme);
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
