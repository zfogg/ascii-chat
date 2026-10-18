#if SIMD_SUPPORT_SSE2
#include <ascii-chat/video/ascii/sse2.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_mono_sse2(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_luminance_sse2);
}

#endif
