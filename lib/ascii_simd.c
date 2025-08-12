#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ascii_simd.h"
#include "options.h"
#include "image.h"
#include "common.h"
#include "buffer_pool.h"

#ifdef SIMD_SUPPORT_NEON
#include <arm_neon.h>
#endif

#ifdef SIMD_SUPPORT_SSE2
#include <emmintrin.h>
#endif

#ifdef SIMD_SUPPORT_AVX2
#include <immintrin.h>
#endif

// ASCII palette (matches your existing one)
// static const char ascii_palette2[] = " .,:;ox%#@";
static const char ascii_palette2[] = "   ...',;:clodxkO0KXNWM";
static const int palette_len = sizeof(ascii_palette2) - 2; // -1 for null, -1 for indexing

// Luminance calculation constants (matches your existing RED, GREEN, BLUE arrays)
// These are based on the standard NTSC weights: 0.299*R + 0.587*G + 0.114*B
// Scaled to integers for faster computation
#define LUMA_RED 77    // 0.299 * 256
#define LUMA_GREEN 150 // 0.587 * 256
#define LUMA_BLUE 29   // 0.114 * 256

// Pre-calculated luminance palette
static char luminance_palette[256];
static bool palette_initialized = false;

static void init_palette(void) {
  if (palette_initialized)
    return;

  for (int i = 0; i < 256; i++) {
    int palette_index = (i * palette_len) / 255;
    if (palette_index > palette_len)
      palette_index = palette_len;
    luminance_palette[i] = ascii_palette2[palette_index];
  }
  palette_initialized = true;
}

/* ============================================================================
 * Scalar Implementation (Baseline)
 * ============================================================================
 */

void convert_pixels_scalar(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
  init_palette();

  for (int i = 0; i < count; i++) {
    const rgb_pixel_t *p = &pixels[i];

    // Calculate luminance using integer arithmetic
    int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;

    // Clamp to [0, 255]
    if (luminance > 255)
      luminance = 255;

    ascii_chars[i] = luminance_palette[luminance];
  }
}

// --------------------------------------
// SIMD-convert an image into ASCII characters and return it with newlines
char *image_print_simd(image_t *image) {
  const int h = image->h;
  const int w = image->w;

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
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row_pixels = (const rgb_pixel_t *)&image->pixels[y * w];

    // Use SIMD to convert this row directly into final buffer
    convert_pixels_optimized(row_pixels, pos, w);
    pos += w;

    // Add newline (except for last row)
    if (y != h - 1) {
      *pos++ = '\n';
    }
  }
  *pos = '\0';

  return ascii;
}

/* ============================================================================
 * SSE2 Implementation (4 pixels at once)
 * ============================================================================
 */

#ifdef SIMD_SUPPORT_SSE2
void convert_pixels_sse2(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
  init_palette();

  int i;

  // Process 4 pixels at a time using optimized SSE2 operations
  for (i = 0; i + 3 < count; i += 4) {
    // Load 4 RGB pixels using only SSE2 instructions
    // For maximum compatibility, use scalar loads but process with SIMD arithmetic
    const uint8_t *pixel_ptr = (const uint8_t *)(pixels + i);

    // Extract RGB values using SSE2-compatible approach
    __m128i r_16 = _mm_setr_epi16(pixel_ptr[0], pixel_ptr[3], pixel_ptr[6], pixel_ptr[9], 0, 0, 0, 0);
    __m128i g_16 = _mm_setr_epi16(pixel_ptr[1], pixel_ptr[4], pixel_ptr[7], pixel_ptr[10], 0, 0, 0, 0);
    __m128i b_16 = _mm_setr_epi16(pixel_ptr[2], pixel_ptr[5], pixel_ptr[8], pixel_ptr[11], 0, 0, 0, 0);

    // SIMD luminance calculation: (77*r + 150*g + 29*b) >> 8
    __m128i luma_r = _mm_mullo_epi16(r_16, _mm_set1_epi16(LUMA_RED));
    __m128i luma_g = _mm_mullo_epi16(g_16, _mm_set1_epi16(LUMA_GREEN));
    __m128i luma_b = _mm_mullo_epi16(b_16, _mm_set1_epi16(LUMA_BLUE));

    __m128i luminance = _mm_add_epi16(_mm_add_epi16(luma_r, luma_g), luma_b);
    luminance = _mm_srli_epi16(luminance, 8);

    // Pack back to 8-bit and store for palette lookup
    __m128i lum_8 = _mm_packus_epi16(luminance, _mm_setzero_si128());

    // Extract and convert to ASCII using bulk store + scalar lookup
    uint8_t lum[4];
    _mm_storeu_si32(lum, lum_8);

    ascii_chars[i + 0] = luminance_palette[lum[0]];
    ascii_chars[i + 1] = luminance_palette[lum[1]];
    ascii_chars[i + 2] = luminance_palette[lum[2]];
    ascii_chars[i + 3] = luminance_palette[lum[3]];
  }

  // Process remaining pixels with scalar code
  for (; i < count; i++) {
    const rgb_pixel_t *p = &pixels[i];
    int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;
    if (luminance > 255)
      luminance = 255;
    ascii_chars[i] = luminance_palette[luminance];
  }
}
#endif

