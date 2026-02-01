/**
 * @file lib/options/colors.h
 * @brief Early color scheme loading interface
 * @ingroup options
 */

#pragma once

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize color scheme early, before logging
 * @param argc Command-line argument count
 * @param argv Command-line arguments
 * @return ASCIICHAT_OK on success, error code on failure (non-fatal)
 *
 * Called from main() BEFORE log_init() to apply color scheme to logging.
 * Scans for --color-scheme and loads ~/.config/ascii-chat/colors.toml.
 *
 * Priority: --color-scheme CLI > colors.toml > built-in default
 */
asciichat_error_t options_colors_init_early(int argc, const char *const argv[]);

#ifdef __cplusplus
}
#endif
