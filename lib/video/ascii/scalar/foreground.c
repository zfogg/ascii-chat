/** @file video/ascii/scalar/foreground.c
 * Shared cell construction and terminal emission for scalar and SIMD kernels.
 */
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/render.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/ascii/scalar/foreground.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>
#include <string.h>

// RGB values occupy 24 bits; indexed colors use only the low byte.
typedef struct {
  const utf8_char_t *glyph;
  int fg, bg;
} ascii_cell_t;

static int pack_rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((int)r << 16) | ((int)g << 8) | b;
}

static void emit_color(outbuf_t *out, ascii_color_t mode, bool background, int value) {
  if (mode == ASCII_COLOR_TRUE) {
    if (background)
      emit_set_truecolor_bg(out, (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value);
    else
      emit_set_truecolor_fg(out, (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value);
  } else if (mode == ASCII_COLOR_256) {
    if (background)
      emit_set_256_color_bg(out, (uint8_t)value);
    else
      emit_set_256_color_fg(out, (uint8_t)value);
  } else if (mode == ASCII_COLOR_16) {
    char sequence[16];
    char *end = background ? append_16color_bg(sequence, (uint8_t)value) : append_16color_fg(sequence, (uint8_t)value);
    ob_write(out, sequence, (size_t)(end - sequence));
  }
}

static bool same_cell(ascii_cell_t a, ascii_cell_t b) {
  return a.fg == b.fg && a.bg == b.bg &&
         (a.glyph == b.glyph || (a.glyph->byte_len == b.glyph->byte_len &&
                                 memcmp(a.glyph->utf8_bytes, b.glyph->utf8_bytes, a.glyph->byte_len) == 0));
}

static void emit_cells(outbuf_t *out, ascii_color_t mode, ascii_cell_t cell, uint32_t count, int *fg, int *bg) {
  if (cell.fg != *fg) {
    emit_color(out, mode, false, cell.fg);
    *fg = cell.fg;
  }
  if (cell.bg != *bg) {
    emit_color(out, mode, true, cell.bg);
    *bg = cell.bg;
  }
  emit_glyph_run(out, cell.glyph->utf8_bytes, cell.glyph->byte_len, count);
}

char *ascii_render(const image_t *image, const char *palette, ascii_color_t mode, bool background, bool dither,
                   simd_luminance_fn luminance) {
  if (!image || !image->pixels || mode < ASCII_COLOR_NONE || mode > ASCII_COLOR_TRUE || !palette || !palette[0] ||
      !luminance || image->w <= 0 || image->h <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid image, palette, or luminance kernel");
    return NULL;
  }
  size_t pixels, initial;
  if (checked_size_mul((size_t)image->w, (size_t)image->h, &pixels) != ASCIICHAT_OK ||
      checked_size_mul(pixels, sizeof(rgb_pixel_t), &initial) != ASCIICHAT_OK)
    return NULL;
  utf8_palette_cache_t *cache = get_utf8_palette_cache(palette);
  if (!cache)
    return NULL;
  // Reserve the worst-case encoded size once, including complete UTF-8 glyphs,
  // per-row resets/newlines, and the terminator. No pixel may overrun a guess.
  size_t bytes_per_cell = mode == ASCII_COLOR_NONE ? 4u : background ? 42u : 23u;
  size_t rows;
  if (checked_size_mul(pixels, bytes_per_cell, &initial) != ASCIICHAT_OK ||
      checked_size_mul((size_t)image->h, 5u, &rows) != ASCIICHAT_OK ||
      checked_size_add(initial, rows, &initial) != ASCIICHAT_OK ||
      checked_size_add(initial, 1u, &initial) != ASCIICHAT_OK)
    return NULL;
  outbuf_t out = {.cap = initial};
  out.buf = SAFE_MALLOC(initial, char *);
  uint8_t *row = SAFE_MALLOC((size_t)image->w, uint8_t *);
  rgb_error_t *errors = NULL;
  if (mode == ASCII_COLOR_16) {
    ansi_fast_init_16color();
    if (dither)
      errors = SAFE_CALLOC(pixels, sizeof(rgb_error_t), rgb_error_t *);
  } else if (mode == ASCII_COLOR_256) {
    ansi_fast_init_256color();
  }
  for (int y = 0; y < image->h; ++y) {
    const rgb_pixel_t *source = image->pixels + (size_t)y * image->w;
    luminance((const uint8_t *)source, row, (size_t)image->w);
    int fg = -1, bg = -1;
    ascii_cell_t run = {0};
    uint32_t count = 0;
    for (int x = 0; x < image->w; ++x) {
      rgb_pixel_t pixel = source[x];
      ascii_cell_t cell = {.glyph = &cache->cache64[row[x] >> 2], .fg = -1, .bg = -1};
      bool repeated =
          x > 0 && !dither && pixel.r == source[x - 1].r && pixel.g == source[x - 1].g && pixel.b == source[x - 1].b;
      if (repeated) {
        cell = run;
      } else if (mode != ASCII_COLOR_NONE) {
        int value;
        int contrast = row[x] < 128 ? 0xffffff : 0;
        if (mode == ASCII_COLOR_TRUE) {
          value = pack_rgb(pixel.r, pixel.g, pixel.b);
        } else if (mode == ASCII_COLOR_256) {
          value = rgb_to_256color(pixel.r, pixel.g, pixel.b);
          contrast = row[x] < 128 ? 15 : 0;
        } else {
          value = dither ? rgb_to_16color_dithered(pixel.r, pixel.g, pixel.b, x, y, image->w, image->h, errors)
                         : rgb_to_16color(pixel.r, pixel.g, pixel.b);
          uint8_t r, g, b;
          get_16color_rgb((uint8_t)value, &r, &g, &b);
          contrast = (77 * r + 150 * g + 29 * b) / 256 < 127 ? 15 : 0;
        }
        cell.fg = background ? contrast : value;
        cell.bg = background ? value : -1;
      }
      if (count && !same_cell(run, cell)) {
        emit_cells(&out, mode, run, count, &fg, &bg);
        count = 0;
      }
      run = cell;
      ++count;
    }
    emit_cells(&out, mode, run, count, &fg, &bg);
    emit_row_end(&out, mode != ASCII_COLOR_NONE, y + 1 < image->h);
  }
  SAFE_FREE(errors);
  SAFE_FREE(row);
  ob_term(&out);
  return out.buf;
}

char *image_print(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_NONE, false, false, simd_backend()->luminance);
}

char *image_print_color(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_TRUE, false, false, simd_backend()->luminance);
}

char *image_print_color_utf8(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_TRUE, false, false, simd_backend()->luminance);
}

char *image_print_256color(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_256, false, false, simd_backend()->luminance);
}

char *image_print_16color(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_16, false, false, simd_backend()->luminance);
}

char *image_print_16color_dithered(const image_t *image, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_16, false, true, simd_backend()->luminance);
}

char *image_print_16color_dithered_with_background(const image_t *image, bool background, const char *palette) {
  return ascii_render(image, palette, ASCII_COLOR_16, background, true, simd_backend()->luminance);
}

void quantize_color(int *r, int *g, int *b, int levels) {
  if (!r || !g || !b) {
    SET_ERRNO(ERROR_INVALID_PARAM, "quantize_color: r, g, or b is NULL");
    return;
  }

  if (levels <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "quantize_color: levels must be positive, got %d", levels);
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
    SET_ERRNO(ERROR_INVALID_PARAM, "rgb_to_ansi_8bit: fg_code or bg_code is NULL");
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
