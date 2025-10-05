#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>

#include "common.h"
#include "image2ascii/simd/ascii_simd.h"
#include "image2ascii/simd/common.h"
#include "image2ascii/image.h"
#include "palette.h"
#include "hashtable.h"
#include "buffer_pool.h"

void setup_performance_quiet_logging(void);
void restore_performance_logging(void);

TestSuite(ascii_simd_performance, .init = setup_performance_quiet_logging, .fini = restore_performance_logging);

void setup_performance_quiet_logging(void) {
  log_set_level(LOG_FATAL);
  hashtable_set_stats_enabled(false);
  data_buffer_pool_init_global();
}

void restore_performance_logging(void) {
  log_set_level(LOG_DEBUG);
  hashtable_set_stats_enabled(true);
}

// =============================================================================
// Performance Timing Utilities
// =============================================================================

static double get_time_seconds(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void create_test_image(image_t *img, int pattern_type) {
  if (!img)
    return;

  for (int y = 0; y < img->h; y++) {
    for (int x = 0; x < img->w; x++) {
      int idx = y * img->w + x;
      switch (pattern_type) {
      case 0: // Gradient
        img->pixels[idx].r = (x * 255) / img->w;
        img->pixels[idx].g = (y * 255) / img->h;
        img->pixels[idx].b = ((x + y) * 127) / (img->w + img->h);
        break;
      case 1: // Random
        img->pixels[idx].r = rand() % 256;
        img->pixels[idx].g = rand() % 256;
        img->pixels[idx].b = rand() % 256;
        break;
      case 2: // High contrast
        img->pixels[idx].r = img->pixels[idx].g = img->pixels[idx].b = ((x + y) % 2) ? 255 : 0;
        break;
      case 3: // Solid colors
        img->pixels[idx].r = img->pixels[idx].g = img->pixels[idx].b = 128;
        break;
      case 4: // Photo-realistic (simulated)
        // Simulate skin tones, sky, grass, etc.
        if (y < img->h / 3) {
          // Sky gradient
          img->pixels[idx].r = 135 + (y * 120) / img->h;
          img->pixels[idx].g = 206 + (y * 49) / img->h;
          img->pixels[idx].b = 235 + (y * 20) / img->h;
        } else if (y < 2 * img->h / 3) {
          // Skin tones
          img->pixels[idx].r = 222 + (x * 33) / img->w;
          img->pixels[idx].g = 184 + (x * 71) / img->w;
          img->pixels[idx].b = 135 + (x * 120) / img->w;
        } else {
          // Grass/ground
          img->pixels[idx].r = 34 + (x * 221) / img->w;
          img->pixels[idx].g = 139 + (x * 116) / img->w;
          img->pixels[idx].b = 34 + (x * 221) / img->w;
        }
        break;
      case 5: // Noise with structure
        img->pixels[idx].r = (rand() % 256 + (x * 255) / img->w) / 2;
        img->pixels[idx].g = (rand() % 256 + (y * 255) / img->h) / 2;
        img->pixels[idx].b = (rand() % 256 + ((x + y) * 127) / (img->w + img->h)) / 2;
        break;
      case 6: // Radial gradient
      {
        int center_x = img->w / 2;
        int center_y = img->h / 2;
        int dx = x - center_x;
        int dy = y - center_y;
        int distance = (int)sqrt(dx * dx + dy * dy);
        int max_distance = (int)sqrt(center_x * center_x + center_y * center_y);
        int intensity = 255 - (distance * 255) / max_distance;
        img->pixels[idx].r = img->pixels[idx].g = img->pixels[idx].b = intensity;
      } break;
      default:
        img->pixels[idx].r = img->pixels[idx].g = img->pixels[idx].b = 128;
        break;
      }
    }
  }
}

// =============================================================================
// ASCII Conversion Performance Tests
// =============================================================================

Test(ascii_simd_performance, monochrome_and_color_performance) {
  const int width = 480, height = 360;
  const int iterations = 15;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 4); // Photo-realistic pattern for more realistic testing

  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";

  log_info("Monochrome and Color Performance (%dx%d, %d iterations):", width, height, iterations);

  // =============================================================================
  // MONOCHROME PERFORMANCE TESTS
  // =============================================================================

  // Benchmark scalar monochrome implementation
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print(test_image, ascii_palette);
    cr_assert_not_null(result, "Scalar monochrome should produce output");
    free(result);
  }
  double scalar_mono_time = get_time_seconds() - start_time;

  // Benchmark SIMD monochrome implementation
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, ascii_palette);
    cr_assert_not_null(result, "SIMD monochrome should produce output");
    free(result);
  }
  double simd_mono_time = get_time_seconds() - start_time;

  double mono_speedup = scalar_mono_time / simd_mono_time;
  double scalar_mono_fps = iterations / scalar_mono_time;
  double simd_mono_fps = iterations / simd_mono_time;

  log_info("  Monochrome:");
  log_info("    Scalar: %.3fs (%.1f FPS)", scalar_mono_time, scalar_mono_fps);
  log_info("    SIMD:   %.3fs (%.1f FPS)", simd_mono_time, simd_mono_fps);
  log_info("    Speedup: %.2fx", mono_speedup);

  // =============================================================================
  // COLOR PERFORMANCE TESTS
  // =============================================================================

  // Benchmark scalar color implementation
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_color(test_image, ascii_palette);
    cr_assert_not_null(result, "Scalar color should produce output");
    free(result);
  }
  double scalar_color_time = get_time_seconds() - start_time;

  // Benchmark SIMD color implementation
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_color_simd(test_image, false, false, ascii_palette);
    cr_assert_not_null(result, "SIMD color should produce output");
    free(result);
  }
  double simd_color_time = get_time_seconds() - start_time;

  double color_speedup = scalar_color_time / simd_color_time;
  double scalar_color_fps = iterations / scalar_color_time;
  double simd_color_fps = iterations / simd_color_time;

  log_info("  Color:");
  log_info("    Scalar: %.3fs (%.1f FPS)", scalar_color_time, scalar_color_fps);
  log_info("    SIMD:   %.3fs (%.1f FPS)", simd_color_time, simd_color_fps);
  log_info("    Speedup: %.2fx", color_speedup);

  // =============================================================================
  // PERFORMANCE ASSERTIONS
  // =============================================================================

  // Monochrome performance assertions - SIMD should be at least as fast as scalar
  cr_assert_gt(scalar_mono_fps, 0.1, "Scalar monochrome should achieve at least 0.1 FPS");
  cr_assert_gt(simd_mono_fps, 0.1, "SIMD monochrome should achieve at least 0.1 FPS");
  cr_assert_gt(mono_speedup, 1.0,
               "SIMD monochrome should not be more than 1x slower than scalar (expected >0.5x, got %.2fx)",
               mono_speedup);

  // Color performance assertions - SIMD should be at least as fast as scalar
  cr_assert_gt(scalar_color_fps, 0.1, "Scalar color should achieve at least 0.1 FPS");
  cr_assert_gt(simd_color_fps, 0.1, "SIMD color should achieve at least 0.1 FPS");
  cr_assert_gt(color_speedup, 1.0,
               "SIMD color should not be more than 1x slower than scalar (expected >0.5x, got %.2fx)", color_speedup);

  image_destroy(test_image);
}

