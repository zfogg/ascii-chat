#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include "ascii_simd.h"
#include "image.h"
#include "common.h"
#include "image2ascii/simd/neon.h"
#include "webcam.h"
#include "ansi_fast.h"
#include "ascii.h"

// Global cache definition - shared across all compilation units
struct ascii_color_cache g_ascii_cache = {.ascii_chars = "   ...',;:clodxkO0KXNWM",
                                          .palette_len = 23, // strlen("   ...',;:clodxkO0KXNWM") = 23
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
  ansi_fast_init();
}

void ascii_simd_init(void) {
  ascii_ctor();
}

// Allocate a new image (RGB8), abort on OOM
ImageRGB alloc_image(int w, int h) {
  ImageRGB out;
  out.w = w;
  out.h = h;
  size_t n = (size_t)w * (size_t)h * 3u;
  out.pixels = (uint8_t *)malloc(n);
  if (!out.pixels) {
    log_error("OOM");
    exit(1);
  }
  return out;
}

// String utility functions
void str_init(Str *s) {
  s->data = NULL;
  s->len = 0;
  s->cap = 0;
}

void str_free(Str *s) {
  free(s->data);
  s->data = NULL;
  s->len = s->cap = 0;
}

void str_reserve(Str *s, size_t need) {
  if (need <= s->cap)
    return;
  size_t ncap = s->cap ? s->cap : 4096;
  while (ncap < need)
    ncap = (ncap * 3) / 2 + 64;
  char *nd = (char *)realloc(s->data, ncap);
  if (!nd) {
    log_error("OOM");
    exit(1);
  }
  s->data = nd;
  s->cap = ncap;
}

void str_append_bytes(Str *s, const void *src, size_t n) {
  str_reserve(s, s->len + n);
  memcpy(s->data + s->len, src, n);
  s->len += n;
}

void str_append_c(Str *s, char c) {
  str_reserve(s, s->len + 1);
  s->data[s->len++] = c;
}

void str_printf(Str *s, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char stackbuf[256];
  int n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
  va_end(ap);
  if (n < 0)
    return;
  if ((size_t)n < sizeof(stackbuf)) {
    str_append_bytes(s, stackbuf, (size_t)n);
    return;
  }
  char *heap = (char *)malloc((size_t)n + 1);
  if (!heap) {
    log_error("OOM");
    exit(1);
  }
  va_start(ap, fmt);
  vsnprintf(heap, (size_t)n + 1, fmt, ap);
  va_end(ap);
  str_append_bytes(s, heap, (size_t)n);
  free(heap);
}

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

  // Calculate exact buffer size (matching non-SIMD version)
  // Add extra space for reset sequence at the beginning
  const ssize_t len = (ssize_t)h * ((ssize_t)w + 1) + 4;

  // Single allocation - no buffer pool overhead
  char *ascii;
  SAFE_MALLOC(ascii, len * sizeof(char), char *);

  // Process directly into final buffer - no copying!
  char *pos = ascii;

  // Add reset sequence to clear any previous terminal colors
  memcpy(pos, "\033[0m", 4);
  pos += 4;

#ifdef SIMD_SUPPORT_NEON
  // Use monochrome NEON function directly - it already handles newlines correctly
  free(ascii); // Free the allocated buffer since we're using NEON's output
  return render_ascii_image_monochrome_neon(image);
#else
  // Non-NEON fallback: process pixels row by row with newlines
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row_pixels = (const rgb_pixel_t *)&image->pixels[y * w];

    // Convert this row of pixels to ASCII characters
    convert_pixels_scalar(row_pixels, pos, w);
    pos += w;

    // Add newline (except for last row)
    if (y != h - 1) {
      *pos++ = '\n';
    }
  }

  *pos = '\0';
  return ascii;
#endif
}

/* ============================================================================
 * Auto-dispatch and any helpers
 * ============================================================================
 */

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
  printf("  ✓ Scalar fallback (1 pixel/cycle)\n");
}

/* ============================================================================
 * Benchmarking
 * ============================================================================
 */

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

