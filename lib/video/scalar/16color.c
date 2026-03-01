/**
 * @file video/scalar/16color.c
 * @ingroup video
 * @brief Scalar 16-color ASCII rendering implementation
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <ascii-chat/common.h>
#include <ascii-chat/video/output_buffer.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/video/scalar/16color.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/math.h>

char *image_print_16color(const image_t *image, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  int h = image->h;
  int w = image->w;

  if (h <= 0 || w <= 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "image_print_16color: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Calculate buffer size (smaller than 256-color due to shorter ANSI sequences)
  // Space for ANSI codes + newlines
  // Calculate with overflow checking
  size_t h_times_w;
  if (checked_size_mul((size_t)h, (size_t)w, &h_times_w) != ASCIICHAT_OK) {
    return NULL;
  }

  size_t h_times_w_times_12;
  if (checked_size_mul(h_times_w, 12u, &h_times_w_times_12) != ASCIICHAT_OK) {
    return NULL;
  }

  size_t buffer_size;
  if (checked_size_add(h_times_w_times_12, (size_t)h, &buffer_size) != ASCIICHAT_OK) {
    return NULL;
  }

  char *buffer;
  buffer = SAFE_MALLOC(buffer_size, char *);

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_pixel_t pixel = image->pixels[y * w + x];

      // Convert RGB to 16-color index and generate ANSI sequence
      uint8_t color_index = rgb_to_16color(pixel.r, pixel.g, pixel.b);
      ptr = append_16color_fg(ptr, color_index);

      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use UTF-8 cache which contains character index ramp
      utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
      if (!utf8_cache) {
        SET_ERRNO(ERROR_INVALID_STATE, "Failed to get UTF-8 cache");
        return NULL;
      }

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      uint8_t safe_luminance = clamp_rgb(luminance);
      uint8_t luma_idx = (uint8_t)(safe_luminance >> 2);        // 0-63 index (same as SIMD)
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx]; // Map to character index (same as SIMD)

      // Use direct palette character lookup (same as SIMD would do)
      char ascii_char = palette[char_idx];
      const utf8_char_t *char_info = &utf8_cache->cache[(unsigned char)ascii_char];

      if (char_info) {
        // Copy UTF-8 character bytes
        for (int byte_idx = 0; byte_idx < char_info->byte_len; byte_idx++) {
          *ptr++ = char_info->utf8_bytes[byte_idx];
        }
      } else {
        // Fallback to simple ASCII if UTF-8 cache fails
        size_t palette_len = strlen(palette);
        int palette_index = (luminance * ((int)palette_len - 1) + 127) / 255;
        *ptr++ = palette[palette_index];
      }
    }

    // Add reset and newline at end of each row
    SAFE_STRNCPY(ptr, reset_code, 5);
    ptr += strlen(reset_code);
    if (y < h - 1) {
      *ptr++ = '\n';
    }
  }

  *ptr = '\0';
  return buffer;
}

// 16-color image printing with Floyd-Steinberg dithering
