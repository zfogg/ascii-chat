#if SIMD_SUPPORT_SSSE3
#define SIMD_CELL_SSSE3 1
#define SIMD_CELL_ISA ssse3
#include "../simd/lookup.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/ssse3.h>
char *render_ascii_mono_ssse3(const image_t *image, const char *palette) {
  return render_ascii_cells_ssse3(image, palette, ASCII_COLOR_NONE, false);
}
#endif
