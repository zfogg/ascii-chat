#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include "platform/abstraction.h"
#include "common.h"
#include "ascii_simd.h"
#include "palette.h"
#include "../ascii.h"
#include "image2ascii/output_buffer.h"
#include "avx2.h"
#include "util/math.h"

global_dec3_cache_t g_dec3_cache = {.dec3_initialized = false};

// Helper: write decimal RGB triplet using dec3 cache
size_t write_rgb_triplet(uint8_t value, char *dst) {
  const dec3_t *d = &g_dec3_cache.dec3_table[value];
  memcpy(dst, d->s, d->len);
  return d->len;
}

// Default luminance palette for legacy functions
char g_default_luminance_palette[256];
static bool g_default_palette_initialized = false;

// Initialize default luminance palette
void init_default_luminance_palette(void) {
  if (g_default_palette_initialized)
    return;

  // Build default luminance mapping using standard palette
  const size_t len = DEFAULT_ASCII_PALETTE_LEN;
  for (int i = 0; i < 256; i++) {
    size_t palette_index = (i * (len - 1) + 127) / 255;
    if (palette_index >= len) {
      palette_index = len - 1;
    }
    g_default_luminance_palette[i] = DEFAULT_ASCII_PALETTE[palette_index];
  }
  g_default_palette_initialized = true;
}

// Helper function for benchmarks and fallback cases
static void ensure_default_palette_ready(void) {
  init_default_luminance_palette();
}

void init_dec3(void) {
  if (g_dec3_cache.dec3_initialized)
    return;
  for (int v = 0; v < 256; ++v) {
    int d2 = v / 100;     // 0..2
    int r = v - d2 * 100; // 0..99
    int d1 = r / 10;      // 0..9
    int d0 = r - d1 * 10; // 0..9

    if (d2) {
      g_dec3_cache.dec3_table[v].len = 3;
      g_dec3_cache.dec3_table[v].s[0] = '0' + d2;
      g_dec3_cache.dec3_table[v].s[1] = '0' + d1;
      g_dec3_cache.dec3_table[v].s[2] = '0' + d0;
    } else if (d1) {
      g_dec3_cache.dec3_table[v].len = 2;
      g_dec3_cache.dec3_table[v].s[0] = '0' + d1;
      g_dec3_cache.dec3_table[v].s[1] = '0' + d0;
    } else {
      g_dec3_cache.dec3_table[v].len = 1;
      g_dec3_cache.dec3_table[v].s[0] = '0' + d0;
    }
  }
  g_dec3_cache.dec3_initialized = true;
}

// **HIGH-IMPACT FIX 2**: Remove init guards from hot path - use constructor
// NOTE: Constructor disabled for musl static builds - causes hangs
// __attribute__((constructor)) static void ascii_ctor(void) {
//   init_dec3();
//   init_default_luminance_palette();
// }

void ascii_simd_init(void) {
  // Initialize SIMD lookup tables manually (constructor disabled for musl compatibility)
  // Both init functions have guards to prevent double-initialization
  init_dec3();
  init_default_luminance_palette();
}

// Allocate a new image (RGB8), use SAFE_MALLOC for consistent error handling
ImageRGB alloc_image(int w, int h) {
  ImageRGB out;
  out.w = w;
  out.h = h;
  size_t n = (size_t)w * (size_t)h * 3u;
  out.pixels = SAFE_MALLOC(n, uint8_t *);
  return out;
}

// String utility functions
void str_init(Str *s) {
  s->data = NULL;
  s->len = 0;
  s->cap = 0;
}

void str_free(Str *s) {
  SAFE_FREE(s->data);
  s->data = NULL;
  s->len = s->cap = 0;
}

void str_reserve(Str *s, size_t need) {
  if (need <= s->cap)
    return;
  size_t ncap = s->cap ? s->cap : 4096;
  while (ncap < need)
    ncap = (ncap * 3) / 2 + 64;
  s->data = SAFE_REALLOC(s->data, ncap, char *);
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
  char *heap;
  heap = SAFE_MALLOC((size_t)n + 1, char *);
  va_start(ap, fmt);
  (void)vsnprintf(heap, (size_t)n + 1, fmt, ap);
  va_end(ap);
  str_append_bytes(s, heap, (size_t)n);
  SAFE_FREE(heap);
}