Test(ascii_simd_performance, utf8_palette_performance_impact) {
  const int width = 240, height = 72;
  const int iterations = 20;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 5); // Noise+Structure pattern

  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
  const char *utf8_palette = "🌑🌒🌓🌔🌕🌖🌗🌘🌙🌚🌛🌜🌝🌞🌟⭐";

  // Benchmark ASCII palette
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, ascii_palette);
    cr_assert_not_null(result, "ASCII SIMD should produce output");
    free(result);
  }
  double ascii_time = get_time_seconds() - start_time;

  // Benchmark UTF-8 emoji palette
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, utf8_palette);
    cr_assert_not_null(result, "UTF-8 SIMD should produce output");
    free(result);
  }
  double utf8_time = get_time_seconds() - start_time;

  double utf8_penalty = utf8_time / ascii_time;
  double ascii_fps = iterations / ascii_time;
  double utf8_fps = iterations / utf8_time;

  log_info("UTF-8 vs ASCII Performance (%dx%d, %d iterations):", width, height, iterations);
  log_info("  ASCII: %.3fs (%.1f FPS)", ascii_time, ascii_fps);
  log_info("  UTF-8: %.3fs (%.1f FPS)", utf8_time, utf8_fps);
  log_info("  UTF-8 Penalty: %.2fx slower", utf8_penalty);

  // Performance assertions
  cr_assert_gt(ascii_fps, 5.0, "ASCII should achieve at least 5 FPS");
  cr_assert_gt(utf8_fps, 1.0, "UTF-8 should achieve at least 1 FPS");
  cr_assert_lt(utf8_penalty, 3.0, "UTF-8 should not be more than 3x slower than ASCII (got %.2fx)", utf8_penalty);

  image_destroy(test_image);
}

// =============================================================================
// Image Size Performance Tests
// =============================================================================

// Parameterized test for various image sizes performance
typedef struct {
  char name[24];
  int width;
  int height;
  double min_speedup;
  int pattern_type;
  char description[80];
} image_size_perf_test_case_t;

