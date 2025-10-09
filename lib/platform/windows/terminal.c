/**
 * @file terminal.c
 * @brief Windows terminal I/O implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides Windows Console API wrappers for the platform abstraction layer,
 * enabling cross-platform terminal operations using a unified API.
 */

#ifdef _WIN32

#include "../../options.h"
#include "../../common.h"
#include "../windows_compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <io.h>
#include <fcntl.h>
#include <string.h>

/**
 * @brief Get terminal size
 * @param size Pointer to terminal_size_t structure to fill
 * @return 0 on success, -1 on failure
 */
int terminal_get_size(terminal_size_t *size) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

  if (h == INVALID_HANDLE_VALUE) {
    return -1;
  }

  if (GetConsoleScreenBufferInfo(h, &csbi)) {
    size->cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    size->rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return 0;
  }

  return -1;
}

/**
 * @brief Get the TTY device path
 * @return Path to console device ("CON" on Windows)
 */
const char *get_tty_path(void) {
  return "CON";
}

/**
 * @brief Set terminal raw mode
 * @param enable True to enable raw mode, false to restore normal mode
 * @return 0 on success, -1 on failure
 */
int terminal_set_raw_mode(bool enable) {
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE)
    return -1;

  DWORD mode;
  if (!GetConsoleMode(hStdin, &mode))
    return -1;

  if (enable) {
    mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
  } else {
    mode |= (ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT);
  }

  return SetConsoleMode(hStdin, mode) ? 0 : -1;
}

/**
 * @brief Set terminal echo mode
 * @param enable True to enable echo, false to disable
 * @return 0 on success, -1 on failure
 */
int terminal_set_echo(bool enable) {
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  if (hStdin == INVALID_HANDLE_VALUE)
    return -1;

  DWORD mode;
  if (!GetConsoleMode(hStdin, &mode))
    return -1;

  if (enable) {
    mode |= ENABLE_ECHO_INPUT;
  } else {
    mode &= ~ENABLE_ECHO_INPUT;
  }

  return SetConsoleMode(hStdin, mode) ? 0 : -1;
}

/**
 * @brief Check if terminal supports UTF-8 output
 * @return True if UTF-8 is supported, false otherwise
 * @note Checks console code page and terminal type
 */
bool terminal_supports_utf8(void) {
  // Check if console output code page is UTF-8 (65001)
  UINT cp = GetConsoleOutputCP();
  if (cp == 65001) {
    return true;
  }

  // Check for Windows Terminal via environment variable
  const char *wt_session = SAFE_GETENV("WT_SESSION");
  if (wt_session != NULL) {
    return true; // Windows Terminal always supports UTF-8
  }

  // Check for ConEmu
  const char *conemu = SAFE_GETENV("ConEmuPID");
  if (conemu != NULL) {
    return true; // ConEmu supports UTF-8
  }

  // Check for newer Windows Console Host with VT support
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE) {
    DWORD mode;
    if (GetConsoleMode(hOut, &mode)) {
      // ENABLE_VIRTUAL_TERMINAL_PROCESSING (0x0004) indicates modern console
      if (mode & 0x0004) {
        return true; // Modern Windows console with VT support likely has UTF-8
      }
    }
  }

  return false; // Default to no UTF-8 support
}

/**
 * @brief Check if terminal supports color output
 * @return True if color is supported, false otherwise
 * @note Windows 10+ supports ANSI colors
 */
bool terminal_supports_color(void) {
  // Windows 10+ supports ANSI colors
  return true;
}

/**
 * @brief Check if terminal supports Unicode output
 * @return True if Unicode is supported, false otherwise
 * @note Windows supports Unicode through wide character APIs
 */
bool terminal_supports_unicode(void) {
  // Windows supports Unicode through wide character APIs
  return true;
}

/**
 * @brief Clear the terminal screen
 * @return 0 on success, non-zero on failure
 */
int terminal_clear_screen(void) {
  system("cls");
  return 0;
}

/**
 * @brief Move cursor to specified position
 * @param row Row position (0-based)
 * @param col Column position (0-based)
 * @return 0 on success, -1 on failure
 */
int terminal_move_cursor(int row, int col) {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hConsole == INVALID_HANDLE_VALUE)
    return -1;

  COORD coord;
  coord.X = (SHORT)col;
  coord.Y = (SHORT)row;

  return SetConsoleCursorPosition(hConsole, coord) ? 0 : -1;
}

/**
 * @brief Enable ANSI escape sequence processing
 * @note Enable ANSI escape sequences on Windows 10+
 */
