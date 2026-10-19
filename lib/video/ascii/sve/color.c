#if SIMD_SUPPORT_SVE
#define SIMD_CELL_SVE 1
#define SIMD_CELL_ISA sve
#include "../simd/quantize.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/sve.h>
char *render_ascii_color_sve(const image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_luminance_sve);
}
void sve_caches_destroy(void) {}
#endif
