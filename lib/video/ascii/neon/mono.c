#if SIMD_SUPPORT_NEON
#define SIMD_CELL_NEON 1
#define SIMD_CELL_ISA neon
#include "../simd/lookup.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/neon.h>
char *render_ascii_mono_neon(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_luminance_neon);
}
#endif
