#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
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
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == -1) {
    // Fallback to environment variables
    char *cols_str = getenv("COLUMNS");
    char *lines_str = getenv("LINES");

    if (cols_str && lines_str) {
      *width = (unsigned short int)atoi(cols_str);
      *height = (unsigned short int)atoi(lines_str);
      log_debug("Terminal size from env: %dx%d", *width, *height);
      return 0;
    }

    // Final fallback to reasonable defaults
    *width = 80;
    *height = 24;
    log_debug("Terminal size fallback: %dx%d", *width, *height);
    return -1;
  }

  *width = w.ws_col;
  *height = w.ws_row;
  log_debug("Terminal size from ioctl: %dx%d", *width, *height);
  return 0;
}

// Environment variable based detection
bool check_colorterm_variable(void) {
  char *colorterm = getenv("COLORTERM");
  if (!colorterm) {
    return false;
  }

  log_debug("COLORTERM=%s", colorterm);

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

  log_debug("TERM=%s", term);

  // Check for color support indicators in TERM
  if (strstr(term, "256") || strstr(term, "color")) {
    return true;
  }

  return false;
}

int get_terminfo_color_count(void) {
  int colors = -1;

#if defined(__linux__) || defined(__APPLE__)
  // Try to initialize terminfo
  int result = setupterm(NULL, STDOUT_FILENO, NULL);
  if (result == 0) { // setupterm returns 0 on success
    colors = tigetnum("colors");
    log_debug("Terminfo colors: %d", colors);
  } else {
    log_debug("Failed to setup terminfo");
  }
#endif

  return colors;
}

// Color support detection functions
bool detect_truecolor_support(void) {
  // Method 1: Check COLORTERM environment variable
  if (check_colorterm_variable()) {
    log_debug("Truecolor detected via COLORTERM");
    return true;
  }

  // Method 2: Check terminfo for very high color count
  int colors = get_terminfo_color_count();
  if (colors >= 16777216) {
    log_debug("Truecolor detected via terminfo (%d colors)", colors);
    return true;
  }

  // Method 3: Check for specific terminal types known to support truecolor
  char *term = getenv("TERM");
  if (term) {
    // Common terminals with truecolor support
    if (strstr(term, "iterm") || strstr(term, "konsole") || strstr(term, "gnome") || strstr(term, "xfce4-terminal") ||
        strstr(term, "alacritty") || strstr(term, "kitty")) {
      log_debug("Truecolor detected via terminal type: %s", term);
      return true;
    }
  }

  return false;
}

bool detect_256color_support(void) {
  // Method 1: Check terminfo
  int colors = get_terminfo_color_count();
  if (colors >= 256) {
    log_debug("256-color detected via terminfo (%d colors)", colors);
    return true;
  }

  // Method 2: Check TERM variable
  char *term = getenv("TERM");
  if (term && strstr(term, "256")) {
    log_debug("256-color detected via TERM: %s", term);
    return true;
  }

  return false;
}

bool detect_16color_support(void) {
  // Method 1: Check terminfo
  int colors = get_terminfo_color_count();
  if (colors >= 16) {
    log_debug("16-color detected via terminfo (%d colors)", colors);
    return true;
  }

  // Method 2: Check for basic color support in TERM
  char *term = getenv("TERM");
  if (term && (strstr(term, "color") || strstr(term, "ansi"))) {
    log_debug("16-color detected via TERM: %s", term);
    return true;
  }

  // Method 3: Most terminals support at least 16 colors
  if (term && strcmp(term, "dumb") != 0) {
    log_debug("16-color assumed for non-dumb terminal: %s", term);
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
    log_debug("Locale encoding: %s", encoding);

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
    log_debug("UTF-8 detected via environment variables");
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

  log_debug("Terminal capabilities detected: color_level=%d, capabilities=0x%x, term=%s, colorterm=%s",
            caps.color_level, caps.capabilities, caps.term_type, caps.colorterm);

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
  printf("  TERM: %s\n", caps->term_type);
  printf("  COLORTERM: %s\n", caps->colorterm);
  printf("  Detection Reliable: %s\n", caps->detection_reliable ? "Yes" : "No");
  printf("  Capabilities Bitmask: 0x%08x\n", caps->capabilities);
}

void test_terminal_output_modes(void) {
  printf("Testing terminal output modes:\n");

  // Test basic ANSI colors (16-color)
  printf("\n16-color test:\n");
  for (int i = 30; i <= 37; i++) {
    printf("\033[%dm█\033[0m", i);
  }
  printf("\n");

  // Test 256-color mode
  printf("\n256-color test:\n");
  for (int i = 0; i < 16; i++) {
    printf("\033[38;5;%dm█\033[0m", i);
  }
  printf("\n");

  // Test truecolor mode
  printf("\nTruecolor test:\n");
  for (int i = 0; i < 16; i++) {
    int r = (i * 255) / 15;
    printf("\033[38;2;%d;0;0m█\033[0m", r);
  }
  printf("\n");

  // Test Unicode characters
  printf("\nUnicode test: ");
  printf("░▒▓\n");

  printf("\nTest complete.\n");
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

  // Handle background mode overrides
  switch (opt_background_mode) {
  case BACKGROUND_MODE_AUTO:
  case BACKGROUND_MODE_FOREGROUND:
    // Default to foreground-only mode (disable background)
    // Background mode should be opt-in, not auto-detected
    caps.capabilities &= ~TERM_CAP_BACKGROUND;
    break;

  case BACKGROUND_MODE_BACKGROUND:
    // Explicitly enable background rendering capability
    caps.capabilities |= TERM_CAP_BACKGROUND;
    break;
  }

  // Handle UTF-8 override
  if (opt_force_utf8) {
    caps.utf8_support = true;
    caps.capabilities |= TERM_CAP_UTF8;
  }

  return caps;
}
