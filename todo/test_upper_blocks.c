#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "ascii_simd.h"
#include "common.h"
#include "image.h"

// Timing function
double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

int main() {
    printf("Upper Half Block Renderer (▀) Performance Test\n");
    printf("==============================================\n\n");
    
    // Create test image
    const int WIDTH = 320;
    const int HEIGHT = 240;
    const int ITERATIONS = 100;
    
    image_t *test_image;
    SAFE_MALLOC(test_image, sizeof(image_t), image_t *);
    test_image->w = WIDTH;
    test_image->h = HEIGHT;
    
    size_t pixel_count = WIDTH * HEIGHT;
    SAFE_MALLOC(test_image->pixels, pixel_count * sizeof(rgb_t), rgb_t *);
    
    // Fill with realistic webcam-like data
    srand(12345);
    for (size_t i = 0; i < pixel_count; i++) {
        test_image->pixels[i].r = 80 + (rand() % 120);   // Realistic skin tones
        test_image->pixels[i].g = 60 + (rand() % 100);   // Not pure random
        test_image->pixels[i].b = 40 + (rand() % 80);    // More natural colors
    }
    
    printf("Testing %dx%d webcam-like image (%zu pixels)\n", WIDTH, HEIGHT, pixel_count);
    printf("Iterations: %d\n\n", ITERATIONS);
    
    // Test 1: Normal ASCII (full height)
    printf("1. Normal ASCII Conversion (full height):\n");
    double start = get_time_ms();
    size_t normal_output_size = 0;
    
    for (int i = 0; i < ITERATIONS; i++) {
        char *result = image_print_colored_simd(test_image);
        if (result) {
            if (i == 0) normal_output_size = strlen(result);
            free(result);
        }
    }
    
    double normal_time = get_time_ms() - start;
    printf("   Time per frame: %.3f ms\n", normal_time / ITERATIONS);
    printf("   Terminal lines: %d\n", HEIGHT);
    printf("   Output size:    %zu KB\n", normal_output_size / 1024);
    printf("\n");
    
    // Test 2: Upper half block (half height) 
    printf("2. Upper Half Block Renderer (▀ - half height):\n");
    start = get_time_ms();
    size_t half_output_size = 0;
    
    for (int i = 0; i < ITERATIONS; i++) {
        char *result = image_print_half_height_blocks(test_image);
        if (result) {
            if (i == 0) half_output_size = strlen(result);
            free(result);
        }
    }
    
    double half_time = get_time_ms() - start;
    printf("   Time per frame: %.3f ms\n", half_time / ITERATIONS);
    printf("   Terminal lines: %d (50%% reduction!)\n", (HEIGHT + 1) / 2);
    printf("   Output size:    %zu KB\n", half_output_size / 1024);
    printf("\n");
    
    // Performance comparison
    printf("3. Performance Comparison:\n");
    double speedup = normal_time / half_time;
    printf("   Upper block speedup: %.2fx faster\n", speedup);
    printf("   Terminal I/O reduction: %.1f%%\n", (1.0 - (double)half_output_size / normal_output_size) * 100);
    
    if (speedup > 1.5) {
        printf("   🎉 EXCELLENT: %.2fx FPS boost for terminal video!\n", speedup);
    } else if (speedup > 1.2) {
        printf("   ✅ GOOD: %.2fx improvement\n", speedup);
    } else {
        printf("   ⚠️  Limited improvement - terminal I/O may not be the bottleneck\n");
    }
    printf("\n");
    
    // Visual comparison
    printf("4. Visual Comparison (first few lines):\n");
    printf("   Normal ASCII:\n");
    char *normal_sample = image_print_colored_simd(test_image);
    if (normal_sample) {
        char *line = strtok(normal_sample, "\n");
        for (int i = 0; i < 3 && line; i++) {
            printf("     %s\n", line);
            line = strtok(NULL, "\n");
        }
        free(normal_sample);
    }
    
    printf("\n   Upper Half Block (▀):\n");
    char *half_sample = image_print_half_height_blocks(test_image);
    if (half_sample) {
        char *line = strtok(half_sample, "\n");
        for (int i = 0; i < 3 && line; i++) {
            printf("     %s\n", line);
            line = strtok(NULL, "\n");
        }
        free(half_sample);
    }
    
    printf("\n5. Explanation:\n");
    printf("   ▀ = Unicode 'Upper Half Block' character (U+2580)\n");
    printf("   • Foreground color = TOP pixel color\n");
    printf("   • Background color = BOTTOM pixel color  \n");
    printf("   • Result: 2 pixels per terminal character vertically\n");
    printf("   • Perfect for terminal video - same quality, 2x density!\n");
    
    // Cleanup
    free(test_image->pixels);
    free(test_image);
    
    return 0;
}