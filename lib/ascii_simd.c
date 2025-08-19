#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ascii_simd.h"
#include "ascii_simd_neon.h"
#include "image.h"
#include "common.h"
#include "webcam.h"

#ifdef SIMD_SUPPORT_NEON
#include <arm_neon.h>
#endif

#ifdef SIMD_SUPPORT_SSE2
#include <emmintrin.h>
#endif

#ifdef SIMD_SUPPORT_SSSE3
#include <tmmintrin.h>
#endif

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>
#endif

// Global cache definition - shared across all compilation units
struct ascii_color_cache g_ascii_cache = {.ascii_chars = "   ...',;:clodxkO0KXNWM",
                                          .palette_len = 21, // sizeof("   ...',;:clodxkO0KXNWM") - 1
                                          .palette_initialized = false,
                                          .dec3_initialized = false};

// Luminance calculation constants (matches your existing RED, GREEN, BLUE arrays)
// These are based on the standard NTSC weights: 0.299*R + 0.587*G + 0.114*B
// Scaled to integers for faster computation
#define LUMA_RED 77    // 0.299 * 256
#define LUMA_GREEN 150 // 0.587 * 256
#define LUMA_BLUE 29   // 0.114 * 256

void init_palette(void) {

  for (int i = 0; i < 256; i++) {
    int palette_index = (i * g_ascii_cache.palette_len) / 255;
    if (palette_index >= g_ascii_cache.palette_len)
      palette_index = g_ascii_cache.palette_len - 1;
    g_ascii_cache.luminance_palette[i] = g_ascii_cache.ascii_chars[palette_index];
  }
  g_ascii_cache.palette_initialized = true;

}

void init_dec3(void) {
  if (g_ascii_cache.dec3_initialized)
    return;
  for (int v = 0; v < 256; ++v) {
    int d2 = v / 100;     // 0..2
    int r = v - d2 * 100; // 0..99
    int d1 = r / 10;      // 0..9
    int d0 = r - d1 * 10; // 0..9

    if (d2) {
      g_ascii_cache.dec3_table[v].len = 3;
      g_ascii_cache.dec3_table[v].s[0] = '0' + d2;
      g_ascii_cache.dec3_table[v].s[1] = '0' + d1;
      g_ascii_cache.dec3_table[v].s[2] = '0' + d0;
    } else if (d1) {
      g_ascii_cache.dec3_table[v].len = 2;
      g_ascii_cache.dec3_table[v].s[0] = '0' + d1;
      g_ascii_cache.dec3_table[v].s[1] = '0' + d0;
    } else {
      g_ascii_cache.dec3_table[v].len = 1;
      g_ascii_cache.dec3_table[v].s[0] = '0' + d0;
    }
  }
  g_ascii_cache.dec3_initialized = true;
}

// **HIGH-IMPACT FIX 2**: Remove init guards from hot path - use constructor
__attribute__((constructor)) static void ascii_ctor(void) {
  init_palette();
  init_dec3();
}

// NEON vtbl lookup table for ASCII mapping optimization (Priority 3)
// Contains ASCII palette padded to 32 bytes for efficient vtbl2q_u8() access
static const uint8_t ascii_vtbl_table[32]
    __attribute__((aligned(16))) = {' ', ' ', ' ', '.', '.', '.', '\'', ',', ';', ':', 'c', 'l', 'o', 'd', 'x', 'k',
                                    'O', '0', 'K', 'X', 'N', 'W', 'M',  'M', 'M', 'M', 'M', 'M', 'M', 'M', 'M', 'M'};

/* ============================================================================
 * Scalar Implementation (Baseline)
 * ============================================================================
 */

void convert_pixels_scalar(const rgb_pixel_t *pixels, char *ascii_chars, int count) {

  for (int i = 0; i < count; i++) {
    const rgb_pixel_t *p = &pixels[i];

    // Calculate luminance using integer arithmetic
    int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;

    // Clamp to [0, 255]
    if (luminance > 255)
      luminance = 255;

    ascii_chars[i] = g_ascii_cache.luminance_palette[luminance];
  }
}

// --------------------------------------
// SIMD-convert an image into ASCII characters and return it with newlines
char *image_print_simd(image_t *image) {
  const int h = image->h;
  const int w = image->w;

  log_debug("SIMD: Processing image %dx%d", w, h);

  // Calculate exact buffer size (matching non-SIMD version)
  const ssize_t len = (ssize_t)h * ((ssize_t)w + 1);

  // Single allocation - no buffer pool overhead
  char *ascii;
  SAFE_MALLOC(ascii, len * sizeof(char), char *);
  if (!ascii) {
    log_error("Failed to allocate ASCII buffer");
    return NULL;
  }

  // Process directly into final buffer - no copying!
  char *pos = ascii;

  log_debug("SIMD: Palette initialized, length=%d", g_ascii_cache.palette_len);

  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row_pixels = (const rgb_pixel_t *)&image->pixels[y * w];

    // Use SIMD to convert this row directly into final buffer
    char *row_start = pos;
    convert_pixels_optimized(row_pixels, pos, w);
    pos += w;

    // Add newline (except for last row)
    if (y != h - 1) {
      *pos++ = '\n';
    }

    // Debug: Log progress every 10 rows or first/last row
    if (y == 0 || y == h-1 || y % 10 == 0) {
      size_t row_chars = pos - row_start;
      size_t total_chars = pos - ascii;
      log_debug("SIMD: Row %d/%d processed, row_chars=%zu, total_chars=%zu",
                y+1, h, row_chars, total_chars);
    }
  }
  *pos = '\0';

  size_t final_len = strlen(ascii);
  log_debug("SIMD: Final string length=%zu, expected=%zd", final_len, len);

  return ascii;
}

