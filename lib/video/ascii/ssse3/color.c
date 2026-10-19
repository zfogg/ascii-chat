#if SIMD_SUPPORT_SSSE3
#define SIMD_CELL_SSSE3 1
#define SIMD_CELL_ISA ssse3
#include "../simd/quantize.c"
#include <ascii-chat/video/ascii/ssse3.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_cells_ssse3(const image_t *image, const char *palette, ascii_color_t mode, bool background) {
  ascii_frame_t frame;
  if (ascii_frame_init(&frame, image, palette) != ASCIICHAT_OK)
    return NULL;
  for (int y = 0; y < image->h; ++y) {
    const rgb_pixel_t *row = image->pixels + (size_t)y * frame.width;
    simd_luminance_ssse3((const uint8_t *)row, frame.luma, frame.width);
    simd_pack_ssse3((const uint8_t *)row, frame.packed, frame.width);
    simd_lookup_ssse3(frame.luma, frame.table, frame.glyph, frame.width);
    if (mode == ASCII_COLOR_256 || mode == ASCII_COLOR_16)
      simd_quantize_ssse3(frame.packed, frame.index, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
    ascii_frame_emit_row(&frame, mode, background, y + 1 < image->h, &simd_cells_ssse3);
  }
  return ascii_frame_finish(&frame);
}
char *render_ascii_color_ssse3(const image_t *image, bool background, bool indexed, const char *palette) {
  return render_ascii_cells_ssse3(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background);
}
void ssse3_caches_destroy(void) {}
#endif
