/**
 * @file platform/wasm/stubs/terminal.c
 * @brief Terminal function stubs for WASM (not needed for mirror mode)
 * @ingroup platform
 */

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/options/options.h>
#include <unistd.h>
#include <stdio.h>

asciichat_error_t terminal_get_size(terminal_size_t *size) {
  (void)size;
  return SET_ERRNO(ERROR_NOT_SUPPORTED, "Terminal operations not supported in WASM");
}

asciichat_error_t terminal_cursor_hide(void) {
  return ASCIICHAT_OK;
}

asciichat_error_t terminal_cursor_show(void) {
  return ASCIICHAT_OK;
}

// Effective terminal width with fallback to option or default
unsigned short int terminal_get_effective_width(void) {
  int width = GET_OPTION(width);
  if (width > 0) {
    return (unsigned short int)width;
  }
  // For WASM, use default since terminal detection is stubbed
  return OPT_WIDTH_DEFAULT;
}

// Effective terminal height with fallback to option or default
unsigned short int terminal_get_effective_height(void) {
  int height = GET_OPTION(height);
  if (height > 0) {
    return (unsigned short int)height;
  }
  // For WASM, use default since terminal detection is stubbed
  return OPT_HEIGHT_DEFAULT;
}

// Additional terminal control stubs for WASM
asciichat_error_t terminal_set_echo(bool enabled) {
  // Send ANSI escape sequence for echo control (xterm ignores but passes through)
  if (!enabled) {
    write(STDOUT_FILENO, "\033[?25l", 6);  // Hide cursor
  } else {
    write(STDOUT_FILENO, "\033[?25h", 6);  // Show cursor
  }
  return ASCIICHAT_OK;
}

bool terminal_should_use_control_sequences(int fd) {
  (void)fd;
  return true;  // Always use ANSI sequences in browser
}

asciichat_error_t terminal_clear_screen(void) {
  // Send ANSI clear screen escape sequence
  write(STDOUT_FILENO, "\033[2J", 4);
  return ASCIICHAT_OK;
}

asciichat_error_t terminal_cursor_home(int fd) {
  (void)fd;
  // Send ANSI cursor home escape sequence
  write(STDOUT_FILENO, "\033[H", 3);
  return ASCIICHAT_OK;
}

// Terminal reader stubs
#include <ascii-chat/terminal/fd/reader.h>

asciichat_error_t terminal_fd_reader_create(int fd, int frame_height, terminal_fd_reader_t **out) {
  (void)fd;
  (void)frame_height;
  if (out) *out = NULL;
  return ASCIICHAT_OK;  // Stub - no stdin available in WASM
}

void terminal_fd_reader_destroy(terminal_fd_reader_t *reader) {
  (void)reader;
  // No-op
}

asciichat_error_t terminal_fd_reader_next(terminal_fd_reader_t *reader, char **out_frame) {
  (void)reader;
  if (out_frame) *out_frame = NULL;  // Signal EOF immediately
  return ASCIICHAT_OK;
}

asciichat_error_t terminal_move_cursor_relative(int offset) {
  if (offset == 0) {
    return ASCIICHAT_OK;
  }

  if (offset > 0) {
    // Move right: \x1b[<n>C
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\033[%dC", offset);
    if (len > 0 && len < (int)sizeof(buf)) {
      write(STDOUT_FILENO, buf, len);
    }
  } else {
    // Move left: \x1b[<n>D
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\033[%dD", -offset);
    if (len > 0 && len < (int)sizeof(buf)) {
      write(STDOUT_FILENO, buf, len);
    }
  }

  return ASCIICHAT_OK;
}
