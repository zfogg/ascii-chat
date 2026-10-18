#if SIMD_SUPPORT_NEON
#include <ascii-chat/video/ascii/neon.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_mono_neon(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_luminance_neon);
}

#endif