void terminal_enable_ansi(void) {
  // Enable ANSI escape sequences on Windows 10+
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE) {
    DWORD mode;
    if (GetConsoleMode(hOut, &mode)) {
      mode |= 0x0004; // ENABLE_VIRTUAL_TERMINAL_PROCESSING
      SetConsoleMode(hOut, mode);
    }
  }
}

// ============================================================================
// Extended Terminal Control
// ============================================================================

/**
 * @brief Set terminal buffering mode
 * @param line_buffered True for line buffering, false for no buffering
 * @return 0 on success, -1 on failure
 */
int terminal_set_buffering(bool line_buffered) {
  // Windows console doesn't have direct line buffering control
  // This is typically handled at the C runtime level
  if (line_buffered) {
    setvbuf(stdout, NULL, _IOLBF, 0);
  } else {
    setvbuf(stdout, NULL, _IONBF, 0);
  }
  return 0;
}

/**
 * @brief Flush terminal output
 * @param fd File descriptor (ignored on Windows - console APIs use stdout)
 * @return 0 on success, -1 on failure
 */
int terminal_flush(int fd) {
  (void)fd; // Windows console APIs operate on stdout, not arbitrary file descriptors
  return fflush(stdout);
}

/**
 * @brief Get current cursor position
 * @param row Pointer to store row position
 * @param col Pointer to store column position
 * @return 0 on success, -1 on failure
 */
int terminal_get_cursor_position(int *row, int *col) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

  if (hOut == INVALID_HANDLE_VALUE) {
    return -1;
  }

  if (!GetConsoleScreenBufferInfo(hOut, &csbi)) {
    return -1;
  }

  if (row)
    *row = csbi.dwCursorPosition.Y + 1; // Convert to 1-based
  if (col)
    *col = csbi.dwCursorPosition.X + 1; // Convert to 1-based

  return 0;
}

/**
 * @brief Save cursor position (using ANSI if available)
 * @return 0 on success, -1 on failure
 */
int terminal_save_cursor(void) {
  // Try ANSI escape sequence first (Windows 10+)
  printf("\033[s");
  fflush(stdout);
  return 0;
}

/**
 * @brief Restore cursor position (using ANSI if available)
 * @return 0 on success, -1 on failure
 */
int terminal_restore_cursor(void) {
  // Try ANSI escape sequence first (Windows 10+)
  printf("\033[u");
  fflush(stdout);
  return 0;
}

/**
 * @brief Set terminal window title
 * @param title New window title
 * @return 0 on success, -1 on failure
 */
int terminal_set_title(const char *title) {
  if (SetConsoleTitleA(title)) {
    return 0;
  }
  // Fallback to ANSI escape sequence
  printf("\033]0;%s\007", title);
  fflush(stdout);
  return 0;
}

/**
 * @brief Ring terminal bell
 * @return 0 on success, -1 on failure
 */
int terminal_ring_bell(void) {
  // Use Windows beep
  Beep(800, 200); // 800Hz for 200ms
  return 0;
}

/**
 * @brief Hide or show cursor
 * @param fd File descriptor (ignored on Windows - console APIs use stdout)
 * @param hide True to hide cursor, false to show
 * @return 0 on success, -1 on failure
 */
int terminal_hide_cursor(int fd, bool hide) {
  (void)fd; // Windows console APIs operate on stdout, not arbitrary file descriptors
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_CURSOR_INFO cursorInfo;

  if (hOut == INVALID_HANDLE_VALUE) {
    // Fallback to ANSI
    printf(hide ? "\033[?25l" : "\033[?25h");
    fflush(stdout);
    return 0;
  }

  if (!GetConsoleCursorInfo(hOut, &cursorInfo)) {
    return -1;
  }

  cursorInfo.bVisible = !hide;

  if (!SetConsoleCursorInfo(hOut, &cursorInfo)) {
    return -1;
  }

  return 0;
}

/**
 * @brief Set scroll region (using ANSI)
 * @param top Top line of scroll region (1-based)
 * @param bottom Bottom line of scroll region (1-based)
 * @return 0 on success, -1 on failure
 */
int terminal_set_scroll_region(int top, int bottom) {
  // Use ANSI escape sequence (Windows 10+ with VT processing enabled)
  printf("\033[%d;%dr", top, bottom);
  fflush(stdout);
  return 0;
}

/**
 * @brief Reset terminal to default state
 * @param fd File descriptor for the terminal
 * @return 0 on success, -1 on failure
 */
