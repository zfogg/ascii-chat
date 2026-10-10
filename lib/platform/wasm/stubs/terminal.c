/**
 * @file platform/wasm/stubs/terminal.c
 * @brief Terminal function stubs for WASM (functions not needed for mirror mode)
 *
 * Terminal control and cursor movement functions are implemented in wasm/terminal.c
 * via ANSI escape sequences. This file contains only the stubs that return errors
 * or no-ops for operations not supported in the browser environment.
 * @ingroup platform
 */

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/options/options.h>

asciichat_error_t terminal_get_size_fd(int fd, terminal_size_t *size) {
  (void)fd;
  return terminal_get_size(size);
}

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

// Terminal reader stubs
#include <ascii-chat/terminal/fd/reader.h>

asciichat_error_t terminal_fd_reader_create(int fd, int frame_height, terminal_fd_reader_t **out) {
  (void)fd;
  (void)frame_height;
  if (out)
    *out = NULL;
  return ASCIICHAT_OK; // Stub - no stdin available in WASM
}

void terminal_fd_reader_destroy(terminal_fd_reader_t *reader) {
  (void)reader;
  // No-op
}

asciichat_error_t terminal_fd_reader_next(terminal_fd_reader_t *reader, char **out_frame) {
  (void)reader;
  if (out_frame)
    *out_frame = NULL; // Signal EOF immediately
  return ASCIICHAT_OK;
}

// Browser rendering has no native terminal presentation owner. Logs still use
// the platform output sink, which forwards them to the browser console.
bool ui_controller_is_presenting(void) {
  return false;
}

bool ui_controller_is_owner(void) {
  return false;
}

asciichat_error_t ui_controller_write(int fd, const char *data, size_t len) {
  if (!data || !len)
    return ASCIICHAT_OK;
  if (platform_write_all(fd, data, len) != len)
    return SET_ERRNO(ERROR_FILE_OPERATION, "Failed to write browser log output");
  return ASCIICHAT_OK;
}