// Scalar version for remaining pixels.
static inline uint8_t rgb_to_ansi256_scalar_u8(uint8_t r, uint8_t g, uint8_t b) {
    int cr = (r * 5 + 127) / 255;
    int cg = (g * 5 + 127) / 255;
    int cb = (b * 5 + 127) / 255;

    int gray = (r + g + b) / 3;
    int gray_idx = 232 + (gray * 23) / 255;
    int gray_level = 8 + (gray_idx - 232) * 10;
    int gray_dist = abs(gray - gray_level);

    int cube_r = (cr * 255) / 5;
    int cube_g = (cg * 255) / 5;
    int cube_b = (cb * 255) / 5;
    int cube_dist = abs(r - cube_r) + abs(g - cube_g) + abs(b - cube_b);

    return (gray_dist < cube_dist) ? (uint8_t)gray_idx
                                   : (uint8_t)(16 + cr * 36 + cg * 6 + cb);
}

/* ============================================================================
 * SSE2 Implementation (16 pixels at once with ILP)
 * ============================================================================
 */

#ifdef SIMD_SUPPORT_SSE2
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
    ascii_chars[i + 0] = luminance_palette[lum[0]];
    ascii_chars[i + 1] = luminance_palette[lum[1]];
    ascii_chars[i + 2] = luminance_palette[lum[2]];
    ascii_chars[i + 3] = luminance_palette[lum[3]];
    ascii_chars[i + 4] = luminance_palette[lum[4]];
    ascii_chars[i + 5] = luminance_palette[lum[5]];
    ascii_chars[i + 6] = luminance_palette[lum[6]];
    ascii_chars[i + 7] = luminance_palette[lum[7]];
    ascii_chars[i + 8] = luminance_palette[lum[8]];
    ascii_chars[i + 9] = luminance_palette[lum[9]];
    ascii_chars[i + 10] = luminance_palette[lum[10]];
    ascii_chars[i + 11] = luminance_palette[lum[11]];
    ascii_chars[i + 12] = luminance_palette[lum[12]];
    ascii_chars[i + 13] = luminance_palette[lum[13]];
    ascii_chars[i + 14] = luminance_palette[lum[14]];
    ascii_chars[i + 15] = luminance_palette[lum[15]];
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
    ascii_chars[i + 0] = luminance_palette[lum[0]];
    ascii_chars[i + 1] = luminance_palette[lum[1]];
    ascii_chars[i + 2] = luminance_palette[lum[2]];
    ascii_chars[i + 3] = luminance_palette[lum[3]];
    ascii_chars[i + 4] = luminance_palette[lum[4]];
    ascii_chars[i + 5] = luminance_palette[lum[5]];
    ascii_chars[i + 6] = luminance_palette[lum[6]];
    ascii_chars[i + 7] = luminance_palette[lum[7]];
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
#endif

/* ============================================================================
 * SSSE3 Implementation (16 pixels at once with efficient RGB deinterleaving)
 * ============================================================================
 */

#ifdef SIMD_SUPPORT_SSSE3
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
    ascii_chars[i + 0] = luminance_palette[lum[0]];
    ascii_chars[i + 1] = luminance_palette[lum[1]];
    ascii_chars[i + 2] = luminance_palette[lum[2]];
    ascii_chars[i + 3] = luminance_palette[lum[3]];
    ascii_chars[i + 4] = luminance_palette[lum[4]];
    ascii_chars[i + 5] = luminance_palette[lum[5]];
    ascii_chars[i + 6] = luminance_palette[lum[6]];
    ascii_chars[i + 7] = luminance_palette[lum[7]];
    ascii_chars[i + 8] = luminance_palette[lum[8]];
    ascii_chars[i + 9] = luminance_palette[lum[9]];
    ascii_chars[i + 10] = luminance_palette[lum[10]];
    ascii_chars[i + 11] = luminance_palette[lum[11]];
    ascii_chars[i + 12] = luminance_palette[lum[12]];
    ascii_chars[i + 13] = luminance_palette[lum[13]];
    ascii_chars[i + 14] = luminance_palette[lum[14]];
    ascii_chars[i + 15] = luminance_palette[lum[15]];
    ascii_chars[i + 16] = luminance_palette[lum[16]];
    ascii_chars[i + 17] = luminance_palette[lum[17]];
    ascii_chars[i + 18] = luminance_palette[lum[18]];
    ascii_chars[i + 19] = luminance_palette[lum[19]];
    ascii_chars[i + 20] = luminance_palette[lum[20]];
    ascii_chars[i + 21] = luminance_palette[lum[21]];
    ascii_chars[i + 22] = luminance_palette[lum[22]];
    ascii_chars[i + 23] = luminance_palette[lum[23]];
    ascii_chars[i + 24] = luminance_palette[lum[24]];
    ascii_chars[i + 25] = luminance_palette[lum[25]];
    ascii_chars[i + 26] = luminance_palette[lum[26]];
    ascii_chars[i + 27] = luminance_palette[lum[27]];
    ascii_chars[i + 28] = luminance_palette[lum[28]];
    ascii_chars[i + 29] = luminance_palette[lum[29]];
    ascii_chars[i + 30] = luminance_palette[lum[30]];
    ascii_chars[i + 31] = luminance_palette[lum[31]];
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
    ascii_chars[i + 0] = luminance_palette[lum[0]];
    ascii_chars[i + 1] = luminance_palette[lum[1]];
    ascii_chars[i + 2] = luminance_palette[lum[2]];
    ascii_chars[i + 3] = luminance_palette[lum[3]];
    ascii_chars[i + 4] = luminance_palette[lum[4]];
    ascii_chars[i + 5] = luminance_palette[lum[5]];
    ascii_chars[i + 6] = luminance_palette[lum[6]];
    ascii_chars[i + 7] = luminance_palette[lum[7]];
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
#endif

/* ============================================================================
 * AVX2 Implementation (8 pixels at once)
 * ============================================================================
 */

