/**
 * @file platform/terminal.c
 * @ingroup platform
 * @brief Cross-platform unified color detection and terminal capabilities
 */

#include "terminal.h"
#include "abstraction.h"
#include "../options/options.h"
#include "../options/rcu.h"
#include "../common.h"
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string.h>

/**
 * @brief Determine if color output should be used
 *
 * Priority order:
 * 1. If --color flag is set → ALWAYS use colors (force override)
 * 2. If CLAUDECODE env var is set → NEVER use colors (LLM automation)
 * 3. If output is not a TTY (piping) → NO colors
 * 4. If --color-mode=none → NO colors (user choice)
 * 5. Otherwise → Use colors
 *
 * @param fd File descriptor to check (STDOUT_FILENO or STDERR_FILENO)
 * @return true if colors should be used, false otherwise
 */
bool terminal_should_color_output(int fd) {
  // Check color_mode setting - if explicitly set to NONE, disable colors
  int color_mode = GET_OPTION(color_mode);
  if (color_mode == COLOR_MODE_NONE) {
    return false; // Color mode explicitly disabled
  }

  // Get current color setting (auto/true/false)
  int color_setting = GET_OPTION(color);

  // Priority 1: --color=true - Force colors ON (overrides everything)
  if (color_setting == COLOR_SETTING_TRUE) {
    return true; // ALWAYS colorize
  }

  // Priority 2: --color=false - Force colors OFF (overrides everything)
  if (color_setting == COLOR_SETTING_FALSE) {
    return false; // NEVER colorize
  }

  // Priority 3: --color=auto (default) - Smart detection
  // Check environment variable overrides first
  if (SAFE_GETENV("ASCII_CHAT_COLOR")) {
    return true; // ASCII_CHAT_COLOR env var forces colors
  }

  // CLAUDECODE environment variable (LLM automation) - disable colors
  if (SAFE_GETENV("CLAUDECODE")) {
    return false; // NO colors in Claude Code environment
  }

  // Check if output is a TTY (pipe detection)
  int is_tty = platform_isatty(fd);
  if (!is_tty) {
    return false; // NO colors when piping/redirecting
  }

  // Default: Use colors (we have TTY and no environment overrides)
  return true;
}

/**
 * @brief Get current color mode considering all overrides
 *
 * Determines effective color mode by checking:
 * 1. --color flag (force enable)
 * 2. --color-mode option (none/16/256/truecolor)
 * 3. Terminal capability detection
 *
 * @return Effective terminal_color_mode_t to use
 */
terminal_color_mode_t terminal_get_effective_color_mode(void) {
  // If colors are disabled entirely by terminal_should_color_output(), return NONE
  if (!terminal_should_color_output(STDOUT_FILENO)) {
    return TERM_COLOR_NONE;
  }

  // If --color-mode is set to specific value, use it
  terminal_color_mode_t color_mode = GET_OPTION(color_mode);
  if (color_mode != TERM_COLOR_AUTO) {
    return color_mode;
  }

  // Otherwise auto-detect from terminal capabilities
  // For now, return the detected color mode from terminal capabilities
  // This will be used by apply_color_mode_override() if needed
  return TERM_COLOR_AUTO; // Let the calling code handle auto-detection
}
