#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/scalar/halfblock.h>

char *rgb_to_truecolor_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride) {
  return ascii_render_halfblocks(rgb, width, height, stride, ASCII_COLOR_TRUE, &simd_cells_scalar);
}
char *rgb_to_256color_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return ascii_render_halfblocks(rgb, width, height, stride, ASCII_COLOR_256, &simd_cells_scalar);
}
char *rgb_to_16color_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return ascii_render_halfblocks(rgb, width, height, stride, ASCII_COLOR_16, &simd_cells_scalar);
}
char *rgb_to_halfblocks_scalar(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return ascii_render_halfblocks(rgb, width, height, stride, ASCII_COLOR_NONE, &simd_cells_scalar);
}