int terminal_reset(int fd) {
  // Reset using ANSI escape sequence
  const char *reset_seq = "\033c"; // Full reset
  HANDLE h = (HANDLE)_get_osfhandle(fd);

  if (h != INVALID_HANDLE_VALUE) {
    DWORD written;
    WriteConsole(h, reset_seq, (DWORD)strlen(reset_seq), &written, NULL);
  } else {
    // Fall back to stdout if handle is invalid
    printf("%s", reset_seq);
    fflush(stdout);
  }

  // Also reset Windows console attributes
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  if (hOut != INVALID_HANDLE_VALUE) {
    SetConsoleTextAttribute(hOut, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
  }

  return 0;
}

/**
 * @brief Move cursor to home position (0,0)
 * @param fd File descriptor (ignored on Windows - console APIs use stdout)
 * @return 0 on success, -1 on failure
 */
int terminal_cursor_home(int fd) {
  (void)fd; // Windows console APIs operate on stdout, not arbitrary file descriptors
  // Use ANSI escape sequence (Windows 10+ with VT processing enabled)
  if (printf("\033[H") < 0) {
    return -1;
  }
  return fflush(stdout);
}

/**
 * @brief Clear terminal scrollback buffer
 * @param fd File descriptor (ignored on Windows - console APIs use stdout)
 * @return 0 on success, -1 on failure
 */
int terminal_clear_scrollback(int fd) {
  (void)fd; // Windows console APIs operate on stdout, not arbitrary file descriptors
  // Use ANSI escape sequence (Windows 10+ with VT processing enabled)
  if (printf("\033[3J") < 0) {
    return -1;
  }
  return fflush(stdout);
}

// ============================================================================
// Terminal Detection and Capabilities (Windows)
// ============================================================================

/**
 * Get terminal size with Windows Console API - simpler than Unix
 */
int get_terminal_size(unsigned short int *width, unsigned short int *height) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);

  log_error("DEBUG_TERMINAL: get_terminal_size() called");

  if (console_handle == INVALID_HANDLE_VALUE) {
    DWORD error = GetLastError();
    log_error("DEBUG_TERMINAL: Failed to get console handle, error=%lu", error);
    goto fallback;
  }

  log_error("DEBUG_TERMINAL: Got console handle=%p, calling GetConsoleScreenBufferInfo", console_handle);

  if (GetConsoleScreenBufferInfo(console_handle, &csbi)) {
    // Use window size, not buffer size
    *width = (unsigned short int)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    *height = (unsigned short int)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    log_error("DEBUG_TERMINAL: SUCCESS! Windows console size: %dx%d", *width, *height);
    log_error("DEBUG_TERMINAL: Window coords: Left=%d, Top=%d, Right=%d, Bottom=%d", csbi.srWindow.Left,
              csbi.srWindow.Top, csbi.srWindow.Right, csbi.srWindow.Bottom);
    log_error("DEBUG_TERMINAL: Buffer size: %dx%d", csbi.dwSize.X, csbi.dwSize.Y);
    return 0;
  }

  DWORD error = GetLastError();
  log_error("DEBUG_TERMINAL: GetConsoleScreenBufferInfo FAILED: error=%lu", error);

fallback:
  log_error("DEBUG_TERMINAL: Entering fallback mode for terminal size detection");

  // Environment variable fallback
  char *cols_str = SAFE_GETENV("COLUMNS");
  char *lines_str = SAFE_GETENV("LINES");

  log_error("DEBUG_TERMINAL: Environment variables: COLUMNS='%s', LINES='%s'", cols_str ? cols_str : "NULL",
            lines_str ? lines_str : "NULL");

  *width = OPT_WIDTH_DEFAULT;
  *height = OPT_HEIGHT_DEFAULT;

  log_error("DEBUG_TERMINAL: Set default dimensions: %dx%d", *width, *height);

  if (cols_str && lines_str) {
    int env_width = atoi(cols_str);
    int env_height = atoi(lines_str);

    log_error("DEBUG_TERMINAL: Parsed environment: width=%d, height=%d", env_width, env_height);

    if (env_width > 0 && env_height > 0) {
      *width = (unsigned short int)env_width;
      *height = (unsigned short int)env_height;
      log_error("DEBUG_TERMINAL: Using environment size: %dx%d", *width, *height);
      return 0;
    }
  }

  log_error("DEBUG_TERMINAL: FINAL FALLBACK: Using default dimensions %dx%d", *width, *height);
  return -1;
}

