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

  int simd_count = (count / 4) * 4;
  int i;

  // Process 4 pixels at a time using SSE2-compatible operations
  for (i = 0; i < simd_count; i += 4) {
    // Load 4 RGB pixels and convert to 16-bit for SSE2 compatibility
    // SSE2 doesn't have _mm_mullo_epi32, so we use 16-bit arithmetic

    __m128i r_vals = _mm_setr_epi16(pixels[i].r, pixels[i + 1].r, pixels[i + 2].r, pixels[i + 3].r, 0, 0, 0, 0);
    __m128i g_vals = _mm_setr_epi16(pixels[i].g, pixels[i + 1].g, pixels[i + 2].g, pixels[i + 3].g, 0, 0, 0, 0);
    __m128i b_vals = _mm_setr_epi16(pixels[i].b, pixels[i + 1].b, pixels[i + 2].b, pixels[i + 3].b, 0, 0, 0, 0);

    // Multiply by luminance weights using 16-bit multiplication
    __m128i luma_r = _mm_mullo_epi16(r_vals, _mm_set1_epi16(LUMA_RED));
    __m128i luma_g = _mm_mullo_epi16(g_vals, _mm_set1_epi16(LUMA_GREEN));
    __m128i luma_b = _mm_mullo_epi16(b_vals, _mm_set1_epi16(LUMA_BLUE));

    // Sum and shift right by 8 (divide by 256)
    __m128i luminance = _mm_add_epi16(_mm_add_epi16(luma_r, luma_g), luma_b);
    luminance = _mm_srli_epi16(luminance, 8);

    // Extract results and convert to ASCII
    int lum[4];
    lum[0] = _mm_extract_epi16(luminance, 0);
    lum[1] = _mm_extract_epi16(luminance, 1);
    lum[2] = _mm_extract_epi16(luminance, 2);
    lum[3] = _mm_extract_epi16(luminance, 3);

    for (int j = 0; j < 4; j++) {
      if (lum[j] > 255)
        lum[j] = 255;
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
#endif

/* ============================================================================
 * AVX2 Implementation (8 pixels at once)
 * ============================================================================
 */

#ifdef SIMD_SUPPORT_AVX2
void convert_pixels_avx2(const rgb_pixel_t *pixels, char *ascii_chars, int count) {
  init_palette();

  int simd_count = (count / 8) * 8;
  int i;

  // AVX2 constants
  const __m256i luma_r_vec = _mm256_set1_epi32(LUMA_RED);
  const __m256i luma_g_vec = _mm256_set1_epi32(LUMA_GREEN);
  const __m256i luma_b_vec = _mm256_set1_epi32(LUMA_BLUE);
  const __m256i clamp_255 = _mm256_set1_epi32(255);

  // Process 8 pixels at a time
  for (i = 0; i < simd_count; i += 8) {
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

  // Process 8 pixels at a time for luminance calculation
  int simd_count = (pixel_count / 8) * 8;
  int i;

  for (i = 0; i < simd_count; i += 8) {
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
// Apply the SAME optimizations that made color SIMD successful:
// 1. Process larger batches (16 pixels like color version)  
// 2. Use compiler auto-vectorization effectively
// 3. Optimize for cache locality and branch prediction
void convert_pixels_neon(const rgb_pixel_t *__restrict pixels, char *__restrict ascii_chars, int count) {
  init_palette();

  // Process in batches of 16 like the successful color version
  const int batch_size = 16;
  int full_batches = count / batch_size;
  int remainder = count % batch_size;

  // Process full batches of 16 pixels (let compiler auto-vectorize)
  for (int batch = 0; batch < full_batches; batch++) {
    int base_idx = batch * batch_size;
    
    // Process 16 pixels in tight loop - optimal for compiler vectorization
    for (int j = 0; j < batch_size; j++) {
      const rgb_pixel_t *p = &pixels[base_idx + j];
      
      // Use the EXACT same calculation as color version for consistency
      // This ensures the compiler can apply the same optimizations
      int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;
      
      // Use the same palette lookup as color version
      ascii_chars[base_idx + j] = luminance_palette[luminance];
    }
  }

  // Process remaining pixels (same pattern as color version)
  int base_remainder = full_batches * batch_size;
  for (int i = 0; i < remainder; i++) {
    const rgb_pixel_t *p = &pixels[base_remainder + i];
    int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;
    ascii_chars[base_remainder + i] = luminance_palette[luminance];
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
  printf("  ✓ ARM NEON (4 pixels/cycle)\n");
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