/* ============================================================================
 * Scalar Implementation (Baseline)
 * ============================================================================
 */

void convert_pixels_scalar(const rgb_pixel_t *pixels, char *ascii_chars, int count, const char luminance_palette[256]) {
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

char *convert_pixels_scalar_with_newlines(image_t *image, const char luminance_palette[256]) {
  const int h = image->h;
  const int w = image->w;

  // Get UTF-8 character cache for RLE emission
  // Note: We need to reverse-engineer the palette chars from luminance_palette
  // For now, use a simpler approach with direct luminance lookup

  // Use outbuf_t for efficient UTF-8 RLE emission (same as SIMD renderers)
  outbuf_t ob = {0};
  const size_t max_char_bytes = 4; // Max UTF-8 character size
  ob.cap = (size_t)h * ((size_t)w * max_char_bytes + 1);
  ob.buf = SAFE_MALLOC(ob.cap ? ob.cap : 1, char *);
  if (!ob.buf) {
    log_error("Failed to allocate output buffer for scalar rendering");
    return NULL;
  }

  // Process pixels with RLE optimization
  for (int y = 0; y < h; y++) {
    const rgb_pixel_t *row_pixels = (const rgb_pixel_t *)&image->pixels[y * w];

    for (int x = 0; x < w;) {
      const rgb_pixel_t *p = &row_pixels[x];

      // Calculate luminance using integer arithmetic
      int luminance = (LUMA_RED * p->r + LUMA_GREEN * p->g + LUMA_BLUE * p->b) >> 8;
      if (luminance > 255)
        luminance = 255;

      char current_char = luminance_palette[luminance];

      // Find run length for same character (RLE optimization)
      int j = x + 1;
      while (j < w) {
        const rgb_pixel_t *next_p = &row_pixels[j];
        int next_luminance = (LUMA_RED * next_p->r + LUMA_GREEN * next_p->g + LUMA_BLUE * next_p->b) >> 8;
        if (next_luminance > 255)
          next_luminance = 255;
        char next_char = luminance_palette[next_luminance];
        if (next_char != current_char)
          break;
        j++;
      }
      uint32_t run = (uint32_t)(j - x);

      // Emit character with RLE (same as SIMD)
      ob_putc(&ob, current_char);
      if (rep_is_profitable(run)) {
        emit_rep(&ob, run - 1);
      } else {
        for (uint32_t k = 1; k < run; k++) {
          ob_putc(&ob, current_char);
        }
      }
      x = j;
    }

    // Add newline (except for last row)
    if (y != h - 1) {
      ob_putc(&ob, '\n');
    }
  }

  ob_term(&ob);
  return ob.buf;
}

// --------------------------------------
// SIMD-convert an image into ASCII characters and return it with newlines
char *image_print_simd(image_t *image, const char *ascii_chars) {
#if SIMD_SUPPORT_AVX2
  return render_ascii_image_monochrome_avx2(image, ascii_chars);
#elif defined(SIMD_SUPPORT_SSSE3)
  return render_ascii_image_monochrome_ssse3(image, ascii_chars);
#elif defined(SIMD_SUPPORT_SSE2)
  return render_ascii_image_monochrome_sse2(image, ascii_chars);
#elif defined(SIMD_SUPPORT_NEON)
  return render_ascii_image_monochrome_neon(image, ascii_chars);
#else
  log_debug("COMPILED WITHOUT SPECIFIC SIMD");
  return convert_pixels_scalar_with_newlines(image, ascii_chars);
#endif
}

// NOTE: image_print_simd_with_palette is now redundant - use image_print_simd() directly

/* ============================================================================
 * Auto-dispatch and any helpers
 * ============================================================================
 */

void print_simd_capabilities(void) {
  printf("SIMD Support:\n");
#if SIMD_SUPPORT_AVX2
  printf("  ✓ AVX2 (32 pixels/cycle)\n");
#endif
#if SIMD_SUPPORT_NEON
  printf("  ✓ ARM NEON (16 pixels/cycle)\n");
#endif
#if SIMD_SUPPORT_SVE
  printf("  ✓ ARM SVE (scalable pixels/cycle)\n");
#endif
#if SIMD_SUPPORT_SSSE3
  printf("  ✓ SSSE3 (16 pixels/cycle)\n");
#endif
#if SIMD_SUPPORT_SSE2
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
    base_iterations = 10; // 640×480 = 307,200 pixels -> 10 iterations
  } else {
    base_iterations = 5; // 1280×720 = 921,600 pixels -> 5 iterations
  }

  // Ensure we have at least the minimum for reliable timing
  const int minimum_iterations = 10;
  return (base_iterations > minimum_iterations) ? base_iterations : minimum_iterations;
}

