/**
 * @file video/scalar/greyscale.c
 * @ingroup video
 * @brief Scalar greyscale and color ASCII rendering implementations
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
#include <ascii-chat/video/render/scalar/foreground.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/math.h>

char *image_print(const image_t *p, const char *palette) {
  if (!p || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_print: p=%p or palette=%p is NULL", p, palette);
    return NULL;
  }
  if (!p->pixels) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_print: p->pixels is NULL");
    return NULL;
  }

  const int h = p->h;
  const int w = p->w;

  if (h <= 0 || w <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image_print: invalid dimensions h=%d, w=%d", h, w);
    return NULL;
  }

  // Get UTF-8 character cache for proper multi-byte character support
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
  if (!utf8_cache) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to get UTF-8 palette cache for scalar rendering");
    return NULL;
  }

  // Character index ramp is now part of UTF-8 cache - no separate cache needed

  // Need space for h rows with UTF-8 characters, plus h-1 newlines, plus null terminator
  const size_t max_char_bytes = 4; // Max UTF-8 character size

  const rgb_pixel_t *pix = p->pixels;

  // Use outbuf_t for efficient UTF-8 RLE emission (same as SIMD renderers)
  outbuf_t ob = {0};

  // Calculate buffer size with overflow checking
  size_t w_times_bytes;
  if (checked_size_mul((size_t)w, max_char_bytes, &w_times_bytes) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Buffer size overflow: width too large for UTF-8 encoding");
    return NULL;
  }

  size_t w_times_bytes_plus_one;
  if (checked_size_add(w_times_bytes, 1, &w_times_bytes_plus_one) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Buffer size overflow: width * bytes + 1 overflow");
    return NULL;
  }

  if (checked_size_mul((size_t)h, w_times_bytes_plus_one, &ob.cap) != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Buffer size overflow: height * (width * bytes + 1) overflow");
    return NULL;
  }

  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate output buffer for scalar rendering");
    return NULL;
  }

  // Process pixels with UTF-8 RLE emission (same approach as SIMD)
  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w;) {
      const rgb_pixel_t pixel = pix[row_offset + x];
      // Use same luminance formula as SIMD: ITU-R BT.601 with rounding
      const int luminance = (77 * pixel.r + 150 * pixel.g + 29 * pixel.b + 128) >> 8;

      // Use same 6-bit precision as SIMD: map luminance (0-255) to bucket (0-63) then to character
      uint8_t safe_luminance = clamp_rgb(luminance);
      uint8_t luma_idx = (uint8_t)(safe_luminance >> 2);        // 0-63 index (same as SIMD)
      uint8_t char_idx = utf8_cache->char_index_ramp[luma_idx]; // Map to character index (same as SIMD)

      // Use same 64-entry cache as SIMD for consistency
      const utf8_char_t *char_info = &utf8_cache->cache64[luma_idx];

      // Find run length for same character (RLE optimization)
      int j = x + 1;
      while (j < w) {
        const rgb_pixel_t next_pixel = pix[row_offset + j];
        const int next_luminance = (77 * next_pixel.r + 150 * next_pixel.g + 29 * next_pixel.b + 128) >> 8;
        uint8_t next_safe_luminance = clamp_rgb(next_luminance);
        uint8_t next_luma_idx = (uint8_t)(next_safe_luminance >> 2);        // 0-63 index (same as SIMD)
        uint8_t next_char_idx = utf8_cache->char_index_ramp[next_luma_idx]; // Map to character index (same as SIMD)
        if (next_char_idx != char_idx)
          break;
        j++;
      }
      uint32_t run = (uint32_t)(j - x);

      // Emit UTF-8 character with RLE (same as SIMD)
      ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
      if (rep_is_profitable(run)) {
        emit_rep(&ob, run - 1);
      } else {
        for (uint32_t k = 1; k < run; k++) {
          ob_write(&ob, char_info->utf8_bytes, char_info->byte_len);
        }
      }
      x = j;
    }

    // Add newline between rows (except last row)
    if (y != h - 1) {
      ob_putc(&ob, '\n');
    }
  }

  ob_term(&ob);
  return ob.buf;
}

// Color quantization to reduce frame size and improve performance
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

/**
 * Converts an image to colored ASCII art with ANSI escape codes.
 *
 * This function generates a string representation of an image where each pixel
 * is converted to an ASCII character with ANSI color codes. The character is
 * chosen based on luminance, and colors are applied using 24-bit RGB ANSI
 * escape sequences.
 *
 * Buffer allocation is precisely calculated to avoid waste and prevent overflows:
 * - Each pixel: 1 ASCII char + foreground ANSI code (19 bytes max)
 * - Background mode: adds background ANSI code (19 bytes max per pixel)
 * - Each row: reset sequence (\033[0m = 4 bytes) + newline (except last row)
 * - At the end: null terminator (1 byte)
 *
 * Color modes:
 * - Foreground only (default): ASCII characters with colored foreground
 * - Background mode (opt_background_color): colored background with contrasting
 *   foreground (black on bright backgrounds, white on dark backgrounds)
 *
 * ANSI escape code format:
 * - Foreground: \033[38;2;R;G;Bm (11-19 bytes depending on RGB values)
 * - Background: \033[48;2;R;G;Bm (11-19 bytes depending on RGB values)
 * - Reset: \033[0m (4 bytes)
 *
 * @param p Pointer to image_t structure containing pixel data
 * @return Dynamically allocated string containing colored ASCII art, or NULL on error.
 *         Caller is responsible for freeing the returned string.
 *
 * @note The function performs overflow checks to prevent integer overflow when
 *       calculating buffer sizes for very large images.
 * @note Uses global opt_background_color to determine color mode.
 * @note Exits with ERROR_BUFFER if buffer overflow is detected
 *       during string construction (should never happen with correct calculation).
 */
