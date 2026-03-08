/**
 * @file video/ascii/avx2.c
 * @ingroup video
 * @brief 🚀 AVX2-accelerated ASCII rendering with 256-bit vector operations for x86_64
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/video/ascii/avx2.h>
#include <ascii-chat/video/ascii/common.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/ascii/avx2/common.h>
#include <ascii-chat/video/terminal/ansi.h>
#include <ascii-chat/util/overflow.h>

#if SIMD_SUPPORT_AVX2
#include <immintrin.h>

// Simple emission functions for direct buffer writing
static inline char *emit_set_256_color_fg_simple(char *pos, uint8_t color_idx) {
  *pos++ = '\x1b';
  *pos++ = '[';
  *pos++ = '3';
  *pos++ = '8';
  *pos++ = ';';
  *pos++ = '5';
  *pos++ = ';';
  if (color_idx >= 100) {
    *pos++ = '0' + (color_idx / 100);
    *pos++ = '0' + ((color_idx / 10) % 10);
    *pos++ = '0' + (color_idx % 10);
  } else if (color_idx >= 10) {
    *pos++ = '0' + (color_idx / 10);
    *pos++ = '0' + (color_idx % 10);
  } else {
    *pos++ = '0' + color_idx;
  }
  *pos++ = 'm';
  return pos;
}

static inline char *emit_set_256_color_bg_simple(char *pos, uint8_t color_idx) {
  *pos++ = '\x1b';
  *pos++ = '[';
  *pos++ = '4';
  *pos++ = '8';
  *pos++ = ';';
  *pos++ = '5';
  *pos++ = ';';
  if (color_idx >= 100) {
    *pos++ = '0' + (color_idx / 100);
    *pos++ = '0' + ((color_idx / 10) % 10);
    *pos++ = '0' + (color_idx % 10);
  } else if (color_idx >= 10) {
    *pos++ = '0' + (color_idx / 10);
    *pos++ = '0' + (color_idx % 10);
  } else {
    *pos++ = '0' + color_idx;
  }
  *pos++ = 'm';
  return pos;
}

static inline char *emit_set_truecolor_fg_simple(char *pos, uint8_t r, uint8_t g, uint8_t b) {
  *pos++ = '\x1b';
  *pos++ = '[';
  *pos++ = '3';
  *pos++ = '8';
  *pos++ = ';';
  *pos++ = '2';
  *pos++ = ';';
  if (r >= 100) {
    *pos++ = '0' + (r / 100);
    *pos++ = '0' + ((r / 10) % 10);
    *pos++ = '0' + (r % 10);
  } else if (r >= 10) {
    *pos++ = '0' + (r / 10);
    *pos++ = '0' + (r % 10);
  } else {
    *pos++ = '0' + r;
  }
  *pos++ = ';';
  if (g >= 100) {
    *pos++ = '0' + (g / 100);
    *pos++ = '0' + ((g / 10) % 10);
    *pos++ = '0' + (g % 10);
  } else if (g >= 10) {
    *pos++ = '0' + (g / 10);
    *pos++ = '0' + (g % 10);
  } else {
    *pos++ = '0' + g;
  }
  *pos++ = ';';
  if (b >= 100) {
    *pos++ = '0' + (b / 100);
    *pos++ = '0' + ((b / 10) % 10);
    *pos++ = '0' + (b % 10);
  } else if (b >= 10) {
    *pos++ = '0' + (b / 10);
    *pos++ = '0' + (b % 10);
  } else {
    *pos++ = '0' + b;
  }
  *pos++ = 'm';
  return pos;
}

static inline char *emit_set_truecolor_bg_simple(char *pos, uint8_t r, uint8_t g, uint8_t b) {
  *pos++ = '\x1b';
  *pos++ = '[';
  *pos++ = '4';
  *pos++ = '8';
  *pos++ = ';';
  *pos++ = '2';
  *pos++ = ';';
  if (r >= 100) {
    *pos++ = '0' + (r / 100);
    *pos++ = '0' + ((r / 10) % 10);
    *pos++ = '0' + (r % 10);
  } else if (r >= 10) {
    *pos++ = '0' + (r / 10);
    *pos++ = '0' + (r % 10);
  } else {
    *pos++ = '0' + r;
  }
  *pos++ = ';';
  if (g >= 100) {
    *pos++ = '0' + (g / 100);
    *pos++ = '0' + ((g / 10) % 10);
    *pos++ = '0' + (g % 10);
  } else if (g >= 10) {
    *pos++ = '0' + (g / 10);
    *pos++ = '0' + (g % 10);
  } else {
    *pos++ = '0' + g;
  }
  *pos++ = ';';
  if (b >= 100) {
    *pos++ = '0' + (b / 100);
    *pos++ = '0' + ((b / 10) % 10);
    *pos++ = '0' + (b % 10);
  } else if (b >= 10) {
    *pos++ = '0' + (b / 10);
    *pos++ = '0' + (b % 10);
  } else {
    *pos++ = '0' + b;
  }
  *pos++ = 'm';
  return pos;
}

char *render_ascii_color_avx2(const image_t *image, bool use_background, bool use_256color, const char *ascii_chars) {
  if (!image || !image->pixels) {
    return NULL;
  }

  const int width = image->w;
  const int height = image->h;

  if (width <= 0 || height <= 0) {
    char *empty;
    empty = SAFE_MALLOC(1, char *);
    empty[0] = '\0';
    return empty;
  }

  // Get cached UTF-8 character mappings
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(ascii_chars);
  if (!utf8_cache) {
    log_error("Failed to get UTF-8 palette cache for AVX2 color");
    return NULL;
  }

  // Use malloc for output buffer (will be freed by caller)
  size_t bytes_per_pixel = use_256color ? 10u : 25u; // Conservative estimates

  // Calculate buffer size with overflow checking
  size_t height_times_width;
  if (checked_size_mul((size_t)height, (size_t)width, &height_times_width) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: height * width overflow");
    return NULL;
  }

  size_t pixel_data_size;
  if (checked_size_mul(height_times_width, bytes_per_pixel, &pixel_data_size) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: (height * width) * bytes_per_pixel overflow");
    return NULL;
  }

  size_t height_times_16;
  if (checked_size_mul((size_t)height, 16u, &height_times_16) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: height * 16 overflow");
    return NULL;
  }

  size_t temp;
  if (checked_size_add(pixel_data_size, height_times_16, &temp) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: pixel_data + height*16 overflow");
    return NULL;
  }

  size_t output_size;
  if (checked_size_add(temp, 1024u, &output_size) != ASCIICHAT_OK) {
    log_error("Buffer size overflow: total output size overflow");
    return NULL;
  }

  char *output = SAFE_MALLOC(output_size, char *);
  if (!output) {
    log_error("Failed to allocate output buffer for AVX2 color rendering");
    return NULL;
  }

  char *pos = output;
  const rgb_pixel_t *pixels_data = (const rgb_pixel_t *)image->pixels;

  // Track current color state
  int curR = -1, curG = -1, curB = -1;
  int cur_color_idx = -1;

  // Generate output row by row with single-pass processing

  for (int y = 0; y < height; y++) {
    const rgb_pixel_t *row_pixels = &pixels_data[y * width];
    int x = 0;

    // AVX2 fast path: process 32 pixels at a time
    while (x + 31 < width) {

      // Process 32 pixels with AVX2 using thread-local buffers
      avx2_load_rgb32_optimized(&row_pixels[x], avx2_r_buffer, avx2_g_buffer, avx2_b_buffer);
      avx2_compute_luminance_32(avx2_r_buffer, avx2_g_buffer, avx2_b_buffer, avx2_luminance_buffer);

      // Process each pixel in the chunk
      int i = 0;
      (void)x;
      while (i < 32) {
        const uint8_t R = avx2_r_buffer[i];
        const uint8_t G = avx2_g_buffer[i];
        const uint8_t B = avx2_b_buffer[i];
        const uint8_t luma_idx = avx2_luminance_buffer[i] >> 2;
        // Use luma_idx directly to index cache64 (0-63), not char_index (0-char_count)
        const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];
        // For RLE comparison, we need char_idx
        const uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx];

        if (use_256color) {
          uint8_t color_idx = rgb_to_256color(R, G, B);

          // Find run length
          int run = 1;
          while (i + run < 32 && x + run < width) {
            const uint8_t next_R = avx2_r_buffer[i + run];
            const uint8_t next_G = avx2_g_buffer[i + run];
            const uint8_t next_B = avx2_b_buffer[i + run];
            const uint8_t next_luma_idx = avx2_luminance_buffer[i + run] >> 2;
            const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
            if (next_char_idx != char_idx)
              break;
            if (rgb_to_256color(next_R, next_G, next_B) != color_idx)
              break;
            run++;
          }

          // Set color if changed
          if (color_idx != cur_color_idx) {
            if (use_background) {
              pos = emit_set_256_color_bg_simple(pos, color_idx);
            } else {
              pos = emit_set_256_color_fg_simple(pos, color_idx);
            }
            cur_color_idx = color_idx;
          }

          // Emit character with RLE
          memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
          pos += char_info->byte_len;

          if (rep_is_profitable(run)) {
            pos = emit_rle_count(pos, run - 1);
          } else {
            for (int k = 1; k < run; k++) {
              memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
              pos += char_info->byte_len;
            }
          }
          i += run;
        } else {
          // Truecolor mode
          // Find run length
          int run = 1;
          while (i + run < 32 && x + run < width) {
            const uint8_t next_R = avx2_r_buffer[i + run];
            const uint8_t next_G = avx2_g_buffer[i + run];
            const uint8_t next_B = avx2_b_buffer[i + run];
            const uint8_t next_luma_idx = avx2_luminance_buffer[i + run] >> 2;
            const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
            if (next_char_idx != char_idx)
              break;
            if (next_R != R || next_G != G || next_B != B)
              break;
            run++;
          }

          // Set color if changed
          if ((int)R != curR || (int)G != curG || (int)B != curB) {
            if (use_background) {
              pos = emit_set_truecolor_bg_simple(pos, R, G, B);
            } else {
              pos = emit_set_truecolor_fg_simple(pos, R, G, B);
            }
            curR = R;
            curG = G;
            curB = B;
          }

          // Emit character with RLE
          memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
          pos += char_info->byte_len;

          if (rep_is_profitable(run)) {
            pos = emit_rle_count(pos, run - 1);
          } else {
            for (int k = 1; k < run; k++) {
              memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
              pos += char_info->byte_len;
            }
          }
          i += run;
        }
      }
      x += 32;
    }

    // Scalar processing for remaining pixels (< 32)
    while (x < width) {
      const rgb_pixel_t *p = &row_pixels[x];
      const uint8_t R = p->r, G = p->g, B = p->b;
      const int luminance = (LUMA_RED * R + LUMA_GREEN * G + LUMA_BLUE * B + 128) >> 8;
      const uint8_t luma_idx = luminance >> 2;
      // Use luma_idx directly to index cache64 (0-63), not char_index (0-char_count)
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];
      // For RLE comparison, we need char_idx
      const uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx];

      if (use_256color) {
        uint8_t color_idx = rgb_to_256color(R, G, B);

        // Find run length
        int run = 1;
        while (x + run < width) {
          const rgb_pixel_t *next_p = &row_pixels[x + run];
          const int next_luminance = (LUMA_RED * next_p->r + LUMA_GREEN * next_p->g + LUMA_BLUE * next_p->b + 128) >> 8;
          const uint8_t next_luma_idx = next_luminance >> 2;
          const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
          if (next_char_idx != char_idx)
            break;
          if (rgb_to_256color(next_p->r, next_p->g, next_p->b) != color_idx)
            break;
          run++;
        }

        // Set color if changed
        if (color_idx != cur_color_idx) {
          if (use_background) {
            pos = emit_set_256_color_bg_simple(pos, color_idx);
          } else {
            pos = emit_set_256_color_fg_simple(pos, color_idx);
          }
          cur_color_idx = color_idx;
        }

        // Emit character with RLE
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;

        if (rep_is_profitable(run)) {
          pos = emit_rle_count(pos, run - 1);
        } else {
          for (int k = 1; k < run; k++) {
            memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
            pos += char_info->byte_len;
          }
        }
        x += run;
      } else {
        // Truecolor mode
        // Find run length
        int run = 1;
        while (x + run < width) {
          const rgb_pixel_t *next_p = &row_pixels[x + run];
          const int next_luminance = (LUMA_RED * next_p->r + LUMA_GREEN * next_p->g + LUMA_BLUE * next_p->b + 128) >> 8;
          const uint8_t next_luma_idx = next_luminance >> 2;
          const uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx];
          if (next_char_idx != char_idx)
            break;
          if (next_p->r != R || next_p->g != G || next_p->b != B)
            break;
          run++;
        }

        // Set color if changed
        if ((int)R != curR || (int)G != curG || (int)B != curB) {
          if (use_background) {
            pos = emit_set_truecolor_bg_simple(pos, R, G, B);
          } else {
            pos = emit_set_truecolor_fg_simple(pos, R, G, B);
          }
          curR = R;
          curG = G;
          curB = B;
        }

        // Emit character with RLE
        memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
        pos += char_info->byte_len;

        if (rep_is_profitable(run)) {
          pos = emit_rle_count(pos, run - 1);
        } else {
          for (int k = 1; k < run; k++) {
            memcpy(pos, char_info->utf8_bytes, char_info->byte_len);
            pos += char_info->byte_len;
          }
        }
        x += run;
      }
    }

    // Add reset sequence and newline after each row (except last)
    *pos++ = '\x1b';
    *pos++ = '[';
    *pos++ = '0';
    *pos++ = 'm';
    if (y < height - 1) {
      *pos++ = '\n';
    }
  }

  *pos = '\0'; // Null terminate

  return output;
}

#endif /* SIMD_SUPPORT_AVX2 */
