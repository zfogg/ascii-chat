#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/scalar/background.h>

char *image_print_color_background(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_TRUE, true, false, simd_backend()->luminance);
}
