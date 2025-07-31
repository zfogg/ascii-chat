#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <jpeglib.h>

#include "ascii.h"
#include "aspect_ratio.h"
#include "image.h"
#include "options.h"
#include "round.h"

typedef void (*image_resize_ptrfun)(const image_t *, image_t *);
image_resize_ptrfun global_image_resize_fun = NULL;

image_t *image_new(int width, int height) {
  image_t *p;

  SAFE_MALLOC(p, sizeof(image_t), image_t *);

  const ssize_t pixels_size = (unsigned long)width * (unsigned long)height * sizeof(rgb_t);
  if ((unsigned long)pixels_size > IMAGE_MAX_PIXELS_SIZE) {
    log_error("Image size exceeds maximum allowed: %d x %d", width, height);
    return NULL;
  }
  SAFE_MALLOC(p->pixels, pixels_size, rgb_t *);

  p->w = width;
  p->h = height;
  return p;
}

void image_destroy(image_t *p) {
  free(p->pixels);
  free(p);
}

void image_clear(image_t *p) {
  memset(p->pixels, 0, (unsigned long)p->w * (unsigned long)p->h * sizeof(rgb_t));
}

inline rgb_t *image_pixel(image_t *p, const int x, const int y) {
  return &p->pixels[x + y * p->w];
}

void image_resize(const image_t *s, image_t *d) {
  global_image_resize_fun(s, d);
}

// Optimized interpolation function with better integer arithmetic and memory
// access
void image_resize_interpolation(const image_t *source, image_t *dest) {
  const int src_w = source->w;
  const int src_h = source->h;
  const int dst_w = dest->w;
  const int dst_h = dest->h;

  // Use fixed-point arithmetic for better performance
  const uint32_t x_ratio = ((src_w << 16) / dst_w) + 1;
  const uint32_t y_ratio = ((src_h << 16) / dst_h) + 1;

  const rgb_t *src_pixels = source->pixels;
  rgb_t *dst_pixels = dest->pixels;

  for (int y = 0; y < dst_h; y++) {
    const uint32_t src_y = (y * y_ratio) >> 16;
    const rgb_t *src_row = src_pixels + src_y * src_w;
    rgb_t *dst_row = dst_pixels + y * dst_w;

    for (int x = 0; x < dst_w; x++) {
      const uint32_t src_x = (x * x_ratio) >> 16;
      dst_row[x] = src_row[src_x];
    }
  }
}

image_t *image_read(FILE *fp) {
  JSAMPARRAY buffer;
  int row_stride;
  struct jpeg_decompress_struct jpg;
  struct jpeg_error_mgr jerr;
  image_t *p;

  global_image_resize_fun = image_resize_interpolation;

  jpg.err = jpeg_std_error(&jerr);

  jpeg_create_decompress(&jpg);
  jpeg_stdio_src(&jpg, fp);
  jpeg_read_header(&jpg, TRUE);
  jpeg_start_decompress(&jpg);

  if (jpg.data_precision != 8) {
    fprintf(stderr, "jp2a: can only handle 8-bit color channels\n");
    exit(1);
  }

  row_stride = jpg.output_width * jpg.output_components;
  buffer = (*jpg.mem->alloc_sarray)((j_common_ptr)&jpg, JPOOL_IMAGE, row_stride, 1);

  // Store image dimensions for aspect ratio recalculation on terminal resize
  last_image_width = jpg.output_width;
  last_image_height = jpg.output_height;

  p = image_new(jpg.output_width, jpg.output_height);

  while (jpg.output_scanline < jpg.output_height) {
    jpeg_read_scanlines(&jpg, buffer, 1);

    if (jpg.output_components == 3) {
      memcpy(&p->pixels[(jpg.output_scanline - 1) * p->w], &buffer[0][0], sizeof(rgb_t) * p->w);
    } else {
      rgb_t *pixels = &p->pixels[(jpg.output_scanline - 1) * p->w];

      // grayscale - optimized loop
      const JSAMPLE *src = buffer[0];
      for (int x = 0; x < (int)jpg.output_width; ++x) {
        const JSAMPLE gray = src[x];
        pixels[x].r = pixels[x].g = pixels[x].b = gray;
      }
    }
  }

  jpeg_finish_decompress(&jpg);
  jpeg_destroy_decompress(&jpg);
  return p;
}