simd_benchmark_t benchmark_simd_conversion(int width, int height, int __attribute__((unused)) iterations) {
  simd_benchmark_t result = {0};

  int pixel_count = width * height;

  // Generate test data and test image
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  test_pixels = SAFE_CALLOC_SIMD(pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  output_buffer = SAFE_MALLOC(pixel_count, char *);

  // Create test image for new image-based functions
  image_t *test_image = image_new(width, height);
  if (!test_image) {
    SAFE_FREE(test_pixels);
    SAFE_FREE(output_buffer);
    return result;
  }

  // Use synthetic data for consistent cross-platform testing
  printf("Using synthetic gradient data for consistent benchmarking\n");
  srand(12345); // Consistent results across runs // NOLINT(cert-msc32-c,cert-msc51-cpp)
  for (int i = 0; i < pixel_count; i++) {
    int x = i % width;
    int y = i / width;
    // Create realistic gradient pattern with some variation
    int base_r = (x * 255) / width;
    int base_g = (y * 255) / height;
    int base_b = ((x + y) * 127) / (width + height);

    // Add small random variation to make it realistic
    int temp_r = base_r + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)
    int temp_g = base_g + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)
    int temp_b = base_b + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)

    test_pixels[i].r = clamp_rgb(temp_r);
    test_pixels[i].g = clamp_rgb(temp_g);
    test_pixels[i].b = clamp_rgb(temp_b);
  }

  // Copy test data to test image pixels
  memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));

  // Calculate adaptive iterations for reliable timing
  int adaptive_iterations = calculate_adaptive_iterations(pixel_count, 10.0);
  printf("Benchmarking MONO %dx%d (%d pixels) using %d adaptive iterations (ignoring passed iterations)...\n", width,
         height, pixel_count, adaptive_iterations);

  // Benchmark scalar using image-based API
  ensure_default_palette_ready();
  double start_mono = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    char *result_str = image_print(test_image, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.scalar_time = (get_time_seconds() - start_mono) / adaptive_iterations;

#if SIMD_SUPPORT_SSE2
  // Benchmark SSE2 using new image-based timing function
  // Benchmark SSE2 monochrome rendering
  double start_sse2 = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    char *result_str = render_ascii_image_monochrome_sse2(test_image, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.sse2_time = (get_time_seconds() - start_sse2) / adaptive_iterations;
#endif

#if SIMD_SUPPORT_SSSE3
  // Benchmark SSSE3 using new image-based timing function
  // Benchmark SSSE3 monochrome rendering
  double start_ssse3 = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    char *result_str = render_ascii_image_monochrome_ssse3(test_image, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.ssse3_time = (get_time_seconds() - start_ssse3) / adaptive_iterations;
#endif

#if SIMD_SUPPORT_AVX2
  // Benchmark AVX2 using optimized single-pass implementation
  // Benchmark AVX2 monochrome rendering
  double start_avx2 = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    char *result_str = render_ascii_image_monochrome_avx2(test_image, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.avx2_time = (get_time_seconds() - start_avx2) / adaptive_iterations;
#endif

#if SIMD_SUPPORT_NEON
  // Benchmark NEON using new image-based timing function
  // TODO: Update benchmark to use custom palette testing
  // Benchmark NEON monochrome rendering
  double start_neon = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    char *result_str = render_ascii_image_monochrome_neon(test_image, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.neon_time = (get_time_seconds() - start_neon) / adaptive_iterations;
#endif

#if SIMD_SUPPORT_SVE
  // SVE benchmarking disabled - function removed
  result.sve_time = 0.0;
#endif

  // Find best method
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#if SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time) {
    best_time = result.sse2_time;
    result.best_method = "SSE2";
  }
#endif

#if SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time) {
    best_time = result.ssse3_time;
    result.best_method = "SSSE3";
  }
#endif

#if SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time) {
    best_time = result.avx2_time;
    result.best_method = "AVX2";
  }
#endif

#if SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time) {
    best_time = result.neon_time;
    result.best_method = "NEON";
  }
#endif

  result.speedup_best = result.scalar_time / best_time;

#if SIMD_SUPPORT_SVE
  if (result.sve_time > 0 && result.sve_time < best_time) {
    best_time = result.sve_time;
    result.best_method = "SVE";
  }
#endif

  // Cleanup
  image_destroy(test_image);
  SAFE_FREE(test_pixels);
  SAFE_FREE(output_buffer);

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
  test_pixels = SAFE_CALLOC_SIMD(pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  output_buffer = SAFE_MALLOC(output_buffer_size, char *);

  // Create test image for new unified functions
  image_t *frame = image_new(width, height);
  if (!frame) {
    SAFE_FREE(test_pixels);
    SAFE_FREE(output_buffer);
    return result;
  }

  // Use synthetic gradient data for consistent cross-platform benchmarking
  printf("Using coherent gradient data for realistic color testing\n");
  srand(12345); // For consistent gradient variation across runs // NOLINT(cert-msc32-c,cert-msc51-cpp)
  for (int i = 0; i < pixel_count; i++) {
    int x = i % width;
    int y = i / width;
    // Create smooth gradients with some variation (mimics real images)
    int base_r = (x * 255) / width;
    int base_g = (y * 255) / height;
    int base_b = ((x + y) * 127) / (width + height);

    // Add realistic variation
    int temp_r = base_r + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)
    int temp_g = base_g + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)
    int temp_b = base_b + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)

    test_pixels[i].r = clamp_rgb(temp_r);
    test_pixels[i].g = clamp_rgb(temp_g);
    test_pixels[i].b = clamp_rgb(temp_b);
  }

  // Populate test image with same data as test_pixels
  frame->pixels = test_pixels;

  const char *mode_str = background_mode ? "background" : "foreground";
  printf("Benchmarking COLOR %s %dx%d (%d pixels) x %d iterations...\n", mode_str, width, height, pixel_count,
         iterations);

  // Benchmark scalar color version
  double start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result_str = image_print_color(frame, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.scalar_time = get_time_seconds() - start;

#if SIMD_SUPPORT_SSE2
  // Benchmark SSE2 color using unified function
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *ascii_output = render_ascii_sse2_unified_optimized(frame, background_mode, true, DEFAULT_ASCII_PALETTE);
    if (ascii_output)
      SAFE_FREE(ascii_output);
  }
  result.sse2_time = get_time_seconds() - start;
