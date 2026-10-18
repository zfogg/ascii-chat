#if SIMD_SUPPORT_SVE
#include <ascii-chat/video/ascii/sve.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_mono_sve(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_luminance_sve);
}

#endif
