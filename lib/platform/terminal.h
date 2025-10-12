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
#include "../common.h"

// ============================================================================
// Platform-Specific Signal Definitions
// ============================================================================

#ifdef _WIN32
// Windows doesn't have these signals natively
#define SIGWINCH 28 // Window size change (not supported on Windows)
#define SIGTERM 15  // Termination signal (limited support on Windows)
#endif

// ============================================================================
// Terminal Functions
// ============================================================================

// Terminal size structure
typedef struct {
  int rows;
  int cols;
} terminal_size_t;

// Basic terminal operations
asciichat_error_t terminal_get_size(terminal_size_t *size);
asciichat_error_t terminal_set_raw_mode(bool enable);
asciichat_error_t terminal_set_echo(bool enable);
bool terminal_supports_color(void);
bool terminal_supports_unicode(void);
bool terminal_supports_utf8(void);
asciichat_error_t terminal_clear_screen(void);
asciichat_error_t terminal_move_cursor(int row, int col);
void terminal_enable_ansi(void);

// Extended terminal control
asciichat_error_t terminal_set_buffering(bool line_buffered);
asciichat_error_t terminal_flush(int fd);
asciichat_error_t terminal_get_cursor_position(int *row, int *col);
asciichat_error_t terminal_save_cursor(void);
asciichat_error_t terminal_restore_cursor(void);
asciichat_error_t terminal_set_title(const char *title);
asciichat_error_t terminal_ring_bell(void);
asciichat_error_t terminal_hide_cursor(int fd, bool hide);
asciichat_error_t terminal_set_scroll_region(int top, int bottom);
asciichat_error_t terminal_reset(int fd);
asciichat_error_t terminal_cursor_home(int fd);
asciichat_error_t terminal_clear_scrollback(int fd);

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
  uint8_t desired_fps;                // Client's desired frame rate (1-144 FPS)
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
asciichat_error_t get_terminal_size(unsigned short int *width, unsigned short int *height);

// Helper functions for capability reporting
const char *terminal_color_level_name(terminal_color_level_t level);
const char *terminal_capabilities_summary(const terminal_capabilities_t *caps);
void print_terminal_capabilities(const terminal_capabilities_t *caps);
void test_terminal_output_modes(void);

// Apply command-line overrides to detected capabilities
terminal_capabilities_t apply_color_mode_override(terminal_capabilities_t caps);

// ============================================================================
// Windows Console Resize Detection
// ============================================================================

#ifdef _WIN32
/**
 * Callback function type for terminal resize events
 * @param cols New terminal width in columns
 * @param rows New terminal height in rows
 */
typedef void (*terminal_resize_callback_t)(int cols, int rows);

/**
 * Start Windows console resize detection thread
 * @param callback Function to call when resize is detected
 * @return 0 on success, -1 on failure
 */
int terminal_start_resize_detection(terminal_resize_callback_t callback);

/**
 * Stop Windows console resize detection thread
 */
void terminal_stop_resize_detection(void);
#endif
