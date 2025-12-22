/**
 * @file platform/posix/terminal.c
 * @ingroup platform
 * @brief 💻 POSIX terminal I/O with ANSI color detection and capability queries
 */

#ifndef _WIN32

#include "../terminal.h"
#include "../file.h"
#include "../internal.h"
#include "../../options.h"
#include "../../common.h"
#include "../../asciichat_errno.h"
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <limits.h>
#include <locale.h>

#ifndef __APPLE__
#include <langinfo.h>
#endif

/**
 * @brief Get terminal size
 * @param size Pointer to terminal_size_t structure to fill
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_get_size(terminal_size_t *size) {
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    size->rows = ws.ws_row;
    size->cols = ws.ws_col;
    return 0;
  }
  return -1;
}

/**
 * @brief Get the TTY device path
 * @return Path to TTY device (typically "/dev/tty")
 */
const char *get_tty_path(void) {
  return "/dev/tty";
}

/**
 * @brief Set terminal raw mode
 * @param enable True to enable raw mode, false to restore normal mode
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_set_raw_mode(bool enable) {
  static struct termios orig_termios;
  static bool saved = false;

  if (enable) {
    if (!saved) {
      tcgetattr(STDIN_FILENO, &orig_termios);
      saved = true;
    }
    struct termios raw = orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
  }
  if (saved) {
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  }
  return 0;
}

/**
 * @brief Set terminal echo mode
 * @param enable True to enable echo, false to disable
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_set_echo(bool enable) {
  struct termios tty;
  if (tcgetattr(STDIN_FILENO, &tty) != 0)
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to get terminal attributes");

  if (enable) {
    tty.c_lflag |= ECHO;
  } else {
    tty.c_lflag &= ~(tcflag_t)ECHO;
  }

  return tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

/**
 * @brief Check if terminal supports color output
 * @return True if color is supported, false otherwise
 */
bool terminal_supports_color(void) {
  const char *term = SAFE_GETENV("TERM");
  if (!term)
    return false;

  // Check for common color-capable terminals
  return (strstr(term, "color") != NULL || strstr(term, "xterm") != NULL || strstr(term, "screen") != NULL ||
          strstr(term, "vt100") != NULL || strstr(term, "linux") != NULL);
}

/**
 * @brief Check if terminal supports Unicode output
 * @return True if Unicode is supported, false otherwise
 */
bool terminal_supports_unicode(void) {
  const char *lang = SAFE_GETENV("LANG");
  const char *lc_all = SAFE_GETENV("LC_ALL");
  const char *lc_ctype = SAFE_GETENV("LC_CTYPE");

  const char *check;
  if (lc_all) {
    check = lc_all;
  } else if (lc_ctype) {
    check = lc_ctype;
  } else {
    check = lang;
  }
  if (!check)
    return false;

  return (strstr(check, "UTF-8") != NULL || strstr(check, "utf8") != NULL);
}

/**
 * @brief Check if terminal supports UTF-8 output (alias for unicode)
 * @return True if UTF-8 is supported, false otherwise
 */
bool terminal_supports_utf8(void) {
  return terminal_supports_unicode();
}

/**
 * @brief Clear the terminal screen
 * @return 0 on success, non-zero on failure
 */
asciichat_error_t terminal_clear_screen(void) {
  // Use ANSI escape codes instead of system("clear") to avoid command processor
  // \033[2J clears entire screen, \033[H moves cursor to home position
  printf("\033[2J\033[H");
  fflush(stdout);
  return ASCIICHAT_OK;
}

/**
 * @brief Move cursor to specified position
 * @param row Row position (0-based)
 * @param col Column position (0-based)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_move_cursor(int row, int col) {
  printf("\033[%d;%dH", row + 1, col + 1);
  (void)fflush(stdout);
  return 0;
}

/**
 * @brief Enable ANSI escape sequence processing
 * @note POSIX terminals typically support ANSI by default
 */
void terminal_enable_ansi(void) {
  // POSIX terminals typically support ANSI by default
  // No special enabling needed
}