/**
 * Get current TTY on Windows - just use CON device
 */
tty_info_t get_current_tty(void) {
  tty_info_t result = {-1, NULL, false};

  // On Windows, use CON for console output
  result.fd = SAFE_OPEN("CON", _O_WRONLY, 0);
  if (result.fd >= 0) {
    result.path = "CON";
    result.owns_fd = true;
    log_debug("Windows TTY: CON (fd=%d)", result.fd);
    return result;
  }

  log_debug("Failed to open CON device: %s", SAFE_STRERROR(errno));
  return result; // No TTY available
}

/**
 * Validate TTY path on Windows - only CON is valid
 */
bool is_valid_tty_path(const char *path) {
  if (!path || strlen(path) == 0) {
    return false;
  }

  // Only CON is valid on Windows
  return (_stricmp(path, "CON") == 0);
}

/**
 * Detect color support on Windows - check ANSI support
 */
static terminal_color_level_t detect_windows_color_support(void) {
  // Check if we can enable ANSI processing
  HANDLE console_handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (console_handle == INVALID_HANDLE_VALUE) {
    log_debug("Cannot get console handle for color detection");
    return TERM_COLOR_NONE;
  }

  DWORD console_mode = 0;
  if (!GetConsoleMode(console_handle, &console_mode)) {
    log_debug("Cannot get console mode for color detection");
    return TERM_COLOR_NONE;
  }

  // Try to enable ANSI escape sequence processing
  DWORD new_mode = console_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  if (SetConsoleMode(console_handle, new_mode)) {
    // Success! Windows supports ANSI escapes, assume truecolor
    log_debug("Windows console supports ANSI escape sequences");
    return TERM_COLOR_TRUECOLOR;
  }

  log_debug("Windows console does not support ANSI escape sequences");
  return TERM_COLOR_NONE;
}

/**
 * Detect UTF-8 support on Windows - check console code page
 */
static bool detect_windows_utf8_support(void) {
  UINT codepage = GetConsoleOutputCP();
  log_debug("Windows console code page: %u", codepage);
  return (codepage == CP_UTF8);
}

/**
 * Detect terminal capabilities on Windows - much simpler than Unix!
 */
terminal_capabilities_t detect_terminal_capabilities(void) {
  terminal_capabilities_t caps = {0};

  // Detect color support level
  caps.color_level = detect_windows_color_support();

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
    caps.detection_reliable = false;
    break;

  case TERM_COLOR_NONE:
    caps.color_count = 0;
    caps.detection_reliable = false;
    break;
  }

  // Detect UTF-8 support
  caps.utf8_support = detect_windows_utf8_support();
  if (caps.utf8_support) {
    caps.capabilities |= TERM_CAP_UTF8;
  }

  // Background color support (assume yes if any color support)
  if (caps.color_level > TERM_COLOR_NONE) {
    caps.capabilities |= TERM_CAP_BACKGROUND;
  }

  // Store environment variables for debugging
  char *term = SAFE_GETENV("TERM");
  char *colorterm = SAFE_GETENV("COLORTERM");

  SAFE_STRNCPY(caps.term_type, term ? term : "windows-console", sizeof(caps.term_type) - 1);
  SAFE_STRNCPY(caps.colorterm, colorterm ? colorterm : "", sizeof(caps.colorterm) - 1);

  // Set default FPS for Windows terminals
  extern int g_max_fps;
  if (g_max_fps > 0) {
    caps.desired_fps = (uint8_t)(g_max_fps > 144 ? 144 : g_max_fps);
  } else {
    caps.desired_fps = DEFAULT_MAX_FPS; // 60 FPS on Windows with timeBeginPeriod(1)
  }

  log_debug("Windows terminal capabilities: color_level=%d, capabilities=0x%x, utf8=%s, fps=%d", caps.color_level,
            caps.capabilities, caps.utf8_support ? "yes" : "no", caps.desired_fps);

  return caps;
}

/**
 * Helper functions for capability reporting
 */
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
  printf("Terminal Capabilities (Windows):\n");
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
  printf("Testing Windows terminal output modes:\n");

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

/**
 * Apply color mode and render mode overrides to detected capabilities
 */
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
    caps.capabilities &= ~(uint32_t)TERM_CAP_BACKGROUND;
    break;

  case RENDER_MODE_BACKGROUND:
    caps.capabilities |= TERM_CAP_BACKGROUND;
    break;

  case RENDER_MODE_HALF_BLOCK:
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

#endif // _WIN32
