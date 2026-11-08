#include "ui.h"
#include <emscripten.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/media/render/renderer.h>

// Keep the native renderer's struct layout private to C. Browser callers must
// not assume a font-path capacity, pointer offset, or padding size.
static asciichat_error_t wasm_renderer_config_write(term_renderer_config_t *config, int cols, int rows,
                                                    double font_size, int theme, const uint8_t *font_data,
                                                    size_t font_size_bytes, int matrix) {
  if (!config)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing browser renderer configuration");
  *config = (term_renderer_config_t){.cols = cols,
                                     .rows = rows,
                                     .font_size_pt = font_size,
                                     .theme = (term_renderer_theme_t)theme,
                                     .font_data = font_data,
                                     .font_data_size = font_size_bytes};
  if (matrix)
    SAFE_STRNCPY(config->font_spec, "Matrix", sizeof(config->font_spec));
  return ASCIICHAT_OK;
}

#include "font.h"

/* Construct the native configuration in C so no foreign caller depends on its layout. */
asciichat_error_t wasm_renderer_create(int cols, int rows, double font_size, int matrix, int selection,
                                       terminal_renderer_t **output) {
  term_renderer_config_t config;
  asciichat_error_t result = wasm_renderer_config_write(
      &config, cols, rows, font_size, 0, get_font_default_ptr(selection), get_font_default_size(selection), matrix);
  return result == ASCIICHAT_OK ? term_renderer_create(&config, output) : result;
}

// Shared log navigation addresses the session executing the browser callback.
static _Thread_local ui_session_t *active_session;

terminal_size_t ui_controller_size(void) {
  return ui_session_size(active_session);
}

void ui_controller_redraw(void) {
  ui_session_redraw(active_session);
}

ui_session_t *wasm_ui_create(void) {
  return ui_session_create();
}
void wasm_ui_destroy(ui_session_t *s) {
  ui_session_destroy(s);
}
void wasm_ui_resize(ui_session_t *s, int cols, int rows) {
  ui_session_resize(s, cols, rows);
}
void wasm_ui_loading(ui_session_t *s, int loading, const char *message) {
  ui_session_loading(s, loading != 0, message);
}
void wasm_ui_log(ui_session_t *s, const char *message) {
  ui_session_log(s, message);
}
asciichat_error_t wasm_ui_media(ui_session_t *s, const char *text, size_t length) {
  return ui_session_media(s, text, length);
}
int wasm_ui_key(ui_session_t *s, int key) {
  ui_session_t *previous = active_session;
  active_session = s;
  int result = ui_session_input(s, (keyboard_key_t)key);
  active_session = previous;
  return result;
}
const char *wasm_ui_tick(ui_session_t *s, double milliseconds) {
  ui_session_t *previous = active_session;
  active_session = s;
  const char *result = ui_session_tick(s, (uint64_t)(milliseconds * 1000000));
  active_session = previous;
  return result;
}
void wasm_ui_clear(ui_session_t *s) {
  ui_session_clear(s);
}
void wasm_ui_redraw(ui_session_t *s) {
  ui_session_redraw(s);
}
void wasm_ui_capture_logs(ui_session_t *s) {
  ui_session_capture_logs(s);
}

double wasm_ui_volume(void) {
  return GET_OPTION(speakers_volume);
}
int wasm_ui_fps(void) {
  return GET_OPTION(fps_counter);
}
asciichat_error_t wasm_ui_audio(int enabled) {
  return options_set_bool("audio_enabled", enabled != 0);
}
