/**
 * @file platform/posix/terminal.c
 * @ingroup platform
 * @brief 💻 POSIX terminal I/O with ANSI color detection and capability queries
 */

#ifndef _WIN32

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/internal.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/parsing.h>
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
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
    return ASCIICHAT_OK;
  }
  return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to get terminal size");
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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
      return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to set raw mode");
    }
    return ASCIICHAT_OK;
  }
  if (saved) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) != 0) {
      return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to restore terminal mode");
    }
  }
  return ASCIICHAT_OK;
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

  if (tcsetattr(STDIN_FILENO, TCSANOW, &tty) != 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to set echo mode");
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Check if terminal supports color output
 * @return True if color is supported, false otherwise
 */
// ============================================================================
// Terminal Capability Caching (POSIX Implementation)
// ============================================================================

// Static cache variables for terminal capabilities (initialized on first use)
static struct {
  int initialized;
  int color_support;
  int unicode_support;
} g_terminal_cache = {0};

/**
 * @brief Initialize terminal capability cache
 *
 * Detects terminal capabilities once and caches results to avoid repeated
 * environment variable lookups on each function call.
 */
static void _init_terminal_cache(void) {
  if (g_terminal_cache.initialized) {
    return;
  }

  // Detect color support
  const char *term = SAFE_GETENV("TERM");
  if (term && (strstr(term, "color") != NULL || strstr(term, "xterm") != NULL || strstr(term, "screen") != NULL ||
               strstr(term, "vt100") != NULL || strstr(term, "linux") != NULL)) {
    g_terminal_cache.color_support = 1;
  } else {
    g_terminal_cache.color_support = 0;
  }

  // Detect unicode support
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

  if (check && (strstr(check, "UTF-8") != NULL || strstr(check, "utf8") != NULL)) {
    g_terminal_cache.unicode_support = 1;
  } else {
    g_terminal_cache.unicode_support = 0;
  }

  g_terminal_cache.initialized = 1;
}

bool terminal_supports_color(void) {
  _init_terminal_cache();
  return g_terminal_cache.color_support;
}

/**
 * @brief Check if terminal supports Unicode output
 * @return True if Unicode is supported, false otherwise
 */
bool terminal_supports_unicode(void) {
  _init_terminal_cache();
  return g_terminal_cache.unicode_support;
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
  int fd = STDOUT_FILENO;
  if (dprintf(fd, "\033[2J\033[H") < 0) {
    return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to clear screen");
  }
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
 *
 * Note: For TTY file descriptors, fsync() returns EINVAL because TTYs are
 * character devices without persistent storage. We use tcdrain() for TTYs
 * which waits until all output has been transmitted.
 */
asciichat_error_t terminal_flush(int fd) {
  if (isatty(fd)) {
    // For TTY output in real-time rendering, skip flushing
    // The kernel buffer is small and will drain quickly without explicit flush
    // Flushing on every frame (tcdrain) causes significant latency in real-time rendering
    // POSIX write() to TTY is already line-buffered, so frames appear immediately
    return ASCIICHAT_OK;
  } else {
    // For regular files (e.g., when redirecting output), use fsync()
    // For pipes, fsync() fails with ENOTSUP which is expected and not an error
    if (fsync(fd) < 0 && errno != ENOTSUP) {
      return SET_ERRNO_SYS(ERROR_TERMINAL, "Failed to flush terminal output");
    }
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
  return terminal_flush(fd);
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
  return terminal_flush(fd);
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
  return terminal_flush(fd);
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
        log_dev("POSIX TTY from $TTY: %s (fd=%d)", tty_env, result.fd);
        return result;
      }
    }
  }

  // Method 2: Check standard file descriptors for TTY status
  if (isatty(STDIN_FILENO)) {
    result.fd = STDIN_FILENO;
    result.path = ttyname(STDIN_FILENO);
    result.owns_fd = false;
    log_dev("POSIX TTY from stdin: %s (fd=%d)", result.path ? result.path : "unknown", result.fd);
    return result;
  }
  if (isatty(STDOUT_FILENO)) {
    result.fd = STDOUT_FILENO;
    result.path = ttyname(STDOUT_FILENO);
    result.owns_fd = false;
    return result;
  }
  if (isatty(STDERR_FILENO)) {
    result.fd = STDERR_FILENO;
    result.path = ttyname(STDERR_FILENO);
    result.owns_fd = false;
    log_dev("POSIX TTY from stderr: %s (fd=%d)", result.path ? result.path : "unknown", result.fd);
    return result;
  }

  // Method 3: Try controlling terminal device (but only if stdout is not piped)
  // If stdout is piped/redirected, don't try /dev/tty - respect the user's intent to pipe output
  // Instead of opening /dev/tty when stdout is piped, just fail gracefully
  log_dev("POSIX TTY: No TTY available");
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

  struct winsize ws = {0};
  const char *lines_env;
  const char *cols_env;
  int tty_fd;
  int stdout_is_tty = isatty(STDOUT_FILENO);

  // Method 1: Try ioctl on stdout first (most common case)
  if (stdout_is_tty && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
    *width = ws.ws_col;
    *height = ws.ws_row;
    log_dev("POSIX terminal size from stdout ioctl: %dx%d", *width, *height);
    return ASCIICHAT_OK;
  }

  // When stdout is NOT a TTY (piped/redirected), skip stdin/stderr checks
  // because stdin might be a TTY connected to terminal, giving inconsistent results
  // Only try stdin/stderr if stdout IS a TTY (interactive terminal)
  if (stdout_is_tty) {
    // Method 2b: Try ioctl on stdin (only if stdout is TTY)
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
      *width = ws.ws_col;
      *height = ws.ws_row;
      log_dev("POSIX terminal size from stdin ioctl: %dx%d", *width, *height);
      return ASCIICHAT_OK;
    }

    // Method 2c: Try ioctl on stderr (only if stdout is TTY)
    if (ioctl(STDERR_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
      *width = ws.ws_col;
      *height = ws.ws_row;
      log_dev("POSIX terminal size from stderr ioctl: %dx%d", *width, *height);
      return ASCIICHAT_OK;
    }

    // Method 3: Environment variables (COLUMNS and LINES) - only for interactive terminals
    lines_env = SAFE_GETENV("LINES");
    cols_env = SAFE_GETENV("COLUMNS");
    if (lines_env && cols_env) {
      uint32_t env_height = 0, env_width = 0;
      // Parse height and width with safe range validation (1 to USHRT_MAX)
      if (parse_uint32(lines_env, &env_height, 1, (uint32_t)USHRT_MAX) == ASCIICHAT_OK &&
          parse_uint32(cols_env, &env_width, 1, (uint32_t)USHRT_MAX) == ASCIICHAT_OK) {
        *width = (unsigned short int)env_width;
        *height = (unsigned short int)env_height;
        log_dev("POSIX terminal size from env: %dx%d", *width, *height);
        return ASCIICHAT_OK;
      }
      log_dev("Invalid environment terminal dimensions: %s x %s", lines_env, cols_env);
    }
  } else {
    // stdout is piped/redirected - skip /dev/tty detection and use defaults
    log_dev("POSIX: stdout is redirected (not a TTY), skipping terminal detection and using defaults");
  }

  // Method 2d: Try opening /dev/tty directly (only if stdout IS a TTY - skip for piped output)
  if (stdout_is_tty) {
    tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd >= 0) {
      if (ioctl(tty_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *width = ws.ws_col;
        *height = ws.ws_row;
        close(tty_fd);
        log_dev("POSIX terminal size from /dev/tty ioctl: %dx%d", *width, *height);
        return ASCIICHAT_OK;
      }
      close(tty_fd);
    }
  }

  // Method 4: Default fallback (match OPT_WIDTH_DEFAULT and OPT_HEIGHT_DEFAULT)
  *width = OPT_WIDTH_DEFAULT;
  *height = OPT_HEIGHT_DEFAULT;
  log_dev("POSIX terminal size fallback: %dx%d (defaults)", *width, *height);
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
      log_dev("POSIX color detection: truecolor from $COLORTERM");
    }
  }

  if (caps.color_level == TERM_COLOR_NONE && term) {
    if (strstr(term, "256color")) {
      caps.color_level = TERM_COLOR_256;
      caps.color_count = 256;
      caps.capabilities |= TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16;
      log_dev("POSIX color detection: 256-color from $TERM");
    } else if (strstr(term, "color") || strstr(term, "xterm") || strstr(term, "screen") || strstr(term, "vt100") ||
               strstr(term, "linux")) {
      caps.color_level = TERM_COLOR_16;
      caps.color_count = 16;
      caps.capabilities |= TERM_CAP_COLOR_16;
      log_dev("POSIX color detection: 16-color from $TERM");
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
    log_dev("POSIX UTF-8 detection: enabled from locale");
  }

#ifndef __APPLE__
  // Additional UTF-8 detection using langinfo (not available on macOS)
  if (!caps.utf8_support) {
    const char *codeset = nl_langinfo(CODESET);
    if (codeset && (strstr(codeset, "UTF-8") || strstr(codeset, "utf8"))) {
      caps.utf8_support = true;
      caps.capabilities |= TERM_CAP_UTF8;
      log_dev("POSIX UTF-8 detection: enabled from langinfo");
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
const char *terminal_color_level_name(terminal_color_mode_t level) {
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
  // However, respect --color=true which explicitly forces colors ON
  if (GET_OPTION(color_mode) == COLOR_MODE_AUTO && platform_getenv("CLAUDECODE") &&
      GET_OPTION(color) != COLOR_SETTING_TRUE) {
    log_debug("CLAUDECODE detected: forcing no color mode");
    caps.color_level = TERM_COLOR_NONE;
    caps.capabilities &= ~(uint32_t)(TERM_CAP_COLOR_16 | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_TRUE);
    caps.color_count = 0;
    return caps;
  }
#endif

  // Apply color mode override if specified in options (not auto mode)
  if (GET_OPTION(color_mode) != COLOR_MODE_AUTO) {
    // Map terminal_color_mode_t to terminal_color_mode_t (enum values don't align)
    terminal_color_mode_t override_level;
    switch (GET_OPTION(color_mode)) {
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
      case TERM_COLOR_AUTO:
        // Auto-detect - no override needed
        break;
      }

      caps.detection_reliable = false; // Mark as overridden
    }
  }

  // Apply render mode from options (user can override via --render-mode)
  // The default render_mode is RENDER_MODE_FOREGROUND which is what we want
  caps.render_mode = GET_OPTION(render_mode);

  // Set default FPS based on platform
  int fps = GET_OPTION(fps);
  if (fps > 0) {
    caps.desired_fps = (uint8_t)(fps > 144 ? 144 : fps);
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

/**
 * Detect if terminal has a dark background
 * Uses environment variables and terminal hints to make a best guess
 */
bool terminal_has_dark_background(void) {
  // Check COLORFGBG environment variable (common in terminal emulators)
  // Format is "foreground;background" where values are color indices
  // Background >= 8 typically indicates a light background (bright colors)
  const char *colorfgbg = SAFE_GETENV("COLORFGBG");
  if (colorfgbg) {
    // Parse background color (after semicolon)
    const char *semicolon = strchr(colorfgbg, ';');
    if (semicolon && *(semicolon + 1)) {
      int bg = atoi(semicolon + 1);
      // Colors 0-7 are dark, 8-15 are light/bright
      // If background is a light color (>= 8), we have a light terminal
      if (bg >= 8) {
        return false; // Light background
      } else if (bg >= 0 && bg < 8) {
        return true; // Dark background
      }
    }
  }

  // Check for VS Code terminal (usually has dark background by default)
  const char *term_program = SAFE_GETENV("TERM_PROGRAM");
  if (term_program && strcmp(term_program, "vscode") == 0) {
    return true; // VS Code terminals are typically dark
  }

  // Check for iTerm2 (can query actual background color, but complicated)
  if (term_program && strcmp(term_program, "iTerm.app") == 0) {
    return true; // iTerm2 defaults to dark
  }

  // Check TERM variable for hints
  const char *term = SAFE_GETENV("TERM");
  if (term) {
    // Some terminal types that typically indicate dark backgrounds
    if (strstr(term, "256color") || strstr(term, "truecolor")) {
      return true; // Modern terminals typically use dark backgrounds
    }
  }

  // Default to dark background (most common for developer terminals)
  return true;
}

/**
 * Query terminal background color using OSC 11
 */
bool terminal_query_background_color(uint8_t *bg_r, uint8_t *bg_g, uint8_t *bg_b) {
  // Only query if stdout is a TTY
  if (!isatty(STDOUT_FILENO)) {
    log_debug("terminal_query_background_color: stdout is not a TTY");
    return false;
  }

  // Save terminal state
  struct termios old_tio, new_tio;
  if (tcgetattr(STDIN_FILENO, &old_tio) != 0) {
    return false;
  }

  // Set raw mode for reading response
  new_tio = old_tio;
  new_tio.c_lflag &= ~(ICANON | ECHO);
  new_tio.c_cc[VMIN] = 0;
  new_tio.c_cc[VTIME] = 1; // 0.1 second timeout
  tcsetattr(STDIN_FILENO, TCSANOW, &new_tio);

  // Send OSC 11 query (query background color)
  const char *query = "\033]11;?\033\\";
  log_debug("terminal_query_background_color: sending OSC 11 query");
  if (write(STDOUT_FILENO, query, strlen(query)) < 0) {
    log_debug("terminal_query_background_color: failed to write query");
    tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);
    return false;
  }

  // Read response (format: ESC]11;rgb:RRRR/GGGG/BBBBESC\ or ESC]11;rgb:RRRR/GGGG/BBBBBEL)
  char response[256];
  ssize_t total = 0;
  ssize_t n;

  // Read with timeout (up to 100ms)
  for (int i = 0; i < 10 && total < (ssize_t)sizeof(response) - 1; i++) {
    n = read(STDIN_FILENO, response + total, sizeof(response) - total - 1);
    if (n > 0) {
      total += n;
      // Check if we have a complete response
      if (total >= 2 && (response[total - 1] == '\\' || response[total - 1] == '\007')) {
        break;
      }
    }
  }

  // Restore terminal state
  tcsetattr(STDIN_FILENO, TCSANOW, &old_tio);

  if (total < 10) {
    log_debug("terminal_query_background_color: no response or too short (got %zd bytes)", total);
    return false; // No response or too short
  }

  response[total] = '\0';
  log_debug("terminal_query_background_color: received response (%zd bytes): %s", total, response);

  // Parse response: ESC]11;rgb:RRRR/GGGG/BBBB...
  const char *rgb_start = strstr(response, "rgb:");
  if (!rgb_start) {
    log_debug("terminal_query_background_color: no 'rgb:' found in response");
    return false;
  }
  rgb_start += 4; // Skip "rgb:"

  // Parse hex values (format is either RRRR/GGGG/BBBB or RR/GG/BB)
  unsigned int r16, g16, b16;
  if (sscanf(rgb_start, "%x/%x/%x", &r16, &g16, &b16) == 3) {
    // Convert from 16-bit to 8-bit
    *bg_r = (uint8_t)(r16 >> 8);
    *bg_g = (uint8_t)(g16 >> 8);
    *bg_b = (uint8_t)(b16 >> 8);
    log_debug("terminal_query_background_color: SUCCESS - detected RGB(%d, %d, %d)", *bg_r, *bg_g, *bg_b);
    return true;
  }

  log_debug("terminal_query_background_color: failed to parse RGB values");
  return false;
}

#endif // !_WIN32
