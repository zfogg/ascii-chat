#if SIMD_SUPPORT_SVE
#define SIMD_CELL_SVE 1
#define SIMD_CELL_ISA sve
#include "../simd/quantize.c"
#include <ascii-chat/video/ascii/sve.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_cells_sve(const image_t *image, const char *palette, ascii_color_t mode, bool background) {
  ascii_frame_t frame;
  if (ascii_frame_init(&frame, image, palette) != ASCIICHAT_OK)
    return NULL;
  for (int y = 0; y < image->h; ++y) {
    const rgb_pixel_t *row = image->pixels + (size_t)y * frame.width;
    simd_luminance_sve((const uint8_t *)row, frame.luma, frame.width);
    simd_pack_sve((const uint8_t *)row, frame.packed, frame.width);
    simd_lookup_sve(frame.luma, frame.table, frame.glyph, frame.width);
    if (mode == ASCII_COLOR_256 || mode == ASCII_COLOR_16)
      simd_quantize_sve(frame.packed, frame.index, frame.width, mode == ASCII_COLOR_16 ? 16 : 256);
    ascii_frame_emit_row(&frame, mode, background, y + 1 < image->h, &simd_cells_sve);
  }
  return ascii_frame_finish(&frame);
}
char *render_ascii_color_sve(const image_t *image, bool background, bool indexed, const char *palette) {
  return render_ascii_cells_sve(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background);
}
void sve_caches_destroy(void) {}
#endif