/* ============================================================================
 * AVX2 Implementation (8 pixels at once)
 * ============================================================================
 */

#ifdef SIMD_SUPPORT_AVX2
void convert_pixels_avx2(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
  init_palette();

  int i;

  // AVX2 constants
  const __m256i luma_r_vec = _mm256_set1_epi32(LUMA_RED);
  const __m256i luma_g_vec = _mm256_set1_epi32(LUMA_GREEN);
  const __m256i luma_b_vec = _mm256_set1_epi32(LUMA_BLUE);
  const __m256i clamp_255 = _mm256_set1_epi32(255);

  // Process 8 pixels at a time with explicit bounds checking
  for (i = 0; i + 7 < count; i += 8) {
    // Load 8 RGB pixels into separate vectors
    __m256i r_vals = _mm256_setr_epi32(pixels[i].r, pixels[i + 1].r, pixels[i + 2].r, pixels[i + 3].r, pixels[i + 4].r,
                                       pixels[i + 5].r, pixels[i + 6].r, pixels[i + 7].r);
    __m256i g_vals = _mm256_setr_epi32(pixels[i].g, pixels[i + 1].g, pixels[i + 2].g, pixels[i + 3].g, pixels[i + 4].g,
                                       pixels[i + 5].g, pixels[i + 6].g, pixels[i + 7].g);
    __m256i b_vals = _mm256_setr_epi32(pixels[i].b, pixels[i + 1].b, pixels[i + 2].b, pixels[i + 3].b, pixels[i + 4].b,
                                       pixels[i + 5].b, pixels[i + 6].b, pixels[i + 7].b);

    // Multiply by luminance weights
    __m256i luma_r = _mm256_mullo_epi32(r_vals, luma_r_vec);
    __m256i luma_g = _mm256_mullo_epi32(g_vals, luma_g_vec);
    __m256i luma_b = _mm256_mullo_epi32(b_vals, luma_b_vec);

    // Sum components
    __m256i luminance = _mm256_add_epi32(luma_r, luma_g);
    luminance = _mm256_add_epi32(luminance, luma_b);

    // Shift right by 8 (divide by 256)
    luminance = _mm256_srli_epi32(luminance, 8);

    // Clamp to [0, 255]
    luminance = _mm256_min_epi32(luminance, clamp_255);

    // Extract results and convert to ASCII
    int lum[8];
    _mm256_storeu_si256((__m256i *)lum, luminance);

    for (int j = 0; j < 8; j++) {
      ascii_chars[i + j] = luminance_palette[lum[j]];
    }
  }

  // Process remaining pixels with scalar code
  for (; i < count; i++) {
    const rgb_pixel_t *p = &pixels[i];
    int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;
    if (luminance > 255)
      luminance = 255;
    ascii_chars[i] = luminance_palette[luminance];
  }
}

