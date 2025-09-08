#include <criterion/criterion.h>
#include <criterion/new/assert.h>
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

void setup_performance_quiet_logging(void);
void restore_performance_logging(void);

TestSuite(ascii_simd_performance, .init = setup_performance_quiet_logging, .fini = restore_performance_logging);

void setup_performance_quiet_logging(void) {
  log_set_level(LOG_FATAL);
  hashtable_set_stats_enabled(false);
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

Test(ascii_simd_performance, monochrome_scalar_vs_simd_performance) {
  const int width = 320, height = 240;
  const int iterations = 20;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 0); // Gradient pattern

  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";

  // Benchmark scalar implementation
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print(test_image, ascii_palette);
    cr_assert_not_null(result, "Scalar should produce output");
    free(result);
  }
  double scalar_time = get_time_seconds() - start_time;

  // Benchmark SIMD implementation
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, ascii_palette);
    cr_assert_not_null(result, "SIMD should produce output");
    free(result);
  }
  double simd_time = get_time_seconds() - start_time;

  double speedup = scalar_time / simd_time;
  double scalar_fps = iterations / scalar_time;
  double simd_fps = iterations / simd_time;

  log_info("Monochrome Performance (%dx%d, %d iterations):", width, height, iterations);
  log_info("  Scalar: %.3fs (%.1f FPS)", scalar_time, scalar_fps);
  log_info("  SIMD:   %.3fs (%.1f FPS)", simd_time, simd_fps);
  log_info("  Speedup: %.2fx", speedup);

  // Performance assertions - SIMD should be significantly faster than scalar
  double min_speedup = 2.0; // SIMD should be at least 2x faster for monochrome conversion

  cr_assert_gt(scalar_fps, 0.1, "Scalar should achieve at least 0.1 FPS");
  cr_assert_gt(simd_fps, 0.1, "SIMD should achieve at least 0.1 FPS");
  cr_assert_gt(speedup, min_speedup, "SIMD should be at least %.1fx faster than scalar (expected >%.1fx, got %.2fx)",
               min_speedup, min_speedup, speedup);

  image_destroy(test_image);
}

Test(ascii_simd_performance, color_scalar_vs_simd_performance) {
  const int width = 320, height = 240;
  const int iterations = 10;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 0); // Gradient pattern

  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";

  // Benchmark scalar color implementation
  double start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_color(test_image, ascii_palette);
    cr_assert_not_null(result, "Scalar color should produce output");
    free(result);
  }
  double scalar_time = get_time_seconds() - start_time;

  // Benchmark SIMD color implementation
  start_time = get_time_seconds();
  for (int i = 0; i < iterations; i++) {
    char *result = image_print_color_simd(test_image, false, false, ascii_palette);
    cr_assert_not_null(result, "SIMD color should produce output");
    free(result);
  }
  double simd_time = get_time_seconds() - start_time;

  double speedup = scalar_time / simd_time;
  double scalar_fps = iterations / scalar_time;
  double simd_fps = iterations / simd_time;

  log_info("Color Performance (%dx%d, %d iterations):", width, height, iterations);
  log_info("  Scalar: %.3fs (%.1f FPS)", scalar_time, scalar_fps);
  log_info("  SIMD:   %.3fs (%.1f FPS)", simd_time, simd_fps);
  log_info("  Speedup: %.2fx", speedup);

  // Performance assertions - SIMD should be faster for color conversion too
  double min_speedup = 1.5; // SIMD should be at least 1.5x faster for color conversion

  cr_assert_gt(scalar_fps, 0.1, "Scalar color should achieve at least 0.1 FPS");
  cr_assert_gt(simd_fps, 0.1, "SIMD color should achieve at least 0.1 FPS");
  cr_assert_gt(speedup, min_speedup, "SIMD color should be faster than scalar (expected >%.1fx, got %.2fx)",
               min_speedup, speedup);

  image_destroy(test_image);
}

Test(ascii_simd_performance, utf8_palette_performance_impact) {
  const int width = 160, height = 48;
  const int iterations = 20;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 0); // Gradient pattern

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

