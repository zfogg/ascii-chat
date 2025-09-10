#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <locale.h>
#include <langinfo.h>

#ifdef __linux__
#include <term.h>
#include <curses.h>
#elif __APPLE__
#include <term.h>
#endif

#include "terminal_detect.h"
#include "common.h"
#include "options.h"

// Terminal size detection (moved from options.c)
int get_terminal_size(unsigned short int *width, unsigned short int *height) {
  struct winsize w;

  // First try ioctl - this works when stdout is a terminal
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
    *width = w.ws_col;
    *height = w.ws_row;
    // log_debug("Terminal size from ioctl: %dx%d", *width, *height);
    return 0;
  }

  // ioctl failed - likely because stdout is redirected
  // Try to get terminal size via $TTY (preferred) or /dev/tty (fallback)
  char *tty_path = getenv("TTY");
  if (tty_path && strlen(tty_path) > 0 && is_valid_tty_path(tty_path)) {
    int tty_fd = open(tty_path, O_RDONLY);
    if (tty_fd >= 0) {
      if (ioctl(tty_fd, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
        *width = w.ws_col;
        *height = w.ws_row;
        close(tty_fd);
        log_debug("Terminal size from $TTY (%s): %dx%d", tty_path, *width, *height);
        return 0;
      }
      close(tty_fd);
    }
  }

  // Fallback to /dev/tty if $TTY not available or failed
  int tty_fd = open("/dev/tty", O_RDONLY);
  if (tty_fd >= 0) {
    if (ioctl(tty_fd, TIOCGWINSZ, &w) == 0 && w.ws_col > 0 && w.ws_row > 0) {
      *width = w.ws_col;
      *height = w.ws_row;
      close(tty_fd);
      log_debug("Terminal size from /dev/tty: %dx%d", *width, *height);
      return 0;
    }
    close(tty_fd);
  } else {
    log_debug("Failed to open /dev/tty");
  }

  // Try environment variables as fallback
  char *cols_str = getenv("COLUMNS");
  char *lines_str = getenv("LINES");

  // Only use environment variables if BOTH are set and valid
  *width = OPT_WIDTH_DEFAULT;   // Default width (matches options.c)
  *height = OPT_HEIGHT_DEFAULT; // Default height (matches options.c)

  if (cols_str && lines_str) {
    int env_width = atoi(cols_str);
    int env_height = atoi(lines_str);

    // Both must be valid positive values
    if (env_width > 0 && env_height > 0) {
      *width = (unsigned short int)env_width;
      *height = (unsigned short int)env_height;
      log_debug("Terminal size from environment: %dx%d", *width, *height);
      return 0;
    }
  }

  // If we're in a terminal but couldn't detect size, try to use stty as last resort
  if (isatty(STDIN_FILENO) || isatty(STDOUT_FILENO)) {
    // We're in a terminal environment, so use more reasonable defaults
    *width = OPT_WIDTH_DEFAULT;
    *height = OPT_HEIGHT_DEFAULT;
    log_debug("Terminal size fallback (terminal but no detection): %dx%d", *width, *height);
    return 0; // Return success with defaults
  }

  // Final fallback for redirected output
  log_debug("Terminal size fallback (redirected output): %dx%d", *width, *height);
  return -1;
}

// Environment variable based detection
bool check_colorterm_variable(void) {
  char *colorterm = getenv("COLORTERM");
  if (!colorterm) {
    return false;
  }

  // Check for explicit truecolor support
  if (strcmp(colorterm, "truecolor") == 0 || strcmp(colorterm, "24bit") == 0) {
    return true;
  }

  return false;
}

bool check_term_variable_for_colors(void) {
  char *term = getenv("TERM");
  if (!term) {
    return false;
  }

  // log_debug("TERM=%s", term);

  // Check for color support indicators in TERM
  if (strstr(term, "256") || strstr(term, "color")) {
    return true;
  }

  return false;
}

int get_terminfo_color_count(void) {
  int colors = -1;

#if defined(__linux__) || defined(__APPLE__)
  // Check if TERM is set before calling setupterm to avoid ncurses error message
  char *term_env = getenv("TERM");
  if (term_env) {
    // Suppress stderr during setupterm to avoid "unknown terminal type" messages
    int stderr_fd = dup(STDERR_FILENO);
    int dev_null = open("/dev/null", O_WRONLY);
    dup2(dev_null, STDERR_FILENO);

    // Try to initialize terminfo
    int result = setupterm(NULL, STDOUT_FILENO, NULL);

    // Restore stderr
    dup2(stderr_fd, STDERR_FILENO);
    close(stderr_fd);
    close(dev_null);

    if (result == 0) { // setupterm returns 0 on success
      colors = tigetnum("colors");
      // log_debug("Terminfo colors: %d", colors);
    } else {
      log_debug("Failed to setup terminfo");
    }
  } else {
    log_debug("TERM environment variable not set, skipping terminfo detection");
  }
#endif

  return colors;
}