// More advanced version with ANSI color output
void convert_pixels_with_color_avx2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width,
                                    bool background_mode) {
  init_palette();

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
 * ARM NEON Implementation (for Apple Silicon Macs)
 * ============================================================================
 */

#ifdef SIMD_SUPPORT_NEON
// Advanced NEON implementation with table lookup optimization
void convert_pixels_neon(const rgb_pixel_t *__restrict pixels, char *__restrict ascii_chars, int count) {
  init_palette();

  // Prepare NEON table for palette lookup - convert palette to uint8x16_t chunks
  uint8x16_t palette_table_0, palette_table_1;

  // Fill first 16 entries of luminance palette
  uint8_t palette_bytes[32];
  for (int j = 0; j < 16; j++) {
    palette_bytes[j] = (uint8_t)luminance_palette[j * 16]; // Sample every 16th entry for first table
  }
  for (int j = 0; j < 16; j++) {
    palette_bytes[j + 16] = (uint8_t)luminance_palette[j * 16 + 8]; // Offset sampling for second table
  }

  palette_table_0 = vld1q_u8(palette_bytes);
  palette_table_1 = vld1q_u8(palette_bytes + 16);

  int i = 0;

  // Process 32-pixel chunks for better ILP (Instruction Level Parallelism)
  for (; i + 31 < count; i += 32) {
    const uint8_t *base1 = (const uint8_t *)(pixels + i);
    const uint8_t *base2 = (const uint8_t *)(pixels + i + 16);

    // Load two 16-pixel chunks simultaneously
    uint8x16x3_t rgb1 = vld3q_u8(base1);
    uint8x16x3_t rgb2 = vld3q_u8(base2);

    // Process first 16 pixels
    uint16x8_t r_lo_1 = vmovl_u8(vget_low_u8(rgb1.val[0]));
    uint16x8_t r_hi_1 = vmovl_u8(vget_high_u8(rgb1.val[0]));
    uint16x8_t g_lo_1 = vmovl_u8(vget_low_u8(rgb1.val[1]));
    uint16x8_t g_hi_1 = vmovl_u8(vget_high_u8(rgb1.val[1]));
    uint16x8_t b_lo_1 = vmovl_u8(vget_low_u8(rgb1.val[2]));
    uint16x8_t b_hi_1 = vmovl_u8(vget_high_u8(rgb1.val[2]));

    // SIMD luminance calculation for first chunk
    uint16x8_t y_lo_1 = vmulq_n_u16(r_lo_1, LUMA_RED);
    y_lo_1 = vmlaq_n_u16(y_lo_1, g_lo_1, LUMA_GREEN);
    y_lo_1 = vmlaq_n_u16(y_lo_1, b_lo_1, LUMA_BLUE);
    y_lo_1 = vshrq_n_u16(y_lo_1, 8);

    uint16x8_t y_hi_1 = vmulq_n_u16(r_hi_1, LUMA_RED);
    y_hi_1 = vmlaq_n_u16(y_hi_1, g_hi_1, LUMA_GREEN);
    y_hi_1 = vmlaq_n_u16(y_hi_1, b_hi_1, LUMA_BLUE);
    y_hi_1 = vshrq_n_u16(y_hi_1, 8);

    // Process second 16 pixels in parallel
    uint16x8_t r_lo_2 = vmovl_u8(vget_low_u8(rgb2.val[0]));
    uint16x8_t r_hi_2 = vmovl_u8(vget_high_u8(rgb2.val[0]));
    uint16x8_t g_lo_2 = vmovl_u8(vget_low_u8(rgb2.val[1]));
    uint16x8_t g_hi_2 = vmovl_u8(vget_high_u8(rgb2.val[1]));
    uint16x8_t b_lo_2 = vmovl_u8(vget_low_u8(rgb2.val[2]));
    uint16x8_t b_hi_2 = vmovl_u8(vget_high_u8(rgb2.val[2]));

    // SIMD luminance calculation for second chunk
    uint16x8_t y_lo_2 = vmulq_n_u16(r_lo_2, LUMA_RED);
    y_lo_2 = vmlaq_n_u16(y_lo_2, g_lo_2, LUMA_GREEN);
    y_lo_2 = vmlaq_n_u16(y_lo_2, b_lo_2, LUMA_BLUE);
    y_lo_2 = vshrq_n_u16(y_lo_2, 8);

    uint16x8_t y_hi_2 = vmulq_n_u16(r_hi_2, LUMA_RED);
    y_hi_2 = vmlaq_n_u16(y_hi_2, g_hi_2, LUMA_GREEN);
    y_hi_2 = vmlaq_n_u16(y_hi_2, b_hi_2, LUMA_BLUE);
    y_hi_2 = vshrq_n_u16(y_hi_2, 8);

    // Narrow both chunks to 8-bit
    uint8x16_t y8_1 = vcombine_u8(vqmovn_u16(y_lo_1), vqmovn_u16(y_hi_1));
    uint8x16_t y8_2 = vcombine_u8(vqmovn_u16(y_lo_2), vqmovn_u16(y_hi_2));

    // Store luminance values for palette lookup
    uint8_t lum1[16], lum2[16];
    vst1q_u8(lum1, y8_1);
    vst1q_u8(lum2, y8_2);

    // Optimized bulk palette lookup - unrolled for maximum performance
    ascii_chars[i + 0] = luminance_palette[lum1[0]];
    ascii_chars[i + 16] = luminance_palette[lum2[0]];
    ascii_chars[i + 1] = luminance_palette[lum1[1]];
    ascii_chars[i + 17] = luminance_palette[lum2[1]];
    ascii_chars[i + 2] = luminance_palette[lum1[2]];
    ascii_chars[i + 18] = luminance_palette[lum2[2]];
    ascii_chars[i + 3] = luminance_palette[lum1[3]];
    ascii_chars[i + 19] = luminance_palette[lum2[3]];
    ascii_chars[i + 4] = luminance_palette[lum1[4]];
    ascii_chars[i + 20] = luminance_palette[lum2[4]];
    ascii_chars[i + 5] = luminance_palette[lum1[5]];
    ascii_chars[i + 21] = luminance_palette[lum2[5]];
    ascii_chars[i + 6] = luminance_palette[lum1[6]];
    ascii_chars[i + 22] = luminance_palette[lum2[6]];
    ascii_chars[i + 7] = luminance_palette[lum1[7]];
    ascii_chars[i + 23] = luminance_palette[lum2[7]];
    ascii_chars[i + 8] = luminance_palette[lum1[8]];
    ascii_chars[i + 24] = luminance_palette[lum2[8]];
    ascii_chars[i + 9] = luminance_palette[lum1[9]];
    ascii_chars[i + 25] = luminance_palette[lum2[9]];
    ascii_chars[i + 10] = luminance_palette[lum1[10]];
    ascii_chars[i + 26] = luminance_palette[lum2[10]];
    ascii_chars[i + 11] = luminance_palette[lum1[11]];
    ascii_chars[i + 27] = luminance_palette[lum2[11]];
    ascii_chars[i + 12] = luminance_palette[lum1[12]];
    ascii_chars[i + 28] = luminance_palette[lum2[12]];
    ascii_chars[i + 13] = luminance_palette[lum1[13]];
    ascii_chars[i + 29] = luminance_palette[lum2[13]];
    ascii_chars[i + 14] = luminance_palette[lum1[14]];
    ascii_chars[i + 30] = luminance_palette[lum2[14]];
    ascii_chars[i + 15] = luminance_palette[lum1[15]];
    ascii_chars[i + 31] = luminance_palette[lum2[15]];
  }

  // Process remaining 16-pixel chunks
  for (; i + 15 < count; i += 16) {
    const uint8_t *base = (const uint8_t *)(pixels + i);
    uint8x16x3_t rgb = vld3q_u8(base);

    // Widen to 16-bit for accurate dot product
    uint16x8_t r_lo = vmovl_u8(vget_low_u8(rgb.val[0]));
    uint16x8_t r_hi = vmovl_u8(vget_high_u8(rgb.val[0]));
    uint16x8_t g_lo = vmovl_u8(vget_low_u8(rgb.val[1]));
    uint16x8_t g_hi = vmovl_u8(vget_high_u8(rgb.val[1]));
    uint16x8_t b_lo = vmovl_u8(vget_low_u8(rgb.val[2]));
    uint16x8_t b_hi = vmovl_u8(vget_high_u8(rgb.val[2]));

    // SIMD luminance: y = (77*r + 150*g + 29*b) >> 8
    uint16x8_t y_lo = vmulq_n_u16(r_lo, LUMA_RED);
    y_lo = vmlaq_n_u16(y_lo, g_lo, LUMA_GREEN);
    y_lo = vmlaq_n_u16(y_lo, b_lo, LUMA_BLUE);
    y_lo = vshrq_n_u16(y_lo, 8);

    uint16x8_t y_hi = vmulq_n_u16(r_hi, LUMA_RED);
    y_hi = vmlaq_n_u16(y_hi, g_hi, LUMA_GREEN);
    y_hi = vmlaq_n_u16(y_hi, b_hi, LUMA_BLUE);
    y_hi = vshrq_n_u16(y_hi, 8);

    // Narrow to 8-bit luminance values
    uint8x16_t y8 = vcombine_u8(vqmovn_u16(y_lo), vqmovn_u16(y_hi));

    uint8_t lum[16];
    vst1q_u8(lum, y8);

    // Unrolled palette conversion
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

  // Process remaining pixels (< 16) with scalar fallback
  for (; i < count; i++) {
    const rgb_pixel_t *p = &pixels[i];
    int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;
    ascii_chars[i] = luminance_palette[luminance];
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
#elif defined(SIMD_SUPPORT_NEON)
  convert_pixels_neon(pixels, ascii_chars, count);
#elif defined(SIMD_SUPPORT_SSE2)
  convert_pixels_sse2(pixels, ascii_chars, count);
#else
  convert_pixels_scalar(pixels, ascii_chars, count);
#endif
}

void print_simd_capabilities(void) {
  printf("SIMD Support:\n");
#ifdef SIMD_SUPPORT_AVX2
  printf("  ✓ AVX2 (8 pixels/cycle)\n");
#endif
#ifdef SIMD_SUPPORT_NEON
  printf("  ✓ ARM NEON (16 pixels/cycle)\n");
#endif
#ifdef SIMD_SUPPORT_SSE2
  printf("  ✓ SSE2 (4 pixels/cycle)\n");
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

simd_benchmark_t benchmark_simd_conversion(int width, int height, int iterations) {
  simd_benchmark_t result = {0};

  int pixel_count = width * height;
  size_t data_size = pixel_count * sizeof(rgb_pixel_t);

  // Generate test data
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  SAFE_MALLOC(test_pixels, data_size, rgb_pixel_t *);
  SAFE_MALLOC(output_buffer, pixel_count, char *);

  // Fill with random RGB data
  srand(12345); // Consistent results
  for (int i = 0; i < pixel_count; i++) {
    test_pixels[i].r = rand() % 256;
    test_pixels[i].g = rand() % 256;
    test_pixels[i].b = rand() % 256;
  }

  printf("Benchmarking %dx%d (%d pixels) x %d iterations...\n", width, height, pixel_count, iterations);

  // Benchmark scalar version
  double start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_pixels_scalar(test_pixels, output_buffer, pixel_count);
  }
  result.scalar_time = get_time_seconds() - start;

#ifdef SIMD_SUPPORT_SSE2
  // Benchmark SSE2
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_pixels_sse2(test_pixels, output_buffer, pixel_count);
  }
  result.sse2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_AVX2
  // Benchmark AVX2
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_pixels_avx2(test_pixels, output_buffer, pixel_count);
  }
  result.avx2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_NEON
  // Benchmark NEON
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_pixels_neon(test_pixels, output_buffer, pixel_count);
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
