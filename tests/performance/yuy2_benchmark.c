// Benchmark test for YUY2 to RGB conversion performance
// Compares scalar vs SIMD implementations

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "common.h"
#include "image2ascii/image.h"
#include "os/windows/yuy2_simd.h"

// High-resolution timer for Windows
static double get_time_ms(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * 1000.0 / (double)freq.QuadPart;
}

// Generate synthetic YUY2 test data
static void generate_yuy2_test_pattern(uint8_t* yuy2, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x += 2) {
            int idx = (y * width + x) * 2;
            
            // Create a gradient pattern
            yuy2[idx + 0] = (x * 255) / width;      // Y0
            yuy2[idx + 1] = 128 + (y * 64) / height; // U
            yuy2[idx + 2] = ((x + 1) * 255) / width; // Y1
            yuy2[idx + 3] = 128 - (y * 64) / height; // V
        }
    }
}

// Verify conversion correctness
static int verify_conversion(const rgb_t* result, const rgb_t* reference, 
                            int pixel_count, int tolerance) {
    int errors = 0;
    for (int i = 0; i < pixel_count; i++) {
        int dr = abs(result[i].r - reference[i].r);
        int dg = abs(result[i].g - reference[i].g);
        int db = abs(result[i].b - reference[i].b);
        
        if (dr > tolerance || dg > tolerance || db > tolerance) {
            if (errors < 10) {  // Report first 10 errors
                printf("  Pixel %d mismatch: Result(%d,%d,%d) vs Reference(%d,%d,%d)\n",
                       i, result[i].r, result[i].g, result[i].b,
                       reference[i].r, reference[i].g, reference[i].b);
            }
            errors++;
        }
    }
    return errors;
}

// Benchmark a single conversion function
static double benchmark_conversion(const char* name,
                                  void (*convert_func)(const uint8_t*, rgb_t*, int, int),
                                  const uint8_t* yuy2, rgb_t* rgb,
                                  int width, int height, int iterations) {
    printf("\nBenchmarking %s:\n", name);
    
    // Warmup
    for (int i = 0; i < 10; i++) {
        convert_func(yuy2, rgb, width, height);
    }
    
    // Timed run
    double start = get_time_ms();
    for (int i = 0; i < iterations; i++) {
        convert_func(yuy2, rgb, width, height);
    }
    double elapsed = get_time_ms() - start;
    
    double ms_per_frame = elapsed / iterations;
    double pixels_per_ms = (width * height) / ms_per_frame;
    double mpixels_per_sec = pixels_per_ms / 1000.0;
    
    printf("  Time per frame: %.3f ms\n", ms_per_frame);
    printf("  Throughput: %.1f Mpixels/sec\n", mpixels_per_sec);
    printf("  FPS capability: %.1f\n", 1000.0 / ms_per_frame);
    
    return ms_per_frame;
}