#ifdef SIMD_SUPPORT_AVX2
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
    ascii_chars[i + 0] = luminance_palette[lum[0]];
    ascii_chars[i + 1] = luminance_palette[lum[1]];
    ascii_chars[i + 2] = luminance_palette[lum[2]];
    ascii_chars[i + 3] = luminance_palette[lum[3]];
    ascii_chars[i + 4] = luminance_palette[lum[4]];
    ascii_chars[i + 5] = luminance_palette[lum[5]];
    ascii_chars[i + 6] = luminance_palette[lum[6]];
    ascii_chars[i + 7] = luminance_palette[lum[7]];
    ascii_chars[i + 8] = luminance_palette[lum[8]];
    ascii_chars[i + 9] = luminance_palette[lum[9]];
    ascii_chars[i + 10] = luminance_palette[lum[10]];
    ascii_chars[i + 11] = luminance_palette[lum[11]];
    ascii_chars[i + 12] = luminance_palette[lum[12]];
    ascii_chars[i + 13] = luminance_palette[lum[13]];
    ascii_chars[i + 14] = luminance_palette[lum[14]];
    ascii_chars[i + 15] = luminance_palette[lum[15]];
    ascii_chars[i + 16] = luminance_palette[lum[16]];
    ascii_chars[i + 17] = luminance_palette[lum[17]];
    ascii_chars[i + 18] = luminance_palette[lum[18]];
    ascii_chars[i + 19] = luminance_palette[lum[19]];
    ascii_chars[i + 20] = luminance_palette[lum[20]];
    ascii_chars[i + 21] = luminance_palette[lum[21]];
    ascii_chars[i + 22] = luminance_palette[lum[22]];
    ascii_chars[i + 23] = luminance_palette[lum[23]];
    ascii_chars[i + 24] = luminance_palette[lum[24]];
    ascii_chars[i + 25] = luminance_palette[lum[25]];
    ascii_chars[i + 26] = luminance_palette[lum[26]];
    ascii_chars[i + 27] = luminance_palette[lum[27]];
    ascii_chars[i + 28] = luminance_palette[lum[28]];
    ascii_chars[i + 29] = luminance_palette[lum[29]];
    ascii_chars[i + 30] = luminance_palette[lum[30]];
    ascii_chars[i + 31] = luminance_palette[lum[31]];
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
    ascii_chars[i + 0] = luminance_palette[lum[0]];
    ascii_chars[i + 1] = luminance_palette[lum[1]];
    ascii_chars[i + 2] = luminance_palette[lum[2]];
    ascii_chars[i + 3] = luminance_palette[lum[3]];
    ascii_chars[i + 4] = luminance_palette[lum[4]];
    ascii_chars[i + 5] = luminance_palette[lum[5]];
    ascii_chars[i + 6] = luminance_palette[lum[6]];
    ascii_chars[i + 7] = luminance_palette[lum[7]];
    ascii_chars[i + 8] = luminance_palette[lum[8]];
    ascii_chars[i + 9] = luminance_palette[lum[9]];
    ascii_chars[i + 10] = luminance_palette[lum[10]];
    ascii_chars[i + 11] = luminance_palette[lum[11]];
    ascii_chars[i + 12] = luminance_palette[lum[12]];
    ascii_chars[i + 13] = luminance_palette[lum[13]];
    ascii_chars[i + 14] = luminance_palette[lum[14]];
    ascii_chars[i + 15] = luminance_palette[lum[15]];
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

// More advanced version with ANSI color output
void convert_pixels_with_color_avx2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                    bool background_mode) {

  char *current_pos = output_buffer;
  char *buffer_end = output_buffer + buffer_size;
  int pixel_count = width; // Assuming single row for this example
  int i;

  // Process 8 pixels at a time for luminance calculation with explicit bounds checking
  for (i = 0; i + 7 < pixel_count; i += 8) {
    // Calculate luminance for 8 pixels (same as above)
    __m256i r_vals = _mm256_setr_epi32(pixels[i].r, pixels[i + 1].r, pixels[i + 2].r, pixels[i + 3].r, pixels[i + 4].r,
                                       pixels[i + 5].r, pixels[i + 6].r, pixels[i + 7].r);
    __m256i g_vals = _mm256_setr_epi32(pixels[i].g, pixels[i + 1].g, pixels[i + 2].g, pixels[i + 3].g, pixels[i + 4].g,
                                       pixels[i + 5].g, pixels[i + 6].g, pixels[i + 7].g);
    __m256i b_vals = _mm256_setr_epi32(pixels[i].b, pixels[i + 1].b, pixels[i + 2].b, pixels[i + 3].b, pixels[i + 4].b,
                                       pixels[i + 5].b, pixels[i + 6].b, pixels[i + 7].b);

    __m256i luma_r = _mm256_mullo_epi32(r_vals, _mm256_set1_epi32(LUMA_RED));
    __m256i luma_g = _mm256_mullo_epi32(g_vals, _mm256_set1_epi32(LUMA_GREEN));
    __m256i luma_b = _mm256_mullo_epi32(b_vals, _mm256_set1_epi32(LUMA_BLUE));

    __m256i luminance = _mm256_add_epi32(luma_r, luma_g);
    luminance = _mm256_add_epi32(luminance, luma_b);
    luminance = _mm256_srli_epi32(luminance, 8);
    luminance = _mm256_min_epi32(luminance, _mm256_set1_epi32(255));

    int lum[8];
    _mm256_storeu_si256((__m256i *)lum, luminance);

    // Generate ANSI color codes for each pixel
    for (int j = 0; j < 8; j++) {
      const rgb_pixel_t *p = &pixels[i + j];
      char ascii_char = luminance_palette[lum[j]];

      size_t remaining = buffer_end - current_pos;
      if (remaining < 64)
        break; // Safety margin for ANSI codes

      if (background_mode) {
        // Background color mode
        int fg_color = (lum[j] < 127) ? 255 : 0; // White on dark, black on bright
        int written = snprintf(current_pos, remaining, "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm%c", fg_color, fg_color,
                               fg_color,         // Foreground
                               p->r, p->g, p->b, // Background
                               ascii_char);
        current_pos += written;
      } else {
        // Foreground color mode
        int written = snprintf(current_pos, remaining, "\033[38;2;%d;%d;%dm%c", p->r, p->g, p->b, ascii_char);
        current_pos += written;
      }
    }
  }

  // Process remaining pixels
  for (; i < pixel_count; i++) {
    const rgb_pixel_t *p = &pixels[i];
    int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;
    if (luminance > 255)
      luminance = 255;
    char ascii_char = luminance_palette[luminance];

    size_t remaining = buffer_end - current_pos;
    if (remaining < 64)
      break;

    if (background_mode) {
      int fg_color = (luminance < 127) ? 255 : 0;
      int written = snprintf(current_pos, remaining, "\033[38;2;%d;%d;%dm\033[48;2;%d;%d;%dm%c", fg_color, fg_color,
                             fg_color, p->r, p->g, p->b, ascii_char);
      current_pos += written;
    } else {
      int written = snprintf(current_pos, remaining, "\033[38;2;%d;%d;%dm%c", p->r, p->g, p->b, ascii_char);
      current_pos += written;
    }
  }

  // Add reset sequence and null terminator
  size_t remaining = buffer_end - current_pos;
  if (remaining > 5) {
    strcpy(current_pos, "\033[0m");
  }
}
#endif

