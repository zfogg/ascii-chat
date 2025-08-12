/*
 * ASCII SIMD Optimization Test Program
 *
 * Demonstrates SIMD-accelerated ASCII art conversion for your project
 *
 * Compile with:
 *   # AVX2 support (Intel/AMD)
 *   gcc -o ascii_simd_test ascii_simd_test.c ascii_simd.c -mavx2 -O3
 *
 *   # SSE2 support (older Intel/AMD)
 *   gcc -o ascii_simd_test ascii_simd_test.c ascii_simd.c -msse2 -O3
 *
 *   # ARM NEON (Apple Silicon)
 *   gcc -o ascii_simd_test ascii_simd_test.c ascii_simd.c -O3
 *
 *   # No SIMD (fallback)
 *   gcc -o ascii_simd_test ascii_simd_test.c ascii_simd.c -O3
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

// Simple bilinear resampling to resize image
void resize_image(const rgb_pixel_t *src, int src_w, int src_h,
                  rgb_pixel_t *dst, int dst_w, int dst_h) {
    float x_ratio = (float)src_w / dst_w;
    float y_ratio = (float)src_h / dst_h;

    for (int y = 0; y < dst_h; y++) {
        for (int x = 0; x < dst_w; x++) {
            float src_x = x * x_ratio;
            float src_y = y * y_ratio;

            int x1 = (int)src_x;
            int y1 = (int)src_y;
            int x2 = (x1 + 1 < src_w) ? x1 + 1 : x1;
            int y2 = (y1 + 1 < src_h) ? y1 + 1 : y1;

            float fx = src_x - x1;
            float fy = src_y - y1;

            // Sample 4 pixels
            const rgb_pixel_t *p00 = &src[y1 * src_w + x1];
            const rgb_pixel_t *p10 = &src[y1 * src_w + x2];
            const rgb_pixel_t *p01 = &src[y2 * src_w + x1];
            const rgb_pixel_t *p11 = &src[y2 * src_w + x2];

            // Bilinear interpolation
            int r = (int)((1-fx)*(1-fy)*p00->r + fx*(1-fy)*p10->r +
                         (1-fx)*fy*p01->r + fx*fy*p11->r);
            int g = (int)((1-fx)*(1-fy)*p00->g + fx*(1-fy)*p10->g +
                         (1-fx)*fy*p01->g + fx*fy*p11->g);
            int b = (int)((1-fx)*(1-fy)*p00->b + fx*(1-fy)*p10->b +
                         (1-fx)*fy*p01->b + fx*fy*p11->b);

            dst[y * dst_w + x].r = r;
            dst[y * dst_w + x].g = g;
            dst[y * dst_w + x].b = b;
        }
    }
}

// Print ASCII art to terminal
void print_ascii_art(const char *ascii_chars, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            putchar(ascii_chars[y * width + x]);
        }
        putchar('\n');
    }
}

// Test 1: Basic functionality comparison
void test_correctness(void) {
    printf("\n=== Test 1: Correctness Verification ===\n");

    const int test_size = 100;
    rgb_pixel_t *test_pixels;
    SAFE_MALLOC(test_pixels, test_size * sizeof(rgb_pixel_t), rgb_pixel_t *);

    char *scalar_result;
    SAFE_MALLOC(scalar_result, test_size, char *);

    char *simd_result;
    SAFE_MALLOC(simd_result, test_size, char *);

    // Generate test pattern
    srand(42);
    for (int i = 0; i < test_size; i++) {
        test_pixels[i].r = rand() % 256;
        test_pixels[i].g = rand() % 256;
        test_pixels[i].b = rand() % 256;
    }

    // Convert with both methods
    convert_pixels_scalar(test_pixels, scalar_result, test_size);
    convert_pixels_optimized(test_pixels, simd_result, test_size);

    // Compare results
    int mismatches = 0;
    for (int i = 0; i < test_size; i++) {
        if (scalar_result[i] != simd_result[i]) {
            mismatches++;
            if (mismatches <= 5) { // Show first few mismatches
                printf("Mismatch at pixel %d: scalar='%c' simd='%c'\n",
                       i, scalar_result[i], simd_result[i]);
            }
        }
    }

    if (mismatches == 0) {
        printf("✓ All %d pixels match perfectly!\n", test_size);
    } else {
        printf("✗ %d/%d pixels don't match (%.1f%%)\n",
               mismatches, test_size, 100.0 * mismatches / test_size);
    }

    free(test_pixels);
    free(scalar_result);
    free(simd_result);
}

// Test 2: Performance benchmarking
void test_performance(void) {
    printf("\n=== Test 2: Performance Benchmarking ===\n");

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

    for (int i = 0; i < num_sizes; i++) {
        int w = sizes[i][0];
        int h = sizes[i][1];
        int iterations = (w * h < 50000) ? 1000 : 100; // Adjust for size

        printf("Testing %dx%d (%d pixels):\n", w, h, w * h);

        simd_benchmark_t benchmark = benchmark_simd_conversion(w, h, iterations);

        printf("  Scalar:    %.3f ms/frame\n", benchmark.scalar_time * 1000 / iterations);

        if (benchmark.sse2_time > 0) {
            double speedup = benchmark.scalar_time / benchmark.sse2_time;
            printf("  SSE2:      %.3f ms/frame (%.1fx speedup)\n",
                   benchmark.sse2_time * 1000 / iterations, speedup);
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

// Test 4: Integration example - NEW OPTIMIZED VERSION
void test_integration(void) {
    printf("\n=== Test 4: Integration with Your Project - NEW OPTIMIZED ===\n");

    printf("Performance test on your 203x64 terminal:\n");
    simd_benchmark_t bench = benchmark_simd_conversion(203, 64, 1000);
    printf("- Scalar: %.2f ms per frame\n", bench.scalar_time * 1000 / 1000);

    double simd_time = 0;
    const char *simd_method = "Unknown";

#ifdef SIMD_SUPPORT_NEON
    if (bench.neon_time > 0) {
        simd_time = bench.neon_time;
        simd_method = "NEON";
    }
#endif
#ifdef SIMD_SUPPORT_AVX2
    if (bench.avx2_time > 0) {
        simd_time = bench.avx2_time;
        simd_method = "AVX2";
    }
#endif
#ifdef SIMD_SUPPORT_SSE2
    if (bench.sse2_time > 0) {
        simd_time = bench.sse2_time;
        simd_method = "SSE2";
    }
#endif

    if (simd_time > 0) {
        printf("- SIMD (%s): %.2f ms per frame (%.1fx faster)\n",
               simd_method, simd_time * 1000 / 1000, bench.scalar_time / simd_time);
    }
    printf("- Best method: %s\n", bench.best_method);
    printf("- At 60 FPS: %.1f%% CPU time saved\n",
           100.0 * (1.0 - 1.0/bench.speedup_best));
}


int main(void) {
    printf("====================================\n");
    printf("   NEW OPTIMIZED SIMD ASCII Test   \n");
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