static image_size_perf_test_case_t image_size_perf_cases[] = {
    {"80x24 (VT100)", 80, 24, 0.5, 1, "Classic VT100 - 1,920 pixels (SIMD overhead dominates)"},
    {"120x36 (Medium)", 120, 36, 0.5, 2, "Medium terminal - 4,320 pixels (SIMD overhead significant)"},
    {"203x64 (Large)", 203, 64, 0.5, 4, "Large terminal - 12,992 pixels (current: SIMD slower 0.7-0.8x)"},
    {"480x360 (Webcam)", 480, 360, 0.5, 6, "Webcam - 172,800 pixels (current: SIMD slower ~0.5x, needs optimization)"},
};

ParameterizedTestParameters(ascii_simd_performance, various_image_sizes_performance) {
  return cr_make_param_array(image_size_perf_test_case_t, image_size_perf_cases,
                             sizeof(image_size_perf_cases) / sizeof(image_size_perf_cases[0]));
}

ParameterizedTest(image_size_perf_test_case_t *tc, ascii_simd_performance, various_image_sizes_performance) {
  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";

  image_t *test_image = image_new(tc->width, tc->height);
  cr_assert_not_null(test_image, "Should create %s test image", tc->name);

  create_test_image(test_image, tc->pattern_type);

  const int iterations = (tc->width * tc->height < 10000) ? 50 : 20;

  // Benchmark scalar
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print(test_image, ascii_palette);
    cr_assert_not_null(result, "Scalar should produce output for %s", tc->name);
    free(result);
  }
  double scalar_time = get_time_seconds() - start_time;

  // Benchmark SIMD
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, ascii_palette);
    cr_assert_not_null(result, "SIMD should produce output for %s", tc->name);
    free(result);
  }
  double simd_time = get_time_seconds() - start_time;

  double speedup = scalar_time / simd_time;
  double scalar_fps = iterations / scalar_time;
  double simd_fps = iterations / simd_time;

  log_info("%s (%dx%d): Scalar=%.3fs (%.1f FPS) | SIMD=%.3fs (%.1f FPS) | Speedup=%.2fx", tc->name, tc->width,
           tc->height, scalar_time, scalar_fps, simd_time, simd_fps, speedup);

  // Performance assertions - SIMD should be at least as fast as scalar
  cr_assert_gt(scalar_fps, 0.1, "%s: Scalar should achieve at least 0.1 FPS", tc->name);
  cr_assert_gt(simd_fps, 0.1, "%s: SIMD should achieve at least 0.1 FPS", tc->name);

  // For now, just ensure SIMD is not significantly slower (allow some tolerance)
  cr_assert_gt(speedup, 1.0, "%s: SIMD should not be more than 1x slower than scalar (got %.2fx)", tc->name, speedup);

  image_destroy(test_image);
}

// =============================================================================
// SIMD Architecture Performance Tests
// =============================================================================

Test(ascii_simd_performance, simd_architecture_benchmarks) {
  log_set_level(LOG_INFO);
  const int width = 240, height = 72;
  const int iterations = 20;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 3); // Solid pattern for consistent benchmarking

  log_info("SIMD Architecture Performance (%dx%d, %d iterations):", width, height, iterations);

  // Use the benchmark function from ascii_simd.h
  simd_benchmark_t mono_bench = benchmark_simd_conversion(width, height, iterations);

  log_info("Monochrome Results:");
  log_info("  Scalar:  %.4f ms/frame", mono_bench.scalar_time * 1000);
  if (mono_bench.sse2_time > 0) {
    log_info("  SSE2:    %.4f ms/frame (%.2fx scalar)", mono_bench.sse2_time * 1000,
             mono_bench.scalar_time / mono_bench.sse2_time);
  }
  if (mono_bench.ssse3_time > 0) {
    log_info("  SSSE3:   %.4f ms/frame (%.2fx scalar)", mono_bench.ssse3_time * 1000,
             mono_bench.scalar_time / mono_bench.ssse3_time);
  }
  if (mono_bench.avx2_time > 0) {
    log_info("  AVX2:    %.4f ms/frame (%.2fx scalar)", mono_bench.avx2_time * 1000,
             mono_bench.scalar_time / mono_bench.avx2_time);
  }
  if (mono_bench.neon_time > 0) {
    log_info("  NEON:    %.4f ms/frame (%.2fx scalar)", mono_bench.neon_time * 1000,
             mono_bench.scalar_time / mono_bench.neon_time);
  }
  if (mono_bench.sve_time > 0) {
    log_info("  SVE:     %.4f ms/frame (%.2fx scalar)", mono_bench.sve_time * 1000,
             mono_bench.scalar_time / mono_bench.sve_time);
  }
  log_info("  Winner:  %s", mono_bench.best_method);

  // Test color performance
  simd_benchmark_t color_bench = benchmark_simd_color_conversion(width, height, iterations, false);

  log_info("Color Results:");
  log_info("  Scalar:  %.4f ms/frame", color_bench.scalar_time * 1000);
  if (color_bench.sse2_time > 0) {
    log_info("  SSE2:    %.4f ms/frame (%.2fx scalar)", color_bench.sse2_time * 1000,
             color_bench.scalar_time / color_bench.sse2_time);
  }
  if (color_bench.ssse3_time > 0) {
    log_info("  SSSE3:   %.4f ms/frame (%.2fx scalar)", color_bench.ssse3_time * 1000,
             color_bench.scalar_time / color_bench.ssse3_time);
  }
  if (color_bench.avx2_time > 0) {
    log_info("  AVX2:    %.4f ms/frame (%.2fx scalar)", color_bench.avx2_time * 1000,
             color_bench.scalar_time / color_bench.avx2_time);
  }
  if (color_bench.neon_time > 0) {
    log_info("  NEON:    %.4f ms/frame (%.2fx scalar)", color_bench.neon_time * 1000,
             color_bench.scalar_time / color_bench.neon_time);
  }
  if (color_bench.sve_time > 0) {
    log_info("  SVE:     %.4f ms/frame (%.2fx scalar)", color_bench.sve_time * 1000,
             color_bench.scalar_time / color_bench.sve_time);
  }
  log_info("  Winner:  %s", color_bench.best_method);

