/**
 * @file platform/wasm/stubs/terminal.c
 * @brief Terminal function stubs for WASM (not needed for mirror mode)
 * @ingroup platform
 */

#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>

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
