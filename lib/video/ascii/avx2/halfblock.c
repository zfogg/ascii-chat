#if SIMD_SUPPORT_AVX2
#define SIMD_CELL_AVX2 1
#define SIMD_CELL_ISA avx2
#include "../simd/pack.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/video/ascii/avx2.h>
char *render_halfblocks_avx2(const uint8_t *rgb, int width, int height, int stride, ascii_color_t mode) {
  if (width <= 0 || height <= 0)
    return platform_strdup("");
  ascii_frame_t frame;
  if (ascii_half_frame_init(&frame, rgb, width, height, &stride, mode) != ASCIICHAT_OK)
    return NULL;
  for (size_t y = 0; y < (size_t)height; y += 2) {
    const uint8_t *top = rgb + y * (size_t)stride;
    const uint8_t *bottom = y + 1 < (size_t)height ? top + stride : top;
    simd_pack_avx2(top, frame.packed, frame.width);
    simd_pack_avx2(bottom, frame.index, frame.width);
    if (mode == ASCII_COLOR_TRUE) {
      memcpy(frame.fg, frame.packed, frame.width * sizeof(uint32_t));
      memcpy(frame.bg, frame.index, frame.width * sizeof(uint32_t));
    } else if (mode != ASCII_COLOR_NONE) {
      simd_quantize_avx2(frame.packed, frame.fg, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
      simd_quantize_avx2(frame.index, frame.bg, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
    }
    simd_halfblock_avx2(frame.packed, frame.index, frame.glyph, frame.fg, frame.bg, frame.width, (unsigned)mode,
                        frame.table);
    ascii_frame_emit_halfrow(&frame, mode, y + 2 < (size_t)height, &simd_cells_avx2);
  }
  return ascii_frame_finish(&frame);
}
char *rgb_to_truecolor_halfblocks_avx2(const uint8_t *rgb, int width, int height, int stride) {
  return render_halfblocks_avx2(rgb, width, height, stride, ASCII_COLOR_TRUE);
}
char *rgb_to_256color_halfblocks_avx2(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return render_halfblocks_avx2(rgb, width, height, stride, ASCII_COLOR_256);
}
char *rgb_to_16color_halfblocks_avx2(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return render_halfblocks_avx2(rgb, width, height, stride, ASCII_COLOR_16);
}
char *rgb_to_halfblocks_avx2(const uint8_t *rgb, int width, int height, int stride, const char *palette) {
  (void)palette;
  return render_halfblocks_avx2(rgb, width, height, stride, ASCII_COLOR_NONE);
}
#endif
