#if SIMD_SUPPORT_AVX2
#define SIMD_CELL_AVX2 1
#define SIMD_CELL_ISA avx2
#include "../simd/lookup.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/avx2.h>
char *render_ascii_mono_avx2(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_luminance_avx2);
}
#endif
