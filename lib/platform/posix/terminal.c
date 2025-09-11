/**
 * @file terminal.c
 * @brief POSIX terminal I/O implementation for ASCII-Chat platform abstraction layer
 * 
 * This file provides POSIX terminal control wrappers for the platform abstraction layer,
 * enabling cross-platform terminal operations using a unified API.
 */

#ifndef _WIN32

#include "../abstraction.h"
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/**
 * @brief Get terminal size
 * @param size Pointer to terminal_size_t structure to fill
 * @return 0 on success, -1 on failure
 */
int terminal_get_size(terminal_size_t *size) {
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
int terminal_set_raw_mode(bool enable) {
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
    } else if (saved) {
        return tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    }
    return 0;
}

/**
 * @brief Set terminal echo mode
 * @param enable True to enable echo, false to disable
 * @return 0 on success, -1 on failure
 */
int terminal_set_echo(bool enable) {
    struct termios tty;
    if (tcgetattr(STDIN_FILENO, &tty) != 0)
        return -1;

    if (enable) {
        tty.c_lflag |= ECHO;
    } else {
        tty.c_lflag &= ~ECHO;
    }

    return tcsetattr(STDIN_FILENO, TCSANOW, &tty);
}

/**
 * @brief Check if terminal supports color output
 * @return True if color is supported, false otherwise
 */
bool terminal_supports_color(void) {
    const char *term = getenv("TERM");
    if (!term)
        return false;

    // Check for common color-capable terminals
    return (strstr(term, "color") != NULL || 
            strstr(term, "xterm") != NULL || 
            strstr(term, "screen") != NULL ||
            strstr(term, "vt100") != NULL || 
            strstr(term, "linux") != NULL);
}

/**
 * @brief Check if terminal supports Unicode output
 * @return True if Unicode is supported, false otherwise
 */
bool terminal_supports_unicode(void) {
    const char *lang = getenv("LANG");
    const char *lc_all = getenv("LC_ALL");
    const char *lc_ctype = getenv("LC_CTYPE");

    const char *check = lc_all ? lc_all : (lc_ctype ? lc_ctype : lang);
    if (!check)
        return false;

    return (strstr(check, "UTF-8") != NULL || strstr(check, "utf8") != NULL);
}

/**
 * @brief Clear the terminal screen
 * @return 0 on success, non-zero on failure
 */
int terminal_clear_screen(void) {
    return system("clear");
}

/**
 * @brief Move cursor to specified position
 * @param row Row position (0-based)
 * @param col Column position (0-based)
 * @return 0 on success, -1 on failure
 */
int terminal_move_cursor(int row, int col) {
    printf("\033[%d;%dH", row + 1, col + 1);
    fflush(stdout);
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

#endif // !_WIN32