// Color support detection functions
bool detect_truecolor_support(void) {
  // Method 1: Check COLORTERM environment variable
  if (check_colorterm_variable()) {
    return true;
  }

  // Method 2: Check terminfo for very high color count
  int colors = get_terminfo_color_count();
  if (colors >= 16777216) {
    return true;
  }

  // Method 3: Check for specific terminal types known to support truecolor
  char *term = getenv("TERM");
  if (term) {
    // Common terminals with truecolor support
    if (strstr(term, "iterm") || strstr(term, "konsole") || strstr(term, "gnome") || strstr(term, "xfce4-terminal") ||
        strstr(term, "alacritty") || strstr(term, "kitty")) {
      return true;
    }
  }

  return false;
}

bool detect_256color_support(void) {
  // Method 1: Check terminfo
  int colors = get_terminfo_color_count();
  if (colors >= 256) {
    return true;
  }

  // Method 2: Check TERM variable
  char *term = getenv("TERM");
  return (term && strstr(term, "256")) != 0;
}

bool detect_16color_support(void) {
  // Method 1: Check terminfo
  int colors = get_terminfo_color_count();
  if (colors >= 16) {
    return true;
  }

  // Method 2: Check for basic color support in TERM
  char *term = getenv("TERM");
  if (term && (strstr(term, "color") || strstr(term, "ansi"))) {
    return true;
  }

  // Method 3: Most terminals support at least 16 colors
  if (term && strcmp(term, "dumb") != 0) {
    return true;
  }

  return false;
}

terminal_color_level_t detect_color_support(void) {
  if (detect_truecolor_support()) {
    return TERM_COLOR_TRUECOLOR;
  } else if (detect_256color_support()) {
    return TERM_COLOR_256;
  } else if (detect_16color_support()) {
    return TERM_COLOR_16;
  } else {
    return TERM_COLOR_NONE;
  }
}

// UTF-8 support detection
bool detect_utf8_support(void) {
  // Method 1: Check locale settings
  setlocale(LC_CTYPE, "");
  char const *encoding = nl_langinfo(CODESET);

  if (encoding) {
    // log_debug("Locale encoding: %s", encoding);

    if (strcasecmp(encoding, "utf8") == 0 || strcasecmp(encoding, "utf-8") == 0) {
      return true;
    }
  }

  // Method 2: Check environment variables
  char *lang = getenv("LANG");
  char *lc_all = getenv("LC_ALL");
  char *lc_ctype = getenv("LC_CTYPE");

  // Check for UTF-8 in any of these variables
  if ((lang && strstr(lang, "UTF-8")) || (lc_all && strstr(lc_all, "UTF-8")) ||
      (lc_ctype && strstr(lc_ctype, "UTF-8"))) {
    // log_debug("UTF-8 detected via environment variables");
    return true;
  }

  return false;
}

bool terminal_supports_unicode_blocks(void) {
  // Unicode block characters require UTF-8 support
  return detect_utf8_support();
}

// Main capability detection function
terminal_capabilities_t detect_terminal_capabilities(void) {
  terminal_capabilities_t caps = {0};

  // Detect color support level
  caps.color_level = detect_color_support();

  // Set capability flags based on color level
  switch (caps.color_level) {
  case TERM_COLOR_TRUECOLOR:
    caps.capabilities |= TERM_CAP_COLOR_TRUE;
    caps.capabilities |= TERM_CAP_COLOR_256;
    caps.capabilities |= TERM_CAP_COLOR_16;
    caps.color_count = 16777216;
    caps.detection_reliable = true;
    break;

  case TERM_COLOR_256:
    caps.capabilities |= TERM_CAP_COLOR_256;
    caps.capabilities |= TERM_CAP_COLOR_16;
    caps.color_count = 256;
    caps.detection_reliable = true;
    break;

  case TERM_COLOR_16:
    caps.capabilities |= TERM_CAP_COLOR_16;
    caps.color_count = 16;
    caps.detection_reliable = false; // Less certain
    break;

  case TERM_COLOR_NONE:
    caps.color_count = 0;
    caps.detection_reliable = false;
    break;
  }

  // Detect UTF-8 and Unicode support
  caps.utf8_support = detect_utf8_support();
  if (caps.utf8_support) {
    caps.capabilities |= TERM_CAP_UTF8;
  }

  // Background color support (assume yes if any color support)
  if (caps.color_level > TERM_COLOR_NONE) {
    caps.capabilities |= TERM_CAP_BACKGROUND;
  }

  // Store environment variables for debugging
  char *term = getenv("TERM");
  char *colorterm = getenv("COLORTERM");

  strncpy(caps.term_type, term ? term : "unknown", sizeof(caps.term_type) - 1);
  strncpy(caps.colorterm, colorterm ? colorterm : "", sizeof(caps.colorterm) - 1);

  // log_debug("Terminal capabilities detected: color_level=%d, capabilities=0x%x, term=%s, colorterm=%s",
  //           caps.color_level, caps.capabilities, caps.term_type, caps.colorterm);

  return caps;
}

