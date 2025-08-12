/*
 * Colored ASCII SIMD Test Program
 *
 * Tests the complete colored ASCII pipeline optimization with the new optimized implementations
 */

#include "../lib/ascii_simd.h"
#include "../lib/common.h"
#include "../lib/options.h"
#include "../lib/image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>

// Include the colored ASCII functions from ascii_simd_color.c
extern size_t convert_row_with_color_optimized(const rgb_pixel_t *pixels, char *output_buffer,
                                               size_t buffer_size, int width, bool background_mode);
extern size_t convert_row_with_color_scalar(const rgb_pixel_t *pixels, char *output_buffer,
                                            size_t buffer_size, int width, bool background_mode);
extern char *image_print_colored_simd(image_t *image);

// For benchmarking - we'll implement our own simple version
typedef struct {
    double scalar_time;
    double simd_time;
    double speedup;
    size_t output_size_scalar;
    size_t output_size_simd;
} color_benchmark_t;

// opt_background_color now comes from options.o
extern unsigned short opt_background_color;

// Simple timing function
static double get_time_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return (double)clock() / CLOCKS_PER_SEC;
    }
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// Our own benchmark implementation
color_benchmark_t benchmark_colored_ascii_new(int width, int height, int iterations, bool background_mode) {
    color_benchmark_t result = {0};

    int pixel_count = width * height;
    size_t max_output_size = pixel_count * 40; // Generous estimate for ANSI codes

    // Generate test data
    rgb_pixel_t *test_pixels;
    SAFE_MALLOC(test_pixels, pixel_count * sizeof(rgb_pixel_t), rgb_pixel_t *);

    char *scalar_output;
    SAFE_MALLOC(scalar_output, max_output_size, char *);

    char *simd_output;
    SAFE_MALLOC(simd_output, max_output_size, char *);

    srand(12345);
    for (int i = 0; i < pixel_count; i++) {
        test_pixels[i].r = rand() % 256;
        test_pixels[i].g = rand() % 256;
        test_pixels[i].b = rand() % 256;
    }

    // Set background mode option
    opt_background_color = background_mode;

    printf("Benchmarking colored ASCII %dx%d (%s mode) x %d iterations...\n", width, height,
           background_mode ? "background" : "foreground", iterations);

    // Benchmark scalar version
    double start = get_time_seconds();
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            size_t row_size = convert_row_with_color_scalar(&test_pixels[y * width], scalar_output,
                                                            max_output_size, width, background_mode);
            if (iter == 0) result.output_size_scalar += row_size;
        }
    }
    result.scalar_time = get_time_seconds() - start;

    // Benchmark SIMD version
    start = get_time_seconds();
    for (int iter = 0; iter < iterations; iter++) {
        for (int y = 0; y < height; y++) {
            size_t row_size = convert_row_with_color_optimized(&test_pixels[y * width], simd_output,
                                                               max_output_size, width, background_mode);
            if (iter == 0) result.output_size_simd += row_size;
        }
    }
    result.simd_time = get_time_seconds() - start;

    result.speedup = result.scalar_time / result.simd_time;

    free(test_pixels);
    free(scalar_output);
    free(simd_output);

    return result;
}

void test_colored_ascii_performance(void) {
    printf("=== Colored ASCII SIMD Performance Test ===\n\n");

    // Test different scenarios
    struct {
        int width, height;
        bool background_mode;
        const char* description;
    } tests[] = {
        {203, 64, false, "Your terminal (foreground colors)"},
        {203, 64, true,  "Your terminal (background colors)"},
        {640, 480, false, "Webcam 640x480 (foreground)"},
        {640, 480, true,  "Webcam 640x480 (background)"},
    };

    int num_tests = sizeof(tests) / sizeof(tests[0]);

    for (int i = 0; i < num_tests; i++) {
        printf("Testing: %s\n", tests[i].description);

        color_benchmark_t result = benchmark_colored_ascii_new(
            tests[i].width, tests[i].height, 100, tests[i].background_mode);

        printf("  Scalar:  %.2f ms/frame (output: %zu KB)\n",
               result.scalar_time * 1000 / 100, result.output_size_scalar / 1024);
        printf("  SIMD:    %.2f ms/frame (output: %zu KB)\n",
               result.simd_time * 1000 / 100, result.output_size_simd / 1024);
        printf("  Speedup: %.1fx faster\n", result.speedup);
        printf("  Savings: %.1f%% CPU time at 60 FPS\n\n",
               100.0 * (1.0 - 1.0/result.speedup));
    }
}

int main(void) {
    printf("====================================\n");
    printf("  Colored ASCII SIMD Optimization  \n");
    printf("====================================\n\n");

    // Initialize logging (required by SAFE_MALLOC)
    log_init(NULL, LOG_ERROR);

    test_colored_ascii_performance();

    log_destroy();

    return 0;
}