/**
 * @brief Flush terminal output buffer
 * @param fd File descriptor for TTY output
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_flush(int fd) {
  if (fsync(fd) < 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to flush terminal output");
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Show or hide terminal cursor
 * @param fd File descriptor for TTY output
 * @param hide true to hide cursor, false to show
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_hide_cursor(int fd, bool hide) {
  if (hide) {
    if (dprintf(fd, "\033[?25l") < 0) {
      return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to hide cursor");
    }
  } else {
    if (dprintf(fd, "\033[?25h") < 0) {
      return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to show cursor");
    }
  }
  if (fsync(fd) < 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to sync cursor state");
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Move cursor to home position (0,0)
 * @param fd File descriptor for TTY output
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_cursor_home(int fd) {
  if (dprintf(fd, "\033[H") < 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to move cursor to home");
  }
  if (fsync(fd) < 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to sync cursor position");
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Clear terminal scrollback buffer
 * @param fd File descriptor for TTY output
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_clear_scrollback(int fd) {
  if (dprintf(fd, "\033[3J") < 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to clear scrollback buffer");
  }
  if (fsync(fd) < 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to sync scrollback clear");
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Get the current TTY device
 *
 * Implements multi-method TTY detection with fallback strategy:
 * 1. Check $TTY environment variable (most specific on macOS)
 * 2. Test standard file descriptors for TTY status
 * 3. Fall back to controlling terminal device (/dev/tty)
 *
 * @return TTY information structure with file descriptor and path
 */
tty_info_t get_current_tty(void) {
  tty_info_t result = {-1, NULL, false};

  // Method 1: Check $TTY environment variable first (most specific on macOS)
  const char *tty_env = SAFE_GETENV("TTY");
  if (tty_env && strlen(tty_env) > 0 && is_valid_tty_path(tty_env)) {
    // Strict validation: path must start with "/dev/", and not contain ".." or extra slashes after "/dev/"
    if (strncmp(tty_env, "/dev/", 5) == 0 && strstr(tty_env, "..") == NULL && strchr(tty_env + 5, '/') == NULL) {
      result.fd = platform_open(tty_env, PLATFORM_O_WRONLY);
      if (result.fd >= 0) {
        result.path = tty_env;
        result.owns_fd = true;
        log_debug("POSIX TTY from $TTY: %s (fd=%d)", tty_env, result.fd);
        return result;
      }
    }
  }

  // Method 2: Check standard file descriptors for TTY status
  if (isatty(STDIN_FILENO)) {
    result.fd = STDIN_FILENO;
    result.path = ttyname(STDIN_FILENO);
    result.owns_fd = false;
    log_debug("POSIX TTY from stdin: %s (fd=%d)", result.path ? result.path : "unknown", result.fd);
    return result;
  }
  if (isatty(STDOUT_FILENO)) {
    result.fd = STDOUT_FILENO;
    result.path = ttyname(STDOUT_FILENO);
    result.owns_fd = false;
    log_debug("POSIX TTY from stdout: %s (fd=%d)", result.path ? result.path : "unknown", result.fd);
    return result;
  }
  if (isatty(STDERR_FILENO)) {
    result.fd = STDERR_FILENO;
    result.path = ttyname(STDERR_FILENO);
    result.owns_fd = false;
    log_debug("POSIX TTY from stderr: %s (fd=%d)", result.path ? result.path : "unknown", result.fd);
    return result;
  }

  // Method 3: Try controlling terminal device
  result.fd = platform_open("/dev/tty", PLATFORM_O_WRONLY);
  if (result.fd >= 0) {
    result.path = "/dev/tty";
    result.owns_fd = true;
    log_debug("POSIX TTY from /dev/tty (fd=%d)", result.fd);
    return result;
  }

  log_debug("POSIX TTY: No TTY available");
  return result; // No TTY available
}

/**
 * @brief Validate TTY device path
 *
 * Checks if the given path represents a valid TTY device by attempting
 * to open it and verify it's a character device.
 *
 * @param path Path to TTY device to validate
 * @return true if valid TTY path, false otherwise
 */
bool is_valid_tty_path(const char *path) {
  if (!path || strlen(path) == 0) {
    return false;
  }

  int fd = platform_open(path, PLATFORM_O_WRONLY | O_NOCTTY);
  if (fd < 0) {
    return false;
  }

  bool is_tty = isatty(fd);
  close(fd);
  return is_tty;
}

/**
 * @brief Get terminal size with multiple fallback methods
 *
 * Attempts to determine terminal dimensions using multiple methods:
 * 1. ioctl(TIOCGWINSZ) - most reliable
 * 2. Environment variables ($LINES, $COLUMNS)
 * 3. Default fallback values
 *
 * @param width Pointer to store terminal width
 * @param height Pointer to store terminal height
 * @return 0 on success, -1 on failure
 */