#endif

#if SIMD_SUPPORT_SSSE3
  // Benchmark SSSE3 color using unified function
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *ascii_output = render_ascii_ssse3_unified_optimized(frame, background_mode, true, DEFAULT_ASCII_PALETTE);
    if (ascii_output)
      SAFE_FREE(ascii_output);
  }
  result.ssse3_time = get_time_seconds() - start;
#endif

#if SIMD_SUPPORT_AVX2
  // Benchmark AVX2 color using unified function
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *ascii_output = render_ascii_avx2_unified_optimized(frame, background_mode, true, DEFAULT_ASCII_PALETTE);
    if (ascii_output)
      SAFE_FREE(ascii_output);
  }
  result.avx2_time = get_time_seconds() - start;
#endif

#if SIMD_SUPPORT_NEON
  // Benchmark NEON color
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    // Create temporary image for unified function
    image_t temp_image = {.pixels = test_pixels, .w = width, .h = height};
    char *ascii_output = render_ascii_neon_unified_optimized(&temp_image, background_mode, true, DEFAULT_ASCII_PALETTE);
    if (ascii_output)
      SAFE_FREE(ascii_output);
  }
  result.neon_time = get_time_seconds() - start;
#endif

  // Find best method
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#if SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time) {
    best_time = result.sse2_time;
    result.best_method = "SSE2";
  }
