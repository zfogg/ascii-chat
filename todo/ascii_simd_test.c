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
#include "../lib/ascii_simd_neon.h"
#include "../lib/options.h"
#include "../lib/image.h"
#include "../lib/webcam.h"

// Test image dimensions (typical webcam frame)
#define TEST_WIDTH  640
#define TEST_HEIGHT 480
#define TEST_PIXELS (TEST_WIDTH * TEST_HEIGHT)

// Terminal dimensions for ASCII output
#define ASCII_WIDTH  80
#define ASCII_HEIGHT 24

// Image source options
typedef enum {
    IMAGE_SOURCE_SYNTHETIC,
    IMAGE_SOURCE_WEBCAM,
    IMAGE_SOURCE_FILE,
    IMAGE_SOURCE_IMG_FILES
} image_source_t;

// Global options
static image_source_t g_image_source = IMAGE_SOURCE_WEBCAM;
static const char *g_image_filename = NULL;  // For single --file (legacy)
static const char *g_img_files_dir = NULL;   // For --img-files directory

// Forward declaration of webcam benchmark function
simd_benchmark_t benchmark_simd_with_webcam(int width, int height, int iterations);

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

// Load PPM image file (P3 ASCII format)
image_t* load_ppm_file(const char* filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("Error: Cannot open file '%s'\n", filename);
        return NULL;
    }

    char magic[3];
    int w, h, maxval;

    // Read PPM header: P3\n640 480\n255\n
    if (fscanf(fp, "%2s\n%d %d\n%d\n", magic, &w, &h, &maxval) != 4) {
        printf("Error: Invalid PPM header in '%s'\n", filename);
        fclose(fp);
        return NULL;
    }

    if (strcmp(magic, "P3") != 0) {
        printf("Error: Only P3 (ASCII) PPM format supported, got '%s'\n", magic);
        fclose(fp);
        return NULL;
    }

    if (maxval != 255) {
        printf("Error: Only 8-bit PPM supported (maxval=255), got maxval=%d\n", maxval);
        fclose(fp);
        return NULL;
    }

    printf("Loading PPM: %dx%d, maxval=%d\n", w, h, maxval);

    image_t *img = image_new(w, h);
    if (!img) {
        printf("Error: Failed to create image buffer\n");
        fclose(fp);
        return NULL;
    }

    // Read RGB values: "255 128 64 ..."
    for (int i = 0; i < w * h; i++) {
        int r, g, b;
        if (fscanf(fp, "%d %d %d", &r, &g, &b) != 3) {
            printf("Error: Failed to read pixel %d/%d\n", i, w * h);
            image_destroy(img);
            fclose(fp);
            return NULL;
        }

        // Clamp to valid range
        if (r < 0) r = 0; if (r > 255) r = 255;
        if (g < 0) g = 0; if (g > 255) g = 255;
        if (b < 0) b = 0; if (b > 255) b = 255;

        img->pixels[i].r = r;
        img->pixels[i].g = g;
        img->pixels[i].b = b;
    }

    fclose(fp);
    printf("Successfully loaded %dx%d PPM image (%d pixels)\n", w, h, w * h);
    return img;
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

// Load PPM file from directory based on resolution - exits on first file open error
image_t* load_ppm_from_directory(const char* directory, int width, int height) {
    if (!directory) {
        printf("Error: No directory specified\n");
        exit(1);
    }

    // Try primary naming convention first
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "%s/test-%dx%d.ppm", directory, width, height);

    // Check if file exists before trying to open it
    FILE *test_fp = fopen(filepath, "r");
    if (!test_fp) {
        printf("Error: Cannot open required file '%s'\n", filepath);
        printf("Expected file for %dx%d resolution testing\n", width, height);
        exit(1);
    }
    fclose(test_fp);

    // Now load the file (we know it exists)
    image_t *img = load_ppm_file(filepath);
    if (!img) {
        printf("Error: Failed to load PPM file '%s'\n", filepath);
        exit(1);
    }

    if (img->w != width || img->h != height) {
        printf("Error: Image dimensions mismatch in '%s'\n", filepath);
        printf("Expected: %dx%d, Got: %dx%d\n", width, height, img->w, img->h);
        image_destroy(img);
        exit(1);
    }

    printf("Loaded native resolution image: %s (%dx%d)\n", filepath, width, height);
    return img;
}