asciichat_error_t get_terminal_size(unsigned short int *width, unsigned short int *height) {
  if (!width || !height) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for get_terminal_size");
  }

  // Method 1: ioctl(TIOCGWINSZ)
  struct winsize ws;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    if (ws.ws_col > 0 && ws.ws_row > 0) {
      *width = ws.ws_col;
      *height = ws.ws_row;
      log_debug("POSIX terminal size from ioctl: %dx%d", *width, *height);
      return ASCIICHAT_OK;
    }
  }

  // Method 2: Environment variables
  const char *lines_env = SAFE_GETENV("LINES");
  const char *cols_env = SAFE_GETENV("COLUMNS");
  if (lines_env && cols_env) {
    char *endptr_height, *endptr_width;
    long env_height = strtol(lines_env, &endptr_height, 10);
    long env_width = strtol(cols_env, &endptr_width, 10);

    // Validate conversion was successful and values are reasonable
    if (endptr_height != lines_env && endptr_width != cols_env && env_height > 0 && env_width > 0 &&
        env_height <= USHRT_MAX && env_width <= USHRT_MAX) {
      *width = (unsigned short int)env_width;
      *height = (unsigned short int)env_height;
      log_debug("POSIX terminal size from env: %dx%d", *width, *height);
      return 0;
    }
    log_debug("Invalid environment terminal dimensions: %s x %s", lines_env, cols_env);
  }

  // Method 3: Default fallback
  *width = 80;
  *height = 24;
  log_debug("POSIX terminal size fallback: %dx%d", *width, *height);
  return ASCIICHAT_OK; // Fallback succeeded with defaults
}

/**
 * @brief Detect comprehensive terminal capabilities
 *
 * Performs detailed terminal capability detection including:
 * - Color support levels (16/256/truecolor)
 * - UTF-8 encoding support
 * - Terminal type and environment analysis
 * - Render mode recommendations
 *
 * @return Complete terminal capabilities structure
 */
terminal_capabilities_t detect_terminal_capabilities(void) {
  terminal_capabilities_t caps = {0};

  // Get terminal type information
  const char *term = SAFE_GETENV("TERM");
  const char *colorterm = SAFE_GETENV("COLORTERM");

  SAFE_STRNCPY(caps.term_type, term ? term : "unknown", sizeof(caps.term_type));
  SAFE_STRNCPY(caps.colorterm, colorterm ? colorterm : "", sizeof(caps.colorterm));

  // Detect color support level
  caps.color_level = TERM_COLOR_NONE;
  caps.color_count = 0;

  if (colorterm) {
    if (strstr(colorterm, "truecolor") || strstr(colorterm, "24bit")) {
      caps.color_level = TERM_COLOR_TRUECOLOR;
      caps.color_count = 16777216; // 2^24
      caps.capabilities |= TERM_CAP_COLOR_TRUE | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16;
      log_debug("POSIX color detection: truecolor from $COLORTERM");
    }
  }

  if (caps.color_level == TERM_COLOR_NONE && term) {
    if (strstr(term, "256color")) {
      caps.color_level = TERM_COLOR_256;
      caps.color_count = 256;
      caps.capabilities |= TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16;
      log_debug("POSIX color detection: 256-color from $TERM");
    } else if (strstr(term, "color") || strstr(term, "xterm") || strstr(term, "screen") || strstr(term, "vt100") ||
               strstr(term, "linux")) {
      caps.color_level = TERM_COLOR_16;
      caps.color_count = 16;
      caps.capabilities |= TERM_CAP_COLOR_16;
      log_debug("POSIX color detection: 16-color from $TERM");
    }
  }

  // Detect UTF-8 support from locale
  caps.utf8_support = false;
  const char *lang = SAFE_GETENV("LANG");
  const char *lc_all = SAFE_GETENV("LC_ALL");
  const char *lc_ctype = SAFE_GETENV("LC_CTYPE");

  const char *check;
  if (lc_all) {
    check = lc_all;
  } else if (lc_ctype) {
    check = lc_ctype;
  } else {
    check = lang;
  }
  if (check && (strstr(check, "UTF-8") || strstr(check, "utf8"))) {
    caps.utf8_support = true;
    caps.capabilities |= TERM_CAP_UTF8;
    log_debug("POSIX UTF-8 detection: enabled from locale");
  }

#ifndef __APPLE__
  // Additional UTF-8 detection using langinfo (not available on macOS)
  if (!caps.utf8_support) {
    const char *codeset = nl_langinfo(CODESET);
    if (codeset && (strstr(codeset, "UTF-8") || strstr(codeset, "utf8"))) {
      caps.utf8_support = true;
      caps.capabilities |= TERM_CAP_UTF8;
      log_debug("POSIX UTF-8 detection: enabled from langinfo");
    }
  }
#endif

  // Determine render mode preference - default to foreground
  // Half-block mode should only be used when explicitly requested via --render-mode
  caps.render_mode = RENDER_MODE_FOREGROUND;

  // Check if background colors are supported for potential future use
  if (caps.color_level >= TERM_COLOR_16) {
    caps.capabilities |= TERM_CAP_BACKGROUND;
  }

  // Mark detection as reliable (POSIX has good environment variable support)
  caps.detection_reliable = true;

  // Don't log here - log after colors are initialized to avoid color changes
  // The log will be done in log_redetect_terminal_capabilities() after detection

  return caps;
}