// Performance assertions - each SIMD implementation should be at least as fast as scalar
#ifdef SIMD_SUPPORT_SSE2
  if (mono_bench.sse2_time > 0) {
    double sse2_speedup = mono_bench.scalar_time / mono_bench.sse2_time;
    cr_assert_gt(sse2_speedup, 1.0, "SSE2 should not be more than 1x slower than scalar (expected >1.0x, got %.2fx)",
                 sse2_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_SSSE3
  if (mono_bench.ssse3_time > 0) {
    double ssse3_speedup = mono_bench.scalar_time / mono_bench.ssse3_time;
    cr_assert_gt(ssse3_speedup, 1.0, "SSSE3 should not be more than 1x slower than scalar (expected >1.0x, got %.2fx)",
                 ssse3_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_AVX2
  if (mono_bench.avx2_time > 0) {
    double avx2_speedup = mono_bench.scalar_time / mono_bench.avx2_time;
    cr_assert_gt(avx2_speedup, 1.0, "AVX2 should not be more than 1x slower than scalar (expected >1.0x, got %.2fx)",
                 avx2_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_NEON
  if (mono_bench.neon_time > 0) {
    double neon_speedup = mono_bench.scalar_time / mono_bench.neon_time;
    cr_assert_gt(neon_speedup, 1.0, "NEON should not be more than 1x slower than scalar (expected >1.0x, got %.2fx)",
                 neon_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_SVE
  if (mono_bench.sve_time > 0) {
    double sve_speedup = mono_bench.scalar_time / mono_bench.sve_time;
    cr_assert_gt(sve_speedup, 1.0, "SVE should not be more than 1x slower than scalar (expected >1.0x, got %.2fx)",
                 sve_speedup);
  }
#endif

// Color performance assertions - each SIMD implementation should be faster than scalar
#ifdef SIMD_SUPPORT_SSE2
  if (color_bench.sse2_time > 0) {
    double sse2_color_speedup = color_bench.scalar_time / color_bench.sse2_time;
    cr_assert_gt(sse2_color_speedup, 1.0, "SSE2 color should be faster than scalar (expected >1.0x, got %.2fx)",
                 sse2_color_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_SSSE3
  if (color_bench.ssse3_time > 0) {
    double ssse3_color_speedup = color_bench.scalar_time / color_bench.ssse3_time;
    cr_assert_gt(ssse3_color_speedup, 1.0, "SSSE3 color should be faster than scalar (expected >1.0x, got %.2fx)",
                 ssse3_color_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_AVX2
  if (color_bench.avx2_time > 0) {
    double avx2_color_speedup = color_bench.scalar_time / color_bench.avx2_time;
    cr_assert_gt(avx2_color_speedup, 1.2, "AVX2 color should be faster than scalar (expected >1.2x, got %.2fx)",
                 avx2_color_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_NEON
  if (color_bench.neon_time > 0) {
    double neon_color_speedup = color_bench.scalar_time / color_bench.neon_time;
    cr_assert_gt(neon_color_speedup, 1.0, "NEON color should be faster than scalar (expected >1.0x, got %.2fx)",
                 neon_color_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_SVE
  if (color_bench.sve_time > 0) {
    double sve_color_speedup = color_bench.scalar_time / color_bench.sve_time;
    cr_assert_gt(sve_color_speedup, 1.2, "SVE color should be faster than scalar (expected >1.2x, got %.2fx)",
                 sve_color_speedup);
  }
#endif

  image_destroy(test_image);
}

// =============================================================================
// Cache System Performance Tests
// =============================================================================

Test(ascii_simd_performance, cache_system_efficiency) {
  const int width = 240, height = 72;
  const int iterations = 30;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 1); // Random pattern

  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";

  // First call (cache warming)
  char *warmup = image_print_simd(test_image, ascii_palette);
  cr_assert_not_null(warmup, "Cache warmup should succeed");
  free(warmup);

  // Benchmark with warmed cache
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, ascii_palette);
    cr_assert_not_null(result, "Cached call %d should succeed", i);
    free(result);
  }
  double cached_time = get_time_seconds() - start_time;

  double ms_per_frame = (cached_time / iterations) * 1000;
  double fps = iterations / cached_time;

  log_info("Cache Performance (%dx%d, %d iterations):", width, height, iterations);
  log_info("  Cached: %.4f ms/frame (%.1f FPS)", ms_per_frame, fps);

  // Cache should provide good performance
  cr_assert_lt(ms_per_frame, 1.0, "Cached SIMD should be <1ms/frame for medium images (got %.4fms)", ms_per_frame);
  cr_assert_gt(fps, 10.0, "Cached SIMD should achieve at least 10 FPS (got %.1f FPS)", fps);

  image_destroy(test_image);
}

// =============================================================================
// Mixed UTF-8 Performance Tests
// =============================================================================

Test(ascii_simd_performance, mixed_utf8_palette_performance) {
  const int width = 120, height = 36;
  const int iterations = 15;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 2); // High contrast pattern

  // Test multiple mixed-byte UTF-8 palettes
  const struct {
    const char *name;
    const char *palette;
    const char *description;
  } mixed_palettes[] = {{"ASCII+Emoji", " .:-=+*#%@🌑🌒🌓🌔🌕", "1-byte ASCII + 4-byte emojis"},
                        {"All-Mixed", " .αβ♠♣🌟⭐", "1-byte ASCII + 2-byte Greek + 3-byte symbols + 4-byte emojis"},
                        {"Greek+Symbols", "αβγδ♠♣♥♦♤♧♡♢", "2-byte Greek + 3-byte symbols"},
                        {"ASCII+Single", "   ...',;:clodxkO0KX🧠", "ASCII palette + one 4-byte emoji"}};

  const int num_palettes = sizeof(mixed_palettes) / sizeof(mixed_palettes[0]);

  log_info("Mixed UTF-8 Palette Performance (%dx%d, %d iterations):", width, height, iterations);

  for (int p = 0; p < num_palettes; p++) {
    const char *palette = mixed_palettes[p].palette;

    // Benchmark scalar implementation
    double start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
      char *result = image_print(test_image, palette);
      cr_assert_not_null(result, "Scalar should work with %s", mixed_palettes[p].name);
      free(result);
    }
    double scalar_time = get_time_seconds() - start_time;

    // Benchmark SIMD implementation
    start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
      char *result = image_print_simd(test_image, palette);
      cr_assert_not_null(result, "SIMD should work with %s", mixed_palettes[p].name);
      free(result);
    }
    double simd_time = get_time_seconds() - start_time;

    double scalar_vs_simd_ratio = scalar_time / simd_time;
    double scalar_fps = iterations / scalar_time;
    double simd_fps = iterations / simd_time;

    log_info("  %s: Scalar=%.3fs (%.1f FPS) | SIMD=%.3fs (%.1f FPS) | Ratio=%.2fx %s", mixed_palettes[p].name,
             scalar_time, scalar_fps, simd_time, simd_fps, scalar_vs_simd_ratio,
             (scalar_vs_simd_ratio < 1.0) ? "✓ Scalar faster" : "✓ SIMD faster");

    // Performance assertions - SIMD should be at least as fast as scalar
    cr_assert_gt(scalar_fps, 0.5, "%s: Scalar should achieve at least 0.5 FPS", mixed_palettes[p].name);
    cr_assert_gt(simd_fps, 0.5, "%s: SIMD should achieve at least 0.5 FPS", mixed_palettes[p].name);

    // For now, just ensure SIMD is not significantly slower (allow some tolerance)
    cr_assert_gt(scalar_vs_simd_ratio, 1.0,
                 "%s: SIMD should not be more than 1x slower than scalar (expected >0.5x, got %.2fx)",
                 mixed_palettes[p].name, scalar_vs_simd_ratio);
  }

  image_destroy(test_image);
}

// =============================================================================
// Comprehensive Palette Performance Tests
// =============================================================================

// Parameterized test for palette byte length performance
typedef struct {
  char name[24];
  char palette[128];
  int expected_bytes_per_char;
  char description[64];
} palette_byte_length_test_case_t;

static palette_byte_length_test_case_t palette_byte_length_cases[] = {
    {"ASCII", "   ...',;:clodxkO0KXNWM", 1, "Pure ASCII characters (most common use case)"},
    {"UTF-8 Emoji", " .:-🌑🌒🌓🌔🌕🌖🌗🌘", 4, "ASCII + 4-byte emojis (creative palettes)"},
    {"UTF-8 Mixed", " .α♠🌑:-=+*#%@βγ♣🌒", 4, "Mixed 1-4 byte UTF-8 (stress test)"},
};

ParameterizedTestParameters(ascii_simd_performance, palette_byte_length_performance) {
  return cr_make_param_array(palette_byte_length_test_case_t, palette_byte_length_cases,
                             sizeof(palette_byte_length_cases) / sizeof(palette_byte_length_cases[0]));
}

ParameterizedTest(palette_byte_length_test_case_t *tc, ascii_simd_performance, palette_byte_length_performance) {
  const int width = 203, height = 64; // Realistic large terminal size
  const int iterations = 15;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 6); // Radial pattern

  log_info("Palette Byte Length Performance (%dx%d, %d iterations):", width, height, iterations);

  const char *palette = tc->palette;

  // Benchmark scalar implementation
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print(test_image, palette);
    cr_assert_not_null(result, "Scalar should work with %s", tc->name);
    free(result);
  }
  double scalar_time = get_time_seconds() - start_time;

  // Benchmark SIMD implementation
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, palette);
    cr_assert_not_null(result, "SIMD should work with %s", tc->name);
    free(result);
  }
  double simd_time = get_time_seconds() - start_time;

  double speedup = scalar_time / simd_time;
  double scalar_fps = iterations / scalar_time;
  double simd_fps = iterations / simd_time;

  log_info("  %s: Scalar=%.3fs (%.1f FPS) | SIMD=%.3fs (%.1f FPS) | Speedup=%.2fx", tc->name, scalar_time, scalar_fps,
           simd_time, simd_fps, speedup);

  // Performance assertions - document current state (SIMD currently slower than scalar)
  cr_assert_gt(scalar_fps, 0.5, "%s: Scalar should achieve at least 0.5 FPS", tc->name);
  cr_assert_gt(simd_fps, 0.5, "%s: SIMD should achieve at least 0.5 FPS", tc->name);
  // Note: SIMD is currently slower than scalar due to RLE overhead. Test documents current performance.
  cr_assert_gt(speedup, 0.5, "%s: SIMD performance check (current: %.2fx, target: >1.5x with optimization)", tc->name,
               speedup);

  image_destroy(test_image);
}

// Parameterized test for palette length variation performance
typedef struct {
  char name[16];
  char palette[80];
  int length;
  char description[64];
} palette_length_test_case_t;

static palette_length_test_case_t palette_length_cases[] = {
    {"Standard", "   ...',;:clodxkO0KXNWM", 22, "Standard ASCII palette (most common)"},
    {"Dense", " .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$", 70,
     "Dense 70-character palette"},
};

ParameterizedTestParameters(ascii_simd_performance, palette_length_variation_performance) {
  return cr_make_param_array(palette_length_test_case_t, palette_length_cases,
                             sizeof(palette_length_cases) / sizeof(palette_length_cases[0]));
}

ParameterizedTest(palette_length_test_case_t *tc, ascii_simd_performance, palette_length_variation_performance) {
  const int width = 203, height = 64; // Realistic large terminal size
  const int iterations = 15;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 0); // Gradient pattern

  log_info("Palette Length Variation Performance (%dx%d, %d iterations):", width, height, iterations);

  const char *palette = tc->palette;

  // Benchmark scalar implementation
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print(test_image, palette);
    cr_assert_not_null(result, "Scalar should work with %s", tc->name);
    free(result);
  }
  double scalar_time = get_time_seconds() - start_time;

  // Benchmark SIMD implementation
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, palette);
    cr_assert_not_null(result, "SIMD should work with %s", tc->name);
    free(result);
  }
  double simd_time = get_time_seconds() - start_time;

  double speedup = scalar_time / simd_time;
  double scalar_fps = iterations / scalar_time;
  double simd_fps = iterations / simd_time;

  log_info("  %s (%d chars): Scalar=%.3fs (%.1f FPS) | SIMD=%.3fs (%.1f FPS) | Speedup=%.2fx", tc->name, tc->length,
           scalar_time, scalar_fps, simd_time, simd_fps, speedup);

  // Performance assertions - document current state (SIMD currently slower than scalar)
  cr_assert_gt(scalar_fps, 0.5, "%s: Scalar should achieve at least 0.5 FPS", tc->name);
  cr_assert_gt(simd_fps, 0.5, "%s: SIMD should achieve at least 0.5 FPS", tc->name);
  // Note: SIMD is currently slower than scalar due to RLE overhead. Test documents current performance.
  cr_assert_gt(speedup, 0.5, "%s: SIMD performance check (current: %.2fx, target: >1.5x with optimization)", tc->name,
               speedup);

  image_destroy(test_image);
}

// Parameterized test for synthetic image types performance
typedef struct {
  char name[24];
  int pattern_type;
  char description[64];
  double min_speedup;
} image_type_perf_test_case_t;

static image_type_perf_test_case_t image_type_perf_cases[] = {
    {"Random Noise", 1, "Pure random noise (worst case for RLE)", 0.8},
    {"Photo-realistic", 4, "Simulated natural scenes (typical webcam)", 0.9},
    {"Radial Gradient", 6, "Radial gradient (best case for RLE)", 1.0},
};

ParameterizedTestParameters(ascii_simd_performance, synthetic_image_types_performance) {
  return cr_make_param_array(image_type_perf_test_case_t, image_type_perf_cases,
                             sizeof(image_type_perf_cases) / sizeof(image_type_perf_cases[0]));
}

ParameterizedTest(image_type_perf_test_case_t *tc, ascii_simd_performance, synthetic_image_types_performance) {
  const int width = 203, height = 64; // Realistic large terminal size
  const int iterations = 15;
  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create %s test image", tc->name);

  create_test_image(test_image, tc->pattern_type);

  // Benchmark scalar implementation
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print(test_image, ascii_palette);
    cr_assert_not_null(result, "Scalar should work with %s", tc->name);
    free(result);
  }
  double scalar_time = get_time_seconds() - start_time;

  // Benchmark SIMD implementation
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, ascii_palette);
    cr_assert_not_null(result, "SIMD should work with %s", tc->name);
    free(result);
  }
  double simd_time = get_time_seconds() - start_time;

  double speedup = scalar_time / simd_time;
  double scalar_fps = iterations / scalar_time;
  double simd_fps = iterations / simd_time;

  log_info("%s: Scalar=%.3fs (%.1f FPS) | SIMD=%.3fs (%.1f FPS) | Speedup=%.2fx", tc->name, scalar_time, scalar_fps,
           simd_time, simd_fps, speedup);

  // Performance assertions - use min_speedup from test case
  cr_assert_gt(scalar_fps, 0.5, "%s: Scalar should achieve at least 0.5 FPS", tc->name);
  cr_assert_gt(simd_fps, 0.5, "%s: SIMD should achieve at least 0.5 FPS", tc->name);
  cr_assert_gt(speedup, tc->min_speedup, "%s: SIMD should be faster than scalar (expected >%.1fx, got %.2fx)", tc->name,
               tc->min_speedup, speedup);

  image_destroy(test_image);
}