#endif

#if SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time) {
    best_time = result.ssse3_time;
    result.best_method = "SSSE3";
  }
#endif

#if SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time) {
    best_time = result.avx2_time;
    result.best_method = "AVX2";
  }
#endif

#if SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time) {
    best_time = result.neon_time;
    result.best_method = "NEON";
  }
#endif

  result.speedup_best = result.scalar_time / best_time;

  // Cleanup - frame owns test_pixels now
  frame->pixels = NULL; // Don't double-free
  image_destroy(frame);
  SAFE_FREE(test_pixels);
  SAFE_FREE(output_buffer);

  return result;
}

// Enhanced benchmark function with image source support
simd_benchmark_t benchmark_simd_conversion_with_source(int width, int height, int iterations, bool background_mode,
                                                       const image_t *source_image, bool use_256color) {
  simd_benchmark_t result = {0};
  (void)background_mode; // Suppress unused parameter warning
  (void)use_256color;    // Suppress unused parameter warning

  int pixel_count = width * height;

  // Generate test data
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  const size_t output_buffer_size = pixel_count * 16;
  test_pixels = SAFE_CALLOC_SIMD(pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  output_buffer = SAFE_MALLOC(output_buffer_size, char *);

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
    srand(12345); // NOLINT(cert-msc32-c,cert-msc51-cpp)
    for (int i = 0; i < pixel_count; i++) {
      int x = i % width;
      int y = i / width;
      int base_r = (x * 255 / width);
      int base_g = (y * 255 / height);
      int base_b = ((x + y) * 127 / (width + height));

      int temp_r = base_r + (rand() % 16 - 8); // NOLINT(cert-msc30-c,cert-msc50-cpp)
      int temp_g = base_g + (rand() % 16 - 8); // NOLINT(cert-msc30-c,cert-msc50-cpp)
      int temp_b = base_b + (rand() % 16 - 8); // NOLINT(cert-msc30-c,cert-msc50-cpp)

      test_pixels[i].r = clamp_rgb(temp_r);
      test_pixels[i].g = clamp_rgb(temp_g);
      test_pixels[i].b = clamp_rgb(temp_b);
    }
  }

  // Calculate adaptive iterations for reliable timing
  int adaptive_iterations = calculate_adaptive_iterations(pixel_count, 10.0);
  printf("Benchmarking %dx%d (%d pixels) using %d adaptive iterations (ignoring passed iterations)...\n", width, height,
         pixel_count, adaptive_iterations);

  // Benchmark all available SIMD variants using unified image-based API
  image_t *frame = image_new(width, height);
  memcpy(frame->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));

  // Benchmark scalar using color conversion
  ensure_default_palette_ready();
  double start_scalar = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result_str = image_print_color(frame, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.scalar_time = (get_time_seconds() - start_scalar) / iterations;

#if SIMD_SUPPORT_SSE2
  // Benchmark SSE2 using unified optimized renderer
  // Benchmark SSE2 color rendering
  ensure_default_palette_ready();
  double start_sse2_color = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result_str = render_ascii_sse2_unified_optimized(frame, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.sse2_time = (get_time_seconds() - start_sse2_color) / iterations;
#endif

#if SIMD_SUPPORT_SSSE3
  // Benchmark SSSE3 using unified optimized renderer
  // Benchmark SSSE3 color rendering
  ensure_default_palette_ready();
  double start_ssse3_color = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result_str =
        render_ascii_ssse3_unified_optimized(frame, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.ssse3_time = (get_time_seconds() - start_ssse3_color) / iterations;
#endif

#if SIMD_SUPPORT_AVX2
  // Benchmark AVX2 using unified optimized renderer
  // Benchmark AVX2 color rendering
  ensure_default_palette_ready();
  double start_avx2_color = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result_str = render_ascii_avx2_unified_optimized(frame, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.avx2_time = (get_time_seconds() - start_avx2_color) / iterations;
#endif

#if SIMD_SUPPORT_NEON
  // Benchmark NEON using unified optimized renderer
  // Benchmark NEON color rendering
  ensure_default_palette_ready();
  double start_neon_color = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result_str = render_ascii_neon_unified_optimized(frame, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.neon_time = (get_time_seconds() - start_neon_color) / iterations;
#endif

#if SIMD_SUPPORT_SVE
  // Benchmark SVE using unified optimized renderer
  // Benchmark SVE color rendering
  ensure_default_palette_ready();
  double start_sve_color = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result_str = render_ascii_sve_unified_optimized(frame, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
    if (result_str)
      SAFE_FREE(result_str);
  }
  result.sve_time = (get_time_seconds() - start_sve_color) / iterations;
#endif

  // Find best method
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#if SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time) {
    best_time = result.sse2_time;
    result.best_method = "SSE2";
  }
#endif

#if SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time) {
    best_time = result.ssse3_time;
    result.best_method = "SSSE3";
  }
#endif

#if SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time) {
    best_time = result.avx2_time;
    result.best_method = "AVX2";
  }
#endif

#if SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time) {
    best_time = result.neon_time;
    result.best_method = "NEON";
  }
#endif

  result.speedup_best = result.scalar_time / best_time;

#if SIMD_SUPPORT_SVE
  if (result.sve_time > 0 && result.sve_time < best_time) {
    best_time = result.sve_time;
    result.best_method = "SVE";
  }
#endif

  image_destroy(frame);
  SAFE_FREE(test_pixels);
  SAFE_FREE(output_buffer);

  return result;
}

