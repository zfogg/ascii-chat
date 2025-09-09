#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lib/common.h"
#include "lib/image2ascii/image.h"
#include "lib/image2ascii/simd/ascii_simd.h"

int main() {
    // Initialize logging
    log_init(NULL, LOG_DEBUG);

    // Create a small test image
    const int width = 8, height = 4;
    image_t *test_image = image_new(width, height);

    // Fill with test pattern
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = y * width + x;
            test_image->pixels[idx].r = (uint8_t)(idx % 256);
            test_image->pixels[idx].g = (uint8_t)(idx % 256);
            test_image->pixels[idx].b = (uint8_t)(idx % 256);
        }
    }

    // Test palette with mixed byte lengths
    const char *test_palette = " .:-αβ🌟⭐🧠";

    printf("Testing palette: '%s'\n", test_palette);
    printf("Palette length: %zu bytes\n", strlen(test_palette));

    // Test scalar implementation
    char *scalar_result = image_print(test_image, test_palette);
    if (scalar_result) {
        printf("Scalar result length: %zu bytes\n", strlen(scalar_result));
        printf("Scalar result (first 200 chars):\n");
        for (int i = 0; i < 200 && scalar_result[i]; i++) {
            if (scalar_result[i] >= 32 && scalar_result[i] <= 126) {
                printf("%c", scalar_result[i]);
            } else {
                printf("[%02X]", (unsigned char)scalar_result[i]);
            }
        }
        printf("\n\n");
        free(scalar_result);
    }

    // Test SIMD implementation
    char *simd_result = image_print_simd(test_image, test_palette);
    if (simd_result) {
        printf("SIMD result length: %zu bytes\n", strlen(simd_result));
        printf("SIMD result (first 200 chars):\n");
        for (int i = 0; i < 200 && simd_result[i]; i++) {
            if (simd_result[i] >= 32 && simd_result[i] <= 126) {
                printf("%c", simd_result[i]);
            } else {
                printf("[%02X]", (unsigned char)simd_result[i]);
            }
        }
        printf("\n\n");
        free(simd_result);
    }

    image_destroy(test_image);
    return 0;
}
