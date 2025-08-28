/*
 * ASCII-Chat SIMD Test Suite
 *
 * Comprehensive testing of:
 * 1. ASCII conversion correctness (ascii.h functions)
 * 2. Color rendering correctness (SIMD vs scalar)
 * 3. Performance benchmarking (all modes)
 * 4. Integration testing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <stdbool.h>
#include <ctype.h>
#include "../lib/common.h"
#include "../lib/ascii_simd.h"
#include "../lib/options.h"
#include "../lib/ascii.h"
#include "../lib/image.h"
#include "../lib/webcam.h"

// Image source options
typedef enum {
    IMAGE_SOURCE_SYNTHETIC,
    IMAGE_SOURCE_WEBCAM,
    IMAGE_SOURCE_FILE,
    IMAGE_SOURCE_IMG_FILES
} image_source_t;

// Global options
static image_source_t g_image_source = IMAGE_SOURCE_WEBCAM;
static const char *g_image_filename = NULL;
static const char *g_img_files_dir = NULL;

// Forward declarations
void test_ascii_correctness(void);
void test_color_correctness(void);
void test_performance_benchmarks(void);
void test_integration(void);
image_t *load_ppm_from_directory(const char *directory, int width, int height);

//=============================================================================
// Test 1: ASCII Conversion Correctness
// Tests the main ascii.h functions for consistency and correctness
//=============================================================================
void test_ascii_correctness(void) {
    printf("=== TEST 1: ASCII Conversion Correctness ===\n");
    printf("Testing ascii_convert() function consistency\n\n");

    const struct {
        const char *name;
        int width;
        int height;
        bool color;
        bool aspect_ratio;
        bool stretch;
    } test_configs[] = {
        {"Monochrome 40x12 Basic", 40, 12, false, false, false},
        {"Monochrome 80x24 Basic", 80, 24, false, false, false},
        {"Monochrome 40x12 AspectRatio", 40, 12, false, true, false},
        {"Color 40x12 Basic", 40, 12, true, false, false},
        {"Color 80x24 Basic", 80, 24, true, false, false},
        {"Color 40x12 AspectRatio", 40, 12, true, true, false},
    };

    const int num_configs = sizeof(test_configs) / sizeof(test_configs[0]);

    // Create deterministic test image
    const int img_width = 160, img_height = 120;
    image_t *test_image = image_new(img_width, img_height);
    if (!test_image) {
        printf("❌ FAILED: Could not create test image\n");
        return;
    }

    // Generate deterministic pattern
    srand(42);
    for (int y = 0; y < img_height; y++) {
        for (int x = 0; x < img_width; x++) {
            int idx = y * img_width + x;
            test_image->pixels[idx].r = (x * 3 + y * 5) % 256;
            test_image->pixels[idx].g = (x * 7 + y * 11) % 256;
            test_image->pixels[idx].b = (x * 13 + y * 17) % 256;
        }
    }

    int passed = 0, total = 0;

    for (int cfg_idx = 0; cfg_idx < num_configs; cfg_idx++) {
        printf("Testing: %s\n", test_configs[cfg_idx].name);
        total++;

        // Test consistency across multiple calls
        bool config_passed = true;
        char *results[3] = {NULL};

        for (int run = 0; run < 3; run++) {
            results[run] = ascii_convert(test_image,
                                       test_configs[cfg_idx].width,
                                       test_configs[cfg_idx].height,
                                       test_configs[cfg_idx].color,
                                       test_configs[cfg_idx].aspect_ratio,
                                       test_configs[cfg_idx].stretch);

            if (!results[run]) {
                printf("  ❌ Run %d: ascii_convert returned NULL\n", run + 1);
                config_passed = false;
                break;
            }
        }

        if (config_passed) {
            // Check all results are identical
            for (int i = 1; i < 3; i++) {
                if (strcmp(results[0], results[i]) != 0) {
                    printf("  ❌ Results inconsistent between calls\n");
                    config_passed = false;
                    break;
                }
            }

            if (config_passed) {
                printf("  ✅ Consistent results (%zu chars)\n", strlen(results[0]));
                passed++;
            }
        }

        // Cleanup
        for (int i = 0; i < 3; i++) {
            if (results[i]) free(results[i]);
        }
    }

    printf("\n📊 ASCII Correctness Results: %d/%d passed (%.1f%%)\n",
           passed, total, 100.0 * passed / total);

    if (passed == total) {
        printf("✅ All ASCII conversion tests PASSED\n\n");
    } else {
        printf("❌ Some ASCII conversion tests FAILED\n\n");
    }

    image_destroy(test_image);
}

//=============================================================================
// Test 2: Scalar vs SIMD Correctness
// Tests scalar vs SIMD image_print functions for correctness
//=============================================================================
void test_color_correctness(void) {
    printf("=== TEST 2: Scalar vs SIMD Correctness ===\n");
    printf("Comparing scalar image_print* vs SIMD image_print*_simd functions\n\n");

    const struct {
        const char *name;
        bool is_color;
    } test_modes[] = {
        {"Monochrome ASCII", false},
        {"Color ASCII", true},
    };

    const int num_modes = sizeof(test_modes) / sizeof(test_modes[0]);
    const int test_sizes[][2] = {
        {40, 12},
        {80, 24},
        {160, 48}
    };
    const int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);

    int passed = 0, total = 0;

    for (int mode_idx = 0; mode_idx < num_modes; mode_idx++) {
        for (int size_idx = 0; size_idx < num_sizes; size_idx++) {
            int width = test_sizes[size_idx][0];
            int height = test_sizes[size_idx][1];

            printf("Testing: %s (%dx%d)\n", test_modes[mode_idx].name, width, height);
            total++;

            // Create test image
            image_t *test_image = image_new(width, height);
            if (!test_image) {
                printf("  ❌ FAILED: Could not create test image\n");
                continue;
            }

            // Fill with deterministic test pattern
            srand(42);
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int idx = y * width + x;
                    test_image->pixels[idx].r = (x * 255) / width;
                    test_image->pixels[idx].g = (y * 255) / height;
                    test_image->pixels[idx].b = ((x + y) * 127) / (width + height);
                }
            }

            char *scalar_result = NULL;
            char *simd_result = NULL;
            bool test_passed = false;

            if (test_modes[mode_idx].is_color) {
                // Test color functions - test 256-color mode (optimized function)
                scalar_result = image_print_color(test_image);
                simd_result = image_print_color_simd(test_image, false, true); // FG mode, 256-color (optimized!)
            } else {
                // Test monochrome functions
                scalar_result = image_print(test_image);
                simd_result = image_print_simd(test_image);
            }

            if (!scalar_result || !simd_result) {
                printf("  ❌ Function returned NULL (scalar=%p, simd=%p)\n",
                       (void*)scalar_result, (void*)simd_result);
            } else {
                // Compare outputs
                if (strcmp(scalar_result, simd_result) == 0) {
                    printf("  ✅ Scalar and SIMD outputs match perfectly (%zu chars)\n", strlen(scalar_result));
                    test_passed = true;
                    passed++;
                } else {
                    printf("  ❌ Output mismatch: scalar=%zu chars, simd=%zu chars\n",
                           strlen(scalar_result), strlen(simd_result));

                    // Show first difference for debugging
                    size_t min_len = strlen(scalar_result) < strlen(simd_result) ?
                                    strlen(scalar_result) : strlen(simd_result);
                    for (size_t i = 0; i < min_len; i++) {
                        if (scalar_result[i] != simd_result[i]) {
                            printf("    First diff at pos %zu: scalar=0x%02x('%c') simd=0x%02x('%c')\n",
                                   i, (unsigned char)scalar_result[i],
                                   isprint(scalar_result[i]) ? scalar_result[i] : '.',
                                   (unsigned char)simd_result[i],
                                   isprint(simd_result[i]) ? simd_result[i] : '.');
                            break;
                        }
                    }
                }
            }

            // Cleanup
            if (scalar_result) free(scalar_result);
            if (simd_result) free(simd_result);
            image_destroy(test_image);
        }
    }

    printf("\n📊 Scalar vs SIMD Correctness Results: %d/%d passed (%.1f%%)\n",
           passed, total, 100.0 * passed / total);

    if (passed == total) {
        printf("✅ All scalar vs SIMD correctness tests PASSED\n\n");
    } else {
        printf("❌ Some scalar vs SIMD correctness tests FAILED\n\n");
    }
}

//=============================================================================
// Test 3: Scalar vs SIMD Performance Benchmarks
// Direct performance comparison using image_print* functions
//=============================================================================
void test_performance_benchmarks(void) {
    printf("=== TEST 3: Scalar vs SIMD Performance Benchmarks ===\n");
    printf("Direct performance comparison: image_print vs image_print_simd functions\n\n");

    printf("SIMD Capabilities:\n");
    print_simd_capabilities();
    printf("\n");

    // Test different resolutions
    const struct {
        const char *name;
        int width;
        int height;
    } test_sizes[] = {
        {"Terminal Small", 40, 12},
        {"Terminal Standard", 80, 24},
        {"Terminal Large", 160, 48},
        {"Webcam Small", 320, 240},
        {"Webcam Standard", 640, 480}
    };

    const int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    const int iterations = 5; // Multiple iterations for timing accuracy

    for (int i = 0; i < num_sizes; i++) {
        printf("--- %s: %dx%d (%d pixels) ---\n",
               test_sizes[i].name, test_sizes[i].width, test_sizes[i].height,
               test_sizes[i].width * test_sizes[i].height);

        // Create test image for this size
        image_t *test_image = image_new(test_sizes[i].width, test_sizes[i].height);
        if (!test_image) {
            printf("  ❌ Failed to create test image\n\n");
            continue;
        }

        // Fill with colorful gradient pattern
        for (int y = 0; y < test_image->h; y++) {
            for (int x = 0; x < test_image->w; x++) {
                int idx = y * test_image->w + x;
                test_image->pixels[idx].r = (x * 255) / test_image->w;
                test_image->pixels[idx].g = (y * 255) / test_image->h;
                test_image->pixels[idx].b = ((x + y) * 127) / (test_image->w + test_image->h);
            }
        }

        // Test Monochrome ASCII
        printf("  Monochrome ASCII:\n");

        // Time scalar version
        clock_t start = clock();
        for (int iter = 0; iter < iterations; iter++) {
            char *result = image_print(test_image);
            if (result) free(result);
        }
        clock_t end = clock();
        double scalar_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;

        // Time SIMD version
        start = clock();
        for (int iter = 0; iter < iterations; iter++) {
            char *result = image_print_simd(test_image);
            if (result) free(result);
        }
        end = clock();
        double simd_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;

        printf("    Scalar:  %8.4f ms/frame\n", scalar_time * 1000);
        printf("    SIMD:    %8.4f ms/frame (%4.2fx speedup)\n",
               simd_time * 1000, scalar_time / simd_time);

        // Test Color ASCII
        printf("  Color ASCII:\n");

        // Time scalar color version
        start = clock();
        for (int iter = 0; iter < iterations; iter++) {
            char *result = image_print_color(test_image);
            if (result) free(result);
        }
        end = clock();
        double color_scalar_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;

        // Time SIMD color version (foreground mode, fast path)
        start = clock();
        for (int iter = 0; iter < iterations; iter++) {
            char *result = image_print_color_simd(test_image, false, true);
            if (result) free(result);
        }
        end = clock();
        double color_simd_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;

        printf("    Scalar:  %8.4f ms/frame\n", color_scalar_time * 1000);
        printf("    SIMD:    %8.4f ms/frame (%4.2fx speedup)\n",
               color_simd_time * 1000, color_scalar_time / color_simd_time);

        // Test SIMD color background mode
        start = clock();
        for (int iter = 0; iter < iterations; iter++) {
            char *result = image_print_color_simd(test_image, true, true);
            if (result) free(result);
        }
        end = clock();
        double color_bg_simd_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;

        printf("    BG Mode: %8.4f ms/frame\n", color_bg_simd_time * 1000);

        image_destroy(test_image);
        printf("\n");
    }

    printf("✅ Performance benchmarking complete\n\n");
}

//=============================================================================
// Test 4: Integration Example
// Shows real-world usage for terminal output
//=============================================================================
void test_integration(void) {
    printf("=== TEST 4: Integration Example ===\n");
    printf("Real-world terminal ASCII conversion example\n\n");

    // Load test image
    image_t *source_image = NULL;
    if (g_img_files_dir) {
        source_image = load_ppm_from_directory(g_img_files_dir, 203, 64);
    }

    if (!source_image) {
        source_image = image_new(203, 64);
        // Create simple test pattern
        for (int y = 0; y < 64; y++) {
            for (int x = 0; x < 203; x++) {
                int idx = y * 203 + x;
                source_image->pixels[idx].r = (x * 255) / 203;
                source_image->pixels[idx].g = (y * 255) / 64;
                source_image->pixels[idx].b = 128;
            }
        }
        printf("Using synthetic test image\n");
    } else {
        printf("Using loaded test image (203x64)\n");
    }

    // Test realistic terminal conversion
    simd_benchmark_t bench = benchmark_simd_conversion_with_source(203, 64, 1, source_image);

    printf("Terminal Performance (203x64):\n");
    printf("  Scalar:     %8.3f ms/frame\n", bench.scalar_time * 1000);

    // Use the benchmark's determination of the best method and time
    double best_simd_time = 0;
    if (strcmp(bench.best_method, "NEON") == 0) {
        best_simd_time = bench.neon_time;
    } else if (strcmp(bench.best_method, "AVX2") == 0) {
        best_simd_time = bench.avx2_time;  
    } else if (strcmp(bench.best_method, "SSSE3") == 0) {
        best_simd_time = bench.ssse3_time;
    } else if (strcmp(bench.best_method, "SSE2") == 0) {
        best_simd_time = bench.sse2_time;
    }

    if (best_simd_time > 0 && strcmp(bench.best_method, "scalar") != 0) {
        printf("  Best SIMD:  %8.3f ms/frame (%4.1fx faster)\n",
               best_simd_time * 1000, bench.scalar_time / best_simd_time);
    }

    printf("  Winner:     %s\n", bench.best_method);
    printf("  CPU Saved:  %.1f%% at 60 FPS\n", 100.0 * (1.0 - 1.0 / bench.speedup_best));

    // Show actual ASCII output sample
    char *ascii_output = ascii_convert(source_image, 40, 12, false, false, false);
    if (ascii_output) {
        printf("\nSample ASCII output (40x12):\n");
        printf("┌────────────────────────────────────────┐\n");
        char *line = ascii_output;
        char *next_line;
        int line_count = 0;
        while (line && line_count < 12) {
            next_line = strchr(line, '\n');
            if (next_line) {
                *next_line = '\0';
                printf("│%-40s│\n", line);
                *next_line = '\n';
                line = next_line + 1;
            } else {
                printf("│%-40s│\n", line);
                break;
            }
            line_count++;
        }
        printf("└────────────────────────────────────────┘\n");
        free(ascii_output);
    }

    image_destroy(source_image);
    printf("✅ Integration test complete\n\n");
}

//=============================================================================
// Helper Functions
//=============================================================================

// Load PPM file from directory (simplified implementation)
image_t *load_ppm_from_directory(const char *directory, int width, int height) {
    // For this test, just return NULL to fall back to synthetic image
    (void)directory; (void)width; (void)height;
    return NULL;
}

// Print usage
void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\nImage source options:\n");
    printf("  --img-files <dir>   Load PPM files from directory\n");
    printf("  --file <filename>   Load single PPM image file\n");
    printf("  --webcam           Use webcam (default)\n");
    printf("  --synthetic        Use synthetic patterns\n");
}

// Parse arguments
int parse_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--img-files") == 0) {
            if (i + 1 >= argc) {
                printf("Error: --img-files requires directory path\n");
                return -1;
            }
            g_image_source = IMAGE_SOURCE_IMG_FILES;
            g_img_files_dir = argv[++i];
        } else if (strcmp(argv[i], "--file") == 0) {
            if (i + 1 >= argc) {
                printf("Error: --file requires filename\n");
                return -1;
            }
            g_image_source = IMAGE_SOURCE_FILE;
            g_image_filename = argv[++i];
        } else if (strcmp(argv[i], "--webcam") == 0) {
            g_image_source = IMAGE_SOURCE_WEBCAM;
        } else if (strcmp(argv[i], "--synthetic") == 0) {
            g_image_source = IMAGE_SOURCE_SYNTHETIC;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 1;
        } else {
            printf("Error: Unknown option '%s'\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

//=============================================================================
// Main Test Runner
//=============================================================================
int main(int argc, char *argv[]) {
    // Parse arguments
    int parse_result = parse_arguments(argc, argv);
    if (parse_result != 0) {
        return (parse_result > 0) ? 0 : 1;
    }

    printf("=====================================\n");
    printf("      ASCII-Chat SIMD Test Suite    \n");
    printf("=====================================\n\n");

    // Show configuration
    switch (g_image_source) {
    case IMAGE_SOURCE_IMG_FILES:
        printf("📁 Image source: Directory (%s)\n", g_img_files_dir ? g_img_files_dir : "none");
        break;
    case IMAGE_SOURCE_FILE:
        printf("📄 Image source: File (%s)\n", g_image_filename ? g_image_filename : "none");
        break;
    case IMAGE_SOURCE_WEBCAM:
        printf("📷 Image source: Webcam\n");
        break;
    case IMAGE_SOURCE_SYNTHETIC:
        printf("🎨 Image source: Synthetic patterns\n");
        break;
    }
    printf("\n");

    // Initialize logging and SIMD
    log_init(NULL, LOG_ERROR);
    ascii_simd_init();
    printf("✅ SIMD system initialized\n\n");

    // Run all tests
    test_ascii_correctness();
    test_color_correctness();
    test_performance_benchmarks();
    test_integration();

    printf("=====================================\n");
    printf("        Test Suite Complete         \n");
    printf("=====================================\n");

    log_destroy();
    return 0;
}
