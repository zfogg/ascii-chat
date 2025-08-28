#include "ssse3.h"
#include "ascii_simd.h"
#include "options.h"

#ifdef SIMD_SUPPORT_SSSE3
#include <tmmintrin.h>

void convert_pixels_ssse3(const rgb_pixel_t *pixels, char *ascii_chars, int count) {

  int i = 0;

  // SSSE3 constants for luminance calculation
  const __m128i luma_const_77 = _mm_set1_epi16(LUMA_RED);
  const __m128i luma_const_150 = _mm_set1_epi16(LUMA_GREEN);
  const __m128i luma_const_29 = _mm_set1_epi16(LUMA_BLUE);

  // Process 32-pixel chunks with dual 16-pixel SSSE3 processing for maximum throughput
  for (; i + 31 < count; i += 32) {
    // Process first 16-pixel chunk with SSSE3 shuffle-based RGB deinterleaving
    const uint8_t *src1 = (const uint8_t *)&pixels[i];

    // Load 48 bytes of RGB data for first 16 pixels using 3 loads (16 bytes each)
    __m128i chunk0_1 = _mm_loadu_si128((const __m128i *)(src1 + 0));  // RGBRGBRGBRGBRGBR (bytes 0-15)
    __m128i chunk1_1 = _mm_loadu_si128((const __m128i *)(src1 + 16)); // GBRGBRGBRGBRGBRG (bytes 16-31)
    __m128i chunk2_1 = _mm_loadu_si128((const __m128i *)(src1 + 32)); // BRGBRGBRGBRGBRGB (bytes 32-47)

    // Use SSSE3 shuffle to deinterleave RGB data efficiently for first 16 pixels
    const __m128i mask_r_0 = _mm_setr_epi8(0, 3, 6, 9, 12, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i mask_r_1 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, 2, 5, 8, 11, 14, -1, -1, -1, -1, -1);
    const __m128i mask_r_2 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1, 4, 7, 10, 13);

    __m128i r_vec1 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(chunk0_1, mask_r_0), _mm_shuffle_epi8(chunk1_1, mask_r_1)),
                     _mm_shuffle_epi8(chunk2_1, mask_r_2));

    const __m128i mask_g_0 = _mm_setr_epi8(1, 4, 7, 10, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i mask_g_1 = _mm_setr_epi8(-1, -1, -1, -1, -1, 0, 3, 6, 9, 12, 15, -1, -1, -1, -1, -1);
    const __m128i mask_g_2 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 5, 8, 11, 14);

    __m128i g_vec1 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(chunk0_1, mask_g_0), _mm_shuffle_epi8(chunk1_1, mask_g_1)),
                     _mm_shuffle_epi8(chunk2_1, mask_g_2));

    const __m128i mask_b_0 = _mm_setr_epi8(2, 5, 8, 11, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i mask_b_1 = _mm_setr_epi8(-1, -1, -1, -1, -1, 1, 4, 7, 10, 13, -1, -1, -1, -1, -1, -1);
    const __m128i mask_b_2 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 3, 6, 9, 12, 15);

    __m128i b_vec1 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(chunk0_1, mask_b_0), _mm_shuffle_epi8(chunk1_1, mask_b_1)),
                     _mm_shuffle_epi8(chunk2_1, mask_b_2));

    // Process second 16-pixel chunk (pixels i+16 through i+31)
    const uint8_t *src2 = (const uint8_t *)&pixels[i + 16];

    // Load 48 bytes of RGB data for second 16 pixels
    __m128i chunk0_2 = _mm_loadu_si128((const __m128i *)(src2 + 0));
    __m128i chunk1_2 = _mm_loadu_si128((const __m128i *)(src2 + 16));
    __m128i chunk2_2 = _mm_loadu_si128((const __m128i *)(src2 + 32));

    // Deinterleave RGB data for second 16 pixels using same masks
    __m128i r_vec2 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(chunk0_2, mask_r_0), _mm_shuffle_epi8(chunk1_2, mask_r_1)),
                     _mm_shuffle_epi8(chunk2_2, mask_r_2));

    __m128i g_vec2 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(chunk0_2, mask_g_0), _mm_shuffle_epi8(chunk1_2, mask_g_1)),
                     _mm_shuffle_epi8(chunk2_2, mask_g_2));

    __m128i b_vec2 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(chunk0_2, mask_b_0), _mm_shuffle_epi8(chunk1_2, mask_b_1)),
                     _mm_shuffle_epi8(chunk2_2, mask_b_2));

    // Widen to 16-bit for accurate arithmetic - first 16 pixels
    __m128i r_lo1 = _mm_unpacklo_epi8(r_vec1, _mm_setzero_si128());
    __m128i r_hi1 = _mm_unpackhi_epi8(r_vec1, _mm_setzero_si128());
    __m128i g_lo1 = _mm_unpacklo_epi8(g_vec1, _mm_setzero_si128());
    __m128i g_hi1 = _mm_unpackhi_epi8(g_vec1, _mm_setzero_si128());
    __m128i b_lo1 = _mm_unpacklo_epi8(b_vec1, _mm_setzero_si128());
    __m128i b_hi1 = _mm_unpackhi_epi8(b_vec1, _mm_setzero_si128());

    // Widen to 16-bit for accurate arithmetic - second 16 pixels
    __m128i r_lo2 = _mm_unpacklo_epi8(r_vec2, _mm_setzero_si128());
    __m128i r_hi2 = _mm_unpackhi_epi8(r_vec2, _mm_setzero_si128());
    __m128i g_lo2 = _mm_unpacklo_epi8(g_vec2, _mm_setzero_si128());
    __m128i g_hi2 = _mm_unpackhi_epi8(g_vec2, _mm_setzero_si128());
    __m128i b_lo2 = _mm_unpacklo_epi8(b_vec2, _mm_setzero_si128());
    __m128i b_hi2 = _mm_unpackhi_epi8(b_vec2, _mm_setzero_si128());

    // SIMD luminance calculation: y = (77*r + 150*g + 29*b) >> 8
    // Process first chunk (4 x 8-pixel groups)
    __m128i luma_lo1 = _mm_mullo_epi16(r_lo1, luma_const_77);
    luma_lo1 = _mm_add_epi16(luma_lo1, _mm_mullo_epi16(g_lo1, luma_const_150));
    luma_lo1 = _mm_add_epi16(luma_lo1, _mm_mullo_epi16(b_lo1, luma_const_29));
    luma_lo1 = _mm_srli_epi16(luma_lo1, 8);

    __m128i luma_hi1 = _mm_mullo_epi16(r_hi1, luma_const_77);
    luma_hi1 = _mm_add_epi16(luma_hi1, _mm_mullo_epi16(g_hi1, luma_const_150));
    luma_hi1 = _mm_add_epi16(luma_hi1, _mm_mullo_epi16(b_hi1, luma_const_29));
    luma_hi1 = _mm_srli_epi16(luma_hi1, 8);

    // Process second chunk (4 x 8-pixel groups)
    __m128i luma_lo2 = _mm_mullo_epi16(r_lo2, luma_const_77);
    luma_lo2 = _mm_add_epi16(luma_lo2, _mm_mullo_epi16(g_lo2, luma_const_150));
    luma_lo2 = _mm_add_epi16(luma_lo2, _mm_mullo_epi16(b_lo2, luma_const_29));
    luma_lo2 = _mm_srli_epi16(luma_lo2, 8);

    __m128i luma_hi2 = _mm_mullo_epi16(r_hi2, luma_const_77);
    luma_hi2 = _mm_add_epi16(luma_hi2, _mm_mullo_epi16(g_hi2, luma_const_150));
    luma_hi2 = _mm_add_epi16(luma_hi2, _mm_mullo_epi16(b_hi2, luma_const_29));
    luma_hi2 = _mm_srli_epi16(luma_hi2, 8);

    // Pack luminance to 8-bit for both chunks
    __m128i lum_packed1 = _mm_packus_epi16(luma_lo1, luma_hi1);
    __m128i lum_packed2 = _mm_packus_epi16(luma_lo2, luma_hi2);

    // Store luminance for palette lookup (32 pixels total)
    uint8_t lum[32];
    _mm_storeu_si128((__m128i *)&lum[0], lum_packed1);
    _mm_storeu_si128((__m128i *)&lum[16], lum_packed2);

    // Unrolled palette lookups for 32 pixels
    ascii_chars[i + 0] = g_ascii_cache.luminance_palette[lum[0]];
    ascii_chars[i + 1] = g_ascii_cache.luminance_palette[lum[1]];
    ascii_chars[i + 2] = g_ascii_cache.luminance_palette[lum[2]];
    ascii_chars[i + 3] = g_ascii_cache.luminance_palette[lum[3]];
    ascii_chars[i + 4] = g_ascii_cache.luminance_palette[lum[4]];
    ascii_chars[i + 5] = g_ascii_cache.luminance_palette[lum[5]];
    ascii_chars[i + 6] = g_ascii_cache.luminance_palette[lum[6]];
    ascii_chars[i + 7] = g_ascii_cache.luminance_palette[lum[7]];
    ascii_chars[i + 8] = g_ascii_cache.luminance_palette[lum[8]];
    ascii_chars[i + 9] = g_ascii_cache.luminance_palette[lum[9]];
    ascii_chars[i + 10] = g_ascii_cache.luminance_palette[lum[10]];
    ascii_chars[i + 11] = g_ascii_cache.luminance_palette[lum[11]];
    ascii_chars[i + 12] = g_ascii_cache.luminance_palette[lum[12]];
    ascii_chars[i + 13] = g_ascii_cache.luminance_palette[lum[13]];
    ascii_chars[i + 14] = g_ascii_cache.luminance_palette[lum[14]];
    ascii_chars[i + 15] = g_ascii_cache.luminance_palette[lum[15]];
    ascii_chars[i + 16] = g_ascii_cache.luminance_palette[lum[16]];
    ascii_chars[i + 17] = g_ascii_cache.luminance_palette[lum[17]];
    ascii_chars[i + 18] = g_ascii_cache.luminance_palette[lum[18]];
    ascii_chars[i + 19] = g_ascii_cache.luminance_palette[lum[19]];
    ascii_chars[i + 20] = g_ascii_cache.luminance_palette[lum[20]];
    ascii_chars[i + 21] = g_ascii_cache.luminance_palette[lum[21]];
    ascii_chars[i + 22] = g_ascii_cache.luminance_palette[lum[22]];
    ascii_chars[i + 23] = g_ascii_cache.luminance_palette[lum[23]];
    ascii_chars[i + 24] = g_ascii_cache.luminance_palette[lum[24]];
    ascii_chars[i + 25] = g_ascii_cache.luminance_palette[lum[25]];
    ascii_chars[i + 26] = g_ascii_cache.luminance_palette[lum[26]];
    ascii_chars[i + 27] = g_ascii_cache.luminance_palette[lum[27]];
    ascii_chars[i + 28] = g_ascii_cache.luminance_palette[lum[28]];
    ascii_chars[i + 29] = g_ascii_cache.luminance_palette[lum[29]];
    ascii_chars[i + 30] = g_ascii_cache.luminance_palette[lum[30]];
    ascii_chars[i + 31] = g_ascii_cache.luminance_palette[lum[31]];
  }

  // Process remaining 8-pixel chunks using SSE2 fallback
  for (; i + 7 < count; i += 8) {
    uint8_t r_vals[8], g_vals[8], b_vals[8];
    const uint8_t *src = (const uint8_t *)&pixels[i];

    // Extract RGB for 8 pixels
    for (int j = 0; j < 8; j++) {
      r_vals[j] = src[j * 3 + 0];
      g_vals[j] = src[j * 3 + 1];
      b_vals[j] = src[j * 3 + 2];
    }

    // Load and widen to 16-bit
    __m128i r_16 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)r_vals), _mm_setzero_si128());
    __m128i g_16 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)g_vals), _mm_setzero_si128());
    __m128i b_16 = _mm_unpacklo_epi8(_mm_loadl_epi64((const __m128i *)b_vals), _mm_setzero_si128());

    // SIMD luminance calculation
    __m128i luma = _mm_mullo_epi16(r_16, luma_const_77);
    luma = _mm_add_epi16(luma, _mm_mullo_epi16(g_16, luma_const_150));
    luma = _mm_add_epi16(luma, _mm_mullo_epi16(b_16, luma_const_29));
    luma = _mm_srli_epi16(luma, 8);

    // Pack and store
    __m128i lum_8 = _mm_packus_epi16(luma, _mm_setzero_si128());
    uint8_t lum[8];
    _mm_storel_epi64((__m128i *)lum, lum_8);

    // Unrolled palette lookups
    ascii_chars[i + 0] = g_ascii_cache.luminance_palette[lum[0]];
    ascii_chars[i + 1] = g_ascii_cache.luminance_palette[lum[1]];
    ascii_chars[i + 2] = g_ascii_cache.luminance_palette[lum[2]];
    ascii_chars[i + 3] = g_ascii_cache.luminance_palette[lum[3]];
    ascii_chars[i + 4] = g_ascii_cache.luminance_palette[lum[4]];
    ascii_chars[i + 5] = g_ascii_cache.luminance_palette[lum[5]];
    ascii_chars[i + 6] = g_ascii_cache.luminance_palette[lum[6]];
    ascii_chars[i + 7] = g_ascii_cache.luminance_palette[lum[7]];
  }

  // Process remaining pixels with scalar code
  for (; i < count; i++) {
    const rgb_pixel_t *p = &pixels[i];
    int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;
    if (luminance > 255)
      luminance = 255;
    ascii_chars[i] = g_ascii_cache.luminance_palette[luminance];
  }
}