/* ============================================================================
 * Auto-dispatch and Benchmarking
 * ============================================================================
 */

void convert_pixels_optimized(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
#ifdef SIMD_SUPPORT_AVX2
  convert_pixels_avx2(pixels, ascii_chars, count);
#elif defined(SIMD_SUPPORT_SSSE3)
  convert_pixels_ssse3(pixels, ascii_chars, count);
#elif defined(SIMD_SUPPORT_SSE2)
  convert_pixels_sse2(pixels, ascii_chars, count);
#elif defined(SIMD_SUPPORT_NEON)
  convert_pixels_neon(pixels, ascii_chars, count);
#else
  convert_pixels_scalar(pixels, ascii_chars, count);
#endif
}

void print_simd_capabilities(void) {
  printf("SIMD Support:\n");
#ifdef SIMD_SUPPORT_AVX2
  printf("  ✓ AVX2 (32 pixels/cycle)\n");
#endif
#ifdef SIMD_SUPPORT_NEON
  printf("  ✓ ARM NEON (16 pixels/cycle)\n");
#endif
#ifdef SIMD_SUPPORT_SSSE3
  printf("  ✓ SSSE3 (32 pixels/cycle)\n");
#endif
#ifdef SIMD_SUPPORT_SSE2
  printf("  ✓ SSE2 (16 pixels/cycle)\n");
#endif
  printf("  ✓ Scalar fallback\n");
}

static double get_time_seconds(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    // Fallback to clock() if CLOCK_MONOTONIC not available
    return (double)clock() / CLOCKS_PER_SEC;
  }
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

// High-resolution adaptive timing for small workloads
// Returns the number of iterations needed to achieve target_duration_ms minimum
static int calculate_adaptive_iterations(int pixel_count, double __attribute__((unused)) target_duration_ms) {
  // Base iterations: scale with image size for consistent measurement accuracy
  int base_iterations = 100; // Minimum iterations for good statistics

  // For very small images, use more iterations for better timing resolution
  if (pixel_count < 5000) {
    base_iterations = 100; // 80×24 = 1,920 pixels -> 100 iterations (was 1000 - too slow!)
  } else if (pixel_count < 50000) {
    base_iterations = 50; // 160×48 = 7,680 pixels -> 50 iterations
  } else if (pixel_count < 200000) {
    base_iterations = 20; // 320×240 = 76,800 pixels -> 20 iterations
  } else if (pixel_count < 500000) {
    base_iterations = 100; // 640×480 = 307,200 pixels -> 100 iterations
  } else {
    base_iterations = 50; // 1280×720 = 921,600 pixels -> 50 iterations
  }

  // Ensure we have at least the minimum for reliable timing
  const int minimum_iterations = 10;
  return (base_iterations > minimum_iterations) ? base_iterations : minimum_iterations;
}

// Measure execution time with adaptive iteration count for accuracy
// Returns average time per operation in seconds
static double measure_function_time(void (*func)(const rgb_pixel_t *, char *, int), const rgb_pixel_t *pixels,
                                    char *output, int pixel_count) {
  int iterations = calculate_adaptive_iterations(pixel_count, 10.0); // Target 10ms minimum

  // Warmup run to stabilize CPU frequency scaling and caches
  func(pixels, output, pixel_count);

  // Actual measurement with multiple iterations
  double start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    func(pixels, output, pixel_count);
  }
  double total_time = get_time_seconds() - start;

  return total_time / iterations; // Return average time per iteration
}
static double measure_function_time2(size_t (*func)(const rgb_pixel_t *, int, char *, size_t, bool, bool),
                                     const rgb_pixel_t *row, int width, char *dst, size_t cap, bool background_mode,
                                     bool use_fast_path) {
  int iterations = calculate_adaptive_iterations(width, 10.0); // Target 10ms minimum

  // Warmup run to stabilize CPU frequency scaling and caches
  func(row, width, dst, cap, background_mode, use_fast_path);

  // Actual measurement with multiple iterations
  double start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    func(row, width, dst, cap, background_mode, use_fast_path);
  }
  double total_time = get_time_seconds() - start;

  return total_time / iterations; // Return average time per iteration
}

// Color conversion timing function with adaptive iterations
// Returns average time per operation in seconds
static double measure_color_function_time(void (*func)(const rgb_pixel_t *, char *, size_t, int, bool),
                                          const rgb_pixel_t *pixels, char *output, size_t output_size, int pixel_count,
                                          bool background_mode) {
  int iterations = calculate_adaptive_iterations(pixel_count, 10.0); // Target 10ms minimum

  // Warmup run to stabilize CPU frequency scaling and caches
  func(pixels, output, output_size, pixel_count, background_mode);

  // Actual measurement with multiple iterations
  double start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    func(pixels, output, output_size, pixel_count, background_mode);
  }
  double total_time = get_time_seconds() - start;

  return total_time / iterations; // Return average time per iteration
}

