#include <emscripten.h>
#include <ascii-chat/ui/session.h>
#include <ascii-chat/options/options.h>

EMSCRIPTEN_KEEPALIVE ui_session_t *wasm_ui_create(void) {
  return ui_session_create();
}
EMSCRIPTEN_KEEPALIVE void wasm_ui_destroy(ui_session_t *s) {
  ui_session_destroy(s);
}
EMSCRIPTEN_KEEPALIVE void wasm_ui_resize(ui_session_t *s, int cols, int rows) {
  ui_session_resize(s, cols, rows);
}
EMSCRIPTEN_KEEPALIVE void wasm_ui_loading(ui_session_t *s, int loading, const char *message) {
  ui_session_loading(s, loading != 0, message);
}
EMSCRIPTEN_KEEPALIVE void wasm_ui_log(ui_session_t *s, const char *message) {
  ui_session_log(s, message);
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t wasm_ui_media(ui_session_t *s, const char *text, size_t length) {
  return ui_session_media(s, text, length);
}
EMSCRIPTEN_KEEPALIVE int wasm_ui_key(ui_session_t *s, int key) {
  return ui_session_input(s, (keyboard_key_t)key);
}
EMSCRIPTEN_KEEPALIVE const char *wasm_ui_tick(ui_session_t *s, double milliseconds) {
  return ui_session_tick(s, (uint64_t)(milliseconds * 1000000));
}
EMSCRIPTEN_KEEPALIVE void wasm_ui_clear(ui_session_t *s) {
  ui_session_clear(s);
}
EMSCRIPTEN_KEEPALIVE void wasm_ui_redraw(ui_session_t *s) {
  ui_session_redraw(s);
}
EMSCRIPTEN_KEEPALIVE void wasm_ui_capture_logs(ui_session_t *s) {
  ui_session_capture_logs(s);
}

EMSCRIPTEN_KEEPALIVE double wasm_ui_volume(void) {
  return GET_OPTION(speakers_volume);
}
EMSCRIPTEN_KEEPALIVE int wasm_ui_fps(void) {
  return GET_OPTION(fps_counter);
}
EMSCRIPTEN_KEEPALIVE asciichat_error_t wasm_ui_audio(int enabled) {
  return options_set_bool("audio_enabled", enabled != 0);
}
