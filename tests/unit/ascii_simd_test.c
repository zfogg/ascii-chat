#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>

#include "common.h"
#include "ascii_simd.h"
#include "ascii_simd_color.h"

void setup_simd_quiet_logging(void);
void restore_simd_logging(void);

TestSuite(ascii_simd, .init = setup_simd_quiet_logging, .fini = restore_simd_logging);

void setup_simd_quiet_logging(void) {
    log_set_level(LOG_FATAL);
}

void restore_simd_logging(void) {
    log_set_level(LOG_DEBUG);
}

// =============================================================================
// SIMD vs Scalar Consistency Tests
// =============================================================================

Test(ascii_simd, neon_vs_scalar_consistency_basic) {
    const int width = 16, height = 4; // Size divisible by SIMD width
    rgb_pixel_t test_pixels[width * height];

    // Create test pattern with known values
    for (int i = 0; i < width * height; i++) {
        test_pixels[i] = (rgb_pixel_t){
            .r = (i * 3) % 256,
            .g = (i * 5) % 256,
            .b = (i * 7) % 256
        };
    }

    char scalar_output[10000];
    char neon_output[10000];

    // Test foreground mode
    size_t scalar_len = ascii_simd_color_scalar(test_pixels, width * height,
                                               scalar_output, sizeof(scalar_output), false, false);
    size_t neon_len = ascii_simd_color_neon(test_pixels, width * height,
                                           neon_output, sizeof(neon_output), false, false);

    cr_assert_gt(scalar_len, 0, "Scalar conversion should produce output");
    cr_assert_gt(neon_len, 0, "NEON conversion should produce output");
    cr_assert_eq(scalar_len, neon_len, "Scalar and NEON should produce same length output");
    cr_assert_str_eq(scalar_output, neon_output, "Scalar and NEON should produce identical output");
}

Test(ascii_simd, neon_vs_scalar_consistency_background) {
    const int width = 32, height = 8;
    rgb_pixel_t test_pixels[width * height];

    // Create gradient test pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_pixels[idx] = (rgb_pixel_t){
                .r = (x * 255) / (width - 1),
                .g = (y * 255) / (height - 1),
                .b = ((x + y) * 255) / (width + height - 2)
            };
        }
    }

    char scalar_output[20000];
    char neon_output[20000];

    // Test background mode
    size_t scalar_len = ascii_simd_color_scalar(test_pixels, width * height,
                                               scalar_output, sizeof(scalar_output), true, false);
    size_t neon_len = ascii_simd_color_neon(test_pixels, width * height,
                                           neon_output, sizeof(neon_output), true, false);

    cr_assert_gt(scalar_len, 0, "Scalar background conversion should produce output");
    cr_assert_gt(neon_len, 0, "NEON background conversion should produce output");
    cr_assert_eq(scalar_len, neon_len, "Background mode: lengths should match");
    cr_assert_str_eq(scalar_output, neon_output, "Background mode: output should match");
}

Test(ascii_simd, different_sizes_consistency) {
    // Test various sizes that may not align perfectly with SIMD width
    int test_sizes[] = {1, 3, 7, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 123};
    int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
        int size = test_sizes[i];
        rgb_pixel_t *pixels;
        SAFE_MALLOC(pixels, size * sizeof(rgb_pixel_t), rgb_pixel_t*);

        // Fill with deterministic pattern
        for (int j = 0; j < size; j++) {
            pixels[j] = (rgb_pixel_t){
                .r = (j * 17) % 256,
                .g = (j * 23) % 256,
                .b = (j * 31) % 256
            };
        }

        char scalar_output[50000];
        char neon_output[50000];

        size_t scalar_len = ascii_simd_color_scalar(pixels, size, scalar_output, sizeof(scalar_output), false, false);
        size_t neon_len = ascii_simd_color_neon(pixels, size, neon_output, sizeof(neon_output), false, false);

        cr_assert_eq(scalar_len, neon_len, "Size %d: lengths should match", size);
        cr_assert_str_eq(scalar_output, neon_output, "Size %d: outputs should match", size);

        free(pixels);
    }
}

// =============================================================================
// Edge Cases and Error Handling
// =============================================================================

