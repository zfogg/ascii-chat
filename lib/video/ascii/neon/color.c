#if SIMD_SUPPORT_NEON
#define SIMD_CELL_NEON 1
#define SIMD_CELL_ISA neon
#include "../simd/quantize.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/neon.h>
char *render_ascii_color_neon(const image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_luminance_neon);
}
#endif
