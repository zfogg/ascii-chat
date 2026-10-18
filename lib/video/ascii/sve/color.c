#if SIMD_SUPPORT_SVE
#include <ascii-chat/video/ascii/sve.h>
#include <ascii-chat/video/ascii/render.h>

char *render_ascii_color_sve(const image_t *image, bool background, bool indexed, const char *palette) {
  return ascii_render(image, palette, indexed ? ASCII_COLOR_256 : ASCII_COLOR_TRUE, background, false,
                      simd_luminance_sve);
}

void sve_caches_destroy(void) {
  // Palette and emission caches are shared across backends.
}

#endif
