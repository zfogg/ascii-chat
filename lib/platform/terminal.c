/**
 * @file platform/terminal.c
 * @ingroup platform
 * @brief Cross-platform unified color detection and terminal capabilities
 */

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/log.h>
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
  // Special case: Show colors for --help and --version even if not a TTY
  // Check for help/version in global argv (set by main.c early)
  extern int g_argc;
  extern char **g_argv;
  if (g_argc > 1 && g_argv) {
    for (int i = 1; i < g_argc; i++) {
      if (strcmp(g_argv[i], "--help") == 0 || strcmp(g_argv[i], "-h") == 0 || strcmp(g_argv[i], "--version") == 0 ||
          strcmp(g_argv[i], "-v") == 0 || strcmp(g_argv[i], "--show-capabilities") == 0) {
        // For help/version/show-capabilities, use colors only if output is a TTY
        // (respect piping convention: no colors unless --color=true)
        int is_tty = platform_isatty(fd);
        if (!is_tty) {
          // Piping detected - fall through to check environment overrides
          break;
        }

        // It's a TTY - show colors for help. Check environment overrides.
        if (SAFE_GETENV("ASCII_CHAT_COLOR")) {
          return true; // ASCII_CHAT_COLOR env var forces colors
        }

        // CLAUDECODE environment variable (LLM automation) - disable colors
        if (SAFE_GETENV("CLAUDECODE")) {
          return false; // NO colors in Claude Code environment
        }

        // For help output to TTY, show colors by default
        return true;
      }
    }
  }

  // Check environment variable overrides
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

/* ============================================================================
 * Interactive Mode and TTY State Detection
 * ============================================================================ */

bool terminal_is_stdin_tty(void) {
  return platform_isatty(STDIN_FILENO) != 0;
}

bool terminal_is_stdout_tty(void) {
  return platform_isatty(STDOUT_FILENO) != 0;
}

bool terminal_is_stderr_tty(void) {
  return platform_isatty(STDERR_FILENO) != 0;
}

bool terminal_is_interactive(void) {
  return terminal_is_stdin_tty() && terminal_is_stdout_tty();
}

bool terminal_is_piped_output(void) {
  return !terminal_is_stdout_tty();
}

bool terminal_should_force_stderr(void) {
  // If stdout is piped/redirected, force logs to stderr to avoid corruption
  if (terminal_is_piped_output()) {
    // Unless in TESTING environment where test framework may capture stdout
    const char *testing = SAFE_GETENV("TESTING");
    if (testing && strcmp(testing, "1") == 0) {
      return false; // Allow stdout in test environments
    }
    return true; // Force stderr when piped
  }
  return false; // stdout is TTY, no need to force stderr
}

bool terminal_can_prompt_user(void) {
  // Must be fully interactive (stdin and stdout are TTYs)
  if (!terminal_is_interactive()) {
    return false;
  }

  // Must not be in snapshot mode (non-interactive capture)
  if (GET_OPTION(snapshot_mode)) {
    return false;
  }

  // Must not have automated prompt responses configured
  const char *auto_response = SAFE_GETENV("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
  if (auto_response && *auto_response != '\0') {
    return false; // Automated responses configured, not interactive
  }

  // All checks passed, interactive prompts are appropriate
  return true;
}

int terminal_choose_log_fd(log_level_t level) {
  // When force_stderr is enabled (client mode), send ALL logs to stderr
  // When terminal is NOT interactive (piped/redirected), send ALL logs to stderr
  // to keep stdout clean for piped data (JSON, frames, etc.)
  if (log_get_force_stderr() || !terminal_is_interactive()) {
    return STDERR_FILENO;
  }

  // In interactive mode, route based on level:
  // WARN and above (ERROR, FATAL) go to stderr
  // Others (DEV, DEBUG, INFO) go to stdout
  if (level >= LOG_WARN) {
    return STDERR_FILENO;
  }

  return STDOUT_FILENO;
}

/* ============================================================================
 * Theme-Aware Renderer Color Selection
 * ============================================================================ */

/**
 * @brief Get theme-aware default foreground color for the renderer
 * @param theme Terminal theme (dark or light)
 * @param out_r Pointer to store red component (0-255)
 * @param out_g Pointer to store green component (0-255)
 * @param out_b Pointer to store blue component (0-255)
 *
 * Returns appropriate default text color based on terminal theme.
 * Both Linux and macOS renderers use this for consistent color selection.
 */
void terminal_get_default_foreground_color(int theme, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
  if (theme == 1) { // TERM_RENDERER_THEME_LIGHT
    *out_r = TERMINAL_COLOR_THEME_LIGHT_FG_R;
    *out_g = TERMINAL_COLOR_THEME_LIGHT_FG_G;
    *out_b = TERMINAL_COLOR_THEME_LIGHT_FG_B;
  } else { // TERM_RENDERER_THEME_DARK or TERM_RENDERER_THEME_AUTO
    *out_r = TERMINAL_COLOR_THEME_DARK_FG_R;
    *out_g = TERMINAL_COLOR_THEME_DARK_FG_G;
    *out_b = TERMINAL_COLOR_THEME_DARK_FG_B;
  }
}

/**
 * @brief Get theme-aware default background color for the renderer
 * @param theme Terminal theme (dark or light)
 * @param out_r Pointer to store red component (0-255)
 * @param out_g Pointer to store green component (0-255)
 * @param out_b Pointer to store blue component (0-255)
 *
 * Returns appropriate default background color based on terminal theme.
 * Light theme uses white background, dark theme uses black background.
 */
void terminal_get_default_background_color(int theme, uint8_t *out_r, uint8_t *out_g, uint8_t *out_b) {
  if (theme == 1) { // TERM_RENDERER_THEME_LIGHT
    *out_r = TERMINAL_COLOR_THEME_LIGHT_BG_R;
    *out_g = TERMINAL_COLOR_THEME_LIGHT_BG_G;
    *out_b = TERMINAL_COLOR_THEME_LIGHT_BG_B;
  } else { // TERM_RENDERER_THEME_DARK or TERM_RENDERER_THEME_AUTO
    *out_r = TERMINAL_COLOR_THEME_DARK_BG_R;
    *out_g = TERMINAL_COLOR_THEME_DARK_BG_G;
    *out_b = TERMINAL_COLOR_THEME_DARK_BG_B;
  }
}

