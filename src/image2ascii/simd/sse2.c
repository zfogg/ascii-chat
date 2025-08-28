#include "sse2.h"
#include "ascii_simd.h"
#include "options.h"

#ifdef SIMD_SUPPORT_SSE2
#include <emmintrin.h>

void convert_pixels_sse2(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
  int i = 0;

  // SSE2 constants for luminance calculation
  const __m128i luma_const_77 = _mm_set1_epi16(LUMA_RED);
  const __m128i luma_const_150 = _mm_set1_epi16(LUMA_GREEN);
  const __m128i luma_const_29 = _mm_set1_epi16(LUMA_BLUE);

  // Process 16-pixel chunks with optimized batch processing
  for (; i + 15 < count; i += 16) {
    // Use simple approach: extract RGB manually for 16 pixels but process with SIMD arithmetic
    const rgb_pixel_t *pixel_batch = &pixels[i];

    // Extract R, G, B values for first 8 pixels
    __m128i r_vals_8 = _mm_setr_epi16(pixel_batch[0].r, pixel_batch[1].r, pixel_batch[2].r, pixel_batch[3].r,
                                      pixel_batch[4].r, pixel_batch[5].r, pixel_batch[6].r, pixel_batch[7].r);
    __m128i g_vals_8 = _mm_setr_epi16(pixel_batch[0].g, pixel_batch[1].g, pixel_batch[2].g, pixel_batch[3].g,
                                      pixel_batch[4].g, pixel_batch[5].g, pixel_batch[6].g, pixel_batch[7].g);
    __m128i b_vals_8 = _mm_setr_epi16(pixel_batch[0].b, pixel_batch[1].b, pixel_batch[2].b, pixel_batch[3].b,
                                      pixel_batch[4].b, pixel_batch[5].b, pixel_batch[6].b, pixel_batch[7].b);

    // SIMD luminance calculation for first 8 pixels: y = (77*r + 150*g + 29*b) >> 8
    __m128i luma_8_1 = _mm_mullo_epi16(r_vals_8, luma_const_77);
    luma_8_1 = _mm_add_epi16(luma_8_1, _mm_mullo_epi16(g_vals_8, luma_const_150));
    luma_8_1 = _mm_add_epi16(luma_8_1, _mm_mullo_epi16(b_vals_8, luma_const_29));
    luma_8_1 = _mm_srli_epi16(luma_8_1, 8);

    // Extract R, G, B values for second 8 pixels
    __m128i r_vals_8_2 = _mm_setr_epi16(pixel_batch[8].r, pixel_batch[9].r, pixel_batch[10].r, pixel_batch[11].r,
                                        pixel_batch[12].r, pixel_batch[13].r, pixel_batch[14].r, pixel_batch[15].r);
    __m128i g_vals_8_2 = _mm_setr_epi16(pixel_batch[8].g, pixel_batch[9].g, pixel_batch[10].g, pixel_batch[11].g,
                                        pixel_batch[12].g, pixel_batch[13].g, pixel_batch[14].g, pixel_batch[15].g);
    __m128i b_vals_8_2 = _mm_setr_epi16(pixel_batch[8].b, pixel_batch[9].b, pixel_batch[10].b, pixel_batch[11].b,
                                        pixel_batch[12].b, pixel_batch[13].b, pixel_batch[14].b, pixel_batch[15].b);

    // SIMD luminance calculation for second 8 pixels
    __m128i luma_8_2 = _mm_mullo_epi16(r_vals_8_2, luma_const_77);
    luma_8_2 = _mm_add_epi16(luma_8_2, _mm_mullo_epi16(g_vals_8_2, luma_const_150));
    luma_8_2 = _mm_add_epi16(luma_8_2, _mm_mullo_epi16(b_vals_8_2, luma_const_29));
    luma_8_2 = _mm_srli_epi16(luma_8_2, 8);

    // Pack both results to 8-bit
    __m128i lum_packed = _mm_packus_epi16(luma_8_1, luma_8_2);
    uint8_t lum[16];
    _mm_storeu_si128((__m128i *)lum, lum_packed);

    // Unrolled palette lookups (16 pixels)
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

  // Process remaining 8-pixel chunks
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
    __m128i r_16 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i *)r_vals), _mm_setzero_si128());
    __m128i g_16 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i *)g_vals), _mm_setzero_si128());
    __m128i b_16 = _mm_unpacklo_epi8(_mm_loadl_epi64((__m128i *)b_vals), _mm_setzero_si128());

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

  // Handle remaining pixels with scalar processing
  for (; i < count; i++) {
    int r = pixels[i].r, g = pixels[i].g, b = pixels[i].b;
    int luminance = (LUMA_RED * r + LUMA_GREEN * g + LUMA_BLUE * b) >> 8;
    if (luminance > 255)
      luminance = 255;
    ascii_chars[i] = g_ascii_cache.luminance_palette[luminance];
  }
}

// SSE2 version for older Intel/AMD systems - OPTIMIZED (no buffer pool)
size_t convert_row_with_color_sse2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                   bool background_mode) {

  // Use stack allocation for small widths, heap for large
  // OPTIMIZATION 15: Pre-allocated static buffer eliminates malloc/free in hot path
  // 8K characters handles up to 8K horizontal resolution
  static char large_ascii_buffer[8192] __attribute__((aligned(64)));
  char stack_ascii_chars[2048]; // Stack buffer for typical terminal widths
  char *ascii_chars = (width > 2048) ? large_ascii_buffer : stack_ascii_chars;
  bool heap_allocated = false; // Never needed now!

  // Step 1: SIMD luminance conversion - NO BUFFER POOL!
  convert_pixels_sse2(pixels, ascii_chars, width);

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

// SSE2 version with pre-allocated buffer to reduce buffer pool contention
size_t convert_row_with_color_sse2_with_buffer(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size,
                                               int width, bool background_mode, char *ascii_chars) {
  // Step 1: Use provided buffer for SIMD luminance conversion
  convert_pixels_sse2(pixels, ascii_chars, width);

  // Step 2: Generate colored output (same as regular SSE2 version)
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

#endif /* SIMD_SUPPORT_SSE2 */