// Helper functions
const char *terminal_color_level_name(terminal_color_level_t level) {
  switch (level) {
  case TERM_COLOR_NONE:
    return "monochrome";
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

const char *terminal_capabilities_summary(const terminal_capabilities_t *caps) {
  static char summary[256];

  snprintf(summary, sizeof(summary), "%s (%d colors), UTF-8: %s, TERM: %s, COLORTERM: %s",
           terminal_color_level_name(caps->color_level), caps->color_count,
           (caps->capabilities & TERM_CAP_UTF8) ? "yes" : "no", caps->term_type, caps->colorterm);

  return summary;
}

void print_terminal_capabilities(const terminal_capabilities_t *caps) {
  printf("Terminal Capabilities:\n");
  printf("  Color Level: %s\n", terminal_color_level_name(caps->color_level));
  printf("  Max Colors: %d\n", caps->color_count);
  printf("  UTF-8 Support: %s\n", caps->utf8_support ? "Yes" : "No");
  printf("  Background Colors: %s\n", (caps->capabilities & TERM_CAP_BACKGROUND) ? "Yes" : "No");
  printf("  Render Mode: %s\n", caps->render_mode == RENDER_MODE_FOREGROUND   ? "foreground"
                                : caps->render_mode == RENDER_MODE_BACKGROUND ? "background"
                                : caps->render_mode == RENDER_MODE_HALF_BLOCK ? "half-block"
                                                                              : "unknown");
  printf("  TERM: %s\n", caps->term_type);
  printf("  COLORTERM: %s\n", caps->colorterm);
  printf("  Detection Reliable: %s\n", caps->detection_reliable ? "Yes" : "No");
  printf("  Capabilities Bitmask: 0x%08x\n", caps->capabilities);
}

void test_terminal_output_modes(void) {
  printf("Testing terminal output modes:\n");

  // Test basic ANSI colors (16-color)
  printf("  16-color: ");
  for (int i = 30; i <= 37; i++) {
    printf("\033[%dm█\033[0m", i);
  }
  printf("\n");

  // Test 256-color mode
  printf("  256-color: ");
  for (int i = 0; i < 16; i++) {
    printf("\033[38;5;%dm█\033[0m", i);
  }
  printf("\n");

  // Test truecolor mode
  printf("  Truecolor: ");
  for (int i = 0; i < 16; i++) {
    int r = (i * 255) / 15;
    printf("\033[38;2;%d;0;0m█\033[0m", r);
  }
  printf("\n");

  // Test Unicode characters
  printf("  Unicode: ");
  printf("░▒▓\n");
}

// Apply color mode and background mode overrides to detected capabilities
terminal_capabilities_t apply_color_mode_override(terminal_capabilities_t caps) {
  // Handle color mode overrides
  switch (opt_color_mode) {
  case COLOR_MODE_AUTO:
    // Use detected capabilities as-is
    break;

  case COLOR_MODE_MONO:
    caps.color_level = TERM_COLOR_NONE;
    caps.color_count = 2;
    caps.capabilities &= ~(TERM_CAP_COLOR_TRUE | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16);
    break;

  case COLOR_MODE_16_COLOR:
    caps.color_level = TERM_COLOR_16;
    caps.color_count = 16;
    caps.capabilities &= ~(TERM_CAP_COLOR_TRUE | TERM_CAP_COLOR_256);
    caps.capabilities |= TERM_CAP_COLOR_16;
    break;

  case COLOR_MODE_256_COLOR:
    caps.color_level = TERM_COLOR_256;
    caps.color_count = 256;
    caps.capabilities &= ~TERM_CAP_COLOR_TRUE;
    caps.capabilities |= TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16;
    break;

  case COLOR_MODE_TRUECOLOR:
    caps.color_level = TERM_COLOR_TRUECOLOR;
    caps.color_count = 16777216;
    caps.capabilities |= TERM_CAP_COLOR_TRUE | TERM_CAP_COLOR_256 | TERM_CAP_COLOR_16;
    break;
  }

  // Handle render mode overrides
  switch (opt_render_mode) {
  case RENDER_MODE_FOREGROUND:
    // Default to foreground-only mode (disable background)
    // Background mode should be opt-in, not auto-detected
    caps.capabilities &= ~TERM_CAP_BACKGROUND;
    break;

  case RENDER_MODE_BACKGROUND:
    // Explicitly enable background rendering capability
    caps.capabilities |= TERM_CAP_BACKGROUND;
    break;

  case RENDER_MODE_HALF_BLOCK:
    // Enable UTF-8 and background capabilities for half-block mode
    caps.capabilities |= TERM_CAP_UTF8 | TERM_CAP_BACKGROUND;
    break;
  }

  // Handle UTF-8 override
  if (opt_force_utf8) {
    caps.utf8_support = true;
    caps.capabilities |= TERM_CAP_UTF8;
  }

  // Include client's render mode preference
  caps.render_mode = opt_render_mode;

  return caps;
}

// Validate that the given path is a safe TTY device under /dev/
int is_valid_tty_path(const char *path) {
  if (!path || strlen(path) < 6)
    return 0; // Too short to be /dev/x
  if (strncmp(path, "/dev/tty", 8) == 0)
    return 1;
  if (strncmp(path, "/dev/pts/", 9) == 0)
    return 1;
  if (strstr(path, "/dev/") != NULL)
    return 1;
  return 0;
}
