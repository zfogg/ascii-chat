#include <ascii-chat/video/anim/test_pattern.h>
#include <emscripten.h>

EMSCRIPTEN_KEEPALIVE test_pattern_t *wasm_test_pattern_create(int width, int height) {
  test_pattern_t *pattern = NULL;
  if (test_pattern_create(width, height, &pattern) != ASCIICHAT_OK)
    return NULL;
  return pattern;
}
EMSCRIPTEN_KEEPALIVE const rgb_pixel_t *wasm_test_pattern_render(test_pattern_t *pattern, int width, int height,
                                                                 int index, double time_ms, bool cadence) {
  if (test_pattern_resize(pattern, width, height) != ASCIICHAT_OK ||
      test_pattern_render(pattern, index, time_ms, cadence) != ASCIICHAT_OK)
    return NULL;
  return test_pattern_image(pattern)->pixels;
}
EMSCRIPTEN_KEEPALIVE void wasm_test_pattern_destroy(test_pattern_t *pattern) {
  test_pattern_destroy(pattern);
}

EMSCRIPTEN_KEEPALIVE const uint8_t *wasm_test_pattern_render_rgba(test_pattern_t *pattern, int width, int height,
                                                                  int index, double time_ms, bool cadence) {
  if (!wasm_test_pattern_render(pattern, width, height, index, time_ms, cadence))
    return NULL;
  return test_pattern_rgba(pattern);
}
