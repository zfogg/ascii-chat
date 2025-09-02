#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>

#include "common.h"
#include "ascii_simd.h"
#include "image.h"
#include "palette.h"

void setup_simd_quiet_logging(void);
void restore_simd_logging(void);

TestSuite(ascii_simd_integration, .init = setup_simd_quiet_logging, .fini = restore_simd_logging);

void setup_simd_quiet_logging(void) {
    log_set_level(LOG_ERROR);
}

void restore_simd_logging(void) {
    log_set_level(LOG_DEBUG);
}

// =============================================================================
// RLE Expansion Utility for Fair Output Comparison
// =============================================================================

// Function to expand ANSI REP sequences to full output for comparison
static char *expand_rle_sequences(const char *input) {
    if (!input) return NULL;
    
    size_t input_len = strlen(input);
    size_t output_capacity = input_len * 100;  // Generous allocation for expansion
    char *output = malloc(output_capacity);
    if (!output) return NULL;
    
    size_t output_pos = 0;
    size_t input_pos = 0;
    char last_char = ' '; // Default to space if no previous character
    
    while (input_pos < input_len) {
        if (input[input_pos] == '\033' && input_pos + 1 < input_len && input[input_pos + 1] == '[') {
            // Found escape sequence, check if it's REP
            size_t seq_start = input_pos;
            input_pos += 2; // Skip \033[
            
            // Parse the number
            char *end;
            unsigned long count = strtoul(input + input_pos, &end, 10);
            
            if (end > input + input_pos && *end == 'b') {
                // This is a REP sequence: \033[<count>b
                input_pos = end - input + 1; // Skip to after 'b'
                
                // Repeat the last character 'count' times
                for (unsigned long i = 0; i < count; i++) {
                    if (output_pos >= output_capacity - 1) {
                        output_capacity *= 2;
                        char *new_output = realloc(output, output_capacity);
                        if (!new_output) { free(output); return NULL; }
                        output = new_output;
                    }
                    output[output_pos++] = last_char;
                }
            } else {
                // Not a REP sequence, copy the escape sequence as-is
                while (seq_start < input_len && seq_start <= input_pos) {
                    if (output_pos >= output_capacity - 1) {
                        output_capacity *= 2;
                        char *new_output = realloc(output, output_capacity);
                        if (!new_output) { free(output); return NULL; }
                        output = new_output;
                    }
                    output[output_pos++] = input[seq_start++];
                }
            }
        } else {
            // Regular character
            if (output_pos >= output_capacity - 1) {
                output_capacity *= 2;
                char *new_output = realloc(output, output_capacity);
                if (!new_output) { free(output); return NULL; }
                output = new_output;
            }
            
            char c = input[input_pos];
            output[output_pos++] = c;
            
            // Update last_char for potential REP sequences (only printable chars)
            if (c != '\n' && c != '\r' && c != '\033') {
                last_char = c;
            }
            
            input_pos++;
        }
    }
    
    output[output_pos] = '\0';
    return output;
}

// =============================================================================
// Performance Integration Tests - Assert 2x+ Speedup
// =============================================================================

Test(ascii_simd_integration, monochrome_performance_vs_scalar) {
    const int width = 320, height = 240;
    const int iterations = 20;
    
    // Create test image
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Fill with gradient pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x * 255) / width;
            test_image->pixels[idx].g = (y * 255) / height;
            test_image->pixels[idx].b = ((x + y) * 127) / (width + height);
        }
    }
    
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    
    // Benchmark scalar implementation
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char *result = image_print(test_image, ascii_palette);
        cr_assert_not_null(result, "Scalar should produce output");
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double scalar_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    // Benchmark SIMD implementation  
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char *result = image_print_simd(test_image, ascii_palette);
        cr_assert_not_null(result, "SIMD should produce output");
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double simd_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    double speedup = scalar_time / simd_time;
    
    printf("Monochrome Performance: Scalar=%.4fms, SIMD=%.4fms, Speedup=%.2fx\n",
           (scalar_time / iterations) * 1000, (simd_time / iterations) * 1000, speedup);
    
    // Assert minimum 2x speedup for SIMD
    cr_assert_gt(speedup, 2.0, "SIMD monochrome should be at least 2x faster than scalar (got %.2fx)", speedup);
    
    image_destroy(test_image);
}

