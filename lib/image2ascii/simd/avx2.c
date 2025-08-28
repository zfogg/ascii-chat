#include "avx2.h"
#include "ascii_simd.h"
#include "options.h"
#include <string.h>

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>

void convert_pixels_avx2(const rgb_pixel_t *pixels, char *ascii_chars, int count) {

  int i = 0;

  // AVX2 constants for 16-bit arithmetic (matches SSSE3 approach)
  const __m256i luma_const_77 = _mm256_set1_epi16(LUMA_RED);
  const __m256i luma_const_150 = _mm256_set1_epi16(LUMA_GREEN);
  const __m256i luma_const_29 = _mm256_set1_epi16(LUMA_BLUE);

  // Process 32-pixel chunks using optimized bulk memory operations for maximum throughput
  for (; i + 31 < count; i += 32) {
    // Load RGB data in bulk using AVX2 memory operations (no scalar extraction!)
    const uint8_t *src1 = (const uint8_t *)&pixels[i];      // First 16 pixels (48 bytes RGB)
    const uint8_t *src2 = (const uint8_t *)&pixels[i + 16]; // Second 16 pixels (48 bytes RGB)

    // Load 48 bytes of RGB data for first 16 pixels using three 16-byte loads
    __m128i rgb_chunk0_1 = _mm_loadu_si128((const __m128i *)(src1 + 0));  // Bytes 0-15
    __m128i rgb_chunk1_1 = _mm_loadu_si128((const __m128i *)(src1 + 16)); // Bytes 16-31
    __m128i rgb_chunk2_1 = _mm_loadu_si128((const __m128i *)(src1 + 32)); // Bytes 32-47

    // Load 48 bytes of RGB data for second 16 pixels
    __m128i rgb_chunk0_2 = _mm_loadu_si128((const __m128i *)(src2 + 0));
    __m128i rgb_chunk1_2 = _mm_loadu_si128((const __m128i *)(src2 + 16));
    __m128i rgb_chunk2_2 = _mm_loadu_si128((const __m128i *)(src2 + 32));

    // Deinterleave RGB using AVX2 shuffle operations (similar to SSSE3 but with 256-bit vectors)

    // Efficient bulk extraction using SSSE3-style masks on 128-bit chunks
    const __m128i mask_r_0 = _mm_setr_epi8(0, 3, 6, 9, 12, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i mask_r_1 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, 2, 5, 8, 11, 14, -1, -1, -1, -1, -1);
    const __m128i mask_r_2 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1, 4, 7, 10, 13);

    __m128i r_vec1 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(rgb_chunk0_1, mask_r_0), _mm_shuffle_epi8(rgb_chunk1_1, mask_r_1)),
                     _mm_shuffle_epi8(rgb_chunk2_1, mask_r_2));

    __m128i r_vec2 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(rgb_chunk0_2, mask_r_0), _mm_shuffle_epi8(rgb_chunk1_2, mask_r_1)),
                     _mm_shuffle_epi8(rgb_chunk2_2, mask_r_2));

    // Extract G values
    const __m128i mask_g_0 = _mm_setr_epi8(1, 4, 7, 10, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i mask_g_1 = _mm_setr_epi8(-1, -1, -1, -1, -1, 0, 3, 6, 9, 12, 15, -1, -1, -1, -1, -1);
    const __m128i mask_g_2 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 5, 8, 11, 14);

    __m128i g_vec1 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(rgb_chunk0_1, mask_g_0), _mm_shuffle_epi8(rgb_chunk1_1, mask_g_1)),
                     _mm_shuffle_epi8(rgb_chunk2_1, mask_g_2));

    __m128i g_vec2 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(rgb_chunk0_2, mask_g_0), _mm_shuffle_epi8(rgb_chunk1_2, mask_g_1)),
                     _mm_shuffle_epi8(rgb_chunk2_2, mask_g_2));

    // Extract B values
    const __m128i mask_b_0 = _mm_setr_epi8(2, 5, 8, 11, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i mask_b_1 = _mm_setr_epi8(-1, -1, -1, -1, -1, 1, 4, 7, 10, 13, -1, -1, -1, -1, -1, -1);
    const __m128i mask_b_2 = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 3, 6, 9, 12, 15);

    __m128i b_vec1 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(rgb_chunk0_1, mask_b_0), _mm_shuffle_epi8(rgb_chunk1_1, mask_b_1)),
                     _mm_shuffle_epi8(rgb_chunk2_1, mask_b_2));

    __m128i b_vec2 =
        _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(rgb_chunk0_2, mask_b_0), _mm_shuffle_epi8(rgb_chunk1_2, mask_b_1)),
                     _mm_shuffle_epi8(rgb_chunk2_2, mask_b_2));

    // Combine into 256-bit AVX2 vectors for parallel processing
    __m256i r_vals1 = _mm256_cvtepu8_epi16(r_vec1); // Widen first 16 R values to 16-bit
    __m256i g_vals1 = _mm256_cvtepu8_epi16(g_vec1); // Widen first 16 G values to 16-bit
    __m256i b_vals1 = _mm256_cvtepu8_epi16(b_vec1); // Widen first 16 B values to 16-bit

    __m256i r_vals2 = _mm256_cvtepu8_epi16(r_vec2); // Widen second 16 R values to 16-bit
    __m256i g_vals2 = _mm256_cvtepu8_epi16(g_vec2); // Widen second 16 G values to 16-bit
    __m256i b_vals2 = _mm256_cvtepu8_epi16(b_vec2); // Widen second 16 B values to 16-bit

    // AVX2 luminance calculation: y = (77*r + 150*g + 29*b) >> 8 for both 16-pixel chunks
    __m256i luma1 = _mm256_mullo_epi16(r_vals1, luma_const_77);
    luma1 = _mm256_add_epi16(luma1, _mm256_mullo_epi16(g_vals1, luma_const_150));
    luma1 = _mm256_add_epi16(luma1, _mm256_mullo_epi16(b_vals1, luma_const_29));
    luma1 = _mm256_srli_epi16(luma1, 8);

    __m256i luma2 = _mm256_mullo_epi16(r_vals2, luma_const_77);
    luma2 = _mm256_add_epi16(luma2, _mm256_mullo_epi16(g_vals2, luma_const_150));
    luma2 = _mm256_add_epi16(luma2, _mm256_mullo_epi16(b_vals2, luma_const_29));
    luma2 = _mm256_srli_epi16(luma2, 8);

    // Convert to 8-bit using proper lane management
    // Split each 16-pixel chunk into two 8-pixel chunks for proper packing
    __m128i luma1_lo = _mm256_castsi256_si128(luma1);      // First 8 pixels of chunk 1
    __m128i luma1_hi = _mm256_extracti128_si256(luma1, 1); // Second 8 pixels of chunk 1
    __m128i luma2_lo = _mm256_castsi256_si128(luma2);      // First 8 pixels of chunk 2
    __m128i luma2_hi = _mm256_extracti128_si256(luma2, 1); // Second 8 pixels of chunk 2

    // Pack to 8-bit using 128-bit operations (more reliable than AVX2 packing)
    __m128i luma_packed_1_2 = _mm_packus_epi16(luma1_lo, luma1_hi); // First 16 pixels
    __m128i luma_packed_3_4 = _mm_packus_epi16(luma2_lo, luma2_hi); // Second 16 pixels

    // Store luminance for palette lookup (32 pixels total, correct order guaranteed)
    uint8_t lum[32];
    _mm_storeu_si128((__m128i *)&lum[0], luma_packed_1_2);
    _mm_storeu_si128((__m128i *)&lum[16], luma_packed_3_4);

    // Unrolled palette lookups for 32 pixels (maximum throughput)
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

  // Process remaining 16-pixel chunks with 128-bit operations
  for (; i + 15 < count; i += 16) {
    const rgb_pixel_t *pixel_batch = &pixels[i];

    // Use SSE2-style 16-pixel processing for the remainder
    __m128i r_vals1 = _mm_setr_epi16(pixel_batch[0].r, pixel_batch[1].r, pixel_batch[2].r, pixel_batch[3].r,
                                     pixel_batch[4].r, pixel_batch[5].r, pixel_batch[6].r, pixel_batch[7].r);
    __m128i g_vals1 = _mm_setr_epi16(pixel_batch[0].g, pixel_batch[1].g, pixel_batch[2].g, pixel_batch[3].g,
                                     pixel_batch[4].g, pixel_batch[5].g, pixel_batch[6].g, pixel_batch[7].g);
    __m128i b_vals1 = _mm_setr_epi16(pixel_batch[0].b, pixel_batch[1].b, pixel_batch[2].b, pixel_batch[3].b,
                                     pixel_batch[4].b, pixel_batch[5].b, pixel_batch[6].b, pixel_batch[7].b);

    __m128i r_vals2 = _mm_setr_epi16(pixel_batch[8].r, pixel_batch[9].r, pixel_batch[10].r, pixel_batch[11].r,
                                     pixel_batch[12].r, pixel_batch[13].r, pixel_batch[14].r, pixel_batch[15].r);
    __m128i g_vals2 = _mm_setr_epi16(pixel_batch[8].g, pixel_batch[9].g, pixel_batch[10].g, pixel_batch[11].g,
                                     pixel_batch[12].g, pixel_batch[13].g, pixel_batch[14].g, pixel_batch[15].g);
    __m128i b_vals2 = _mm_setr_epi16(pixel_batch[8].b, pixel_batch[9].b, pixel_batch[10].b, pixel_batch[11].b,
                                     pixel_batch[12].b, pixel_batch[13].b, pixel_batch[14].b, pixel_batch[15].b);

    // 128-bit luminance calculation
    const __m128i luma_const_77_128 = _mm_set1_epi16(LUMA_RED);
    const __m128i luma_const_150_128 = _mm_set1_epi16(LUMA_GREEN);
    const __m128i luma_const_29_128 = _mm_set1_epi16(LUMA_BLUE);

    __m128i luma1 = _mm_mullo_epi16(r_vals1, luma_const_77_128);
    luma1 = _mm_add_epi16(luma1, _mm_mullo_epi16(g_vals1, luma_const_150_128));
    luma1 = _mm_add_epi16(luma1, _mm_mullo_epi16(b_vals1, luma_const_29_128));
    luma1 = _mm_srli_epi16(luma1, 8);

    __m128i luma2 = _mm_mullo_epi16(r_vals2, luma_const_77_128);
    luma2 = _mm_add_epi16(luma2, _mm_mullo_epi16(g_vals2, luma_const_150_128));
    luma2 = _mm_add_epi16(luma2, _mm_mullo_epi16(b_vals2, luma_const_29_128));
    luma2 = _mm_srli_epi16(luma2, 8);

    __m128i luma_packed = _mm_packus_epi16(luma1, luma2);

    uint8_t lum[16];
    _mm_storeu_si128((__m128i *)lum, luma_packed);

    // Unrolled palette lookups for 16 pixels
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

// Process entire row with SIMD luminance + optimized color generation - OPTIMIZED (no buffer pool)
size_t convert_row_with_color_avx2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {

  // Use stack allocation for small widths, heap for large
  // OPTIMIZATION 15: Pre-allocated static buffer eliminates malloc/free in hot path
  // 8K characters handles up to 8K horizontal resolution
  static char large_ascii_buffer[8192] __attribute__((aligned(64)));
  char stack_ascii_chars[2048]; // Stack buffer for typical terminal widths
  char *ascii_chars = (width > 2048) ? large_ascii_buffer : stack_ascii_chars;
  bool heap_allocated = false; // Never needed now!

  // Step 1: SIMD luminance conversion - NO BUFFER POOL!
  convert_pixels_avx2(pixels, ascii_chars, width);

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

      // Generate combined FG+BG color code
      current_pos = append_sgr_truecolor_fg_bg(current_pos, fg_color, fg_color, fg_color, pixel->r, pixel->g, pixel->b);

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      current_pos = append_sgr_truecolor_fg(current_pos, pixel->r, pixel->g, pixel->b);
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining = buffer_end - current_pos;
  if (remaining >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // OPTIMIZATION 15: No heap cleanup needed - using static/stack buffers only!
  (void)heap_allocated; // Suppress unused variable warning

  return current_pos - output_buffer;
}

// AVX2 version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_avx2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars) {
  // Step 1: Use provided buffer for SIMD luminance conversion
  convert_pixels_avx2(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as regular AVX2 version)
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

      // Generate combined FG+BG color code
      current_pos = append_sgr_truecolor_fg_bg(current_pos, fg_color, fg_color, fg_color, pixel->r, pixel->g, pixel->b);

      // Add ASCII character
      *current_pos++ = ascii_char;

    } else {
      // Foreground mode: colored character
      current_pos = append_sgr_truecolor_fg(current_pos, pixel->r, pixel->g, pixel->b);
      *current_pos++ = ascii_char;
    }
  }

  // Add reset sequence
  size_t remaining2 = buffer_end - current_pos;
  if (remaining2 >= sizeof(ANSI_RESET)) {
    memcpy(current_pos, ANSI_RESET, sizeof(ANSI_RESET) - 1);
    current_pos += sizeof(ANSI_RESET) - 1;
  }

  // No buffer pool free - using pre-allocated buffer
  return current_pos - output_buffer;
}

