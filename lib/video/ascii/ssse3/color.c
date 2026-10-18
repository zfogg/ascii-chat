#if SIMD_SUPPORT_SSSE3
#include <ascii-chat/video/ascii/ssse3.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_color_ssse3(const image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_luminance_ssse3);
}

void ssse3_caches_destroy(void) {
  // Palette and emission caches are shared across backends.
}

#endif