// Generate test pixels based on global image source setting
void generate_test_pixels(rgb_pixel_t *pixels, int width, int height) {
    switch (g_image_source) {
        case IMAGE_SOURCE_IMG_FILES: {
            if (!g_img_files_dir) {
                printf("Error: No directory specified for img-files source\n");
                generate_test_image(pixels, width, height, 0);
                return;
            }

            image_t *file_img = load_ppm_from_directory(g_img_files_dir, width, height);
            // load_ppm_from_directory exits on error, so file_img is guaranteed to be valid

            // Direct copy (already exact resolution)
            for (int i = 0; i < width * height; i++) {
                pixels[i].r = file_img->pixels[i].r;
                pixels[i].g = file_img->pixels[i].g;
                pixels[i].b = file_img->pixels[i].b;
            }
            printf("Using native resolution data (%dx%d)\n", width, height);
            image_destroy(file_img);
            break;
        }

        case IMAGE_SOURCE_FILE: {
            if (!g_image_filename) {
                printf("Error: No filename specified for file source\n");
                // Fall back to synthetic
                generate_test_image(pixels, width, height, 0);
                return;
            }

            image_t *file_img = load_ppm_file(g_image_filename);
            if (!file_img) {
                printf("Warning: Failed to load file, falling back to synthetic data\n");
                generate_test_image(pixels, width, height, 0);
                return;
            }

            // Convert from rgb_t to rgb_pixel_t and resize if needed
            if (file_img->w == width && file_img->h == height) {
                // Direct copy
                for (int i = 0; i < width * height; i++) {
                    pixels[i].r = file_img->pixels[i].r;
                    pixels[i].g = file_img->pixels[i].g;
                    pixels[i].b = file_img->pixels[i].b;
                }
                printf("Using file data directly (%dx%d)\n", width, height);
            } else {
                // Need to resize - convert to rgb_pixel_t format first
                rgb_pixel_t *file_pixels;
                SAFE_MALLOC_SIMD(file_pixels, file_img->w * file_img->h * sizeof(rgb_pixel_t), rgb_pixel_t *);

                for (int i = 0; i < file_img->w * file_img->h; i++) {
                    file_pixels[i].r = file_img->pixels[i].r;
                    file_pixels[i].g = file_img->pixels[i].g;
                    file_pixels[i].b = file_img->pixels[i].b;
                }

                resize_image(file_pixels, file_img->w, file_img->h, pixels, width, height);
                printf("Resized file data from %dx%d to %dx%d\n", file_img->w, file_img->h, width, height);

                free(file_pixels);
            }

            image_destroy(file_img);
            break;
        }

        case IMAGE_SOURCE_WEBCAM: {
            // Use actual webcam data - this test program is linked with webcam support
            printf("Using webcam data for realistic testing\n");

            // Initialize webcam with index 0 (default camera)
            webcam_init(0);

            // Read a frame from the webcam
            image_t *webcam_frame = webcam_read();
            if (!webcam_frame) {
                printf("Warning: Failed to capture webcam frame, falling back to synthetic data\n");
                webcam_cleanup();
                generate_test_image(pixels, width, height, 0);
                break;
            }

            // Convert and resize webcam data if needed
            if (webcam_frame->w == width && webcam_frame->h == height) {
                // Direct copy - dimensions match
                for (int i = 0; i < width * height; i++) {
                    pixels[i].r = webcam_frame->pixels[i].r;
                    pixels[i].g = webcam_frame->pixels[i].g;
                    pixels[i].b = webcam_frame->pixels[i].b;
                }
                printf("Using webcam data directly (%dx%d)\n", width, height);
            } else {
                // Need to resize - use resize_image_from_rgb_t function or convert inline
                // Simple resize by converting on-the-fly to avoid extra allocation
                float x_ratio = (float)webcam_frame->w / width;
                float y_ratio = (float)webcam_frame->h / height;

                for (int y = 0; y < height; y++) {
                    for (int x = 0; x < width; x++) {
                        int src_x = (int)(x * x_ratio);
                        int src_y = (int)(y * y_ratio);

                        // Clamp to source bounds
                        if (src_x >= webcam_frame->w) src_x = webcam_frame->w - 1;
                        if (src_y >= webcam_frame->h) src_y = webcam_frame->h - 1;

                        int src_idx = src_y * webcam_frame->w + src_x;
                        int dst_idx = y * width + x;

                        pixels[dst_idx].r = webcam_frame->pixels[src_idx].r;
                        pixels[dst_idx].g = webcam_frame->pixels[src_idx].g;
                        pixels[dst_idx].b = webcam_frame->pixels[src_idx].b;
                    }
                }
                printf("Resized webcam data from %dx%d to %dx%d\n", webcam_frame->w, webcam_frame->h, width, height);
            }

            image_destroy(webcam_frame);
            webcam_cleanup();
            break;
        }

        case IMAGE_SOURCE_SYNTHETIC:
        default:
            generate_test_image(pixels, width, height, 0);
            break;
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
    SAFE_MALLOC_SIMD(test_pixels, test_size * sizeof(rgb_pixel_t), rgb_pixel_t *);

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

#ifdef SIMD_SUPPORT_NEON
// Test 2.5: NEON Renderer-Specific Performance Testing 
void test_neon_renderers(void) {
    printf("\n=== Test 2.5: NEON Renderer Performance Testing ===\n");

    // Test different image sizes
    int sizes[][2] = {
        {203, 64},     // Terminal size  
        {320, 240},    // Small webcam
        {640, 480},    // Standard webcam
        {1280, 720},   // HD webcam
    };
    int num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < num_sizes; i++) {
        int width = sizes[i][0];
        int height = sizes[i][1];
        int pixel_count = width * height;
        const int iterations = 100; // More iterations for accurate timing

        printf("\nTesting NEON renderers at %dx%d (%d pixels):\n", width, height, pixel_count);

        // Allocate test data
        rgb_pixel_t *test_pixels;
        SAFE_MALLOC_SIMD(test_pixels, pixel_count * sizeof(rgb_pixel_t), rgb_pixel_t *);
        
        // Generate realistic test data
        srand(42); // Consistent seed for reproducible results
        generate_test_pixels(test_pixels, width, height);

        // Allocate output buffer (generous size for any renderer)
        size_t buffer_size = pixel_count * 64; // 64 bytes per pixel should be more than enough
        char *output_buffer;
        SAFE_MALLOC(output_buffer, buffer_size, char *);

        // Renderer 1: 256-color Background (UTF-8 blocks)
        printf("  256-color BG (UTF-8 blocks): ");
        fflush(stdout);
        
        clock_t start = clock();
        size_t total_bytes = 0;
        for (int iter = 0; iter < iterations; iter++) {
            total_bytes += render_row_neon_256_bg_block_rep(test_pixels, width, output_buffer, buffer_size);
        }
        clock_t end = clock();
        double bg_256_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;
        printf("%.4f ms/frame (%.1f MB output)\n", bg_256_time * 1000, total_bytes / 1024.0 / 1024.0 / iterations);

        // Renderer 2: 256-color Foreground (ASCII chars)
        printf("  256-color FG (ASCII chars):  ");
        fflush(stdout);
        
        start = clock();
        total_bytes = 0;
        for (int iter = 0; iter < iterations; iter++) {
            total_bytes += render_row_neon_256_fg_rep(test_pixels, width, output_buffer, buffer_size);
        }
        end = clock();
        double fg_256_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;
        printf("%.4f ms/frame (%.1f MB output)\n", fg_256_time * 1000, total_bytes / 1024.0 / 1024.0 / iterations);

        // Renderer 3: Truecolor Background (UTF-8 blocks)
        printf("  Truecolor BG (UTF-8 blocks):  ");
        fflush(stdout);
        
        start = clock();
        total_bytes = 0;
        for (int iter = 0; iter < iterations; iter++) {
            total_bytes += render_row_neon_truecolor_bg_block_rep(test_pixels, width, output_buffer, buffer_size);
        }
        end = clock();
        double bg_true_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;
        printf("%.4f ms/frame (%.1f MB output)\n", bg_true_time * 1000, total_bytes / 1024.0 / 1024.0 / iterations);

        // Renderer 4: Truecolor Foreground (ASCII chars)
        printf("  Truecolor FG (ASCII chars):   ");
        fflush(stdout);
        
        start = clock();
        total_bytes = 0;
        for (int iter = 0; iter < iterations; iter++) {
            total_bytes += render_row_neon_truecolor_fg_rep(test_pixels, width, output_buffer, buffer_size);
        }
        end = clock();
        double fg_true_time = ((double)(end - start)) / CLOCKS_PER_SEC / iterations;
        printf("%.4f ms/frame (%.1f MB output)\n", fg_true_time * 1000, total_bytes / 1024.0 / 1024.0 / iterations);

        // Performance Analysis
        printf("  Performance comparison:\n");
        
        // Find fastest renderer
        double min_time = bg_256_time;
        const char *fastest = "256-color BG";
        
        if (fg_256_time < min_time) { min_time = fg_256_time; fastest = "256-color FG"; }
        if (bg_true_time < min_time) { min_time = bg_true_time; fastest = "Truecolor BG"; }
        if (fg_true_time < min_time) { min_time = fg_true_time; fastest = "Truecolor FG"; }
        
        printf("    Fastest: %s (%.4f ms)\n", fastest, min_time * 1000);
        printf("    256-color vs Truecolor BG: %.1fx (%s faster)\n", 
               bg_true_time / bg_256_time,
               (bg_256_time < bg_true_time) ? "256-color" : "truecolor");
        printf("    256-color vs Truecolor FG: %.1fx (%s faster)\n", 
               fg_true_time / fg_256_time,
               (fg_256_time < fg_true_time) ? "256-color" : "truecolor");
        printf("    BG vs FG (256-color): %.1fx (%s faster)\n", 
               fg_256_time / bg_256_time,
               (bg_256_time < fg_256_time) ? "background" : "foreground");
        printf("    BG vs FG (truecolor): %.1fx (%s faster)\n", 
               fg_true_time / bg_true_time,
               (bg_true_time < fg_true_time) ? "background" : "foreground");

        free(test_pixels);
        free(output_buffer);
    }
}
#endif

// Test 2: Performance benchmarking
void test_performance(void) {
    printf("\n=== Test 2: Performance Benchmarking ===\n");

    print_simd_capabilities();
    printf("\n");

    // Create source image early for both monochrome and color testing
    image_t *source_image = NULL;
    if (g_image_source == IMAGE_SOURCE_SYNTHETIC) {
        // Create a synthetic gradient image for all testing
        source_image = image_new(320, 240); // Standard size for synthetic data
        if (source_image) {
            printf("Creating synthetic gradient image (320x240) for testing...\n");
            srand(12345); // Consistent seed for reproducible results
            for (int y = 0; y < source_image->h; y++) {
                for (int x = 0; x < source_image->w; x++) {
                    int idx = y * source_image->w + x;

                    // Create colorful gradient with some randomization
                    int base_r = (x * 255 / source_image->w);
                    int base_g = (y * 255 / source_image->h);
                    int base_b = ((x + y) * 127 / (source_image->w + source_image->h));

                    // Add some noise for variety
                    int noise_r = rand() % 32 - 16;
                    int noise_g = rand() % 32 - 16;
                    int noise_b = rand() % 32 - 16;

                    int r = base_r + noise_r;
                    int g = base_g + noise_g;
                    int b = base_b + noise_b;

                    // Clamp to valid range
                    source_image->pixels[idx].r = (r < 0) ? 0 : (r > 255) ? 255 : r;
                    source_image->pixels[idx].g = (g < 0) ? 0 : (g > 255) ? 255 : g;
                    source_image->pixels[idx].b = (b < 0) ? 0 : (b > 255) ? 255 : b;
                }
            }
        }
    } else if (g_image_source == IMAGE_SOURCE_FILE && g_image_filename) {
        source_image = load_ppm_file(g_image_filename);
        if (!source_image) {
            printf("Warning: Could not load PPM file, falling back to synthetic data\n");
        }
    } else if (g_image_source == IMAGE_SOURCE_WEBCAM) {
        printf("Initializing webcam for performance testing...\n");
        webcam_init(0);
        // Capture one frame to verify webcam is working
        image_t *test_frame = webcam_read();
        if (test_frame) {
            printf("Webcam ready: %dx%d\n", test_frame->w, test_frame->h);
            image_destroy(test_frame);
        } else {
            printf("Warning: Webcam initialization failed, falling back to synthetic data\n");
            webcam_cleanup();
            g_image_source = IMAGE_SOURCE_SYNTHETIC; // Switch to synthetic for the rest of the test
        }
    }

    // Note: For IMG_FILES mode, we load specific images per resolution in the loop below
    // Source image already created above for all modes

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
        int iterations = 1; // Single iteration like other benchmarks

        printf("Testing %dx%d (%d pixels):\n", w, h, w * h);

        // For IMG_FILES mode, load specific image for this resolution
        // For WEBCAM mode, we'll pass NULL to trigger webcam capture during benchmarking
        image_t *resolution_specific_image = source_image;
        if (g_image_source == IMAGE_SOURCE_IMG_FILES && g_img_files_dir) {
            resolution_specific_image = load_ppm_from_directory(g_img_files_dir, w, h);
            // load_ppm_from_directory exits on error, so resolution_specific_image is guaranteed to be valid
        } else if (g_image_source == IMAGE_SOURCE_WEBCAM) {
            resolution_specific_image = NULL; // Signal to benchmark function to use webcam
        }

        simd_benchmark_t benchmark;
        if (g_image_source == IMAGE_SOURCE_WEBCAM && resolution_specific_image == NULL) {
            // Use webcam-specific benchmarking that captures fresh frames
            benchmark = benchmark_simd_with_webcam(w, h, iterations);
        } else {
            // Use standard benchmarking with static image data
            benchmark = benchmark_simd_conversion_with_source(w, h, iterations, resolution_specific_image);
        }

        printf("  Scalar:    %.4f ms/frame\n", benchmark.scalar_time * 100 / iterations);

        if (benchmark.sse2_time > 0) {
            double speedup = benchmark.scalar_time / benchmark.sse2_time;
            printf("  SSE2:      %.4f ms/frame (%.1fx speedup)\n",
                   benchmark.sse2_time * 100 / iterations, speedup);
        }

        if (benchmark.ssse3_time > 0) {
            double speedup = benchmark.scalar_time / benchmark.ssse3_time;
            printf("  SSSE3:     %.4f ms/frame (%.1fx speedup)\n",
                   benchmark.ssse3_time * 100 / iterations, speedup);
        }

        if (benchmark.avx2_time > 0) {
            double speedup = benchmark.scalar_time / benchmark.avx2_time;
            printf("  AVX2:      %.4f ms/frame (%.1fx speedup)\n",
                   benchmark.avx2_time * 100 / iterations, speedup);
        }

        if (benchmark.neon_time > 0) {
            double speedup = benchmark.scalar_time / benchmark.neon_time;
            printf("  NEON:      %.4f ms/frame (%.1fx speedup)\n",
                   benchmark.neon_time * 100 / iterations, speedup);
        }

        printf("  Best:      %s\n\n",
               benchmark.best_method);

        // Clean up resolution-specific image if we loaded it
        if (resolution_specific_image && resolution_specific_image != source_image) {
            image_destroy(resolution_specific_image);
        }
    }

    // NEW: Test color conversion performance
    printf("\n--- COLOR ASCII Performance Tests ---\n");

    for (int i = 0; i < num_sizes; i++) {
        int w = sizes[i][0];
        int h = sizes[i][1];
        int iterations = 1; // Single iteration for color benchmarks

        printf("Testing COLOR %dx%d (%d pixels):\n", w, h, w * h);

        // For IMG_FILES mode, load specific image for this resolution
        // For WEBCAM mode, we'll pass NULL to trigger webcam capture during benchmarking
        image_t *color_resolution_image = source_image;
        if (g_image_source == IMAGE_SOURCE_IMG_FILES && g_img_files_dir) {
            color_resolution_image = load_ppm_from_directory(g_img_files_dir, w, h);
            // load_ppm_from_directory exits on error, so color_resolution_image is guaranteed to be valid
        } else if (g_image_source == IMAGE_SOURCE_WEBCAM) {
            color_resolution_image = NULL; // Signal to benchmark function to use webcam
        }

        // Test foreground color mode
        simd_benchmark_t fg_benchmark = benchmark_simd_color_conversion_with_source(w, h, iterations, false, color_resolution_image);

        printf("  FOREGROUND MODE:\n");
        printf("    Scalar:    %.4f ms/frame\n", fg_benchmark.scalar_time * 100 / iterations);

        if (fg_benchmark.sse2_time > 0) {
            double speedup = fg_benchmark.scalar_time / fg_benchmark.sse2_time;
            printf("    SSE2:      %.4f ms/frame (%.1fx speedup)\n",
                   fg_benchmark.sse2_time * 100 / iterations, speedup);
        }

        if (fg_benchmark.ssse3_time > 0) {
            double speedup = fg_benchmark.scalar_time / fg_benchmark.ssse3_time;
            printf("    SSSE3:     %.4f ms/frame (%.1fx speedup)\n",
                   fg_benchmark.ssse3_time * 100 / iterations, speedup);
        }

        if (fg_benchmark.avx2_time > 0) {
            double speedup = fg_benchmark.scalar_time / fg_benchmark.avx2_time;
            printf("    AVX2:      %.4f ms/frame (%.1fx speedup)\n",
                   fg_benchmark.avx2_time * 100 / iterations, speedup);
        }

        if (fg_benchmark.neon_time > 0) {
            double speedup = fg_benchmark.scalar_time / fg_benchmark.neon_time;
            printf("    NEON:      %.4f ms/frame (%.1fx speedup)\n",
                   fg_benchmark.neon_time * 100 / iterations, speedup);
        }

        printf("    Best:      %s\n",
               fg_benchmark.best_method);

        // Test background color mode (use same image source)
        simd_benchmark_t bg_benchmark = benchmark_simd_color_conversion_with_source(w, h, iterations, true, color_resolution_image);

        printf("  BACKGROUND MODE:\n");
        printf("    Scalar:    %.4f ms/frame\n", bg_benchmark.scalar_time * 100 / iterations);

        if (bg_benchmark.sse2_time > 0) {
            double speedup = bg_benchmark.scalar_time / bg_benchmark.sse2_time;
            printf("    SSE2:      %.4f ms/frame (%.1fx speedup)\n",
                   bg_benchmark.sse2_time * 100 / iterations, speedup);
        }

        if (bg_benchmark.ssse3_time > 0) {
            double speedup = bg_benchmark.scalar_time / bg_benchmark.ssse3_time;
            printf("    SSSE3:     %.4f ms/frame (%.1fx speedup)\n",
                   bg_benchmark.ssse3_time * 100 / iterations, speedup);
        }

        if (bg_benchmark.avx2_time > 0) {
            double speedup = bg_benchmark.scalar_time / bg_benchmark.avx2_time;
            printf("    AVX2:      %.4f ms/frame (%.1fx speedup)\n",
                   bg_benchmark.avx2_time * 100 / iterations, speedup);
        }

        if (bg_benchmark.neon_time > 0) {
            double speedup = bg_benchmark.scalar_time / bg_benchmark.neon_time;
            printf("    NEON:      %.4f ms/frame (%.1fx speedup)\n",
                   bg_benchmark.neon_time * 100 / iterations, speedup);
        }

        printf("    Best:      %s\n\n",
               bg_benchmark.best_method);

        // Clean up color resolution-specific image if we loaded it
        if (color_resolution_image && color_resolution_image != source_image) {
            image_destroy(color_resolution_image);
        }
    }

    // Clean up resources
    if (source_image) {
        image_destroy(source_image);
    }

    // Clean up webcam if it was initialized
    if (g_image_source == IMAGE_SOURCE_WEBCAM) {
        webcam_cleanup();
        printf("Webcam cleaned up after performance testing\n");
    }
}

