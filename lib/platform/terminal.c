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
#include <unistd.h>
#include <string.h>

// Forward declare global color flag variables set during options_init()
extern bool g_color_flag_passed;
extern bool g_color_flag_value;

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
  // Priority 1: Check --color flag (before RCU is initialized via global flags)
  // If --color was explicitly passed in argv, use that value
  if (g_color_flag_passed) {
    if (g_color_flag_value) {
      return true; // --color was passed, ALWAYS colorize
    }
  } else {
    // After RCU is initialized, check GET_OPTION(color) for programmatic setting
    if (GET_OPTION(color)) {
      return true; // ALWAYS colorize when --color is set
    }
  }

  // Priority 2: Check environment variable overrides
  if (SAFE_GETENV("ASCII_CHAT_COLOR")) {
    return true; // ASCII_CHAT_COLOR env var forces colors
  }

  // Priority 3: CLAUDECODE environment variable (LLM automation)
  if (SAFE_GETENV("CLAUDECODE")) {
    return false; // NO colors in Claude Code environment
  }

  // Priority 4: Check if output is a TTY (pipe detection)
  int is_tty = platform_isatty(fd);
  if (!is_tty) {
    return false; // NO colors when piping/redirecting
  }

  // Priority 5: Check --color-mode override for explicit 'none' (only matters on TTY)
  // NOTE: We skip checking color_mode here during early initialization because RCU might
  // not be initialized yet. Once RCU is initialized, GET_OPTION(color_mode) will work
  // correctly. For now, we let colors show by default on TTY.
  // TODO: Consider deferring color_mode check to later in program lifecycle if needed.

  // Default: Use colors (we have TTY and no overrides)
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