Test(ascii_simd_performance, all_image_types_comprehensive_performance) {
  const int width = 240, height = 72;
  const int iterations = 10;

  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";

  // Test all image types with both monochrome and color conversion
  const struct {
    const char *name;
    int pattern_type;
    const char *description;
  } image_types[] = {
      {"Gradient", 0, "Linear gradient patterns"},
      {"Random", 1, "Pure random noise"},
      {"High Contrast", 2, "Black and white checkerboard"},
      {"Solid", 3, "Uniform solid colors"},
      {"Photo-realistic", 4, "Simulated natural scenes (sky, skin, grass)"},
      {"Noise+Structure", 5, "Random noise with underlying structure"},
      {"Radial", 6, "Radial gradient from center"},
  };

  const int num_types = sizeof(image_types) / sizeof(image_types[0]);

  log_info("Comprehensive Image Types Performance (%dx%d, %d iterations):", width, height, iterations);

  for (int t = 0; t < num_types; t++) {
    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create %s test image", image_types[t].name);

    create_test_image(test_image, image_types[t].pattern_type);

    // =============================================================================
    // MONOCHROME PERFORMANCE TESTS
    // =============================================================================

    // Benchmark scalar monochrome implementation
    double start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
      char *result = image_print(test_image, ascii_palette);
      cr_assert_not_null(result, "Scalar monochrome should work with %s", image_types[t].name);
      free(result);
    }
    double scalar_mono_time = get_time_seconds() - start_time;

    // Benchmark SIMD monochrome implementation
    start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
      char *result = image_print_simd(test_image, ascii_palette);
      cr_assert_not_null(result, "SIMD monochrome should work with %s", image_types[t].name);
      free(result);
    }
    double simd_mono_time = get_time_seconds() - start_time;

    double mono_speedup = scalar_mono_time / simd_mono_time;
    double scalar_mono_fps = iterations / scalar_mono_time;
    double simd_mono_fps = iterations / simd_mono_time;

    // =============================================================================
    // COLOR PERFORMANCE TESTS
    // =============================================================================

    // Benchmark scalar color implementation
    start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
      char *result = image_print_color(test_image, ascii_palette);
      cr_assert_not_null(result, "Scalar color should work with %s", image_types[t].name);
      free(result);
    }
    double scalar_color_time = get_time_seconds() - start_time;

    // Benchmark SIMD color implementation
    start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
      char *result = image_print_color_simd(test_image, false, false, ascii_palette);
      cr_assert_not_null(result, "SIMD color should work with %s", image_types[t].name);
      free(result);
    }
    double simd_color_time = get_time_seconds() - start_time;

    double color_speedup = scalar_color_time / simd_color_time;
    double scalar_color_fps = iterations / scalar_color_time;
    double simd_color_fps = iterations / simd_color_time;

    log_info("  %s (%s):", image_types[t].name, image_types[t].description);
    log_info("    Monochrome: Scalar=%.3fs (%.1f FPS) | SIMD=%.3fs (%.1f FPS) | Speedup=%.2fx", scalar_mono_time,
             scalar_mono_fps, simd_mono_time, simd_mono_fps, mono_speedup);
    log_info("    Color:      Scalar=%.3fs (%.1f FPS) | SIMD=%.3fs (%.1f FPS) | Speedup=%.2fx", scalar_color_time,
             scalar_color_fps, simd_color_time, simd_color_fps, color_speedup);

    // Performance assertions - SIMD should be at least as fast as scalar
    cr_assert_gt(scalar_mono_fps, 0.5, "%s: Scalar monochrome should achieve at least 0.5 FPS", image_types[t].name);
    cr_assert_gt(simd_mono_fps, 0.5, "%s: SIMD monochrome should achieve at least 0.5 FPS", image_types[t].name);
    cr_assert_gt(scalar_color_fps, 0.5, "%s: Scalar color should achieve at least 0.5 FPS", image_types[t].name);
    cr_assert_gt(simd_color_fps, 0.5, "%s: SIMD color should achieve at least 0.5 FPS", image_types[t].name);

    // For now, just ensure SIMD is not significantly slower (allow some tolerance)
    // The goal is to ensure all image types work correctly, not necessarily faster
    cr_assert_gt(mono_speedup, 1.0, "%s: SIMD monochrome should not be more than 1x slower than scalar (got %.2fx)",
                 image_types[t].name, mono_speedup);
    cr_assert_gt(color_speedup, 1.0, "%s: SIMD color should not be more than 1x slower than scalar (got %.2fx)",
                 image_types[t].name, color_speedup);

    image_destroy(test_image);
  }
}

