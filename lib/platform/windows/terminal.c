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

#endif // _WIN32