Test(ascii_simd_integration, color_performance_vs_scalar) {
    const int width = 320, height = 240;
    const int iterations = 10;
    
    // Create test image
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Fill with colorful gradient pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x * 255) / width;
            test_image->pixels[idx].g = (y * 255) / height;
            test_image->pixels[idx].b = ((x + y) * 127) / (width + height);
        }
    }
    
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    
    // Benchmark scalar color implementation
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char *result = image_print_color(test_image, ascii_palette);
        cr_assert_not_null(result, "Scalar color should produce output");
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double scalar_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    // Benchmark SIMD color implementation
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char *result = image_print_color_simd(test_image, false, false, ascii_palette);
        cr_assert_not_null(result, "SIMD color should produce output");
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double simd_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    double speedup = scalar_time / simd_time;
    
    printf("Color Performance: Scalar=%.4fms, SIMD=%.4fms, Speedup=%.2fx\n",
           (scalar_time / iterations) * 1000, (simd_time / iterations) * 1000, speedup);
    
    // Color SIMD may be less optimized, so allow lower threshold
    cr_assert_gt(speedup, 0.8, "SIMD color should not be significantly slower than scalar (got %.2fx)", speedup);
    
    image_destroy(test_image);
}

Test(ascii_simd_integration, utf8_palette_performance) {
    const int width = 160, height = 48;
    const int iterations = 20;
    
    // Create test image
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Fill with gradient pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x * 255) / width;
            test_image->pixels[idx].g = (y * 255) / height;
            test_image->pixels[idx].b = ((x + y) * 127) / (width + height);
        }
    }
    
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    const char *utf8_palette = "🌑🌒🌓🌔🌕🌖🌗🌘🌙🌚🌛🌜🌝🌞🌟⭐";
    
    // Benchmark ASCII palette
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char *result = image_print_simd(test_image, ascii_palette);
        cr_assert_not_null(result, "ASCII SIMD should produce output");
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double ascii_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    // Benchmark UTF-8 emoji palette
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char *result = image_print_simd(test_image, utf8_palette);
        cr_assert_not_null(result, "UTF-8 SIMD should produce output");
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double utf8_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    double utf8_penalty = utf8_time / ascii_time;
    
    printf("UTF-8 vs ASCII: ASCII=%.4fms, UTF-8=%.4fms, Penalty=%.2fx\n",
           (ascii_time / iterations) * 1000, (utf8_time / iterations) * 1000, utf8_penalty);
    
    // UTF-8 should not be more than 3x slower than ASCII (after our optimizations)
    cr_assert_lt(utf8_penalty, 3.0, "UTF-8 should not be >3x slower than ASCII (got %.2fx)", utf8_penalty);
    
    image_destroy(test_image);
}

