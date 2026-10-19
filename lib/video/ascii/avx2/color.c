#if SIMD_SUPPORT_AVX2
#define SIMD_CELL_AVX2 1
#define SIMD_CELL_ISA avx2
#include "../simd/quantize.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/avx2.h>
char *render_ascii_color_avx2(const image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_luminance_avx2);
}
#endif