// Forward declarations for AVX-512 functions
static size_t convert_row_colored_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                         bool background_mode);
static size_t convert_row_mono_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width);

// AVX-512 dispatch function
size_t convert_row_with_color_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                     bool background_mode) {
  if (opt_color_output || background_mode) {
    return convert_row_colored_avx512(pixels, output_buffer, buffer_size, width, background_mode);
  } else {
    return convert_row_mono_avx512(pixels, output_buffer, buffer_size, width);
  }
}

// TODO: Implement AVX-512 64-pixel parallel monochrome ASCII conversion
static size_t convert_row_mono_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width) {
  // FUTURE IMPLEMENTATION:
  // - Process 64 pixels per iteration using __m512i vectors
  // - Use _mm512_load_si512 for aligned loads
  // - Implement luminance calculation with _mm512_maddubs_epi16
  // - Use gather/scatter operations for ASCII lookup
  // - Expected performance: 2-4x faster than AVX2 implementation

  // Fallback to scalar implementation for now
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, false);
}

// TODO: Implement AVX-512 64-pixel parallel colored ASCII conversion
static size_t convert_row_colored_avx512(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                         bool background_mode) {
  // FUTURE IMPLEMENTATION:
  // - Process 64 RGB pixels per iteration
  // - Use __m512i for 64x 8-bit RGB values
  // - Vectorized luminance: (77*R + 150*G + 29*B) >> 8 for 64 pixels
  // - Parallel ASCII character lookup with _mm512_shuffle_epi8
  // - Vectorized ANSI color code generation
  // - Use masked stores to handle variable-width output

  // Fallback to scalar implementation for now
  return convert_row_with_color_scalar(pixels, output_buffer, buffer_size, width, background_mode);
}

#endif /* SIMD_SUPPORT_AVX2 */