char *image_print_color(const image_t *p, const char *palette) {
  if (!p || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "p=%p or palette=%p is NULL", p, palette);
    return NULL;
  }
  if (!p->pixels) {
    SET_ERRNO(ERROR_INVALID_PARAM, "p->pixels is NULL");
    return NULL;
  }

  // Get UTF-8 character cache for proper multi-byte character support
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(palette);
  if (!utf8_cache) {
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to get UTF-8 palette cache for scalar color rendering");
    return NULL;
  }

  const int h = p->h;
  const int w = p->w;

  // Constants for ANSI escape codes (using exact sizes from ansi_fast.c)
  const size_t max_fg_ansi = 19; // \033[38;2;255;255;255m
  const size_t max_bg_ansi = 19; // \033[48;2;255;255;255m
  const size_t reset_len = 4;    // \033[0m

  const size_t h_sz = (size_t)h;
  const size_t w_sz = (size_t)w;

  // Ensure h * w won't overflow
  if (h_sz > 0 && w_sz > SIZE_MAX / h_sz) {
    SET_ERRNO(ERROR_INVALID_STATE, "Image dimensions too large: %d x %d", h, w);
    return NULL;
  }

  const size_t total_pixels = h_sz * w_sz;
  const size_t bytes_per_pixel = 1 + max_fg_ansi + max_bg_ansi; // Conservative estimate for max possible

  // Ensure total_pixels * bytes_per_pixel won't overflow
  if (total_pixels > SIZE_MAX / bytes_per_pixel) {
    SET_ERRNO(ERROR_INVALID_STATE, "Pixel data too large for buffer: %d x %d", h, w);
    return NULL;
  }

  const size_t pixel_bytes = total_pixels * bytes_per_pixel;

  // Per row: newline (except last row)
  // Final reset added once by ansi_rle_finish()
  const size_t total_newlines = (h_sz > 0) ? (h_sz - 1) : 0;
  const size_t final_reset = reset_len; // One reset at end (from ansi_rle_finish)

  // Final buffer size: pixel bytes + per-row newlines + final reset + null terminator
  const size_t extra_bytes = total_newlines + final_reset + 1;

  if (pixel_bytes > SIZE_MAX - extra_bytes) {
    SET_ERRNO(ERROR_INVALID_STATE, "Final buffer size would overflow: %d x %d", h, w);
    return NULL;
  }

  const size_t lines_size = pixel_bytes + extra_bytes;
  char *lines;
  lines = SAFE_MALLOC(lines_size, char *);

  const rgb_pixel_t *pix = p->pixels;
  // char *current_pos = lines;
  // const char *buffer_end = lines + lines_size - 1; // reserve space for '\0'

  // Initialize the optimized RLE context for color sequence caching
  // Note: This function should be called via image_print_with_capabilities() for proper per-client rendering
  ansi_rle_context_t rle_ctx;
  ansi_color_mode_t color_mode = ANSI_MODE_FOREGROUND; // Default to foreground-only for legacy usage
  ansi_rle_init(&rle_ctx, lines, lines_size, color_mode);

  // Process each pixel using the optimized RLE context
  for (int y = 0; y < h; y++) {
    const int row_offset = y * w;

    for (int x = 0; x < w; x++) {
      const rgb_pixel_t pixel = pix[row_offset + x];
      int r = pixel.r, g = pixel.g, b = pixel.b;
      // Standard ITU-R BT.601 luminance calculation
      const int luminance = (77 * r + 150 * g + 29 * b + 128) >> 8;

      // Use UTF-8 character cache for proper character selection
      uint8_t safe_luminance = clamp_rgb(luminance);
      const utf8_char_t *char_info = &utf8_cache->cache[safe_luminance];

      // For RLE, we need to pass the first byte of the UTF-8 character
      // Note: RLE system may need updates for full UTF-8 support
      const char ascii_char = char_info->utf8_bytes[0];

      ansi_rle_add_pixel(&rle_ctx, (uint8_t)r, (uint8_t)g, (uint8_t)b, ascii_char);
    }

    // Add newline after each row (except the last row)
    if (y != h - 1 && rle_ctx.length < rle_ctx.capacity - 1) {
      rle_ctx.buffer[rle_ctx.length++] = '\n';
    }
  }

  ansi_rle_finish(&rle_ctx);

  // DEBUG: Validate frame integrity right after rendering
  // Check for incomplete ANSI sequences line by line
  {
    const char *line_start = lines;
    int line_num = 0;
    while (*line_start) {
      const char *line_end = line_start;
      while (*line_end && *line_end != '\n')
        line_end++;

      // Check for incomplete ANSI sequence at end of this line
      for (const char *p = line_start; p < line_end;) {
        if (*p == '\033' && p + 1 < line_end && *(p + 1) == '[') {
          const char *seq_start = p;
          p += 2;
          // Scan for terminator (@ through ~, i.e., 0x40-0x7E)
          while (p < line_end && !(*p >= '@' && *p <= '~'))
            p++;
          if (p >= line_end) {
            // Incomplete sequence found
            size_t incomplete_len = (size_t)(line_end - seq_start);
            log_error("RENDER_INCOMPLETE_ANSI: Line %d (w=%d h=%d) has incomplete ANSI sequence (%zu bytes)", line_num,
                      w, h, incomplete_len);
            // Show the incomplete sequence
            char debug_buf[128] = {0};
            size_t debug_pos = 0;
            for (const char *d = seq_start; d < line_end && debug_pos < sizeof(debug_buf) - 10; d++) {
              unsigned char c = (unsigned char)*d;
              if (c == '\033') {
                debug_buf[debug_pos++] = '<';
                debug_buf[debug_pos++] = 'E';
                debug_buf[debug_pos++] = 'S';
                debug_buf[debug_pos++] = 'C';
                debug_buf[debug_pos++] = '>';
              } else if (c >= 0x20 && c < 0x7F) {
                debug_buf[debug_pos++] = (char)c;
              } else {
                debug_pos += (size_t)safe_snprintf(debug_buf + debug_pos, sizeof(debug_buf) - debug_pos, "<%02X>", c);
              }
            }
            log_error("  Incomplete seq: %s", debug_buf);
            break;
          } else {
            p++; // Skip terminator
          }
        } else {
          p++;
        }
      }

      line_num++;
      line_start = *line_end ? line_end + 1 : line_end;
    }
  }

  return lines;
}

// RGB to ANSI color conversion functions
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

// Capability-aware image printing function
/**
 * @file video/scalar/256color.c
 * @ingroup video
 * @brief Scalar 256-color ASCII rendering implementation
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
#include <ascii-chat/video/render/scalar/foreground.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/overflow.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/math.h>

char *image_print_256color(const image_t *image, const char *palette) {
  if (!image || !image->pixels || !palette) {
    SET_ERRNO(ERROR_INVALID_PARAM, "image=%p or image->pixels=%p or palette=%p is NULL", image, image->pixels, palette);
    return NULL;
  }

  // Use the existing optimized SIMD colored printing (no background for 256-color mode)
#ifdef SIMD_SUPPORT
  char *result = image_print_color_simd((image_t *)image, false, true, palette);
#else
  char *result = image_print_color(image, palette);
#endif

  return result;
}

// 16-color image printing function using ansi_fast color conversion
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
#include <ascii-chat/video/render/scalar/foreground.h>
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
#include <ascii-chat/video/render/scalar/foreground.h>
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
