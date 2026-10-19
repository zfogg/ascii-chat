#if SIMD_SUPPORT_SSSE3
#define SIMD_CELL_SSSE3 1
#define SIMD_CELL_ISA ssse3
#include "../simd/quantize.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/ssse3.h>
char *render_ascii_color_ssse3(const image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_luminance_ssse3);
}
void ssse3_caches_destroy(void) {}
#endif
