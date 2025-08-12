/*
 * Colored ASCII SIMD Optimization Test Program
 *
 * Demonstrates SIMD-accelerated colored ASCII art conversion
 * Tests all SIMD variants: NEON, AVX2, SSSE3, SSE2, and Scalar
 *
 * Compile with:
 *   # AVX2 support (Intel/AMD)
 *   gcc -o ascii_simd_color_test ascii_simd_color_test.c -mavx2 -O3
 *
 *   # SSSE3 support (Intel/AMD)
 *   gcc -o ascii_simd_color_test ascii_simd_color_test.c -mssse3 -O3
 *
 *   # SSE2 support (older Intel/AMD)
 *   gcc -o ascii_simd_color_test ascii_simd_color_test.c -msse2 -O3
 *
 *   # ARM NEON (Apple Silicon)
 *   gcc -o ascii_simd_color_test ascii_simd_color_test.c -O3
 *
 *   # No SIMD (fallback)
 *   gcc -o ascii_simd_color_test ascii_simd_color_test.c -O3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>
#include "../lib/common.h"
#include "../lib/ascii_simd.h"
#include "../lib/options.h"
#include "../lib/image.h"

// Test image dimensions (typical webcam frame)
#define TEST_WIDTH  640
#define TEST_HEIGHT 480
#define TEST_PIXELS (TEST_WIDTH * TEST_HEIGHT)

// Terminal dimensions for ASCII output
#define ASCII_WIDTH  80
#define ASCII_HEIGHT 24

// Color mode benchmark structure (matches simd_benchmark_t structure)
typedef struct {
  double scalar_time;
  double sse2_time;
  double ssse3_time;
  double avx2_time;
  double neon_time;
  double speedup_best;
  const char *best_method;
  size_t output_size;
} color_simd_benchmark_t;

// External function declarations for all color variants
extern size_t convert_row_with_color_scalar(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width, bool background_mode);

#ifdef SIMD_SUPPORT_SSE2
extern size_t convert_row_with_color_sse2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width, bool background_mode);
#endif

#ifdef SIMD_SUPPORT_SSSE3
extern size_t convert_row_with_color_ssse3(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width, bool background_mode);
#endif

#ifdef SIMD_SUPPORT_AVX2
extern size_t convert_row_with_color_avx2(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width, bool background_mode);
#endif

#ifdef SIMD_SUPPORT_NEON
extern size_t convert_row_with_color_neon(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width, bool background_mode);
#endif

extern size_t convert_row_with_color_optimized(const rgb_pixel_t *pixels, char *output_buffer, size_t buffer_size, int width, bool background_mode);

// Generate a test image with interesting patterns
void generate_test_image(rgb_pixel_t *pixels, int width, int height, int frame_num) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;

            // Create animated patterns
            float fx = (float)x / width;
            float fy = (float)y / height;
            float ft = frame_num * 0.1f;

            // Moving gradient with sine waves
            int r = (int)(127 + 127 * sin(fx * 4 + ft));
            int g = (int)(127 + 127 * sin(fy * 6 + ft * 1.1));
            int b = (int)(127 + 127 * sin((fx + fy) * 3 + ft * 0.8));

            // Add some noise for realism
            r += (rand() % 40) - 20;
            g += (rand() % 40) - 20;
            b += (rand() % 40) - 20;

            // Clamp to valid range
            if (r < 0) r = 0; if (r > 255) r = 255;
            if (g < 0) g = 0; if (g > 255) g = 255;
            if (b < 0) b = 0; if (b > 255) b = 255;

            pixels[idx].r = r;
            pixels[idx].g = g;
            pixels[idx].b = b;
        }
    }
}

static double get_time_seconds(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    // Fallback to clock() if CLOCK_MONOTONIC not available
    return (double)clock() / CLOCKS_PER_SEC;
  }
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Comprehensive colored ASCII benchmark function
color_simd_benchmark_t benchmark_colored_simd_conversion(int width, int height, int iterations, bool background_mode) {
  color_simd_benchmark_t result = {0};

  int pixel_count = width * height;
  size_t data_size = pixel_count * sizeof(rgb_pixel_t);
  
  // Estimate buffer size needed for colored output (conservative)
  size_t buffer_size = pixel_count * 50; // 50 bytes per character should be plenty

  // Generate test data
  rgb_pixel_t *test_pixels;
  char *output_buffer;
  SAFE_MALLOC(test_pixels, data_size, rgb_pixel_t *);
  SAFE_MALLOC(output_buffer, buffer_size, char *);

  // Fill with random RGB data
  srand(12345); // Consistent results
  for (int i = 0; i < pixel_count; i++) {
    test_pixels[i].r = rand() % 256;
    test_pixels[i].g = rand() % 256;
    test_pixels[i].b = rand() % 256;
  }

  printf("Benchmarking colored %dx%d (%d pixels) x %d iterations (%s mode)...\n", 
         width, height, pixel_count, iterations, background_mode ? "background" : "foreground");

  // Benchmark scalar version
  double start = get_time_seconds();
  size_t output_size = 0;
  for (int i = 0; i < iterations; i++) {
    output_size = convert_row_with_color_scalar(test_pixels, output_buffer, buffer_size, pixel_count, background_mode);
  }
  result.scalar_time = get_time_seconds() - start;
  result.output_size = output_size;

#ifdef SIMD_SUPPORT_SSE2
  // Benchmark SSE2
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_row_with_color_sse2(test_pixels, output_buffer, buffer_size, pixel_count, background_mode);
  }
  result.sse2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_SSSE3
  // Benchmark SSSE3
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_row_with_color_ssse3(test_pixels, output_buffer, buffer_size, pixel_count, background_mode);
  }
  result.ssse3_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_AVX2
  // Benchmark AVX2
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_row_with_color_avx2(test_pixels, output_buffer, buffer_size, pixel_count, background_mode);
  }
  result.avx2_time = get_time_seconds() - start;
#endif

#ifdef SIMD_SUPPORT_NEON
  // Benchmark NEON
  start = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    convert_row_with_color_neon(test_pixels, output_buffer, buffer_size, pixel_count, background_mode);
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

// Test 1: Basic functionality comparison (same pixels, check output consistency)
void test_correctness(void) {
    printf("\n=== Test 1: Colored ASCII Correctness Verification ===\n");

    const int test_size = 100;
    rgb_pixel_t *test_pixels;
    SAFE_MALLOC(test_pixels, test_size * sizeof(rgb_pixel_t), rgb_pixel_t *);

    size_t buffer_size = test_size * 50; // 50 bytes per character
    char *scalar_result;
    char *simd_result;
    SAFE_MALLOC(scalar_result, buffer_size, char *);
    SAFE_MALLOC(simd_result, buffer_size, char *);

    // Generate test pattern
    srand(42);
    for (int i = 0; i < test_size; i++) {
        test_pixels[i].r = rand() % 256;
        test_pixels[i].g = rand() % 256;
        test_pixels[i].b = rand() % 256;
    }

    // Convert with both methods (foreground mode)
    size_t scalar_len = convert_row_with_color_scalar(test_pixels, scalar_result, buffer_size, test_size, false);
    size_t simd_len = convert_row_with_color_optimized(test_pixels, simd_result, buffer_size, test_size, false);

    // Compare results
    if (scalar_len == simd_len && memcmp(scalar_result, simd_result, scalar_len) == 0) {
        printf("✓ All %d colored pixels match perfectly! (output: %zu bytes)\n", test_size, scalar_len);
    } else {
        printf("✗ Colored outputs don't match: scalar=%zu bytes, simd=%zu bytes\n", scalar_len, simd_len);
        if (scalar_len == simd_len) {
            // Same length, find first difference
            for (size_t i = 0; i < scalar_len; i++) {
                if (scalar_result[i] != simd_result[i]) {
                    printf("First difference at byte %zu: scalar=0x%02x, simd=0x%02x\n", 
                           i, (unsigned char)scalar_result[i], (unsigned char)simd_result[i]);
                    break;
                }
            }
        }
    }

    free(test_pixels);
    free(scalar_result);
    free(simd_result);
}

// Test 2: Performance benchmarking (matches ascii_simd_test format)
void test_performance(void) {
    printf("\n=== Test 2: Colored ASCII Performance Benchmarking ===\n");

    print_simd_capabilities();
    printf("\n");

    // Test different image sizes
    int sizes[][2] = {
        {80, 24},      // Terminal size
        {160, 48},     // 2x terminal
        {320, 240},    // Small webcam
        {640, 480},    // Standard webcam
        {1280, 720},   // HD webcam
    };

    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int mode = 0; mode <= 1; mode++) { // 0=foreground, 1=background
        bool background_mode = (mode == 1);
        printf("--- %s Color Mode ---\n", background_mode ? "Background" : "Foreground");
        
        for (int i = 0; i < num_sizes; i++) {
            int w = sizes[i][0];
            int h = sizes[i][1];
            int iterations = (w * h < 50000) ? 500 : 100; // Fewer iterations for colored (it's slower)

            printf("Testing colored %dx%d (%d pixels):\n", w, h, w * h);

            color_simd_benchmark_t benchmark = benchmark_colored_simd_conversion(w, h, iterations, background_mode);

            printf("  Scalar:    %.3f ms/frame (output: %.1f KB)\n", 
                   benchmark.scalar_time * 1000 / iterations,
                   benchmark.output_size / 1024.0);

            if (benchmark.sse2_time > 0) {
                double speedup = benchmark.scalar_time / benchmark.sse2_time;
                printf("  SSE2:      %.3f ms/frame (%.1fx speedup)\n",
                       benchmark.sse2_time * 1000 / iterations, speedup);
            }

            if (benchmark.ssse3_time > 0) {
                double speedup = benchmark.scalar_time / benchmark.ssse3_time;
                printf("  SSSE3:     %.3f ms/frame (%.1fx speedup)\n",
                       benchmark.ssse3_time * 1000 / iterations, speedup);
            }

            if (benchmark.avx2_time > 0) {
                double speedup = benchmark.scalar_time / benchmark.avx2_time;
                printf("  AVX2:      %.3f ms/frame (%.1fx speedup)\n",
                       benchmark.avx2_time * 1000 / iterations, speedup);
            }

            if (benchmark.neon_time > 0) {
                double speedup = benchmark.scalar_time / benchmark.neon_time;
                printf("  NEON:      %.3f ms/frame (%.1fx speedup)\n",
                       benchmark.neon_time * 1000 / iterations, speedup);
            }

            printf("  Best:      %s\n\n",
                   benchmark.best_method);
        }
    }
}

// Test 3: Integration example - matches ascii_simd_test format  
void test_integration(void) {
    printf("\n=== Test 3: Integration with Your Project - COLORED OPTIMIZED ===\n");

    printf("Performance test on your 203x64 terminal (colored):\n");
    
    // Test both foreground and background modes
    color_simd_benchmark_t bench_fg = benchmark_colored_simd_conversion(203, 64, 1000, false);
    color_simd_benchmark_t bench_bg = benchmark_colored_simd_conversion(203, 64, 1000, true);
    
    printf("Foreground colored mode:\n");
    printf("- Scalar: %.2f ms per frame (output: %.1f KB)\n", 
           bench_fg.scalar_time * 1000 / 1000, bench_fg.output_size / 1024.0);

    double simd_time_fg = 0;
    const char *simd_method_fg = "Unknown";

#ifdef SIMD_SUPPORT_NEON
    if (bench_fg.neon_time > 0) {
        simd_time_fg = bench_fg.neon_time;
        simd_method_fg = "NEON";
    }
#endif
#ifdef SIMD_SUPPORT_AVX2
    if (bench_fg.avx2_time > 0) {
        simd_time_fg = bench_fg.avx2_time;
        simd_method_fg = "AVX2";
    }
#endif
#ifdef SIMD_SUPPORT_SSSE3
    if (bench_fg.ssse3_time > 0) {
        simd_time_fg = bench_fg.ssse3_time;
        simd_method_fg = "SSSE3";
    }
#endif
#ifdef SIMD_SUPPORT_SSE2
    if (bench_fg.sse2_time > 0) {
        simd_time_fg = bench_fg.sse2_time;
        simd_method_fg = "SSE2";
    }
#endif

    if (simd_time_fg > 0) {
        printf("- SIMD (%s): %.2f ms per frame (%.1fx faster)\n",
               simd_method_fg, simd_time_fg * 1000 / 1000, bench_fg.scalar_time / simd_time_fg);
    }
    printf("- Best method: %s\n", bench_fg.best_method);
    printf("- At 60 FPS: %.1f%% CPU time saved\n",
           100.0 * (1.0 - 1.0/bench_fg.speedup_best));

    printf("\nBackground colored mode:\n");
    printf("- Scalar: %.2f ms per frame (output: %.1f KB)\n", 
           bench_bg.scalar_time * 1000 / 1000, bench_bg.output_size / 1024.0);
    printf("- Best method: %s\n", bench_bg.best_method);
    printf("- At 60 FPS: %.1f%% CPU time saved\n",
           100.0 * (1.0 - 1.0/bench_bg.speedup_best));
}

int main(void) {
    printf("====================================\n");
    printf("   COLORED ASCII SIMD Test Suite   \n");
    printf("====================================\n");

    // Initialize logging (required by SAFE_MALLOC)
    log_init(NULL, LOG_ERROR);

    // Run all tests
    test_correctness();
    test_performance();
    test_integration();

    log_destroy();

    return 0;
}