simd_benchmark_t benchmark_simd_conversion(int width, int height, int __attribute__((unused)) iterations) {
  simd_benchmark_t result = {0};

  int pixel_count = width * height;

  // Generate test data
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  SAFE_CALLOC_SIMD(test_pixels, pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  SAFE_MALLOC(output_buffer, pixel_count, char *);

  // Use real webcam data for realistic testing (matches color benchmark approach)
  webcam_init(0);
  image_t *webcam_frame = webcam_read();

  if (webcam_frame && webcam_frame->pixels) {
    printf("Using real webcam data (%dx%d) for realistic testing\n", webcam_frame->w, webcam_frame->h);

    // Resize webcam data to test dimensions
    if (webcam_frame->w * webcam_frame->h == pixel_count) {
      // Perfect match - copy directly
      for (int i = 0; i < pixel_count; i++) {
        test_pixels[i].r = webcam_frame->pixels[i].r;
        test_pixels[i].g = webcam_frame->pixels[i].g;
        test_pixels[i].b = webcam_frame->pixels[i].b;
      }
    } else {
      // Resize/sample webcam data to fit test dimensions
      float x_scale = (float)webcam_frame->w / width;
      float y_scale = (float)webcam_frame->h / height;

      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          int src_x = (int)(x * x_scale);
          int src_y = (int)(y * y_scale);
          if (src_x >= webcam_frame->w)
            src_x = webcam_frame->w - 1;
          if (src_y >= webcam_frame->h)
            src_y = webcam_frame->h - 1;

          int src_idx = src_y * webcam_frame->w + src_x;
          int dst_idx = y * width + x;

          test_pixels[dst_idx].r = webcam_frame->pixels[src_idx].r;
          test_pixels[dst_idx].g = webcam_frame->pixels[src_idx].g;
          test_pixels[dst_idx].b = webcam_frame->pixels[src_idx].b;
        }
      }
    }

    image_destroy(webcam_frame);
    webcam_cleanup();
  } else {
    // Fallback to synthetic data if webcam fails
    printf("Webcam not available, using synthetic test data\n");
    srand(12345); // Consistent results
    for (int i = 0; i < pixel_count; i++) {
      test_pixels[i].r = rand() % 256;
      test_pixels[i].g = rand() % 256;
      test_pixels[i].b = rand() % 256;
    }
    if (webcam_frame)
      image_destroy(webcam_frame);
    webcam_cleanup();
  }

  // Calculate adaptive iterations for reliable timing
  int adaptive_iterations = calculate_adaptive_iterations(pixel_count, 10.0);
  printf("Benchmarking %dx%d (%d pixels) using %d adaptive iterations (ignoring passed iterations)...\n", width, height,
         pixel_count, adaptive_iterations);

  // Benchmark scalar version with adaptive timing
  result.scalar_time = measure_function_time(convert_pixels_scalar, test_pixels, output_buffer, pixel_count);

#ifdef SIMD_SUPPORT_SSE2
  // Benchmark SSE2 with adaptive timing
  result.sse2_time = measure_function_time(convert_pixels_sse2, test_pixels, output_buffer, pixel_count);
#endif

#ifdef SIMD_SUPPORT_SSSE3
  // Benchmark SSSE3 with adaptive timing
  result.ssse3_time = measure_function_time(convert_pixels_ssse3, test_pixels, output_buffer, pixel_count);
#endif

#ifdef SIMD_SUPPORT_AVX2
  // Benchmark AVX2 with adaptive timing
  result.avx2_time = measure_function_time(convert_pixels_avx2, test_pixels, output_buffer, pixel_count);
#endif

#ifdef SIMD_SUPPORT_NEON
  // Benchmark NEON with adaptive timing
  result.neon_time = measure_function_time(convert_pixels_neon, test_pixels, output_buffer, pixel_count);
  //double timestart = get_time_seconds();
  //rgb_to_ansi256_neon(test_pixels, ???);
  //double timeend = get_time_seconds();

  // Allocate output indices buffer
  uint8_t *idx;
  SAFE_MALLOC(idx, (size_t)pixel_count, uint8_t *);
  // Choose repeats to avoid timer resolution issues
  clock_t t0 = clock();
    int i = 0;
    for (; i + 15 < pixel_count; i += 16) {
      rgb_to_ansi256_neon(&test_pixels[i], &idx[i]);
    }
    // Tail (<16) scalar fallback
    for (; i < pixel_count; ++i) {
      idx[i] = rgb_to_ansi256_scalar_u8(test_pixels[i].r,
                                        test_pixels[i].g,
                                        test_pixels[i].b);
    }
  clock_t t1 = clock();
  double per_frame_ms = ((double)(t1 - t0) / CLOCKS_PER_SEC) * 1000.0;

  printf("  NEON rgb->ansi256 (indices only): %.5f ms/frame\n", per_frame_ms);
  free(idx);
#endif

  // Find best method
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#ifdef SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time) {
    best_time = result.sse2_time;
    result.best_method = "SSE2";
  }
#endif

#ifdef SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time) {
    best_time = result.ssse3_time;
    result.best_method = "SSSE3";
  }
#endif

#ifdef SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time) {
    best_time = result.avx2_time;
    result.best_method = "AVX2";
  }
#endif

#ifdef SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time) {
    best_time = result.neon_time;
    result.best_method = "NEON";
  }
#endif

  result.speedup_best = result.scalar_time / best_time;

  free(test_pixels);
  free(output_buffer);

  return result;
}