// Test 4: Integration example - NEW OPTIMIZED VERSION
void test_integration(void) {
    printf("\n=== Test 4: Integration with Your Project - NEW OPTIMIZED ===\n");

    // Load source image based on mode
    image_t *source_image = NULL;
    if (g_image_source == IMAGE_SOURCE_FILE && g_image_filename) {
        source_image = load_ppm_file(g_image_filename);
        if (source_image) {
            printf("Using loaded PPM file for terminal test\n");
        }
    } else if (g_image_source == IMAGE_SOURCE_IMG_FILES && g_img_files_dir) {
        // load_ppm_from_directory exits on error, so source_image is guaranteed to be valid
        printf("Using directory image for terminal test (203x64)\n");
    }
    if (!source_image) {
        source_image = load_ppm_from_directory("imgs", 203, 64);
        printf("Using directory image for terminal test (203x64)\n");
    }

    printf("Performance test on your 203x64 terminal:\n");
    simd_benchmark_t bench = benchmark_simd_conversion_with_source(203, 64, 1, source_image);
    printf("- Scalar: %.2f ms per frame\n", bench.scalar_time * 100 / 1);

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
#ifdef SIMD_SUPPORT_SSSE3
    if (bench.ssse3_time > 0) {
        simd_time = bench.ssse3_time;
        simd_method = "SSSE3";
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
               simd_method, simd_time * 100 / 1, bench.scalar_time / simd_time);
    }
    printf("- Best method: %s\n", bench.best_method);
    printf("- At 60 FPS: %.1f%% CPU time saved\n",
           100.0 * (1.0 - 1.0/bench.speedup_best));

    // Clean up loaded image
    if (source_image) {
        image_destroy(source_image);
    }
}


