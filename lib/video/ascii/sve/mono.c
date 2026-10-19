#if SIMD_SUPPORT_SVE
#define SIMD_CELL_SVE 1
#define SIMD_CELL_ISA sve
#include "../simd/lookup.c"
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/sve.h>
char *render_ascii_mono_sve(const image_t *image, const char *palette) {
  return render_ascii_cells_sve(image, palette, ASCII_COLOR_NONE, false);
}
#endif