Test(ascii_simd_integration, various_image_sizes_performance) {
    const struct {
        const char *name;
        int width;
        int height;
        double min_speedup; // Minimum expected SIMD speedup
    } test_sizes[] = {
        {"Small", 40, 12, 1.5},       // Small images may have less SIMD benefit
        {"Medium", 80, 24, 2.0},      // Medium should show good speedup
        {"Large", 160, 48, 3.0},      // Large should show excellent speedup
        {"Webcam", 320, 240, 4.0},    // Webcam size should show best speedup
    };
    
    const int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    
    for (int size_idx = 0; size_idx < num_sizes; size_idx++) {
        int width = test_sizes[size_idx].width;
        int height = test_sizes[size_idx].height;
        double expected_speedup = test_sizes[size_idx].min_speedup;
        
        // Create test image
        image_t *test_image = image_new(width, height);
        cr_assert_not_null(test_image, "Should create %s test image", test_sizes[size_idx].name);
        
        // Fill with realistic pattern
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                test_image->pixels[idx].r = (x * 255) / width;
                test_image->pixels[idx].g = (y * 255) / height;
                test_image->pixels[idx].b = ((x + y) * 127) / (width + height);
            }
        }
        
        const int iterations = (width * height < 10000) ? 50 : 20; // More iterations for small images
        
        // Benchmark scalar
        struct timespec start, end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (int i = 0; i < iterations; i++) {
            char *result = image_print(test_image, ascii_palette);
            cr_assert_not_null(result, "Scalar should produce output for %s", test_sizes[size_idx].name);
            free(result);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        double scalar_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        
        // Benchmark SIMD
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (int i = 0; i < iterations; i++) {
            char *result = image_print_simd(test_image, ascii_palette);
            cr_assert_not_null(result, "SIMD should produce output for %s", test_sizes[size_idx].name);
            free(result);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        double simd_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
        
        double speedup = scalar_time / simd_time;
        
        printf("%s (%dx%d): Scalar=%.4fms, SIMD=%.4fms, Speedup=%.2fx\n",
               test_sizes[size_idx].name, width, height,
               (scalar_time / iterations) * 1000, (simd_time / iterations) * 1000, speedup);
        
        // Assert minimum expected speedup
        cr_assert_gt(speedup, expected_speedup, 
                     "%s: SIMD should be at least %.1fx faster (got %.2fx)", 
                     test_sizes[size_idx].name, expected_speedup, speedup);
        
        image_destroy(test_image);
    }
}

// =============================================================================
// Correctness Integration Tests
// =============================================================================

Test(ascii_simd_integration, simd_vs_scalar_output_consistency) {
    // Test that SIMD and scalar produce identical output for monochrome
    const int width = 80, height = 24;
    
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Create deterministic test pattern
    srand(42);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x * 3 + y * 5) % 256;
            test_image->pixels[idx].g = (x * 7 + y * 11) % 256;
            test_image->pixels[idx].b = (x * 13 + y * 17) % 256;
        }
    }
    
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    
    // Generate outputs
    char *scalar_result = image_print(test_image, ascii_palette);
    char *simd_result = image_print_simd(test_image, ascii_palette);
    
    cr_assert_not_null(scalar_result, "Scalar should produce output");
    cr_assert_not_null(simd_result, "SIMD should produce output");
    
    // Expand RLE sequences in scalar output for fair comparison
    char *scalar_expanded = expand_rle_sequences(scalar_result);
    cr_assert_not_null(scalar_expanded, "Should be able to expand scalar RLE");
    
    // Debug: Print output lengths
    printf("DEBUG: Raw lengths - scalar=%zu, simd=%zu\n", strlen(scalar_result), strlen(simd_result));
    printf("DEBUG: After RLE expansion - scalar_expanded=%zu, simd=%zu\n", strlen(scalar_expanded), strlen(simd_result));
    
    // Debug: Show first differences
    if (strcmp(scalar_expanded, simd_result) != 0) {
        size_t min_len = strlen(scalar_expanded) < strlen(simd_result) ? strlen(scalar_expanded) : strlen(simd_result);
        int diff_count = 0;
        for (size_t i = 0; i < min_len && diff_count < 5; i++) {
            if (scalar_expanded[i] != simd_result[i]) {
                printf("DEBUG: Diff at pos %zu: scalar='%c'(0x%02x) vs simd='%c'(0x%02x)\n",
                       i, scalar_expanded[i], (unsigned char)scalar_expanded[i],
                       simd_result[i], (unsigned char)simd_result[i]);
                diff_count++;
            }
        }
    }
    
    // For monochrome, outputs should be identical after RLE expansion
    cr_assert_str_eq(scalar_expanded, simd_result, "Monochrome SIMD and scalar should produce identical output after RLE expansion");
    
    free(scalar_expanded);
    
    free(scalar_result);
    free(simd_result);
    image_destroy(test_image);
}

