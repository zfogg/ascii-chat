#if SIMD_SUPPORT_SSSE3
#define SIMD_CELL_SSSE3 1
#define SIMD_CELL_ISA ssse3
#include "../simd/pack.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/ssse3.h>
char *rgb_to_truecolor_halfblocks_ssse3(const uint8_t *rgb, int width, int height, int stride) {
  return ascii_render_halfblocks(rgb, width, height, stride, ASCII_COLOR_TRUE, &simd_cells_ssse3);
}
char *rgb_to_256color_halfblocks_ssse3(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return ascii_render_halfblocks(rgb, width, height, stride, ASCII_COLOR_256, &simd_cells_ssse3);
}
char *rgb_to_16color_halfblocks_ssse3(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return ascii_render_halfblocks(rgb, width, height, stride, ASCII_COLOR_16, &simd_cells_ssse3);
}
char *rgb_to_halfblocks_ssse3(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return ascii_render_halfblocks(rgb, width, height, stride, ASCII_COLOR_NONE, &simd_cells_ssse3);
}
#endif
