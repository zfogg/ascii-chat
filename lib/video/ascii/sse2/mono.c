#if SIMD_SUPPORT_SSE2
#define SIMD_CELL_SSE2 1
#define SIMD_CELL_ISA sse2
#include "../simd/lookup.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/sse2.h>
char *render_ascii_mono_sse2(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_luminance_sse2);
}
#endif
