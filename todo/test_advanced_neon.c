#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <string.h>
#include "../lib/ascii_simd.h"
#include "../lib/image.h"

static double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

int main(void) {
    const int width = 640;
    const int height = 480;
    const int pixel_count = width * height;
    const int iterations = 100;

    // Generate test data - large webcam size
    rgb_pixel_t *test_pixels = malloc(pixel_count * sizeof(rgb_pixel_t));
    char *output_buffer = malloc(pixel_count);

    srand(42);
    for (int i = 0; i < pixel_count; i++) {
        test_pixels[i].r = rand() % 256;
        test_pixels[i].g = rand() % 256;
        test_pixels[i].b = rand() % 256;
    }

    printf("Advanced NEON Performance Test\n");
    printf("==============================\n");
    printf("Image size: %dx%d (%d pixels)\n", width, height, pixel_count);
    printf("Iterations: %d\n\n", iterations);

    // Test scalar version
    double start = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
        convert_pixels_scalar(test_pixels, output_buffer, pixel_count);
    }
    double scalar_time = get_time_seconds() - start;
    double scalar_avg = (scalar_time * 1000.0) / iterations;

    printf("Scalar:       %.3f ms/frame (%.1f FPS)\n", scalar_avg, 1000.0 / scalar_avg);

    // Test enhanced NEON version
    start = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
        image_t *img = image_new(width, height);
        if (!img) {
            log_error("Failed to allocate image");
            exit(1);
        }
        img->pixels = test_pixels;
        img->w = width;
        img->h = height;
        render_ascii_image_monochrome_neon(img);
        free(img);
    }
    double neon_time = get_time_seconds() - start;
    double neon_avg = (neon_time * 1000.0) / iterations;

    printf("Enhanced NEON: %.3f ms/frame (%.1f FPS)\n", neon_avg, 1000.0 / neon_avg);

    // Calculate speedup
    double speedup = scalar_time / neon_time;
    printf("\nSpeedup: %.2fx %s\n", speedup, speedup > 1.0 ? "🚀" : "❌");

    // Verify correctness - test a few pixels
    convert_pixels_scalar(test_pixels, output_buffer, 16);
    char scalar_result[17];
    memcpy(scalar_result, output_buffer, 16);
    scalar_result[16] = '\0';

    image_t *img = image_new(width, height);
    if (!img) {
      log_error("Failed to allocate image");
      exit(1);
    }
    img->pixels = test_pixels;
    img->w = width;
    img->h = height;
    char *neon_result = render_ascii_image_monochrome_neon(img);
    if (!neon_result) {
        log_error("Failed to render ASCII image");
        exit(1);
    }
    free(img);
    memcpy(neon_result, output_buffer, 16);
    neon_result[16] = '\0';

    printf("\nCorrectness check:\n");
    printf("Scalar: %s\n", scalar_result);
    printf("NEON:   %s\n", neon_result);
    printf("Match:  %s\n", strcmp(scalar_result, neon_result) == 0 ? "✅" : "❌");

    free(test_pixels);
    free(output_buffer);
    return 0;
}
