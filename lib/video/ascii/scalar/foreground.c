/** @file video/ascii/scalar/foreground.c
 * Foreground rendering compatibility entry points and color utilities.
 */
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/scalar/foreground.h>

char *image_print(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_cells_backend());
}

char *image_print_color(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_TRUE, false, false, simd_cells_backend());
}

char *image_print_color_utf8(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_TRUE, false, false, simd_cells_backend());
}

char *image_print_256color(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_256, false, false, simd_cells_backend());
}

char *image_print_16color(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_16, false, false, simd_cells_backend());
}

char *image_print_16color_dithered(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_16, false, true, simd_cells_backend());
}

char *image_print_16color_dithered_with_background(const image_t *image, bool background, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_16, background, true, simd_cells_backend());
}

void quantize_color(int *r, int *g, int *b, int levels) {
  if (!r || !g || !b) {
    SET_ERRNO(ERROR_INVALID_PARAM, "r, g, or b is NULL");
    return;
  }

  if (levels <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "levels must be positive, got %d", levels);
    return;
  }

  int step = 256 / levels;
  *r = (*r / step) * step;
  *g = (*g / step) * step;
  *b = (*b / step) * step;
}

char *rgb_to_ansi_fg(int r, int g, int b) {
  _Thread_local static char color_code[20]; // \033[38;2;255;255;255m + \0 = 20 bytes max
  SAFE_SNPRINTF(color_code, sizeof(color_code), "\033[38;2;%d;%d;%dm", r, g, b);
  return color_code;
}

char *rgb_to_ansi_bg(int r, int g, int b) {
  _Thread_local static char color_code[20]; // \033[48;2;255;255;255m + \0 = 20 bytes max
  SAFE_SNPRINTF(color_code, sizeof(color_code), "\033[48;2;%d;%d;%dm", r, g, b);
  return color_code;
}

void rgb_to_ansi_8bit(int r, int g, int b, int *fg_code, int *bg_code) {
  if (!fg_code || !bg_code) {
    SET_ERRNO(ERROR_INVALID_PARAM, "fg_code or bg_code is NULL");
    return;
  }

  // Convert RGB to 8-bit color code (216 color cube + 24 grayscale)
  if (r == g && g == b) {
    // Grayscale
    if (r < 8) {
      *fg_code = 16;
    } else if (r > 248) {
      *fg_code = 231;
    } else {
      *fg_code = 232 + (r - 8) / 10;
    }
  } else {
    // Color cube: 16 + 36*r + 6*g + b where r,g,b are 0-5
    int r_level = (r * 5) / 255;
    int g_level = (g * 5) / 255;
    int b_level = (b * 5) / 255;
    *fg_code = 16 + 36 * r_level + 6 * g_level + b_level;
  }
  *bg_code = *fg_code; // Same logic for background
}