// SSSE3 version with 32-pixel processing for maximum performance
size_t convert_row_with_color_ssse3(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                    bool background_mode) {

  // Use stack allocation for small widths, heap for large
  // OPTIMIZATION 15: Pre-allocated static buffer eliminates malloc/free in hot path
  // 8K characters handles up to 8K horizontal resolution
  static char large_ascii_buffer[8192] __attribute__((aligned(64)));
  char stack_ascii_chars[2048]; // Stack buffer for typical terminal widths
  char *ascii_chars = (width > 2048) ? large_ascii_buffer : stack_ascii_chars;
  bool heap_allocated = false; // Never needed now!

  // Step 1: SIMD luminance conversion with 32-pixel processing
  convert_pixels_ssse3(pixels, ascii_chars, width);

  // Step 2: Generate colored output
  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;

  for (int x = 0; x < width; x++) {
    const rgb_pixel_t *pixel = &pixels[x];
    char ascii_char = ascii_chars[x];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break; // Safety margin

    if (background_mode) {
      // Background mode: colored background, contrasting foreground
      uint8_t luminance = (77 * pixel->r + 150 * pixel->g + 29 * pixel->b) >> 8;
      uint8_t fg_color = (luminance < 127) ? 15 : 0; // FIX #6: use 15 not 255!

      // Generate foreground color code
      int fg_len = generate_ansi_fg(fg_color, fg_color, fg_color, current_pos);
      current_pos += fg_len;

      // Generate background color code
      int bg_len = generate_ansi_bg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += bg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      int fg_len = generate_ansi_fg(pixel->r, pixel->g, pixel->b, current_pos);
      current_pos += fg_len;

      // Add ASCII character
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  const int reset_len = 4;
  if (buffer_end - current_pos >= reset_len) {
    memcpy(current_pos, "\033[0m", reset_len);
    current_pos += reset_len;
  }

  if (heap_allocated) {
    free(ascii_chars);
  }

  return (size_t)(current_pos - output_buffer);
}

#endif /* SIMD_SUPPORT_SSSE3 */