#include <emscripten.h>
#include <stdint.h>
#include <ascii-chat/options/options.h>

/* Uploads carry pixels, not remote flip preferences. Resolve orientation from
 * the same C option store used by local rendering, before any upload codec. */
EMSCRIPTEN_KEEPALIVE int wasm_capture_transform_required(void) {
  return GET_OPTION(flip_x) || GET_OPTION(flip_y);
}

EMSCRIPTEN_KEEPALIVE int wasm_capture_transform_rgba(uint8_t *pixels, int width, int height) {
  if (!pixels || width <= 0 || height <= 0 || (size_t)width > SIZE_MAX / (size_t)height / 4)
    return -1;
  bool flip_x = GET_OPTION(flip_x), flip_y = GET_OPTION(flip_y);
  if (!flip_x && !flip_y)
    return 0;
  size_t count = (size_t)width * (size_t)height;
  for (size_t i = 0; i < count; ++i) {
    size_t x = i % (size_t)width, y = i / (size_t)width;
    size_t target = (flip_y ? (size_t)height - 1 - y : y) * (size_t)width + (flip_x ? (size_t)width - 1 - x : x);
    if (i >= target)
      continue;
    for (size_t channel = 0; channel < 4; ++channel) {
      uint8_t value = pixels[i * 4 + channel];
      pixels[i * 4 + channel] = pixels[target * 4 + channel];
      pixels[target * 4 + channel] = value;
    }
  }
  return 0;
}