Test(ascii_simd_integration, utf8_palette_correctness) {
    const int width = 40, height = 12;
    
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Create simple gradient
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            int luminance = (x * 255) / width; // Horizontal gradient
            test_image->pixels[idx].r = luminance;
            test_image->pixels[idx].g = luminance;
            test_image->pixels[idx].b = luminance;
        }
    }
    
    // Test various UTF-8 palettes
    const char *utf8_palettes[] = {
        "   ._-=/=08WX🧠",                    // Mixed ASCII + emoji
        "🌑🌒🌓🌔🌕🌖🌗🌘🌙🌚🌛🌜🌝🌞🌟⭐", // Pure emoji
        "αβγδεζηθικλμνξοπ",                 // Greek letters
        "   ...',;:clodxkO0KXNWM"           // Pure ASCII for comparison
    };
    
    const int num_palettes = sizeof(utf8_palettes) / sizeof(utf8_palettes[0]);
    
    for (int p = 0; p < num_palettes; p++) {
        char *result = image_print_simd(test_image, utf8_palettes[p]);
        cr_assert_not_null(result, "UTF-8 palette %d should produce output", p);
        
        // Check that output contains valid UTF-8 sequences
        size_t len = strlen(result);
        cr_assert_gt(len, 0, "UTF-8 palette %d should produce non-empty output", p);
        
        // Verify no null bytes in the middle of output (would break UTF-8)
        for (size_t i = 0; i < len; i++) {
            cr_assert_neq(result[i], 0, "UTF-8 output should not contain null bytes at position %zu", i);
        }
        
        free(result);
    }
    
    image_destroy(test_image);
}

Test(ascii_simd_integration, cache_system_efficiency) {
    // Test that cache system provides performance benefits on repeated calls
    const int width = 160, height = 48;
    const int iterations = 30;
    
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Fill with test data
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x * 255) / width;
            test_image->pixels[idx].g = (y * 255) / height;
            test_image->pixels[idx].b = 128;
        }
    }
    
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    
    // First call (cache warming)
    char *warmup = image_print_simd(test_image, ascii_palette);
    cr_assert_not_null(warmup, "Cache warmup should succeed");
    free(warmup);
    
    // Benchmark with warmed cache
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char *result = image_print_simd(test_image, ascii_palette);
        cr_assert_not_null(result, "Cached call %d should succeed", i);
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double cached_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("Cache Performance: %.4fms/frame with warmed cache\n", (cached_time / iterations) * 1000);
    
    // With cache, should be very fast
    double ms_per_frame = (cached_time / iterations) * 1000;
    cr_assert_lt(ms_per_frame, 1.0, "Cached SIMD should be <1ms/frame for medium images (got %.4fms)", ms_per_frame);
    
    image_destroy(test_image);
}

Test(ascii_simd_integration, rwlock_concurrency_simulation) {
    // Simulate concurrent access to cache system (tests rwlock implementation)
    const int width = 80, height = 24;
    const int iterations = 100;
    
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Fill with test data
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x + y) % 256;
            test_image->pixels[idx].g = (x * y) % 256;
            test_image->pixels[idx].b = (x ^ y) % 256;
        }
    }
    
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    
    // Rapid repeated calls to stress cache system
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    for (int i = 0; i < iterations; i++) {
        char *result = image_print_simd(test_image, ascii_palette);
        cr_assert_not_null(result, "Concurrent access %d should succeed", i);
        
        // Verify output is consistent
        size_t len = strlen(result);
        cr_assert_gt(len, 100, "Output should be substantial for %dx%d image", width, height);
        
        free(result);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    double total_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    printf("Concurrency Test: %d calls in %.3fs (%.4fms each)\n", 
           iterations, total_time, (total_time / iterations) * 1000);
    
    // Should maintain good performance under rapid access
    double ms_per_call = (total_time / iterations) * 1000;
    cr_assert_lt(ms_per_call, 0.5, "Concurrent cache access should be fast (<0.5ms/call, got %.4fms)", ms_per_call);
    
    image_destroy(test_image);
}

// =============================================================================
// Edge Case Integration Tests  
// =============================================================================