Test(ascii_simd_performance, various_image_sizes_performance) {
  const struct {
    const char *name;
    int width;
    int height;
    double min_speedup;
  } test_sizes[] = {
      {"Small", 40, 12, 1.5},
      {"Medium", 80, 24, 2.0},
      {"Large", 160, 48, 2.0},
      {"Webcam", 320, 240, 2.0},
  };

  const int num_sizes = sizeof(test_sizes) / sizeof(test_sizes[0]);
  const char *ascii_palette = "   ...',;:clodxkO0KXNWM";

  for (int size_idx = 0; size_idx < num_sizes; size_idx++) {
    int width = test_sizes[size_idx].width;
    int height = test_sizes[size_idx].height;
    double expected_speedup = test_sizes[size_idx].min_speedup;

    image_t *test_image = image_new(width, height);
    cr_assert_not_null(test_image, "Should create %s test image", test_sizes[size_idx].name);

    create_test_image(test_image, 0); // Gradient pattern

    const int iterations = (width * height < 10000) ? 50 : 20;

    // Benchmark scalar
    double start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
      char *result = image_print(test_image, ascii_palette);
      cr_assert_not_null(result, "Scalar should produce output for %s", test_sizes[size_idx].name);
      free(result);
    }
    double scalar_time = get_time_seconds() - start_time;

    // Benchmark SIMD
    start_time = get_time_seconds();
    for (int i = 0; i < iterations; i++) {
      char *result = image_print_simd(test_image, ascii_palette);
      cr_assert_not_null(result, "SIMD should produce output for %s", test_sizes[size_idx].name);
      free(result);
    }
    double simd_time = get_time_seconds() - start_time;

    double speedup = scalar_time / simd_time;
    double scalar_fps = iterations / scalar_time;
    double simd_fps = iterations / simd_time;

    log_info("%s (%dx%d): Scalar=%.3fs (%.1f FPS) | SIMD=%.3fs (%.1f FPS) | Speedup=%.2fx", test_sizes[size_idx].name,
             width, height, scalar_time, scalar_fps, simd_time, simd_fps, speedup);

    // Performance assertions - SIMD should be faster across all image sizes
    // Keep the original expected speedup from test_sizes array

    cr_assert_gt(scalar_fps, 0.1, "%s: Scalar should achieve at least 0.1 FPS", test_sizes[size_idx].name);
    cr_assert_gt(simd_fps, 0.1, "%s: SIMD should achieve at least 0.1 FPS", test_sizes[size_idx].name);
    cr_assert_gt(speedup, expected_speedup, "%s: SIMD should be at least %.1fx faster (expected >%.1fx, got %.2fx)",
                 test_sizes[size_idx].name, expected_speedup, expected_speedup, speedup);

    image_destroy(test_image);
  }
}

// =============================================================================
// SIMD Architecture Performance Tests
// =============================================================================

Test(ascii_simd_performance, simd_architecture_benchmarks) {
  const int width = 160, height = 48;
  const int iterations = 20;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 0); // Gradient pattern

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

// Performance assertions - each SIMD implementation should be faster than scalar
#ifdef SIMD_SUPPORT_SSE2
  if (mono_bench.sse2_time > 0) {
    double sse2_speedup = mono_bench.scalar_time / mono_bench.sse2_time;
    cr_assert_gt(sse2_speedup, 1.2, "SSE2 should be faster than scalar (expected >1.2x, got %.2fx)", sse2_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_SSSE3
  if (mono_bench.ssse3_time > 0) {
    double ssse3_speedup = mono_bench.scalar_time / mono_bench.ssse3_time;
    cr_assert_gt(ssse3_speedup, 1.2, "SSSE3 should be faster than scalar (expected >1.2x, got %.2fx)", ssse3_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_AVX2
  if (mono_bench.avx2_time > 0) {
    double avx2_speedup = mono_bench.scalar_time / mono_bench.avx2_time;
    cr_assert_gt(avx2_speedup, 1.5, "AVX2 should be faster than scalar (expected >1.5x, got %.2fx)", avx2_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_NEON
  if (mono_bench.neon_time > 0) {
    double neon_speedup = mono_bench.scalar_time / mono_bench.neon_time;
    cr_assert_gt(neon_speedup, 1.2, "NEON should be faster than scalar (expected >1.2x, got %.2fx)", neon_speedup);
  }
#endif

#ifdef SIMD_SUPPORT_SVE
  if (mono_bench.sve_time > 0) {
    double sve_speedup = mono_bench.scalar_time / mono_bench.sve_time;
    cr_assert_gt(sve_speedup, 1.5, "SVE should be faster than scalar (expected >1.5x, got %.2fx)", sve_speedup);
  }
#endif

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
  const int width = 160, height = 48;
  const int iterations = 30;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 0); // Gradient pattern

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
  const int width = 80, height = 24;
  const int iterations = 15;

  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  create_test_image(test_image, 0); // Gradient pattern

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

    // Performance assertions - SIMD should be faster even with complex UTF-8 palettes
    cr_assert_gt(scalar_fps, 0.5, "%s: Scalar should achieve at least 0.5 FPS", mixed_palettes[p].name);
    cr_assert_gt(simd_fps, 0.5, "%s: SIMD should achieve at least 0.5 FPS", mixed_palettes[p].name);

    // SIMD should be faster than scalar, even with complex UTF-8 processing
    cr_assert_gt(scalar_vs_simd_ratio, 1.0, "%s: SIMD should be faster than scalar (expected >1.0x, got %.2fx)",
                 mixed_palettes[p].name, scalar_vs_simd_ratio);
  }

  image_destroy(test_image);
}

// =============================================================================
// End-to-End Performance Tests
// =============================================================================

Test(ascii_simd_performance, full_pipeline_performance) {
  const int width = 160, height = 48;
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