int main(int argc, char** argv) {
    printf("YUY2 to RGB Conversion Benchmark\n");
    printf("=================================\n");
    
    // Test parameters
    const int test_sizes[][2] = {
        {640, 480},    // VGA
        {1280, 720},   // 720p
        {1920, 1080},  // 1080p
        {1920, 1440},  // Common webcam resolution
    };
    const int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
    const int iterations = 100;
    
    // Detect CPU features
    printf("\nCPU Features:\n");
    printf("  SSE2:  %s\n", yuy2_cpu_has_sse2() ? "YES" : "NO");
    printf("  SSSE3: %s\n", yuy2_cpu_has_ssse3() ? "YES" : "NO");
    printf("  AVX2:  %s\n", yuy2_cpu_has_avx2() ? "YES" : "NO");
    
    for (int s = 0; s < num_sizes; s++) {
        int width = test_sizes[s][0];
        int height = test_sizes[s][1];
        int pixel_count = width * height;
        
        printf("\n\nResolution: %dx%d (%d pixels)\n", width, height, pixel_count);
        printf("----------------------------------------\n");
        
        // Allocate buffers
        uint8_t* yuy2;
        SAFE_MALLOC_ALIGNED(yuy2, width * height * 2, 32, uint8_t*);
        
        rgb_t* rgb_scalar;
        rgb_t* rgb_sse2;
        rgb_t* rgb_ssse3;
        rgb_t* rgb_avx2;
        rgb_t* rgb_optimized;
        
        SAFE_MALLOC_ALIGNED(rgb_scalar, sizeof(rgb_t) * pixel_count, 32, rgb_t*);
        SAFE_MALLOC_ALIGNED(rgb_sse2, sizeof(rgb_t) * pixel_count, 32, rgb_t*);
        SAFE_MALLOC_ALIGNED(rgb_ssse3, sizeof(rgb_t) * pixel_count, 32, rgb_t*);
        SAFE_MALLOC_ALIGNED(rgb_avx2, sizeof(rgb_t) * pixel_count, 32, rgb_t*);
        SAFE_MALLOC_ALIGNED(rgb_optimized, sizeof(rgb_t) * pixel_count, 32, rgb_t*);
        
        // Generate test pattern
        generate_yuy2_test_pattern(yuy2, width, height);
        
        // Benchmark scalar (baseline)
        double scalar_time = benchmark_conversion("Scalar", 
                                                 convert_yuy2_to_rgb_scalar,
                                                 yuy2, rgb_scalar,
                                                 width, height, iterations);
        
        // Benchmark optimized (auto-dispatch)
        double optimized_time = benchmark_conversion("Optimized (Auto-dispatch)",
                                                    convert_yuy2_to_rgb_optimized,
                                                    yuy2, rgb_optimized,
                                                    width, height, iterations);
        
        printf("\nSpeedup: %.2fx\n", scalar_time / optimized_time);
        
        // Verify correctness (allow small rounding differences)
        printf("\nVerifying correctness (tolerance=2):\n");
        int errors = verify_conversion(rgb_optimized, rgb_scalar, pixel_count, 2);
        if (errors == 0) {
            printf("  PASS - All pixels match\n");
        } else {
            printf("  FAIL - %d pixels differ (%.2f%%)\n", 
                   errors, (errors * 100.0) / pixel_count);
        }
        
        // Benchmark individual SIMD implementations if available
#ifdef SIMD_SUPPORT_SSE2
        if (yuy2_cpu_has_sse2()) {
            double sse2_time = benchmark_conversion("SSE2",
                                                   convert_yuy2_to_rgb_sse2,
                                                   yuy2, rgb_sse2,
                                                   width, height, iterations);
            printf("Speedup vs scalar: %.2fx\n", scalar_time / sse2_time);
        }
#endif

#ifdef SIMD_SUPPORT_SSSE3  
        if (yuy2_cpu_has_ssse3()) {
            double ssse3_time = benchmark_conversion("SSSE3",
                                                    convert_yuy2_to_rgb_ssse3,
                                                    yuy2, rgb_ssse3,
                                                    width, height, iterations);
            printf("Speedup vs scalar: %.2fx\n", scalar_time / ssse3_time);
        }
#endif

#ifdef SIMD_SUPPORT_AVX2
        if (yuy2_cpu_has_avx2()) {
            double avx2_time = benchmark_conversion("AVX2",
                                                   convert_yuy2_to_rgb_avx2,
                                                   yuy2, rgb_avx2,
                                                   width, height, iterations);
            printf("Speedup vs scalar: %.2fx\n", scalar_time / avx2_time);
        }
#endif
        
        // Cleanup
        free(yuy2);
        free(rgb_scalar);
        free(rgb_sse2);
        free(rgb_ssse3);
        free(rgb_avx2);
        free(rgb_optimized);
    }
    
    printf("\n\nBenchmark complete!\n");
    return 0;
}

#else // !_WIN32

#include <stdio.h>

int main(void) {
    printf("YUY2 benchmark is Windows-only\n");
    return 0;
}

#endif // _WIN32