// Enhanced color benchmark function with image source support
simd_benchmark_t benchmark_simd_color_conversion_with_source(int width, int height,
                                                             int __attribute__((unused)) iterations,
                                                             bool background_mode, const image_t *source_image,
                                                             bool use_256color) {
  simd_benchmark_t result = {0};
  (void)use_256color; // Suppress unused parameter warning

  int pixel_count = width * height;
  size_t output_buffer_size = (size_t)pixel_count * 30 + width * 10;

  // Allocate buffers for benchmarking
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  test_pixels = SAFE_CALLOC_SIMD(pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
  output_buffer = SAFE_MALLOC(output_buffer_size, char *);

  // Calculate adaptive iterations for color benchmarking (ignore passed iterations)
  int adaptive_iterations = calculate_adaptive_iterations(pixel_count, 10.0);

  const char *mode_str = background_mode ? "background" : "foreground";

  // Variables for webcam capture cleanup

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
    // No source image provided: use synthetic gradient data for consistent testing
    printf("Using synthetic gradient data for COLOR %s %dx%d benchmarking with %d iterations...\n", mode_str, width,
           height, adaptive_iterations);

    srand(12345); // Consistent results across runs // NOLINT(cert-msc32-c,cert-msc51-cpp)
    for (int i = 0; i < pixel_count; i++) {
      int x = i % width;
      int y = i / width;
      int base_r = (x * 255) / width;
      int base_g = (y * 255) / height;
      int base_b = ((x + y) * 127) / (width + height);

      int temp_r = base_r + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)
      int temp_g = base_g + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)
      int temp_b = base_b + (rand() % 32 - 16); // NOLINT(cert-msc30-c,cert-msc50-cpp)

      test_pixels[i].r = clamp_rgb(temp_r);
      test_pixels[i].g = clamp_rgb(temp_g);
      test_pixels[i].b = clamp_rgb(temp_b);
    }
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
      SAFE_FREE(test_pixels);
      SAFE_FREE(output_buffer);
      FATAL(ERROR_MEMORY, "Failed to allocate test_image in benchmark iteration %d", i);
    }
    memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
    char *result_ascii = ascii_convert(test_image, width, height, false, false, false, DEFAULT_ASCII_PALETTE,
                                       g_default_luminance_palette);
    if (result_ascii)
      SAFE_FREE(result_ascii);
    image_destroy(test_image);
  }
  result.scalar_time = get_time_seconds() - start;

  // Find best method -- default to scalar and let simd beat it.
  double best_time = result.scalar_time;
  result.best_method = "scalar";