Test(ascii_simd, edge_cases_null_inputs) {
    char output[1000];

    // Test NULL pixel input
    size_t result = ascii_simd_color_scalar(NULL, 10, output, sizeof(output), false, false);
    cr_assert_eq(result, 0, "Scalar should handle NULL pixel input gracefully");

    result = ascii_simd_color_neon(NULL, 10, output, sizeof(output), false, false);
    cr_assert_eq(result, 0, "NEON should handle NULL pixel input gracefully");

    // Test NULL output buffer
    rgb_pixel_t pixels[10] = {{255, 255, 255}};
    result = ascii_simd_color_scalar(pixels, 10, NULL, 1000, false, false);
    cr_assert_eq(result, 0, "Scalar should handle NULL output buffer gracefully");

    result = ascii_simd_color_neon(pixels, 10, NULL, 1000, false, false);
    cr_assert_eq(result, 0, "NEON should handle NULL output buffer gracefully");
}

Test(ascii_simd, edge_cases_zero_pixels) {
    rgb_pixel_t pixels[1] = {{128, 128, 128}};
    char output[1000];

    // Test with zero pixel count
    size_t result = ascii_simd_color_scalar(pixels, 0, output, sizeof(output), false, false);
    cr_assert_eq(result, 0, "Scalar should handle zero pixels gracefully");

    result = ascii_simd_color_neon(pixels, 0, output, sizeof(output), false, false);
    cr_assert_eq(result, 0, "NEON should handle zero pixels gracefully");
}

Test(ascii_simd, edge_cases_buffer_too_small) {
    rgb_pixel_t pixels[100];
    for (int i = 0; i < 100; i++) {
        pixels[i] = (rgb_pixel_t){255, 255, 255}; // Bright pixels = lots of ANSI codes
    }

    char small_buffer[10]; // Intentionally too small

    size_t scalar_result = ascii_simd_color_scalar(pixels, 100, small_buffer, sizeof(small_buffer), false, false);
    size_t neon_result = ascii_simd_color_neon(pixels, 100, small_buffer, sizeof(small_buffer), false, false);

    // Both should either return 0 (failure) or return safely without overflow
    cr_assert_leq(scalar_result, sizeof(small_buffer), "Scalar should not overflow buffer");
    cr_assert_leq(neon_result, sizeof(small_buffer), "NEON should not overflow buffer");
    cr_assert_eq(scalar_result, neon_result, "Both should handle buffer overflow identically");
}

Test(ascii_simd, extreme_color_values) {
    // Test with extreme color combinations
    rgb_pixel_t extreme_pixels[] = {
        {0, 0, 0},         // Pure black
        {255, 255, 255},   // Pure white
        {255, 0, 0},       // Pure red
        {0, 255, 0},       // Pure green
        {0, 0, 255},       // Pure blue
        {255, 255, 0},     // Yellow
        {255, 0, 255},     // Magenta
        {0, 255, 255},     // Cyan
        {128, 128, 128},   // Middle gray
        {64, 192, 96}      // Random color
    };

    int num_pixels = sizeof(extreme_pixels) / sizeof(extreme_pixels[0]);
    char scalar_output[5000];
    char neon_output[5000];

    size_t scalar_len = ascii_simd_color_scalar(extreme_pixels, num_pixels,
                                               scalar_output, sizeof(scalar_output), false, false);
    size_t neon_len = ascii_simd_color_neon(extreme_pixels, num_pixels,
                                           neon_output, sizeof(neon_output), false, false);

    cr_assert_eq(scalar_len, neon_len, "Extreme colors: lengths should match");
    cr_assert_str_eq(scalar_output, neon_output, "Extreme colors: outputs should match");
}

// =============================================================================
// ASCII Character Tests
// =============================================================================

