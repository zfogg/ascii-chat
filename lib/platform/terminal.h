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
#include <stdint.h>

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
int terminal_flush(int fd);
int terminal_get_cursor_position(int *row, int *col);
int terminal_save_cursor(void);
int terminal_restore_cursor(void);
int terminal_set_title(const char *title);
int terminal_ring_bell(void);
int terminal_hide_cursor(int fd, bool hide);
int terminal_set_scroll_region(int top, int bottom);
int terminal_reset(int fd);
int terminal_cursor_home(int fd);
int terminal_clear_scrollback(int fd);

// ============================================================================
// Terminal Detection and Capabilities
// ============================================================================

// Terminal color support levels
typedef enum {
  TERM_COLOR_NONE = 0,     // No color support (monochrome)
  TERM_COLOR_16 = 1,       // 16 ANSI colors
  TERM_COLOR_256 = 2,      // 256 colors
  TERM_COLOR_TRUECOLOR = 3 // 24-bit truecolor
} terminal_color_level_t;

// Terminal capability flags (bitmask)
typedef enum {
  TERM_CAP_COLOR_16 = 0x0001,   // 16-color support
  TERM_CAP_COLOR_256 = 0x0002,  // 256-color support
  TERM_CAP_COLOR_TRUE = 0x0004, // Truecolor support
  TERM_CAP_UTF8 = 0x0008,       // UTF-8 encoding support
  TERM_CAP_BACKGROUND = 0x0010  // Background color support
} terminal_capability_flags_t;

// Render mode preferences
typedef enum {
  RENDER_MODE_FOREGROUND = 0, // Foreground colors only
  RENDER_MODE_BACKGROUND = 1, // Background colors
  RENDER_MODE_HALF_BLOCK = 2  // Unicode half-block characters
} render_mode_t;

// Complete terminal capabilities structure
typedef struct {
  terminal_color_level_t color_level; // Detected color support level
  uint32_t capabilities;              // Capability flags bitmask
  uint32_t color_count;               // Maximum number of colors
  bool utf8_support;                  // UTF-8 encoding support
  bool detection_reliable;            // True if detection is confident
  render_mode_t render_mode;          // Preferred rendering mode
  char term_type[64];                 // $TERM environment variable
  char colorterm[64];                 // $COLORTERM environment variable
  bool wants_background;              // True if background colors preferred
  int palette_type;                   // Palette type enum value
  char palette_custom[64];            // Custom palette characters
} terminal_capabilities_t;

// TTY detection and management
typedef struct {
  int fd;           // File descriptor for TTY access
  const char *path; // Path to TTY device
  bool owns_fd;     // Whether we opened the FD and should close it
} tty_info_t;

// Main capability detection function
terminal_capabilities_t detect_terminal_capabilities(void);

// TTY detection and access
tty_info_t get_current_tty(void);
bool is_valid_tty_path(const char *path);

// Terminal size detection with multiple fallback methods
int get_terminal_size(unsigned short int *width, unsigned short int *height);

// Helper functions for capability reporting
const char *terminal_color_level_name(terminal_color_level_t level);
const char *terminal_capabilities_summary(const terminal_capabilities_t *caps);
void print_terminal_capabilities(const terminal_capabilities_t *caps);
void test_terminal_output_modes(void);

// Apply command-line overrides to detected capabilities
terminal_capabilities_t apply_color_mode_override(terminal_capabilities_t caps);
