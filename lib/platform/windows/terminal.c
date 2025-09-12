/**
 * @file terminal.c
 * @brief Windows terminal I/O implementation for ASCII-Chat platform abstraction layer
 *
 * This file provides Windows Console API wrappers for the platform abstraction layer,
 * enabling cross-platform terminal operations using a unified API.
 */

#ifdef _WIN32

#include "../abstraction.h"
#include "../internal.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

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
 * @return 0 on success, -1 on failure
 */
int terminal_reset(void) {
  // Reset using ANSI escape sequence
  printf("\033c"); // Full reset
  fflush(stdout);

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

#endif // _WIN32