/**
 * @brief Get human-readable color level name
 *
 * @param level Terminal color support level
 * @return String description of color level
 */
const char *terminal_color_level_name(terminal_color_level_t level) {
  switch (level) {
  case TERM_COLOR_NONE:
    return "none";
  case TERM_COLOR_16:
    return "16-color";
  case TERM_COLOR_256:
    return "256-color";
  case TERM_COLOR_TRUECOLOR:
    return "truecolor";
  default:
    return "unknown";
  }
}

/**
 * @brief Generate capabilities summary string
 *
 * Creates a concise summary of terminal capabilities for logging/debugging.
 *
 * @param caps Terminal capabilities structure
 * @return Static string with capabilities summary
 */
const char *terminal_capabilities_summary(const terminal_capabilities_t *caps) {
  static char summary[256];
  const char *render_mode_str;
  if (caps->render_mode == RENDER_MODE_HALF_BLOCK) {
    render_mode_str = "half-block";
  } else if (caps->render_mode == RENDER_MODE_BACKGROUND) {
    render_mode_str = "background";
  } else {
    render_mode_str = "foreground";
  }

  SAFE_SNPRINTF(summary, sizeof(summary), "%s, %s, %s, %s", terminal_color_level_name(caps->color_level),
                caps->utf8_support ? "UTF-8" : "ASCII", render_mode_str,
                caps->detection_reliable ? "reliable" : "fallback");
  return summary;
}

/**
 * @brief Print detailed terminal capabilities report
 *
 * Outputs comprehensive terminal capability information for debugging
 * and user information purposes.
 *
 * @param caps Terminal capabilities structure to report
 */
void print_terminal_capabilities(const terminal_capabilities_t *caps) {
  // Use printf instead of log_info since logging may not be initialized
  printf("Terminal Capabilities:\n");
  printf("  Color Level: %s\n", terminal_color_level_name(caps->color_level));
  printf("  Max Colors: %u\n", caps->color_count);
  printf("  UTF-8 Support: %s\n", caps->utf8_support ? "Yes" : "No");
  printf("  Background Colors: %s\n", caps->render_mode == RENDER_MODE_BACKGROUND ? "Yes" : "No");
  const char *render_mode_str_print;
  if (caps->render_mode == RENDER_MODE_HALF_BLOCK) {
    render_mode_str_print = "half-block";
  } else if (caps->render_mode == RENDER_MODE_BACKGROUND) {
    render_mode_str_print = "background";
  } else {
    render_mode_str_print = "foreground";
  }
  printf("  Render Mode: %s\n", render_mode_str_print);
  printf("  TERM: %s\n", caps->term_type);
  printf("  COLORTERM: %s\n", strlen(caps->colorterm) ? caps->colorterm : "(not set)");
  printf("  Detection Reliable: %s\n", caps->detection_reliable ? "Yes" : "No");
  printf("  Capabilities Bitmask: 0x%08x\n", caps->capabilities);
}

/**
 * @brief Test terminal output modes
 *
 * Outputs test patterns to verify terminal color and Unicode support.
 * Used for capability validation and user verification.
 */