// NEW: Measure execution time for image-based functions (like NEON)
// Returns average time per operation in seconds
static double measure_image_function_time(char *(*func)(const image_t *), const image_t *test_image) {
  int pixel_count = test_image->w * test_image->h;
  int iterations = calculate_adaptive_iterations(pixel_count, 10.0); // Target 10ms minimum

  // Warmup run to stabilize CPU frequency scaling and caches
  char *result = func(test_image);
  if (result)
    free(result);

  // Actual measurement with multiple iterations
  double start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = func(test_image);
    if (result)
      free(result);
  }
  double total_time = get_time_seconds() - start;

  return total_time / iterations; // Return average time per iteration
}

simd_benchmark_t benchmark_simd_conversion(int width, int height, int __attribute__((unused)) iterations) {
  simd_benchmark_t result = {0};

  int pixel_count = width * height;

  // Generate test data and test image
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  SAFE_CALLOC_SIMD(test_pixels, pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  SAFE_MALLOC(output_buffer, pixel_count, char *);

  // Create test image for new image-based functions
  image_t *test_image = image_new(width, height);
  if (!test_image) {
    free(test_pixels);
    free(output_buffer);
    return result;
  }

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

  // Copy test data to test image pixels
  memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));

  // Calculate adaptive iterations for reliable timing
  int adaptive_iterations = calculate_adaptive_iterations(pixel_count, 10.0);
  printf("Benchmarking %dx%d (%d pixels) using %d adaptive iterations (ignoring passed iterations)...\n", width, height,
         pixel_count, adaptive_iterations);

  // Benchmark scalar version with adaptive timing (keep using old API for now)
  result.scalar_time = measure_function_time(convert_pixels_scalar, test_pixels, output_buffer, pixel_count);

#ifdef SIMD_SUPPORT_SSE2
  // Benchmark SSE2 using new image-based timing function
  result.sse2_time = measure_image_function_time(render_ascii_image_monochrome_sse2, test_image);
#endif

#ifdef SIMD_SUPPORT_SSSE3
  // Benchmark SSSE3 using new image-based timing function
  result.ssse3_time = measure_image_function_time(render_ascii_image_monochrome_ssse3, test_image);
#endif

#ifdef SIMD_SUPPORT_AVX2
  // Benchmark AVX2 using new image-based timing function
  result.avx2_time = measure_image_function_time(render_ascii_image_monochrome_avx2, test_image);
#endif

#ifdef SIMD_SUPPORT_NEON
  // Benchmark NEON using new image-based timing function
  result.neon_time = measure_image_function_time(render_ascii_image_monochrome_neon, test_image);
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

  // Cleanup
  image_destroy(test_image);
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

  // Generate test data and test image for unified functions
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  SAFE_CALLOC_SIMD(test_pixels, pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  SAFE_MALLOC(output_buffer, output_buffer_size, char *);

  // Create test image for new unified functions
  image_t *frame = image_new(width, height);
  if (!frame) {
    free(test_pixels);
    free(output_buffer);
    return result;
  }

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

  // Populate test image with same data as test_pixels
  frame->pixels = test_pixels;

  const char *mode_str = background_mode ? "background" : "foreground";
  printf("Benchmarking COLOR %s %dx%d (%d pixels) x %d iterations...\n", mode_str, width, height, pixel_count,
         iterations);

  // Benchmark scalar color version
  double start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result_str = image_print_color(frame);
    if (result_str)
      free(result_str);
  }
  result.scalar_time = get_time_seconds() - start;

