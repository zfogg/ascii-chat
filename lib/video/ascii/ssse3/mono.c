#if SIMD_SUPPORT_SSSE3
#include <ascii-chat/video/ascii/ssse3.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_mono_ssse3(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_luminance_ssse3);
}

#endif
