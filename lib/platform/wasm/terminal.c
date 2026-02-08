/**
 * @file platform/wasm/terminal.c
 * @brief Terminal abstraction for WASM/Emscripten via EM_JS bridge to xterm.js
 * @ingroup platform
 */

#include <emscripten.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// EM_JS: JavaScript bridge functions
// ============================================================================

EM_JS(int, js_get_terminal_cols, (), { return Module.xterm ? Module.xterm.cols : 80; });

EM_JS(int, js_get_terminal_rows, (), { return Module.xterm ? Module.xterm.rows : 24; });

EM_JS(void, js_terminal_write, (const char *data, int len), {
  if (Module.xterm) {
    Module.xterm.write(UTF8ToString(data, len));
  }
});

// ============================================================================
// Platform API Implementation
// ============================================================================

int platform_get_terminal_size(int *cols, int *rows) {
  if (!cols || !rows) {
    return -1;
  }
  *cols = js_get_terminal_cols();
  *rows = js_get_terminal_rows();
  return 0;
}

// Color mode detection - not needed for WASM mirror mode
// Terminal capabilities are managed via JavaScript xterm.js settings

int platform_write_terminal(const char *data, size_t len) {
  js_terminal_write(data, (int)len);
  return (int)len;
}

// Stub implementations (not needed for mirror mode)
int platform_set_terminal_raw_mode(bool enable) {
  (void)enable;
  return 0; // No-op: xterm.js handles this
}

int platform_read_keyboard(char *buffer, size_t len) {
  (void)buffer;
  (void)len;
  return -1; // Not supported: use JavaScript event listeners
}

bool platform_is_terminal(int fd) {
  (void)fd;
  return true; // Always true for WASM terminal
}

int platform_get_cursor_position(int *row, int *col) {
  (void)row;
  (void)col;
  return -1; // Not supported in WASM
}

int platform_set_cursor_position(int row, int col) {
  (void)row;
  (void)col;
  return -1; // Not supported in WASM
}

// get_terminal_size wrapper
asciichat_error_t get_terminal_size(unsigned short int *width, unsigned short int *height) {
  if (!width || !height) {
    return ERROR_INVALID_PARAM;
  }
  int cols, rows;
  if (platform_get_terminal_size(&cols, &rows) != 0) {
    return ERROR_PLATFORM_INIT;
  }
  *width = (unsigned short int)cols;
  *height = (unsigned short int)rows;
  return ASCIICHAT_OK;
}

// Terminal UTF-8 support detection
bool terminal_supports_utf8(void) {
  return true; // xterm.js supports UTF-8
}

// Piped output detection
bool terminal_is_piped_output(void) {
  return false; // WASM mirror mode is not piped
}

// Color output detection
bool terminal_should_color_output(int fd) {
  (void)fd;
  return true; // WASM mirror mode supports color
}

// TTY detection stubs
bool terminal_is_stdin_tty(void) {
  return false; // No stdin in WASM
}

bool terminal_is_stdout_tty(void) {
  return true; // xterm.js output is a TTY
}

bool terminal_is_stderr_tty(void) {
  return true; // xterm.js output is a TTY
}

bool terminal_is_interactive(void) {
  return false; // WASM mirror mode is not interactive
}

bool terminal_should_force_stderr(void) {
  return false; // Don't force stderr in WASM
}

bool terminal_can_prompt_user(void) {
  return false; // Cannot prompt in WASM
}

// Terminal background detection
bool terminal_has_dark_background(void) {
  return true; // Default to dark background for xterm.js
}

// Terminal flush stub
asciichat_error_t terminal_flush(int fd) {
  (void)fd;
  return ASCIICHAT_OK; // No-op - xterm.js handles flushing
}

// Terminal capabilities detection stub
terminal_capabilities_t detect_terminal_capabilities(void) {
  terminal_capabilities_t caps = {0};
  caps.color_level = TERM_COLOR_256; // xterm.js supports 256 colors
  caps.capabilities = 0;
  caps.color_count = 256;
  caps.utf8_support = true; // xterm.js supports UTF-8
  caps.detection_reliable = true;
  caps.render_mode = RENDER_MODE_FOREGROUND; // Default to foreground mode
  platform_strlcpy(caps.term_type, "xterm-256color", sizeof(caps.term_type));
  platform_strlcpy(caps.colorterm, "truecolor", sizeof(caps.colorterm));
  caps.wants_background = true;
  caps.palette_type = 0; // Default palette
  caps.desired_fps = 30;
  caps.color_filter = COLOR_FILTER_NONE;
  return caps;
}

// Terminal background color query stub
bool terminal_query_background_color(uint8_t *bg_r, uint8_t *bg_g, uint8_t *bg_b) {
  // Return dark background (black) for xterm.js
  if (bg_r)
    *bg_r = 0;
  if (bg_g)
    *bg_g = 0;
  if (bg_b)
    *bg_b = 0;
  return true;
}