#ifdef SIMD_SUPPORT_SSE2
  // Benchmark SSE2 color using unified function
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = render_ascii_sse2_unified_optimized(frame, background_mode, true);
    if (result)
      free(result);
  }
  result.sse2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_SSSE3
  // Benchmark SSSE3 color using unified function
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = render_ascii_ssse3_unified_optimized(frame, background_mode, true);
    if (result)
      free(result);
  }
  result.ssse3_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_AVX2
  // Benchmark AVX2 color using unified function
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = render_ascii_avx2_unified_optimized(frame, background_mode, true);
    if (result)
      free(result);
  }
  result.avx2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_NEON
  // Benchmark NEON color
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    // Create temporary image for unified function
    image_t temp_image = {.pixels = test_pixels, .w = width, .h = height};
    char *result = render_ascii_neon_unified_optimized(&temp_image, background_mode, true);
    if (result)
      free(result);
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

  // Cleanup - frame owns test_pixels now
  frame->pixels = NULL; // Don't double-free
  image_destroy(frame);
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
  // result.scalar_time = measure_function_time(convert_pixels_scalar, test_pixels, output_buffer, pixel_count);
  image_t *frame = image_new(width, height);
  memcpy(frame->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
  // Actual measurement with multiple iterations
  double start = get_time_seconds();
  int scalar_iterations = calculate_adaptive_iterations(pixel_count, 10.0); // Target 10ms minimum
  for (int i = 0; i < scalar_iterations; i++) {
    char *output = ascii_convert(frame, width, height, false, false, false);
    free(output);
  }
  double total_time = get_time_seconds() - start;
  result.scalar_time = total_time / iterations; // Return average time per iteration

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
  // Benchmark NEON using new image-based timing function
  image_t *test_image = image_new(width, height);
  if (!test_image) {
    exit(1);
  }
  memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
  result.neon_time = measure_image_function_time(render_ascii_image_monochrome_neon, test_image);
  image_destroy(test_image);
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

  image_destroy(frame);
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
        if (webcam_frame) {
          image_destroy(webcam_frame);
          webcam_frame = NULL;
        }
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
      resized_frame = NULL;
      image_destroy(webcam_frame);
      webcam_frame = NULL;
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
    webcam_cleanup();
  }

  printf("Benchmarking COLOR %s conversion using %d iterations...\n", mode_str, adaptive_iterations);

  // FIX #5: Prewarm 256-color caches to avoid first-frame penalty (~1.5-2MB cache build)
  prewarm_sgr256_fg_cache(); // Warmup 256-entry FG cache
  prewarm_sgr256_cache();    // Warmup 65,536-entry FG+BG cache

  // Benchmark scalar color conversion (pure conversion, no I/O)
  double start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    image_t *test_image = image_new(width, height);
    if (test_image == NULL) {
      fprintf(stderr, "Failed to allocate test_image in benchmark. Aborting loop.\n");
      break;
    }
    memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
    char *result_ascii = ascii_convert(test_image, width, height, false, false, false);
    if (result_ascii)
      free(result_ascii);
    image_destroy(test_image);
  }
  result.scalar_time = get_time_seconds() - start;

  // Find best method -- default to scalar and let simd beat it.
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#ifdef SIMD_SUPPORT_SSE2
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    image_t *test_image = image_new(width, height);
    if (test_image) {
      memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
      char *result_str = render_ascii_sse2_unified_optimized(test_image, background_mode, use_fast_path);
      if (result_str)
        free(result_str);
      image_destroy(test_image);
    }
  }
  result.sse2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_SSSE3
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    image_t *test_image = image_new(width, height);
    if (test_image) {
      memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
      char *result_str = render_ascii_ssse3_unified_optimized(test_image, background_mode, use_fast_path);
      if (result_str)
        free(result_str);
      image_destroy(test_image);
    }
  }
  result.ssse3_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_AVX2
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    image_t *test_image = image_new(width, height);
    if (test_image) {
      memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
      char *result_str = render_ascii_avx2_unified_optimized(test_image, background_mode, use_fast_path);
      if (result_str)
        free(result_str);
      image_destroy(test_image);
    }
  }
  result.avx2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_NEON
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    // Create temporary image for unified function
    image_t temp_image = {.pixels = test_pixels, .w = width, .h = height};
    char *result = render_ascii_neon_unified_optimized(&temp_image, background_mode, use_fast_path);
    if (result)
      free(result);
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
    result.best_method = "AVX2";
  }
#endif

#ifdef SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time) {
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

  printf("------------\n");
  printf("scalar: %f\n", result.scalar_time);
  if (result.sse2_time > 0)
    printf("SSE2: %f\n", result.sse2_time);
  if (result.ssse3_time > 0)
    printf("SSSE3: %f\n", result.ssse3_time);
  if (result.avx2_time > 0)
    printf("avx2: %f\n", result.avx2_time);
  if (result.neon_time > 0)
    printf("neon: %f\n", result.neon_time);
  printf("Best method: %s, time: %f (%.2fx speedup)\n", result.best_method, best_time, result.speedup_best);
  printf("------------\n");

  // Frame data already cleaned up in webcam capture section
  free(test_pixels);
  free(output_buffer);

  return result;
}