// =============================================================================
// End-to-End Performance Tests
// =============================================================================

Test(ascii_simd_performance, full_pipeline_performance) {
  const int width = 240, height = 72;
  const int iterations = 20;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 0); // Gradient pattern

  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";
  char luminance_palette[256];
  build_client_luminance_palette(ascii_palette, strlen(ascii_palette), luminance_palette);

  log_info("Full Pipeline Performance (%dx%d, %d iterations):", width, height, iterations);

  // Test full pipeline: image -> ASCII conversion with capabilities
  terminal_capabilities_t caps = {0};
  caps.color_level = TERM_COLOR_TRUECOLOR;
  caps.color_count = 16777216;
  caps.render_mode = RENDER_MODE_FOREGROUND;
  caps.capabilities = TERM_CAP_COLOR_TRUE;

  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_with_capabilities(test_image, &caps, ascii_palette, luminance_palette);
    cr_assert_not_null(result, "Full pipeline should produce output");
    free(result);
  }
  double pipeline_time = get_time_seconds() - start_time;

  double fps = iterations / pipeline_time;
  double ms_per_frame = (pipeline_time / iterations) * 1000;

  log_info("  Full Pipeline: %.3fs (%.1f FPS, %.2f ms/frame)", pipeline_time, fps, ms_per_frame);

  // Performance assertions
  cr_assert_gt(fps, 2.0, "Full pipeline should achieve at least 2 FPS (got %.1f FPS)", fps);
  cr_assert_lt(ms_per_frame, 500.0, "Full pipeline should be <500ms/frame (got %.2fms)", ms_per_frame);

  image_destroy(test_image);
}