#if SIMD_SUPPORT_SSE2
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    image_t *test_image = image_new(width, height);
    if (test_image) {
      memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
      char *result_str =
          render_ascii_sse2_unified_optimized(test_image, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
      if (result_str)
        SAFE_FREE(result_str);
      image_destroy(test_image);
    }
  }
  result.sse2_time = get_time_seconds() - start;
#endif

#if SIMD_SUPPORT_SSSE3
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    image_t *test_image = image_new(width, height);
    if (test_image) {
      memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
      char *result_str =
          render_ascii_ssse3_unified_optimized(test_image, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
      if (result_str)
        SAFE_FREE(result_str);
      image_destroy(test_image);
    }
  }
  result.ssse3_time = get_time_seconds() - start;
#endif

#if SIMD_SUPPORT_AVX2
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    image_t *test_image = image_new(width, height);
    if (test_image) {
      memcpy(test_image->pixels, test_pixels, pixel_count * sizeof(rgb_pixel_t));
      char *result_str =
          render_ascii_avx2_unified_optimized(test_image, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
      if (result_str)
        SAFE_FREE(result_str);
      image_destroy(test_image);
    }
  }
  result.avx2_time = get_time_seconds() - start;
#endif

#if SIMD_SUPPORT_NEON
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    // Create temporary image for unified function
    image_t temp_image = {.pixels = test_pixels, .w = width, .h = height};
    char *result =
        render_ascii_neon_unified_optimized(&temp_image, background_mode, use_256color, DEFAULT_ASCII_PALETTE);
    if (result)
      SAFE_FREE(result);
  }
  result.neon_time = get_time_seconds() - start;
#endif

#if SIMD_SUPPORT_SVE
  start = get_time_seconds();
  for (int i = 0; i < adaptive_iterations; i++) {
    // Create temporary image for unified function
    image_t temp_image = {.pixels = test_pixels, .w = width, .h = height};
    char *result = render_ascii_sve_unified_optimized(&temp_image, background_mode, use_256color);
    if (result)
      SAFE_FREE(result);
  }
  result.sve_time = get_time_seconds() - start;
#endif

#if SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time) {
    best_time = result.sse2_time;
    result.best_method = "SSE2";
  }
#endif

#if SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time) {
    best_time = result.ssse3_time;
    result.best_method = "SSSE3";
  }
#endif

#if SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time) {
    best_time = result.avx2_time;
    result.best_method = "AVX2";
  }
#endif

#if SIMD_SUPPORT_NEON
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

#if SIMD_SUPPORT_SSE2
  if (result.sse2_time > 0 && result.sse2_time < best_time)
    best_time = result.sse2_time;
#endif
#if SIMD_SUPPORT_SSSE3
  if (result.ssse3_time > 0 && result.ssse3_time < best_time)
    best_time = result.ssse3_time;
#endif
#if SIMD_SUPPORT_AVX2
  if (result.avx2_time > 0 && result.avx2_time < best_time)
    best_time = result.avx2_time;
#endif
#if SIMD_SUPPORT_NEON
  if (result.neon_time > 0 && result.neon_time < best_time)
    best_time = result.neon_time;
#endif
#if SIMD_SUPPORT_SVE
  if (result.sve_time > 0 && result.sve_time < best_time)
    best_time = result.sve_time;
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
  if (result.sve_time > 0)
    printf("sve: %f\n", result.sve_time);
  printf("Best method: %s, time: %f (%.2fx speedup (<1.0 = bad))\n", result.best_method, best_time,
         result.speedup_best);
  printf("------------\n");

  // Frame data already cleaned up in webcam capture section
  SAFE_FREE(test_pixels);
  SAFE_FREE(output_buffer);

  return result;
}