Test(ascii_simd, ascii_character_selection) {
    // Test that brighter pixels get denser ASCII characters
    rgb_pixel_t dark_pixel = {32, 32, 32};    // Should get lighter ASCII char
    rgb_pixel_t bright_pixel = {224, 224, 224}; // Should get denser ASCII char

    char dark_output[100];
    char bright_output[100];

    ascii_simd_color_scalar(&dark_pixel, 1, dark_output, sizeof(dark_output), false, false);
    ascii_simd_color_scalar(&bright_pixel, 1, bright_output, sizeof(bright_output), false, false);

    // Find the actual ASCII character in the output (skip ANSI codes)
    char dark_char = extract_ascii_char_from_ansi(dark_output);
    char bright_char = extract_ascii_char_from_ansi(bright_output);

    // Verify that brighter pixel gets denser character
    int dark_density = get_ascii_char_density(dark_char);
    int bright_density = get_ascii_char_density(bright_char);

    cr_assert_lt(dark_density, bright_density,
                 "Brighter pixels should produce denser ASCII characters");
}

Test(ascii_simd, color_accuracy) {
    // Test specific color values produce expected ANSI codes
    rgb_pixel_t red_pixel = {255, 0, 0};
    rgb_pixel_t green_pixel = {0, 255, 0};
    rgb_pixel_t blue_pixel = {0, 0, 255};

    char red_output[200];
    char green_output[200];
    char blue_output[200];

    ascii_simd_color_scalar(&red_pixel, 1, red_output, sizeof(red_output), false, false);
    ascii_simd_color_scalar(&green_pixel, 1, green_output, sizeof(green_output), false, false);
    ascii_simd_color_scalar(&blue_pixel, 1, blue_output, sizeof(blue_output), false, false);

    // Verify outputs contain appropriate color codes
    cr_assert(strstr(red_output, "255;0;0") != NULL, "Red pixel should contain red ANSI code");
    cr_assert(strstr(green_output, "0;255;0") != NULL, "Green pixel should contain green ANSI code");
    cr_assert(strstr(blue_output, "0;0;255") != NULL, "Blue pixel should contain blue ANSI code");
}

// =============================================================================
// Performance Tests
// =============================================================================

Test(ascii_simd, performance_benchmark) {
    const int width = 320, height = 240;
    const int iterations = 10;
    rgb_pixel_t *pixels;

    SAFE_MALLOC(pixels, width * height * sizeof(rgb_pixel_t), rgb_pixel_t*);

    // Fill with realistic image-like data
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            // Create a gradient with some noise
            pixels[idx] = (rgb_pixel_t){
                .r = ((x * 255) / width + (rand() % 32)) % 256,
                .g = ((y * 255) / height + (rand() % 32)) % 256,
                .b = (((x + y) * 255) / (width + height) + (rand() % 32)) % 256
            };
        }
    }

    char output[width * height * 25]; // Large buffer for ANSI codes

    // Benchmark scalar implementation
    clock_t start_scalar = clock();
    for (int i = 0; i < iterations; i++) {
        ascii_simd_color_scalar(pixels, width * height, output, sizeof(output), false, false);
    }
    clock_t end_scalar = clock();
    double scalar_time = ((double)(end_scalar - start_scalar)) / CLOCKS_PER_SEC;

    // Benchmark NEON implementation
    clock_t start_neon = clock();
    for (int i = 0; i < iterations; i++) {
        ascii_simd_color_neon(pixels, width * height, output, sizeof(output), false, false);
    }
    clock_t end_neon = clock();
    double neon_time = ((double)(end_neon - start_neon)) / CLOCKS_PER_SEC;

    // Log performance results
    double speedup = scalar_time / neon_time;
    log_info("Performance (%dx%d, %d iterations): Scalar=%.3fs, NEON=%.3fs, Speedup=%.2fx",
             width, height, iterations, scalar_time, neon_time, speedup);

    // NEON should be at least as fast as scalar (though exact speedup depends on many factors)
    cr_assert_gt(speedup, 0.5, "NEON should not be significantly slower than scalar");

    free(pixels);
}

