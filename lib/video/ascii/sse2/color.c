#if SIMD_SUPPORT_SSE2
#define SIMD_CELL_SSE2 1
#define SIMD_CELL_ISA sse2
#include "../simd/quantize.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/sse2.h>
char *render_ascii_color_sse2(const image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_luminance_sse2);
}
void sse2_caches_destroy(void) {}
#endif
