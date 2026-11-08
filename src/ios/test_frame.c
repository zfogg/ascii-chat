#include "test_frame.h"

int asciichat_ios_test_frame(uint32_t *pixels, size_t capacity, size_t *width, size_t *height) {
  const size_t frame_width = 96;
  const size_t frame_height = 64;
  if (!pixels || capacity < frame_width * frame_height || !width || !height) {
    return 0;
  }
  for (size_t y = 0; y < frame_height; ++y) {
    for (size_t x = 0; x < frame_width; ++x) {
      const uint32_t red = (uint32_t)(x * 255 / frame_width);
      const uint32_t green = (uint32_t)(y * 255 / frame_height);
      const uint32_t blue = ((x / 8 + y / 8) & 1U) ? 0xFFU : 0x40U;
      pixels[y * frame_width + x] = 0xFF000000U | (red << 16) | (green << 8) | blue;
    }
  }
  *width = frame_width;
  *height = frame_height;
  return 1;
}
