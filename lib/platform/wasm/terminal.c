/**
 * @file platform/wasm/terminal.c
 * @brief Terminal abstraction for WASM/Emscripten via EM_JS bridge to xterm.js
 */

#include <emscripten.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================================
// EM_JS: JavaScript bridge functions
// ============================================================================

EM_JS(int, js_get_terminal_cols, (), { return Module.xterm ? Module.xterm.cols : 80; });

EM_JS(int, js_get_terminal_rows, (), { return Module.xterm ? Module.xterm.rows : 24; });

EM_JS(int, js_get_color_mode, (), {
  if (!Module.xterm)
    return 0; // COLOR_MODE_AUTO
  return 4;   // COLOR_MODE_TRUECOLOR (xterm.js supports it)
});

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

color_mode_t platform_detect_color_support(void) {
  return (color_mode_t)js_get_color_mode();
}

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