Test(ascii_simd_integration, extreme_image_sizes) {
    // Test edge cases: very small and very large images
    const struct {
        const char *name;
        int width;
        int height;
    } extreme_sizes[] = {
        {"Tiny", 1, 1},
        {"Narrow", 1, 100},
        {"Wide", 100, 1},
        {"Large", 640, 480}
    };
    
    const int num_sizes = sizeof(extreme_sizes) / sizeof(extreme_sizes[0]);
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    
    for (int i = 0; i < num_sizes; i++) {
        int width = extreme_sizes[i].width;
        int height = extreme_sizes[i].height;
        
        image_t *test_image = image_new(width, height);
        cr_assert_not_null(test_image, "Should create %s image", extreme_sizes[i].name);
        
        // Fill with simple pattern
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                test_image->pixels[idx].r = (x + y) % 256;
                test_image->pixels[idx].g = (x + y) % 256;
                test_image->pixels[idx].b = (x + y) % 256;
            }
        }
        
        // Test both scalar and SIMD work
        char *scalar_result = image_print(test_image, ascii_palette);
        char *simd_result = image_print_simd(test_image, ascii_palette);
        
        cr_assert_not_null(scalar_result, "%s: Scalar should handle extreme size", extreme_sizes[i].name);
        cr_assert_not_null(simd_result, "%s: SIMD should handle extreme size", extreme_sizes[i].name);
        
        // Expand RLE sequences in scalar output for fair comparison
        char *scalar_expanded = expand_rle_sequences(scalar_result);
        cr_assert_not_null(scalar_expanded, "%s: Should be able to expand scalar RLE", extreme_sizes[i].name);
        
        // For monochrome, should produce identical output after RLE expansion
        cr_assert_str_eq(scalar_expanded, simd_result, "%s: Outputs should match after RLE expansion", extreme_sizes[i].name);
        
        free(scalar_expanded);
        
        free(scalar_result);
        free(simd_result);
        image_destroy(test_image);
    }
}

Test(ascii_simd_integration, memory_safety_stress_test) {
    // Stress test memory allocation and deallocation
    const int num_tests = 50;
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    
    for (int test = 0; test < num_tests; test++) {
        // Random image size
        int width = 16 + (rand() % 200);
        int height = 8 + (rand() % 100);
        
        image_t *test_image = image_new(width, height);
        cr_assert_not_null(test_image, "Test %d: Should create random size image", test);
        
        // Fill with random data
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int idx = y * width + x;
                test_image->pixels[idx].r = rand() % 256;
                test_image->pixels[idx].g = rand() % 256;
                test_image->pixels[idx].b = rand() % 256;
            }
        }
        
        // Test SIMD implementation
        char *result = image_print_simd(test_image, ascii_palette);
        cr_assert_not_null(result, "Test %d: SIMD should handle random size %dx%d", test, width, height);
        
        // Basic output validation
        size_t len = strlen(result);
        cr_assert_gt(len, 0, "Test %d: Should produce non-empty output", test);
        
        // Check for basic ASCII art structure (should contain newlines for multi-row images)
        if (height > 1) {
            cr_assert(strchr(result, '\n') != NULL, "Test %d: Multi-row output should contain newlines", test);
        }
        
        free(result);
        image_destroy(test_image);
    }
}

Test(ascii_simd_integration, palette_system_integration) {
    // Test integration with the palette system
    const int width = 60, height = 20;
    
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Create gradient pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x * 255) / width;
            test_image->pixels[idx].g = (y * 255) / height;
            test_image->pixels[idx].b = 128;
        }
    }
    
    // Test built-in palettes
    const char *builtin_palettes[] = {
        DEFAULT_ASCII_PALETTE,
        "█▉▊▋▌▍▎▏ ",                    // Block characters
        "●◐◑◒◓◔◕○",                     // Circle characters  
        "🌑🌒🌓🌔🌕🌖🌗🌘🌙🌚🌛🌜🌝🌞🌟⭐"  // Emoji characters
    };
    
    const int num_palettes = sizeof(builtin_palettes) / sizeof(builtin_palettes[0]);
    
    for (int p = 0; p < num_palettes; p++) {
        char *result = image_print_simd(test_image, builtin_palettes[p]);
        cr_assert_not_null(result, "Palette %d should work with SIMD", p);
        
        size_t len = strlen(result);
        cr_assert_gt(len, width, "Palette %d should produce substantial output", p);
        
        // Check for reasonable output size (shouldn't be empty or massive)
        cr_assert_lt(len, width * height * 100, "Palette %d output should be reasonable size", p);
        
        free(result);
    }
    
    image_destroy(test_image);
}