// Optimized palette generation with caching
static char luminance_palette[ASCII_LUMINANCE_LEVELS * sizeof(char)];

void precalc_luminance_palette() {
  const int palette_len = strlen(ascii_palette) - 1;
  for (int n = 0; n < ASCII_LUMINANCE_LEVELS; n++) {
    luminance_palette[n] = ascii_palette[ROUND((float)palette_len * (float)n / (ASCII_LUMINANCE_LEVELS - 1))];
  }
}

void precalc_rgb_palettes(const float red, const float green, const float blue) {
  for (int n = 0; n < ASCII_LUMINANCE_LEVELS; ++n) {
    RED[n] = ((float)n) * red;
    GREEN[n] = ((float)n) * green;
    BLUE[n] = ((float)n) * blue;
    GRAY[n] = ((float)n);
  }
}

// Optimized image printing with better memory access patterns
char *image_print(const image_t *p) {
  const int h = p->h;
  const int w = p->w;
  const ssize_t len = h * w;

  const rgb_t *pix = p->pixels;
  const unsigned short int *red_lut = RED;
  const unsigned short int *green_lut = GREEN;
  const unsigned short int *blue_lut = BLUE;

  char *lines;
  SAFE_MALLOC(lines, (len + 2) * sizeof(char), char *);

  lines[len] = ASCII_DELIMITER;
  lines[len + 1] = '\0';

  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w; x++) {
      const rgb_t pixel = pix[row_offset + x];
      const int luminance = red_lut[pixel.r] + green_lut[pixel.g] + blue_lut[pixel.b];
      lines[row_offset + x] = luminance_palette[luminance];
    }
    lines[row_offset + w - 1] = '\n';
  }

  return lines;
}

// Color quantization to reduce frame size and improve performance
void quantize_color(int *r, int *g, int *b, int levels) {
  int step = 256 / levels;
  *r = (*r / step) * step;
  *g = (*g / step) * step;
  *b = (*b / step) * step;
}

// Colored ASCII art printing function with quantization
char *image_print_colored(const image_t *p) {
  const int h = p->h;
  const int w = p->w;

  size_t lines_size = FRAME_BUFFER_SIZE_FINAL * sizeof(char);
  char *lines;
  SAFE_MALLOC(lines, lines_size, char *);

  // Now we can use the lines buffer safely in this function.
  // No buffer overflows and don't worry about snprintf's return value.

  const rgb_t *pix = p->pixels;

  char *current_pos = lines;

  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w; x++) {
      const rgb_t pixel = pix[row_offset + x];
      int r = pixel.r, g = pixel.g, b = pixel.b;
      const int luminance = RED[r] + GREEN[g] + BLUE[b];
      const char ascii_char = luminance_palette[luminance];

      // Quantize colors.
      // quantize_color(&r, &g, &b, 8);

      if (opt_background_color) {
        // Choose contrasting foreground color (black or white) based on
        // luminance
        int fg_r, fg_g, fg_b;
        if (luminance < 127) { // Dim background, use black text
          fg_r = fg_g = fg_b = 0;
        } else { // Bright background, use white text
          fg_r = fg_g = fg_b = 255;
        }

        const char *ascii_fg = rgb_to_ansi_fg(fg_r, fg_g, fg_b);
        const char *ascii_bg = rgb_to_ansi_bg(r, g, b);
        const size_t operation_size =
            strlen(ascii_fg) + strlen(ascii_bg) + 1 + 1; // strlen + ascii char + null terminator
        const int written = snprintf(current_pos, operation_size, "%s%s%c", ascii_fg, ascii_bg, ascii_char);
        current_pos += written;

      } else {
        const char *ascii_fg = rgb_to_ansi_fg(r, g, b);
        const size_t operation_size = strlen(ascii_fg) + 1 + 1; // strlen + ascii char + null terminator
        const int written = snprintf(current_pos, operation_size, "%s%c", ascii_fg, ascii_char);
        current_pos += written;
      }
    }

    // Add newline and reset color at end of each row
    const size_t operation_size = 4 + 1 + 1; // 4 for color reset + newline + null terminator
    const int written = snprintf(current_pos, operation_size, "\033[0m\n");
    current_pos += written;
  }

  // Add ASCII delimiter, and null terminator making it a valid C string.
  *current_pos++ = ASCII_DELIMITER;
  *current_pos = '\0';

  return lines;
}