simd_benchmark_t benchmark_simd_color_conversion(int width, int height, int iterations, bool background_mode) {
  simd_benchmark_t result = {0};

  int pixel_count = width * height;

  // Estimate output buffer size for colored ASCII (much larger than monochrome)
  // Each pixel can generate ~25 bytes of ANSI escape codes + 1 char
  size_t output_buffer_size = (size_t)pixel_count * 30 + width * 10; // Extra for newlines/reset codes

  // Generate test data
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  SAFE_CALLOC_SIMD(test_pixels, pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  SAFE_MALLOC(output_buffer, output_buffer_size, char *);

  // Use real webcam data for realistic color coherence testing
  // This gives much more realistic results than random RGB data
  webcam_init(0);
  image_t *webcam_frame = webcam_read();

  if (webcam_frame && webcam_frame->pixels) {
    printf("Using real webcam data (%dx%d) for realistic color testing\n", webcam_frame->w, webcam_frame->h);

    // Resize webcam data to match test dimensions
    for (int i = 0; i < pixel_count; i++) {
      // Sample from webcam with wrapping (simple but effective)
      int src_idx = i % (webcam_frame->w * webcam_frame->h);
      rgb_t *src_pixel = &webcam_frame->pixels[src_idx];
      test_pixels[i].r = src_pixel->r;
      test_pixels[i].g = src_pixel->g;
      test_pixels[i].b = src_pixel->b;
    }
  } else {
    printf("Webcam unavailable, using coherent gradient data (much more realistic than random)\n");
    // Generate coherent gradient data instead of random (much more realistic)
    srand(12345); // For consistent gradient variation
    for (int i = 0; i < pixel_count; i++) {
      int x = i % width;
      int y = i / width;
      // Create smooth gradients with some variation (mimics real images)
      int base_r = (x * 255 / width);
      int base_g = (y * 255 / height);
      int base_b = ((x + y) * 127 / (width + height));

      // Clamp to valid range during assignment
      int temp_r = base_r + (rand() % 16 - 8);
      int temp_g = base_g + (rand() % 16 - 8);
      int temp_b = base_b + (rand() % 16 - 8);

      test_pixels[i].r = (temp_r < 0) ? 0 : (temp_r > 255) ? 255 : temp_r;
      test_pixels[i].g = (temp_g < 0) ? 0 : (temp_g > 255) ? 255 : temp_g;
      test_pixels[i].b = (temp_b < 0) ? 0 : (temp_b > 255) ? 255 : temp_b;
    }
  }

  webcam_cleanup();

  const char *mode_str = background_mode ? "background" : "foreground";
  printf("Benchmarking COLOR %s %dx%d (%d pixels) x %d iterations...\n", mode_str, width, height, pixel_count,
         iterations);

  // Benchmark scalar color version
  double start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_row_with_color_scalar(test_pixels, output_buffer, output_buffer_size, pixel_count, background_mode);
  }
  result.scalar_time = get_time_seconds() - start;

#ifdef SIMD_SUPPORT_SSE2
  // Benchmark SSE2 color
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_row_with_color_sse2(test_pixels, output_buffer, output_buffer_size, pixel_count, background_mode);
  }
  result.sse2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_SSSE3
  // Benchmark SSSE3 color
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_row_with_color_ssse3(test_pixels, output_buffer, output_buffer_size, pixel_count, background_mode);
  }
  result.ssse3_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_AVX2
  // Benchmark AVX2 color
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_row_with_color_avx2(test_pixels, output_buffer, output_buffer_size, pixel_count, background_mode);
  }
  result.avx2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_NEON
  // Benchmark NEON color
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    render_row_ascii_rep_dispatch_neon(test_pixels, pixel_count, output_buffer, output_buffer_size, background_mode,
                                       true);
  }
  {
    // Allocate output indices buffer
    uint8_t *idx;
    SAFE_MALLOC(idx, (size_t)pixel_count, uint8_t *);

    // Choose repeats to avoid timer resolution issues
    int repeats = 1;
    if (pixel_count < 5000) repeats = 2000;
    else if (pixel_count < 20000) repeats = 500;
    else if (pixel_count < 100000) repeats = 100;
    else repeats = 20;

    clock_t t0 = clock();
    for (int r = 0; r < repeats; ++r) {
      int i = 0;
      for (; i + 15 < pixel_count; i += 16) {
        rgb_to_ansi256_neon(&test_pixels[i], &idx[i]);
      }
      // Tail (<16) scalar fallback
      for (; i < pixel_count; ++i) {
        idx[i] = rgb_to_ansi256_scalar_u8(test_pixels[i].r,
            test_pixels[i].g,
            test_pixels[i].b);
      }
    }
    clock_t t1 = clock();
    double per_frame_ms = ((double)(t1 - t0) / CLOCKS_PER_SEC) * 1000.0 / repeats;

    printf("  NEON rgb->ansi256 (indices only): %.5f ms/frame\n", per_frame_ms);

    free(idx);
  }
  result.neon_time = get_time_seconds() - start;
#endif

  // Find best method
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#ifdef SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time) {
    best_time = result.sse2_time;
    result.best_method = "SSE2";
  }
#endif

#ifdef SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time) {
    best_time = result.ssse3_time;
    result.best_method = "SSSE3";
  }
#endif

#ifdef SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time) {
    best_time = result.avx2_time;
    result.best_method = "AVX2";
  }
#endif

#ifdef SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time) {
    best_time = result.neon_time;
    result.best_method = "NEON";
  }
#endif

  result.speedup_best = result.scalar_time / best_time;

  free(test_pixels);
  free(output_buffer);

  return result;
}