// Webcam-specific benchmark function that captures fresh frames for each iteration
simd_benchmark_t benchmark_simd_with_webcam(int width, int height, int iterations) {
    simd_benchmark_t result = {0};
    int pixel_count = width * height;

    // Allocate buffers for benchmarking
    rgb_pixel_t **frame_data; // Array of pre-captured frame data
    char *output_buffer;
    SAFE_CALLOC(frame_data, iterations, sizeof(rgb_pixel_t*), rgb_pixel_t **);
    SAFE_MALLOC(output_buffer, pixel_count, char *);

    printf("Pre-capturing %d webcam frames at %dx%d...\n", iterations, width, height);

    // Pre-capture and resize all webcam frames
    int captured_frames = 0;
    for (int iter = 0; iter < iterations; iter++) {
        printf("Pre-capturing frame %d/%d...\n", iter, iterations);
        // Capture webcam frame
        image_t *webcam_frame = webcam_read();
        if (!webcam_frame) {
            printf("Warning: Failed to capture webcam frame %d\n", iter);
            continue;
        }

        // Create temp image with desired dimensions
        image_t *resized_frame = image_new(width, height);

        // Use image_resize to resize webcam frame to test dimensions
        image_resize(webcam_frame, resized_frame);

        // Allocate and copy resized data (convert rgb_t to rgb_pixel_t) with SIMD alignment
        SAFE_CALLOC_SIMD(frame_data[captured_frames], pixel_count, sizeof(rgb_pixel_t), rgb_pixel_t *);
        for (int i = 0; i < pixel_count; i++) {
            frame_data[captured_frames][i].r = resized_frame->pixels[i].r;
            frame_data[captured_frames][i].g = resized_frame->pixels[i].g;
            frame_data[captured_frames][i].b = resized_frame->pixels[i].b;
        }

        image_destroy(resized_frame);
        image_destroy(webcam_frame);
        captured_frames++;
    }

    if (captured_frames == 0) {
        printf("Error: No webcam frames captured!\n");
        free(frame_data);
        free(output_buffer);
        return result;
    }

    printf("Captured %d frames, benchmarking pure conversion performance...\n", captured_frames);

    // Benchmark scalar conversion (pure conversion, no I/O)
    clock_t start_time = clock();
    for (int iter = 0; iter < captured_frames; iter++) {
        convert_pixels_scalar(frame_data[iter], output_buffer, pixel_count);
    }
    clock_t end_time = clock();
    result.scalar_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

    // Benchmark optimized conversion (pure conversion, no I/O)
    start_time = clock();
    for (int iter = 0; iter < captured_frames; iter++) {
        convert_pixels_optimized(frame_data[iter], output_buffer, pixel_count);
    }
    end_time = clock();

    // Calculate best performance metric
    double optimized_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;

#ifdef SIMD_SUPPORT_NEON
    result.neon_time = optimized_time;
#elif defined(SIMD_SUPPORT_AVX2)
    result.avx2_time = optimized_time;
#elif defined(SIMD_SUPPORT_SSSE3)
    result.ssse3_time = optimized_time;
#elif defined(SIMD_SUPPORT_SSE2)
    result.sse2_time = optimized_time;
#endif

    // Find the actual best method by comparing times
    if (result.scalar_time <= optimized_time) {
        result.best_method = "Scalar (Webcam)";
        result.speedup_best = 1.0;
    } else {
#ifdef SIMD_SUPPORT_NEON
        result.best_method = "NEON (Webcam)";
#elif defined(SIMD_SUPPORT_AVX2)
        result.best_method = "AVX2 (Webcam)";
#elif defined(SIMD_SUPPORT_SSSE3)
        result.best_method = "SSSE3 (Webcam)";
#elif defined(SIMD_SUPPORT_SSE2)
        result.best_method = "SSE2 (Webcam)";
#else
        result.best_method = "Scalar (Webcam)";
#endif
        result.speedup_best = result.scalar_time / optimized_time;
    }

    // Clean up pre-captured frame data
    for (int i = 0; i < captured_frames; i++) {
        free(frame_data[i]);
    }
    free(frame_data);
    free(output_buffer);

    return result;
}

