/**
 * @file video/ascii/scalar/background.c
 * @ingroup video
 * @brief Scalar ASCII background color rendering (truecolor)
 *
 * Renders images using ANSI truecolor background colors with contrasting
 * foreground text. Background is set to the pixel color, foreground is
 * white on dark pixels, black on bright pixels.
 */

#include <ascii-chat/video/ascii/scalar/background.h>
#include <ascii-chat/video/ascii/scalar/foreground.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/simd/dispatch.h>
#include <ascii-chat/util/math.h>

char *image_print_color_background(const image_t *p, const char *palette) {
  if (!p || !palette || !p->pixels) {
    return NULL;
  }

  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
  if (!utf8_cache) {
    return NULL;
  }

  const int h = p->h;
  const int w = p->w;
  const size_t h_sz = (size_t)h;
  const size_t w_sz = (size_t)w;

  if (h_sz > 0 && w_sz > SIZE_MAX / h_sz) {
    return NULL;
  }

  // Each pixel needs: bg sequence (~19) + fg sequence (~19) + char (up to 4) + possible reset
  const size_t bytes_per_pixel = 50;
  const size_t total_pixels = h_sz * w_sz;
  const size_t lines_size = total_pixels * bytes_per_pixel + h_sz + 16;

  char *lines;
  lines = SAFE_MALLOC(lines_size, char *);

  char *ptr = lines;
  const rgb_pixel_t *pix = p->pixels;

  uint8_t *luminance_row = SAFE_MALLOC((size_t)w, uint8_t *);
  simd_luminance_fn luminance_fn = simd_backend()->luminance;
  for (int y = 0; y < h; y++) {
    luminance_fn((const uint8_t *)(p->pixels + (size_t)y * w), luminance_row, (size_t)w);
    const int row_offset = y * w;

    for (int x = 0; x < w; x++) {
      const rgb_pixel_t pixel = pix[row_offset + x];

      // Set background to pixel color
      ptr += SAFE_SNPRINTF(ptr, 20, "\033[48;2;%d;%d;%dm", pixel.r, pixel.g, pixel.b);

      // Choose contrasting foreground: white on dark, black on bright
      int luminance = luminance_row[x];
      if (luminance < 128) {
        ptr += SAFE_SNPRINTF(ptr, 20, "\033[38;2;255;255;255m");
      } else {
        ptr += SAFE_SNPRINTF(ptr, 20, "\033[38;2;0;0;0m");
      }

      // Select character based on luminance
      uint8_t safe_luminance = clamp_rgb(luminance);
      const utf8_char_t *char_info = &utf8_cache->cache[safe_luminance];
      for (int i = 0; i < char_info->byte_len; i++) {
        *ptr++ = char_info->utf8_bytes[i];
      }
    }

    // Reset and newline
    *ptr++ = '\033';
    *ptr++ = '[';
    *ptr++ = '0';
    *ptr++ = 'm';
    if (y < h - 1) {
      *ptr++ = '\n';
    }
  }

  *ptr = '\0';
  SAFE_FREE(luminance_row);
  return lines;
}