// Enhanced benchmark function with image source support
simd_benchmark_t benchmark_simd_conversion_with_source(int width, int height, int __attribute__((unused)) iterations,
                                                       const image_t *source_image) {
  simd_benchmark_t result = {0};

  int pixel_count = width * height;

  // Generate test data
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  const size_t output_buffer_size = pixel_count * 16;
  SAFE_CALLOC_SIMD(test_pixels, pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  SAFE_MALLOC(output_buffer, output_buffer_size, char *);

  if (source_image && source_image->pixels) {
    printf("Using provided image data (%dx%d) for testing\n", source_image->w, source_image->h);

    // Resize source image to test dimensions if needed
    if (source_image->w == width && source_image->h == height) {
      // Direct copy
      for (int i = 0; i < pixel_count; i++) {
        test_pixels[i].r = source_image->pixels[i].r;
        test_pixels[i].g = source_image->pixels[i].g;
        test_pixels[i].b = source_image->pixels[i].b;
      }
    } else {
      // Simple nearest-neighbor resize
      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          int src_x = (x * source_image->w) / width;
          int src_y = (y * source_image->h) / height;
          int src_idx = src_y * source_image->w + src_x;
          int dst_idx = y * width + x;

          if (src_idx < source_image->w * source_image->h) {
            test_pixels[dst_idx].r = source_image->pixels[src_idx].r;
            test_pixels[dst_idx].g = source_image->pixels[src_idx].g;
            test_pixels[dst_idx].b = source_image->pixels[src_idx].b;
          }
        }
      }
      printf("Resized image data from %dx%d to %dx%d\n", source_image->w, source_image->h, width, height);
    }
  } else {
    // Fall back to synthetic gradient data
    printf("No source image provided, using synthetic gradient data\n");
    srand(12345);
    for (int i = 0; i < pixel_count; i++) {
      int x = i % width;
      int y = i / width;
      int base_r = (x * 255 / width);
      int base_g = (y * 255 / height);
      int base_b = ((x + y) * 127 / (width + height));

      int temp_r = base_r + (rand() % 16 - 8);
      int temp_g = base_g + (rand() % 16 - 8);
      int temp_b = base_b + (rand() % 16 - 8);

      test_pixels[i].r = (temp_r < 0) ? 0 : (temp_r > 255) ? 255 : temp_r;
      test_pixels[i].g = (temp_g < 0) ? 0 : (temp_g > 255) ? 255 : temp_g;
      test_pixels[i].b = (temp_b < 0) ? 0 : (temp_b > 255) ? 255 : temp_b;
    }
  }

  // Calculate adaptive iterations for reliable timing
  int adaptive_iterations = calculate_adaptive_iterations(pixel_count, 10.0);
  printf("Benchmarking %dx%d (%d pixels) using %d adaptive iterations (ignoring passed iterations)...\n", width, height,
         pixel_count, adaptive_iterations);

  // Benchmark all available SIMD variants with adaptive timing
  result.scalar_time = measure_function_time(convert_pixels_scalar, test_pixels, output_buffer, pixel_count);

#ifdef SIMD_SUPPORT_SSE2
  result.sse2_time = measure_function_time(convert_pixels_sse2, test_pixels, output_buffer, pixel_count);
#endif

#ifdef SIMD_SUPPORT_SSSE3
  result.ssse3_time = measure_function_time(convert_pixels_ssse3, test_pixels, output_buffer, pixel_count);
#endif

#ifdef SIMD_SUPPORT_AVX2
  result.avx2_time = measure_function_time(convert_pixels_avx2, test_pixels, output_buffer, pixel_count);
#endif

#ifdef SIMD_SUPPORT_NEON
  result.neon_time = measure_function_time(convert_pixels_neon, test_pixels, output_buffer, pixel_count);
#endif

  // Find best method
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#ifdef SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time) {
    best_time = result.sse2_time;
    result.best_method = "SSE2";
  }
#endif

#ifdef SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time) {
    best_time = result.ssse3_time;
    result.best_method = "SSSE3";
  }
#endif

#ifdef SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time) {
    best_time = result.avx2_time;
    result.best_method = "AVX2";
  }
#endif

#ifdef SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time) {
    best_time = result.neon_time;
    result.best_method = "NEON";
  }
#endif

  result.speedup_best = result.scalar_time / best_time;

  free(test_pixels);
  free(output_buffer);

  return result;
}

