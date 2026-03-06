/**
 * @file platform/wasm/stubs/terminal.c
 * @brief Terminal function stubs for WASM (not needed for mirror mode)
 * @ingroup platform
 */

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/options/options.h>

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
