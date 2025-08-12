#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "../lib/ascii_simd.h"
#include "../lib/common.h"

// Test colored ASCII correctness by comparing scalar vs SIMD implementations
int main() {
    log_init(NULL, LOG_ERROR);

    printf("=== Color ASCII Correctness Test ===\n\n");

    // Test parameters
    int test_width = 20;
    int test_height = 10;
    int pixel_count = test_width * test_height;

    // Buffer size for colored ASCII output (generous allocation)
    size_t buffer_size = pixel_count * 50; // ~50 bytes per pixel for ANSI codes

    // Create test data
    rgb_pixel_t *test_pixels;
    char *scalar_output;
    char *simd_output;

    SAFE_MALLOC(test_pixels, pixel_count * sizeof(rgb_pixel_t), rgb_pixel_t *);
    SAFE_MALLOC(scalar_output, buffer_size, char *);
    SAFE_MALLOC(simd_output, buffer_size, char *);

    // Generate diverse test pattern
    printf("Generating test pattern with %d pixels...\n", pixel_count);
    srand(42); // Consistent results
    for (int i = 0; i < pixel_count; i++) {
        // Create varied colors to stress test ANSI generation
        test_pixels[i].r = (i * 7) % 256;      // Red gradient
        test_pixels[i].g = (i * 11 + 85) % 256; // Green offset pattern
        test_pixels[i].b = (255 - i * 13) % 256; // Blue inverse pattern
    }

    printf("Testing both foreground and background modes...\n\n");

    // Test both modes
    bool modes[] = {false, true}; // foreground, background
    const char *mode_names[] = {"FOREGROUND", "BACKGROUND"};

    for (int mode_idx = 0; mode_idx < 2; mode_idx++) {
        bool background_mode = modes[mode_idx];
        printf("=== %s MODE ===\n", mode_names[mode_idx]);

        // Clear output buffers
        memset(scalar_output, 0, buffer_size);
        memset(simd_output, 0, buffer_size);

        // Generate scalar output
        size_t scalar_len = convert_row_with_color_scalar(
            test_pixels, scalar_output, buffer_size, pixel_count, background_mode);

        // Generate SIMD output
        size_t simd_len = convert_row_with_color_optimized(
            test_pixels, simd_output, buffer_size, pixel_count, background_mode);

        printf("Scalar output length: %zu bytes\n", scalar_len);
        printf("SIMD output length:   %zu bytes\n", simd_len);

        // Compare lengths
        if (scalar_len != simd_len) {
            printf("❌ LENGTH MISMATCH: Scalar=%zu, SIMD=%zu\n", scalar_len, simd_len);
        } else {
            printf("✅ Lengths match\n");
        }

        // Compare content byte by byte
        size_t min_len = (scalar_len < simd_len) ? scalar_len : simd_len;
        int mismatches = 0;
        int first_mismatch = -1;

        for (size_t i = 0; i < min_len; i++) {
            if (scalar_output[i] != simd_output[i]) {
                mismatches++;
                if (first_mismatch == -1) {
                    first_mismatch = i;
                }
                if (mismatches <= 5) { // Show first few mismatches
                    printf("❌ Byte %zu: scalar=0x%02x('%c'), simd=0x%02x('%c')\n",
                           i,
                           (unsigned char)scalar_output[i],
                           (scalar_output[i] >= 32 && scalar_output[i] <= 126) ? scalar_output[i] : '.',
                           (unsigned char)simd_output[i],
                           (simd_output[i] >= 32 && simd_output[i] <= 126) ? simd_output[i] : '.');
                }
            }
        }

        if (mismatches == 0) {
            printf("✅ All %zu bytes match perfectly!\n", min_len);
        } else {
            printf("❌ %d byte mismatches found (first at byte %d)\n", mismatches, first_mismatch);

            // Show context around first mismatch
            if (first_mismatch >= 0) {
                printf("\nContext around first mismatch (byte %d):\n", first_mismatch);
                int start = (first_mismatch > 20) ? first_mismatch - 20 : 0;
                int end = (first_mismatch + 20 < (int)min_len) ? first_mismatch + 20 : (int)min_len;

                printf("Scalar:  ");
                for (int i = start; i < end; i++) {
                    if (i == first_mismatch) printf("[");
                    printf("%c", (scalar_output[i] >= 32 && scalar_output[i] <= 126) ? scalar_output[i] : '.');
                    if (i == first_mismatch) printf("]");
                }
                printf("\n");

                printf("SIMD:    ");
                for (int i = start; i < end; i++) {
                    if (i == first_mismatch) printf("[");
                    printf("%c", (simd_output[i] >= 32 && simd_output[i] <= 126) ? simd_output[i] : '.');
                    if (i == first_mismatch) printf("]");
                }
                printf("\n");
            }
        }
        printf("\n");
    }

    free(test_pixels);
    free(scalar_output);
    free(simd_output);
    log_destroy();

    return 0;
}