// Enhanced color benchmark function with image source support
simd_benchmark_t benchmark_simd_color_conversion_with_source(int width, int height,
                                                             int __attribute__((unused)) iterations,
                                                             bool background_mode, const image_t *source_image,
                                                             bool use_fast_path) {
  simd_benchmark_t result = {0};

  int pixel_count = width * height;
  size_t output_buffer_size = (size_t)pixel_count * 30 + width * 10;

  // Allocate buffers for benchmarking
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  SAFE_CALLOC_SIMD(test_pixels, pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  SAFE_MALLOC(output_buffer, output_buffer_size, char *);

  // Calculate adaptive iterations for color benchmarking (ignore passed iterations)
  int adaptive_iterations = calculate_adaptive_iterations(pixel_count, 10.0);

  const char *mode_str = background_mode ? "background" : "foreground";

  // Variables for webcam capture cleanup
  rgb_pixel_t **frame_data = NULL;
  int captured_frames = 0;

  if (source_image) {
    printf("Using provided source image data for COLOR %s %dx%d benchmarking with %d iterations...\n", mode_str, width,
           height, adaptive_iterations);

    // Use provided source image - resize if needed
    if (source_image->w == width && source_image->h == height) {
      // Direct copy
      for (int i = 0; i < pixel_count; i++) {
        test_pixels[i].r = source_image->pixels[i].r;
        test_pixels[i].g = source_image->pixels[i].g;
        test_pixels[i].b = source_image->pixels[i].b;
      }
    } else {
      // Resize source image to target dimensions
      float x_ratio = (float)source_image->w / width;
      float y_ratio = (float)source_image->h / height;

      for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
          int src_x = (int)(x * x_ratio);
          int src_y = (int)(y * y_ratio);

          // Bounds check
          if (src_x >= source_image->w)
            src_x = source_image->w - 1;
          if (src_y >= source_image->h)
            src_y = source_image->h - 1;

          int src_idx = src_y * source_image->w + src_x;
          int dst_idx = y * width + x;

          test_pixels[dst_idx].r = source_image->pixels[src_idx].r;
          test_pixels[dst_idx].g = source_image->pixels[src_idx].g;
          test_pixels[dst_idx].b = source_image->pixels[src_idx].b;
        }
      }
    }
  } else {
    // No source image provided: try to capture real webcam frames for realistic color testing
    webcam_init(0);
    printf("Pre-capturing %d adaptive webcam frames for COLOR %s %dx%d (ignoring passed iterations)...\n",
           adaptive_iterations, mode_str, width, height);

    // Pre-capture adaptive number of webcam frames
    SAFE_CALLOC(frame_data, adaptive_iterations, sizeof(rgb_pixel_t *), rgb_pixel_t **);
    for (int i = 0; i < adaptive_iterations; i++) {
      // Capture fresh webcam frame
      image_t *webcam_frame = webcam_read();
      if (!webcam_frame) {
        printf("Warning: Failed to capture webcam frame %d during color benchmarking\n", i);
        continue;
      }

      // Create temp image with desired dimensions
      image_t *resized_frame = image_new(width, height);
      if (!resized_frame) {
        printf("Warning: Failed to allocate resized_frame for webcam frame %d during color benchmarking\n", i);
        image_destroy(webcam_frame);
        continue;
      }

      // Use image_resize to resize webcam frame to test dimensions
      image_resize(webcam_frame, resized_frame);

      // Allocate and copy resized data (convert rgb_t to rgb_pixel_t)
      SAFE_CALLOC(frame_data[captured_frames], pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
      for (int j = 0; j < pixel_count; j++) {
        frame_data[captured_frames][j].r = resized_frame->pixels[j].r;
        frame_data[captured_frames][j].g = resized_frame->pixels[j].g;
        frame_data[captured_frames][j].b = resized_frame->pixels[j].b;
      }

      image_destroy(resized_frame);
      image_destroy(webcam_frame);
      captured_frames++;
    }

    if (captured_frames == 0) {
      printf("No webcam frames captured for color test, using synthetic data\n");
      // Fall back to synthetic data like the original implementation
      srand(12345);
      for (int i = 0; i < pixel_count; i++) {
        int x = i % width;
        int y = i / width;
        int base_r = (x * 255 / width);
        int base_g = (y * 255 / height);
        int base_b = ((x + y) * 127 / (width + height));

        int temp_r = base_r + (rand() % 16 - 8);
        int temp_g = base_g + (rand() % 16 - 8);
        int temp_b = base_b + (rand() % 16 - 8);

        test_pixels[i].r = (temp_r < 0) ? 0 : (temp_r > 255) ? 255 : temp_r;
        test_pixels[i].g = (temp_g < 0) ? 0 : (temp_g > 255) ? 255 : temp_g;
        test_pixels[i].b = (temp_b < 0) ? 0 : (temp_b > 255) ? 255 : temp_b;
      }
    } else {
      // Use first frame for all iterations
      for (int i = 0; i < pixel_count; i++) {
        test_pixels[i] = frame_data[0][i];
      }
    }

    // Cleanup frame data after copying to test_pixels
    for (int i = 0; i < captured_frames; i++) {
      SAFE_FREE(frame_data[i]);
    }
    SAFE_FREE(frame_data);
    frame_data = NULL;
    captured_frames = 0;
    webcam_cleanup();
  }

  printf("Benchmarking COLOR %s conversion using %d iterations...\n", mode_str, adaptive_iterations);

  // FIX #5: Prewarm 256-color caches to avoid first-frame penalty (~1.5-2MB cache build)
  printf("Prewarming SGR256 caches...\n");
  prewarm_sgr256_fg_cache(); // Warmup 256-entry FG cache
  prewarm_sgr256_cache();    // Warmup 65,536-entry FG+BG cache
  printf("Cache prewarming complete.\n");

  // Benchmark scalar color conversion (pure conversion, no I/O)
  double start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    convert_row_with_color_scalar(test_pixels, output_buffer, output_buffer_size, pixel_count, background_mode);
  }
  result.scalar_time = get_time_seconds() - start;

  // Find best method -- default to scalar and let simd beat it.
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#ifdef SIMD_SUPPORT_SSE2
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    convert_row_with_color_sse2(test_pixels, output_buffer, output_buffer_size, pixel_count, background_mode);
  }
  result.sse2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_SSSE3
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    convert_row_with_color_ssse3(test_pixels, output_buffer, output_buffer_size, pixel_count, background_mode);
  }
  result.ssse3_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_AVX2
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    convert_row_with_color_avx2(test_pixels, output_buffer, output_buffer_size, pixel_count, background_mode);
  }
  result.avx2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_NEON
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    render_row_ascii_rep_dispatch_neon(test_pixels, pixel_count, output_buffer, output_buffer_size, background_mode,
                                       true);
    /*measure_function_time2(render_row_ascii_rep_dispatch_neon, test_pixels, width, output_buffer,
     * output_buffer_size,*/
    /*                       background_mode, true); */
  }
  result.neon_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time) {
    best_time = result.sse2_time;
    result.best_method = "SSE2";
  }
#endif

#ifdef SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time) {
    best_time = result.ssse3_time;
    result.best_method = "SSSE3";
  }
#endif

#ifdef SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time) {
    best_time = result.avx2_time;
    result.best_method = "AVX2";
  }
#endif

#ifdef SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time) {
    best_time = result.neon_time;
    result.best_method = "NEON";
  }
#endif

  // Normalize timing results by iteration count to get per-frame times
  result.scalar_time /= adaptive_iterations;
  if (result.sse2_time > 0)
    result.sse2_time /= adaptive_iterations;
  if (result.ssse3_time > 0)
    result.ssse3_time /= adaptive_iterations;
  if (result.avx2_time > 0)
    result.avx2_time /= adaptive_iterations;
  if (result.neon_time > 0)
    result.neon_time /= adaptive_iterations;
  // Recalculate best time after normalization
  best_time = result.scalar_time;

#ifdef SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time)
    best_time = result.sse2_time;
#endif
#ifdef SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time)
    best_time = result.ssse3_time;
#endif
#ifdef SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time)
    best_time = result.avx2_time;
#endif
#ifdef SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time)
    best_time = result.neon_time;
#endif

  result.speedup_best = result.scalar_time / best_time;
  // Frame data already cleaned up in webcam capture section
  free(test_pixels);
  free(output_buffer);

  return result;
}
