/**
 * @file video/scalar/16color_dithered.c
 * @ingroup video
 * @brief Scalar dithered 16-color ASCII rendering implementations
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
#include <ascii-chat/video/scalar/16color_dithered.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/math.h>

char *image_print_16color_dithered(const image_t *image, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  int h = image->h;
  int w = image->w;

  if (h <= 0 || w <= 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "image_print_16color_dithered: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Allocate error buffer for Floyd-Steinberg dithering
  size_t pixel_count = (size_t)h * (size_t)w;
  rgb_error_t *error_buffer;
  error_buffer = SAFE_CALLOC(pixel_count, sizeof(rgb_error_t), rgb_error_t *);

  // Calculate buffer size (same as non-dithered version)
  // Space for ANSI codes + newlines
  // Calculate with overflow checking
  size_t h_times_w2;
  if (checked_size_mul((size_t)h, (size_t)w, &h_times_w2) != ASCIICHAT_OK) {
    SAFE_FREE(error_buffer);
    return NULL;
  }

  size_t h_times_w2_times_12;
  if (checked_size_mul(h_times_w2, 12u, &h_times_w2_times_12) != ASCIICHAT_OK) {
    SAFE_FREE(error_buffer);
    return NULL;
  }

  size_t buffer_size;
  if (checked_size_add(h_times_w2_times_12, (size_t)h, &buffer_size) != ASCIICHAT_OK) {
    SAFE_FREE(error_buffer);
    return NULL;
  }

  char *buffer;
  buffer = SAFE_MALLOC(buffer_size, char *);

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_pixel_t pixel = image->pixels[y * w + x];

      // Convert RGB to 16-color index using dithering
      uint8_t color_index = rgb_to_16color_dithered(pixel.r, pixel.g, pixel.b, x, y, w, h, error_buffer);
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
  SAFE_FREE(error_buffer); // Clean up error buffer
  return buffer;
}

// 16-color image printing with Floyd-Steinberg dithering and background mode support
char *image_print_16color_dithered_with_background(const image_t *image, bool use_background, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  int h = image->h;
  int w = image->w;

  if (h <= 0 || w <= 0) {
    SET_ERRNO(ERROR_INVALID_STATE, "image_print_16color_dithered_with_background: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Initialize 16-color lookup table
  ansi_fast_init_16color();

  // Allocate error buffer for Floyd-Steinberg dithering
  size_t pixel_count = (size_t)h * (size_t)w;
  rgb_error_t *error_buffer;
  error_buffer = SAFE_CALLOC(pixel_count, sizeof(rgb_error_t), rgb_error_t *);

  // Calculate buffer size (larger for background mode due to more ANSI sequences)
  size_t buffer_size =
      (size_t)h * (size_t)w * (use_background ? 24 : 12) + (size_t)h; // Space for ANSI codes + newlines
  char *buffer;
  buffer = SAFE_MALLOC(buffer_size, char *);

  char *ptr = buffer;
  const char *reset_code = "\033[0m";

  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      rgb_pixel_t pixel = image->pixels[y * w + x];

      // Convert RGB to 16-color index using dithering
      uint8_t color_index = rgb_to_16color_dithered(pixel.r, pixel.g, pixel.b, x, y, w, h, error_buffer);

      if (use_background) {
        // Background mode: use contrasting foreground on background color
        uint8_t bg_r, bg_g, bg_b;
        get_16color_rgb(color_index, &bg_r, &bg_g, &bg_b);

        // Calculate luminance to choose contrasting foreground
        int bg_luminance = (bg_r * 77 + bg_g * 150 + bg_b * 29) / 256;
        uint8_t fg_color = (bg_luminance < 127) ? 15 : 0; // White on dark, black on bright

        ptr = append_16color_bg(ptr, color_index); // Set background to pixel color
        ptr = append_16color_fg(ptr, fg_color);    // Set contrasting foreground
      } else {
        // Foreground mode: set foreground to pixel color
        ptr = append_16color_fg(ptr, color_index);
      }

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
  SAFE_FREE(error_buffer); // Clean up error buffer
  return buffer;
}