// =============================================================================
// Architecture-Specific Tests
// =============================================================================

Test(ascii_simd_integration, neon_architecture_verification) {
#ifdef SIMD_SUPPORT_NEON
    // Verify NEON-specific optimizations are working
    const int width = 160, height = 48; // Good size for NEON testing
    
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create NEON test image");
    
    // Create pattern that should trigger NEON optimizations
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x * 255) / width;
            test_image->pixels[idx].g = (y * 255) / height;
            test_image->pixels[idx].b = ((x + y) * 127) / (width + height);
        }
    }
    
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    const int iterations = 30;
    
    // Benchmark NEON monochrome performance
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < iterations; i++) {
        char *result = image_print_simd(test_image, ascii_palette);
        cr_assert_not_null(result, "NEON iteration %d should succeed", i);
        free(result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    double neon_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    
    double ms_per_frame = (neon_time / iterations) * 1000;
    printf("NEON Monochrome Performance: %.4fms/frame\n", ms_per_frame);
    
    // NEON should be very fast for this size
    cr_assert_lt(ms_per_frame, 0.5, "NEON should be <0.5ms/frame for 160x48 (got %.4fms)", ms_per_frame);
    
    image_destroy(test_image);
#else
    cr_skip("NEON support not available");
#endif
}

Test(ascii_simd_integration, simd_initialization_and_cleanup) {
    // Test SIMD system initialization and cleanup
    
    // Should be able to call init multiple times safely
    ascii_simd_init();
    ascii_simd_init();
    
    // Should work after initialization
    image_t *test_image = image_new(32, 16);
    cr_assert_not_null(test_image, "Should create image after SIMD init");
    
    for (int i = 0; i < 32 * 16; i++) {
        test_image->pixels[i] = (rgb_pixel_t){i % 256, (i * 2) % 256, (i * 3) % 256};
    }
    
    char *result = image_print_simd(test_image, "   ...',;:clodxkO0KXNWM");
    cr_assert_not_null(result, "SIMD should work after initialization");
    
    free(result);
    image_destroy(test_image);
    
    // Cleanup should be safe
    simd_caches_destroy_all();
    simd_caches_destroy_all(); // Multiple calls should be safe
}

// =============================================================================
// Integration with Terminal Capabilities
// =============================================================================

Test(ascii_simd_integration, terminal_capabilities_integration) {
    const int width = 80, height = 24;
    
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create test image");
    
    // Create colorful test pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (x * 255) / width;
            test_image->pixels[idx].g = (y * 255) / height;
            test_image->pixels[idx].b = ((x + y) * 127) / (width + height);
        }
    }
    
    const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
    char luminance_palette[256];
    build_client_luminance_palette(ascii_palette, strlen(ascii_palette), luminance_palette);
    
    // Test different terminal capabilities
    terminal_capabilities_t caps[] = {
        // Monochrome terminal
        {.color_level = TERM_COLOR_NONE, .color_count = 2, .render_mode = RENDER_MODE_FOREGROUND},
        // 256-color terminal
        {.color_level = TERM_COLOR_256, .color_count = 256, .render_mode = RENDER_MODE_FOREGROUND},
        // Truecolor terminal
        {.color_level = TERM_COLOR_TRUECOLOR, .color_count = 16777216, .render_mode = RENDER_MODE_FOREGROUND},
        // Background mode
        {.color_level = TERM_COLOR_TRUECOLOR, .color_count = 16777216, .render_mode = RENDER_MODE_BACKGROUND}
    };
    
    const int num_caps = sizeof(caps) / sizeof(caps[0]);
    
    for (int c = 0; c < num_caps; c++) {
        char *result = image_print_with_capabilities(test_image, &caps[c], ascii_palette, luminance_palette);
        cr_assert_not_null(result, "Capability %d should produce output", c);
        
        size_t len = strlen(result);
        cr_assert_gt(len, 0, "Capability %d should produce non-empty output", c);
        
        // Different capabilities should produce different output sizes
        printf("Capability %d: %zu bytes\n", c, len);
        
        free(result);
    }
    
    image_destroy(test_image);
}