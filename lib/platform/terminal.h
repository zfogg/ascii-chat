#pragma once

/**
 * @file terminal.h
 * @brief Cross-platform terminal interface for ASCII-Chat
 *
 * This header provides unified terminal I/O operations including
 * ANSI escape sequences, cursor control, and terminal configuration.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>

// ============================================================================
// Terminal Functions
// ============================================================================

// Terminal size structure
typedef struct {
  int rows;
  int cols;
} terminal_size_t;

// Basic terminal operations
int terminal_get_size(terminal_size_t *size);
int terminal_set_raw_mode(bool enable);
int terminal_set_echo(bool enable);
bool terminal_supports_color(void);
bool terminal_supports_unicode(void);
int terminal_clear_screen(void);
int terminal_move_cursor(int row, int col);
void terminal_enable_ansi(void);

// Extended terminal control
int terminal_set_buffering(bool line_buffered);
int terminal_flush(void);
int terminal_get_cursor_position(int *row, int *col);
int terminal_save_cursor(void);
int terminal_restore_cursor(void);
int terminal_set_title(const char *title);
int terminal_ring_bell(void);
int terminal_hide_cursor(bool hide);
int terminal_set_scroll_region(int top, int bottom);
int terminal_reset(void);