// Print usage information
void print_usage(const char* program_name) {
    printf("Usage: %s [OPTIONS]\n", program_name);
    printf("\nImage source options (pick one):\n");
    printf("  --img-files <dir>   Load PPM files from directory (requires all resolutions)\n");
    printf("  --file <filename>   Load single PPM image file for testing\n");
    printf("  --webcam           Use webcam for realistic data (default)\n");
    printf("  --synthetic        Use synthetic test patterns\n");
    printf("\nExamples:\n");
    printf("  %s --img-files imgs/                    # Load test-WxH.ppm files\n", program_name);
    printf("  %s --file imgs/test-640x480.ppm        # Single file (resized)\n", program_name);
    printf("  %s --webcam                           # Webcam mode\n", program_name);
    printf("  %s --synthetic                        # Synthetic patterns\n", program_name);
    printf("  %s                                    # Same as --webcam\n", program_name);
    printf("\nDirectory structure for --img-files:\n");
    printf("  imgs/test-80x24.ppm     # Terminal size\n");
    printf("  imgs/test-320x240.ppm   # Small webcam\n");
    printf("  imgs/test-640x480.ppm   # Standard webcam\n");
    printf("  imgs/test-1280x720.ppm  # HD webcam\n");
    printf("\n");
}

// Parse command line arguments
int parse_arguments(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--img-files") == 0) {
            if (i + 1 >= argc) {
                printf("Error: --img-files requires a directory path\n");
                return -1;
            }
            g_image_source = IMAGE_SOURCE_IMG_FILES;
            g_img_files_dir = argv[++i];
        } else if (strcmp(argv[i], "--file") == 0) {
            if (i + 1 >= argc) {
                printf("Error: --file requires a filename\n");
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
            return 1; // Exit with success
        } else {
            printf("Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    // Parse command line arguments
    int parse_result = parse_arguments(argc, argv);
    if (parse_result != 0) {
        return (parse_result > 0) ? 0 : 1; // 1 for help, -1 for error
    }

    printf("====================================\n");
    printf("   NEW OPTIMIZED SIMD ASCII Test   \n");
    printf("====================================\n");

    // Show image source selection
    switch (g_image_source) {
        case IMAGE_SOURCE_IMG_FILES:
            printf("Image source: IMG_FILES (%s)\n", g_img_files_dir);
            break;
        case IMAGE_SOURCE_FILE:
            printf("Image source: FILE (%s)\n", g_image_filename);
            break;
        case IMAGE_SOURCE_WEBCAM:
            printf("Image source: WEBCAM (default)\n");
            break;
        case IMAGE_SOURCE_SYNTHETIC:
            printf("Image source: SYNTHETIC\n");
            break;
    }
    printf("\n");

    // Initialize logging (required by SAFE_MALLOC)
    log_init(NULL, LOG_ERROR);

    // Run all tests
    test_correctness();
    test_performance();
#ifdef SIMD_SUPPORT_NEON
    test_neon_renderers();
#endif
    test_integration();

    log_destroy();

    return 0;
}
