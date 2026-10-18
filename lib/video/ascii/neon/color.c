#if SIMD_SUPPORT_NEON
#include <ascii-chat/video/ascii/neon.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_color_neon(const image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_luminance_neon);
}

#endif
