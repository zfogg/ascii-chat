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

void test_colored_output_sample(void) {
    printf("=== Colored ASCII Output Sample ===\n");
    
    // Generate a small test image with gradient
    const int width = 60, height = 8;
    rgb_pixel_t *pixels;
    SAFE_MALLOC(pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t *);
    
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            
            // Create rainbow gradient
            float hue = (float)x / width * 360.0f;  // Hue across width
            float brightness = 1.0f - (float)y / height * 0.7f;  // Brightness down height
            
            // Simple HSV to RGB conversion
            float c = brightness;
            float h_prime = hue / 60.0f;
            float x_val = c * (1.0f - fabsf(fmodf(h_prime, 2.0f) - 1.0f));
            
            float r, g, b;
            if (h_prime < 1) { r = c; g = x_val; b = 0; }
            else if (h_prime < 2) { r = x_val; g = c; b = 0; }
            else if (h_prime < 3) { r = 0; g = c; b = x_val; }
            else if (h_prime < 4) { r = 0; g = x_val; b = c; }
            else if (h_prime < 5) { r = x_val; g = 0; b = c; }
            else { r = c; g = 0; b = x_val; }
            
            pixels[idx].r = (uint8_t)(r * 255);
            pixels[idx].g = (uint8_t)(g * 255);
            pixels[idx].b = (uint8_t)(b * 255);
        }
    }
    
    // Test the new image_print_colored_simd function
    image_t test_image = {
        .pixels = (rgb_t *)pixels,
        .w = width,
        .h = height
    };
    
    printf("\nTesting new optimized image_print_colored_simd function:\n\n");
    
    // Test foreground mode
    opt_background_color = false;
    char *foreground_ascii = image_print_colored_simd(&test_image);
    if (foreground_ascii) {
        printf("Foreground colored ASCII (SIMD):\n%s\n\n", foreground_ascii);
        free(foreground_ascii);
    }
    
    // Test background mode  
    opt_background_color = true;
    char *background_ascii = image_print_colored_simd(&test_image);
    if (background_ascii) {
        printf("Background colored ASCII (SIMD):\n%s\n\n", background_ascii);
        free(background_ascii);
    }
    
    // Also test row-by-row conversion
    size_t max_output = width * 40;
    char *output_buffer;
    SAFE_MALLOC(output_buffer, max_output, char *);
    
    printf("Row-by-row conversion test (foreground):\n");
    for (int y = 0; y < height; y++) {
        size_t len = convert_row_with_color_optimized(
            &pixels[y * width], output_buffer, max_output, width, false);
        output_buffer[len] = '\0';
        printf("%s\n", output_buffer);
    }
    
    free(pixels);
    free(output_buffer);
}

int main(void) {
    printf("====================================\n");
    printf("  Colored ASCII SIMD Optimization  \n");
    printf("====================================\n\n");
    
    // Initialize logging (required by SAFE_MALLOC)
    log_init(NULL, LOG_ERROR);
    
    test_colored_ascii_performance();
    
    printf("Running colored ASCII output sample test...\n");
    test_colored_output_sample();
    
    printf("\n=== Summary ===\n");
    printf("NEW OPTIMIZED Implementation provides:\n");
    printf("1. Single allocation - no buffer pool overhead\n");
    printf("2. Direct processing into final buffer - no copying\n");
    printf("3. Eliminated memory allocation churn\n");
    printf("4. Fixed newline formatting consistency\n");
    printf("5. SIMD acceleration for luminance calculation\n");
    printf("6. Should now be FASTER than scalar version\n\n");
    
    printf("Key optimizations made:\n");
    printf("- Removed buffer pool usage in image_print_colored_simd()\n");
    printf("- Single SAFE_MALLOC instead of multiple allocations\n");
    printf("- Direct row processing into final output buffer\n");
    printf("- No intermediate memory copying\n");
    printf("- Matches non-SIMD allocation pattern for compatibility\n");
    
    log_destroy();
    
    return 0;
}