void test_terminal_output_modes(void) {
  log_info("Testing terminal output modes:");

  // Test basic colors (16-color)
  printf("16-color test: ");
  for (int i = 30; i < 38; i++) {
    printf("\033[%dm█\033[0m", i);
  }
  printf("\n");

  // Test 256-color mode
  printf("256-color test: ");
  for (int i = 16; i < 24; i++) {
    printf("\033[38;5;%dm█\033[0m", i);
  }
  printf("\n");

  // Test truecolor
  printf("Truecolor test: ");
  printf("\033[38;2;255;0;0m█\033[38;2;0;255;0m█\033[38;2;0;0;255m█\033[0m\n");

  // Test Unicode half-blocks
  printf("Unicode test: ▀▄█▌▐░▒▓\n");

  (void)fflush(stdout);
}

/**
 * @brief Apply command-line color mode overrides
 *
 * Modifies detected capabilities based on user command-line preferences.
 * Allows users to force specific color modes or disable features.
 *
 * @param caps Original detected capabilities
 * @return Modified capabilities with overrides applied
 */
terminal_capabilities_t apply_color_mode_override(terminal_capabilities_t caps) {
#ifndef NDEBUG
  // In debug builds, force no-color mode for Claude Code (LLM doesn't need colors, saves tokens)
  if (opt_color_mode == COLOR_MODE_AUTO && platform_getenv("CLAUDECODE")) {
    log_debug("CLAUDECODE detected: forcing no color mode");
    caps.color_level = TERM_COLOR_NONE;
    caps.capabilities &= ~(uint32_t)(TERM_CAP_COLOR_16 | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_TRUE);
    caps.color_count = 0;
    return caps;
  }
#endif

  // Apply color mode override if specified in options (not auto mode)
  if (opt_color_mode != COLOR_MODE_AUTO) {
    // Map color_mode_t to terminal_color_level_t (enum values don't align)
    terminal_color_level_t override_level;
    switch (opt_color_mode) {
    case COLOR_MODE_NONE:
      override_level = TERM_COLOR_NONE;
      break;
    case COLOR_MODE_16_COLOR:
      override_level = TERM_COLOR_16;
      break;
    case COLOR_MODE_256_COLOR:
      override_level = TERM_COLOR_256;
      break;
    case COLOR_MODE_TRUECOLOR:
      override_level = TERM_COLOR_TRUECOLOR;
      break;
    default:
      override_level = caps.color_level; // Keep detected level
      break;
    }

    if (override_level != caps.color_level) {
      log_debug("Color override: %s -> %s", terminal_color_level_name(caps.color_level),
                terminal_color_level_name(override_level));
      caps.color_level = override_level;

      // Update capabilities flags and count
      caps.capabilities &= ~(uint32_t)(TERM_CAP_COLOR_16 | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_TRUE);
      switch (override_level) {
      case TERM_COLOR_TRUECOLOR:
        caps.capabilities |= TERM_CAP_COLOR_TRUE | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16;
        caps.color_count = 16777216;
        break;
      case TERM_COLOR_256:
        caps.capabilities |= TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16;
        caps.color_count = 256;
        break;
      case TERM_COLOR_16:
        caps.capabilities |= TERM_CAP_COLOR_16;
        caps.color_count = 16;
        break;
      case TERM_COLOR_NONE:
        caps.color_count = 0;
        break;
      }

      caps.detection_reliable = false; // Mark as overridden
    }
  }

  // Apply render mode from options (user can override via --render-mode)
  // The default opt_render_mode is RENDER_MODE_FOREGROUND which is what we want
  caps.render_mode = opt_render_mode;

  // Set default FPS based on platform
  extern int g_max_fps;
  if (g_max_fps > 0) {
    caps.desired_fps = (uint8_t)(g_max_fps > 144 ? 144 : g_max_fps);
  } else {
    caps.desired_fps = DEFAULT_MAX_FPS; // 60 FPS on Unix by default
  }

  return caps;
}

/**
 * Reset the terminal to default state
 * @param fd File descriptor for the terminal (e.g., from $TTY on macOS)
 * @return 0 on success, -1 on failure
 */
asciichat_error_t terminal_reset(int fd) {
  // Reset using ANSI escape sequence
  const char *reset_seq = "\033c"; // Full reset
  if (write(fd, reset_seq, strlen(reset_seq)) < 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to reset terminal");
  }
  return 0;
}

#endif // !_WIN32