Test(ascii_simd, memory_access_patterns) {
    // Test that SIMD implementation handles different memory layouts correctly
    const int size = 64;
    rgb_pixel_t *pixels;
    SAFE_MALLOC(pixels, size * sizeof(rgb_pixel_t), rgb_pixel_t*);

    // Test aligned memory access
    for (int i = 0; i < size; i++) {
        pixels[i] = (rgb_pixel_t){i % 256, (i * 2) % 256, (i * 3) % 256};
    }

    char aligned_output[5000];
    size_t aligned_len = ascii_simd_color_neon(pixels, size, aligned_output, sizeof(aligned_output), false, false);
    cr_assert_gt(aligned_len, 0, "NEON should handle aligned memory");

    // Test potentially unaligned access (offset by 1 pixel)
    if (size > 1) {
        char unaligned_output[5000];
        size_t unaligned_len = ascii_simd_color_neon(pixels + 1, size - 1,
                                                     unaligned_output, sizeof(unaligned_output), false, false);
        cr_assert_gt(unaligned_len, 0, "NEON should handle potentially unaligned memory");
    }

    free(pixels);
}

// =============================================================================
// Property-Based Tests
// =============================================================================

Test(ascii_simd, property_output_always_valid) {
    srand(42); // Deterministic randomness for test repeatability

    for (int test = 0; test < 100; test++) {
        // Generate random pixel data
        int pixel_count = (rand() % 100) + 1; // 1-100 pixels
        rgb_pixel_t *pixels;
        SAFE_MALLOC(pixels, pixel_count * sizeof(rgb_pixel_t), rgb_pixel_t*);

        for (int i = 0; i < pixel_count; i++) {
            pixels[i] = (rgb_pixel_t){
                .r = rand() % 256,
                .g = rand() % 256,
                .b = rand() % 256
            };
        }

        char output[10000];
        size_t len = ascii_simd_color_neon(pixels, pixel_count, output, sizeof(output), false, false);

        // Properties that should always hold:
        cr_assert_gt(len, 0, "Test %d: Should always produce output for valid input", test);
        cr_assert_lt(len, sizeof(output), "Test %d: Should not overflow buffer", test);

        // Output should be null-terminated
        cr_assert_eq(output[len], '\0', "Test %d: Output should be null-terminated", test);

        // Output should contain printable ASCII characters and ANSI escape sequences
        for (size_t i = 0; i < len; i++) {
            char c = output[i];
            cr_assert(c >= 32 || c == '\n' || c == '\033',
                     "Test %d: Character at position %zu should be printable or control", test, i);
        }

        free(pixels);
    }
}

Test(ascii_simd, property_deterministic_output) {
    // Same input should always produce same output
    const int size = 50;
    rgb_pixel_t pixels[size];

    // Create fixed test pattern
    for (int i = 0; i < size; i++) {
        pixels[i] = (rgb_pixel_t){100 + i, 150 - i, 200};
    }

    char output1[5000], output2[5000];

    // Generate output twice
    size_t len1 = ascii_simd_color_neon(pixels, size, output1, sizeof(output1), false, false);
    size_t len2 = ascii_simd_color_neon(pixels, size, output2, sizeof(output2), false, false);

    cr_assert_eq(len1, len2, "Deterministic: lengths should match");
    cr_assert_str_eq(output1, output2, "Deterministic: outputs should be identical");
}

// =============================================================================
// Helper Functions for Tests
// =============================================================================

char extract_ascii_char_from_ansi(const char *ansi_output) {
    // Skip ANSI escape sequences and find the actual ASCII character
    const char *ptr = ansi_output;
    while (*ptr) {
        if (*ptr == '\033') {
            // Skip ANSI sequence
            while (*ptr && *ptr != 'm') ptr++;
            if (*ptr) ptr++; // Skip the 'm'
        } else if (*ptr >= 32 && *ptr <= 126) {
            // Found printable ASCII character
            return *ptr;
        } else {
            ptr++;
        }
    }
    return ' '; // Default to space if no character found
}

int get_ascii_char_density(char c) {
    // Return density score for ASCII characters (higher = denser)
    switch (c) {
        case ' ': return 0;
        case '.': case ',': case '\'': case '`': return 1;
        case '-': case '_': case '~': case '^': return 2;
        case ':': case ';': case '!': case '|': return 3;
        case '+': case '=': case '<': case '>': return 4;
        case '*': case 'o': case 'O': case 'x': return 5;
        case '#': case '@': case '&': case '%': return 6;
        case '█': return 10; // Block character (highest density)
        default: return 3; // Default medium density
    }
}
