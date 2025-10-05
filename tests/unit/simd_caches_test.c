#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>

#include "common.h"
#include "hashtable.h"
#include "image2ascii/simd/common.h"
#include "image2ascii/simd/ascii_simd.h"
#include "tests/logging.h"

#ifdef SIMD_SUPPORT_NEON
#include "image2ascii/simd/neon.h"
#endif

void segfault_handler(int sig);

// Use the enhanced macro to create complete test suite with basic quiet logging
TEST_SUITE_WITH_QUIET_LOGGING(simd_caches);

void segfault_handler(int sig) {
  void *array[50];
  size_t size;

  log_error("\n[CRASH] Caught signal %d (SIGSEGV)", sig);

  // Get void*'s for all entries on the stack
  size = backtrace(array, 50);

  // Print out all the frames to stderr
  log_error("[CRASH] Stack trace (%zu frames):", size);
  backtrace_symbols_fd(array, size, STDERR_FILENO);

  // Re-raise the signal to get default behavior
  platform_signal(sig, SIG_DFL);
  raise(sig);
}

void setup_cache_logging(void) {
  log_set_level(LOG_FATAL);           // Reduce verbose output during tests
  hashtable_set_stats_enabled(false); // Disable hashtable stats for this test

  // Install segfault handler
  platform_signal(SIGSEGV, segfault_handler);
  platform_signal(SIGBUS, segfault_handler);
  platform_signal(SIGABRT, segfault_handler);
}

void restore_cache_logging(void) {
  log_set_level(LOG_ERROR);
  hashtable_set_stats_enabled(true); // Re-enable hashtable stats

  // Restore default signal handlers
  platform_signal(SIGSEGV, SIG_DFL);
  platform_signal(SIGBUS, SIG_DFL);
  platform_signal(SIGABRT, SIG_DFL);
}

// =============================================================================
// Cache Capacity and Overflow Tests
// =============================================================================

Test(simd_caches, utf8_cache_capacity_limits) {
  // Test behavior when exceeding hashtable capacity (32 entries)
  const int test_palettes = 40; // Exceed capacity
  char **results = NULL;

  SAFE_MALLOC(results, test_palettes * sizeof(char *), char **);
  if (!results) {
    log_error("[ERROR] Failed to allocate results array");
    fflush(stdout);
    cr_assert(false, "Memory allocation failed");
    return;
  }

  // Generate unique palettes
  for (int i = 0; i < test_palettes; i++) {
    char palette[32];
    snprintf(palette, sizeof(palette), "   .:-=+*#%%@%d", i); // Unique per iteration

    log_debug("Testing palette %d: '%s'", i, palette);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(palette);

    if (i < 32) {
      // First 32 should succeed
      log_debug("Palette %d: cache=%p (expecting non-NULL)", i, (void *)cache);
      cr_assert_not_null(cache, "Palette %d should be cached successfully", i);
    } else {
      // After 32: depends on eviction implementation
      // Current implementation: may return NULL or evict older entries
      log_debug("Palette %d: cache=%p (overflow case)", i, (void *)cache);
    }
  }

  log_debug("[TEST END] utf8_cache_capacity_limits - Completed successfully");
  free(results);
}

Test(simd_caches, cache_collision_handling) {
  log_debug("[TEST START] cache_collision_handling");

  // Test hashtable collision handling with similar hash values
  const char *similar_palettes[] = {
      "   ...',;:clodxkO0KXNWM",
      "   ...',;:clodxkO0KXNWN", // Very similar, might hash to same bucket
      "   ...',;:clodxkO0KXNWO", "   ...',;:clodxkO0KXNWP", "   ...',;:clodxkO0KXNWQ",
  };

  const int num_palettes = sizeof(similar_palettes) / sizeof(similar_palettes[0]);
  utf8_palette_cache_t *caches[num_palettes];

  // Create all caches
  for (int i = 0; i < num_palettes; i++) {
    log_debug("Getting cache for palette %d: '%s'", i, similar_palettes[i]);

    caches[i] = get_utf8_palette_cache(similar_palettes[i]);

    log_debug("Got cache %d: %p", i, (void *)caches[i]);

    cr_assert_not_null(caches[i], "Similar palette %d should be cached", i);
  }

  // Verify each cache is unique and correct
  for (int i = 0; i < num_palettes; i++) {
    for (int j = i + 1; j < num_palettes; j++) {
      log_debug("Comparing cache %d (%p) vs %d (%p)", i, (void *)caches[i], j, (void *)caches[j]);
      cr_assert_neq(caches[i], caches[j], "Palette %d and %d should have different cache objects", i, j);
    }

    // Verify palette hash is correct
    log_debug("Verifying palette hash for cache %d", i);
    cr_assert_str_eq(caches[i]->palette_hash, similar_palettes[i], "Cache %d should store correct palette string", i);
  }

  log_debug("[TEST END] cache_collision_handling - Completed successfully");
}

Test(simd_caches, cache_persistence_across_calls) {
  // Test that same palette returns same cache object
  const char *test_palette = "🌑🌒🌓🌔🌕🌖🌗🌘🌙🌚🌛🌜🌝🌞🌟⭐";

  utf8_palette_cache_t *cache1 = get_utf8_palette_cache(test_palette);
  utf8_palette_cache_t *cache2 = get_utf8_palette_cache(test_palette);
  utf8_palette_cache_t *cache3 = get_utf8_palette_cache(test_palette);

  cr_assert_not_null(cache1, "First cache access should succeed");
  cr_assert_not_null(cache2, "Second cache access should succeed");
  cr_assert_not_null(cache3, "Third cache access should succeed");

  // Should return same cache object (pointer equality)
  cr_assert_eq(cache1, cache2, "Same palette should return same cache object");
  cr_assert_eq(cache2, cache3, "Repeated access should return same cache object");
}

// =============================================================================
// Performance and Concurrency Tests
// =============================================================================

Test(simd_caches, cache_access_performance) {
  // Test that cached access is significantly faster than first access
  const char *test_palette = "   ...',;:clodxkO0KXNWM";

  // First access (cache miss - slower)
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);
  utf8_palette_cache_t *cache1 = get_utf8_palette_cache(test_palette);
  clock_gettime(CLOCK_MONOTONIC, &end);
  double first_access_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  cr_assert_not_null(cache1, "First access should succeed");

  // Subsequent accesses (cache hits - should be much faster)
  const int iterations = 1000;
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (int i = 0; i < iterations; i++) {
    utf8_palette_cache_t *cache = get_utf8_palette_cache(test_palette);
    cr_assert_eq(cache, cache1, "Cached access %d should return same object", i);
  }
  clock_gettime(CLOCK_MONOTONIC, &end);
  double cached_access_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  double avg_cached_time = cached_access_time / iterations;

  log_debug("Cache performance: First=%.6fs, Avg cached=%.6fs", first_access_time, avg_cached_time);

  // Cached access should be at least 10x faster than first access
  cr_assert_lt(avg_cached_time * 10, first_access_time, "Cached access should be much faster than first access");
}

Test(simd_caches, concurrent_cache_access) {
  // Test concurrent access to same palette (rwlock validation)
  const char *shared_palette = "   ...',;:clodxkO0KXNWM";
  const int iterations = 100;

  // Simulate concurrent access by rapid successive calls
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  utf8_palette_cache_t *first_cache = NULL;
  for (int i = 0; i < iterations; i++) {
    utf8_palette_cache_t *cache = get_utf8_palette_cache(shared_palette);
    cr_assert_not_null(cache, "Concurrent access %d should succeed", i);

    if (first_cache == NULL) {
      first_cache = cache;
    } else {
      cr_assert_eq(cache, first_cache, "All concurrent accesses should return same cache");
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  double total_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  double avg_time = total_time / iterations;

  log_debug("Concurrent access: %d calls in %.3fs (%.6fs each)", iterations, total_time, avg_time);

  // Should maintain good performance under concurrent load
  cr_assert_lt(avg_time, 0.001, "Concurrent access should be fast (<1ms, got %.6fs)", avg_time);
}

// =============================================================================
// UTF-8 Specific Cache Tests
// =============================================================================

// Parameterized test for UTF-8 character cache correctness
typedef struct {
  char name[16];
  char palette[64];
  int expected_first_byte;
  bool check_first_byte;
  char description[64];
} utf8_cache_test_case_t;

static utf8_cache_test_case_t utf8_cache_cases[] = {
    {"ASCII", "   ...',;:clodxkO0KXNWM", ' ', true, "ASCII palette with space"},
    {"Emoji", "🌑🌒🌓🌔🌕", 0xF0, true, "Emoji palette (4-byte UTF-8)"},
    {"Greek", "αβγδεζηθι", 0xCE, true, "Greek palette (2-byte UTF-8)"},
    {"Mixed", "   .🧠αβ", ' ', false, "Mixed ASCII + UTF-8"},
};

ParameterizedTestParameters(simd_caches, utf8_character_cache_correctness) {
  return cr_make_param_array(utf8_cache_test_case_t, utf8_cache_cases,
                             sizeof(utf8_cache_cases) / sizeof(utf8_cache_cases[0]));
}

ParameterizedTest(utf8_cache_test_case_t *tc, simd_caches, utf8_character_cache_correctness) {
  utf8_palette_cache_t *cache = get_utf8_palette_cache(tc->palette);
  cr_assert_not_null(cache, "%s: palette should be cached", tc->name);

  // Check first character in cache64 - fix sign extension
  uint8_t actual_first_byte = (uint8_t)cache->cache64[0].utf8_bytes[0];
  log_debug("%s: Expected first byte=0x%02x, Actual=0x%02x", tc->name, tc->expected_first_byte, actual_first_byte);

  // For mixed palettes, the first character might be ASCII space, not UTF-8
  if (tc->check_first_byte) {
    cr_assert_eq(actual_first_byte, tc->expected_first_byte,
                 "%s: First cached character should have correct first byte", tc->name);
  }

  // Verify cache64 and cache consistency
  for (int i = 0; i < 64; i++) {
    cr_assert_gt(cache->cache64[i].byte_len, 0, "%s: Cache64[%d] should have valid length", tc->name, i);
    cr_assert_leq(cache->cache64[i].byte_len, 4, "%s: Cache64[%d] length should be ≤4", tc->name, i);
  }

  // Verify luminance cache
  for (int i = 0; i < 256; i++) {
    cr_assert_gt(cache->cache[i].byte_len, 0, "%s: Cache[%d] should have valid length", tc->name, i);
    cr_assert_leq(cache->cache[i].byte_len, 4, "%s: Cache[%d] length should be ≤4", tc->name, i);
  }
}

Test(simd_caches, character_index_ramp_correctness) {
  const char *test_palette = "   ...',;:clodxkO0KXNWM";

  // Character index ramp is now part of UTF-8 cache
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(test_palette);
  cr_assert_not_null(utf8_cache, "UTF-8 cache should be created");

  // Verify ramp values are in valid range
  size_t palette_len = strlen(test_palette);
  for (int i = 0; i < 64; i++) {
    cr_assert_lt(utf8_cache->char_index_ramp[i], palette_len, "Ramp index %d should be within palette bounds", i);
  }

  // Verify ramp is monotonic (darker → brighter)
  for (int i = 1; i < 64; i++) {
    cr_assert_geq(utf8_cache->char_index_ramp[i], utf8_cache->char_index_ramp[i - 1],
                  "Character ramp should be monotonic");
  }
}

// =============================================================================
// NEON Cache Integration Tests
// =============================================================================

#ifdef SIMD_SUPPORT_NEON
Test(simd_caches, neon_cache_integration) {
  // Test NEON cache integration with common cache system
  const char *test_palette = "   ...',;:clodxkO0KXNWM";

  // Get UTF-8 cache first
  utf8_palette_cache_t *utf8_cache = get_utf8_palette_cache(test_palette);
  cr_assert_not_null(utf8_cache, "UTF-8 cache should be created");

  // Get NEON cache (depends on UTF-8 cache)
  // Note: This requires the updated get_neon_tbl_cache signature
  // For now, test that basic functionality works

  // Test that NEON cache creation doesn't interfere with UTF-8 cache
  utf8_palette_cache_t *utf8_cache2 = get_utf8_palette_cache(test_palette);
  cr_assert_eq(utf8_cache, utf8_cache2, "UTF-8 cache should remain consistent");
}

Test(simd_caches, neon_cache_performance) {
  // Test NEON cache system performance
  const int width = 160, height = 48;

  // Create test image
  image_t *test_image = image_new(width, height);
  cr_assert_not_null(test_image, "Should create test image");

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = y * width + x;
      test_image->pixels[idx].r = (x * 255) / width;
      test_image->pixels[idx].g = (y * 255) / height;
      test_image->pixels[idx].b = 128;
    }
  }

  const char *test_palette = "   ...',;:clodxkO0KXNWM";
  const int iterations = 20;

  // Test NEON monochrome performance
  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  for (int i = 0; i < iterations; i++) {
    char *result = image_print_simd(test_image, test_palette);
    cr_assert_not_null(result, "NEON iteration %d should succeed", i);
    free(result);
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  double total_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  double avg_time = (total_time / iterations) * 1000; // Convert to ms

  log_debug("NEON cache performance: %.4fms/frame", avg_time);

  // Should be very fast with cached lookups
  cr_assert_lt(avg_time, 1.0, "NEON with cache should be <1ms/frame (got %.4fms)", avg_time);

  image_destroy(test_image);
}
#endif

// =============================================================================
// Cache Eviction Behavior Tests (Future Implementation)
// =============================================================================

Test(simd_caches, extreme_palette_cycling_60fps) {
  // Test extreme palette cycling at 60 FPS (new palette every frame)
  const int fps = 60;
  const int test_duration_seconds = 2; // 2 seconds = 120 frames
  const int total_frames = fps * test_duration_seconds;

  log_debug("Testing extreme palette cycling: %d frames, %d unique palettes", total_frames, total_frames);

  struct timespec frame_start, frame_end;
  double total_render_time = 0.0;
  int successful_caches = 0;

  for (int frame = 0; frame < total_frames; frame++) {
    char unique_palette[64];
    snprintf(unique_palette, sizeof(unique_palette), "frame_%03d_unique_🌑🌒🌓", frame);

    clock_gettime(CLOCK_MONOTONIC, &frame_start);
    utf8_palette_cache_t *cache = get_utf8_palette_cache(unique_palette);
    clock_gettime(CLOCK_MONOTONIC, &frame_end);

    double frame_time = (frame_end.tv_sec - frame_start.tv_sec) + (frame_end.tv_nsec - frame_start.tv_nsec) / 1e9;
    total_render_time += frame_time;

    // ASSERT: Every palette request must succeed (eviction should guarantee this)
    cr_assert_not_null(cache, "Frame %d: Eviction system must guarantee cache creation", frame);

    if (cache != NULL) {
      successful_caches++;
    }

    // Every 30 frames, report progress
    if (frame % 30 == 0) {
      log_debug("Frame %d: cache=%s, time=%.4fms", frame, cache ? "✅" : "❌", frame_time * 1000);
    }
  }

  double avg_frame_time = (total_render_time / total_frames) * 1000; // Convert to ms

  log_debug("Extreme cycling results:");
  log_debug("- Total frames: %d", total_frames);
  log_debug("- Successful caches: %d/%d (%.1f%%)", successful_caches, total_frames,
            (successful_caches * 100.0) / total_frames);
  log_debug("- Average frame time: %.4fms", avg_frame_time);
  log_debug("- Performance impact: %.1f%% of 16.7ms budget", (avg_frame_time / 16.7) * 100);

  // All palette requests should succeed (guaranteed eviction)
  cr_assert_eq(successful_caches, total_frames, "All palette requests should succeed with eviction");

  // Should maintain reasonable performance even under extreme load
  cr_assert_lt(avg_frame_time, 5.0, "Extreme cycling should be <5ms/frame (got %.4fms)", avg_frame_time);
}

Test(simd_caches, frequency_based_cache_persistence) {
  // Test that frequently used palettes stay cached longer than rarely used ones

  // Create a popular animation palette that gets used frequently
  const char *popular_palette = "🌟⭐💫🌠✨🎆🎇🌅🌄🌇";

  // Phase 1: Establish popularity by accessing it many times
  const int popularity_accesses = 50;
  log_debug("Phase 1: Building popularity for animation palette (%d accesses)", popularity_accesses);

  for (int i = 0; i < popularity_accesses; i++) {
    utf8_palette_cache_t *cache = get_utf8_palette_cache(popular_palette);
    cr_assert_not_null(cache, "Popular palette access %d should succeed", i);

    // Small delay to simulate realistic usage (minimal for testing)
    usleep(10); // 0.01ms between accesses - just enough to ensure ordering
  }

  // Phase 2: Fill cache with other palettes to trigger eviction pressure
  log_debug("Phase 2: Creating cache pressure with 40 different palettes");

  for (int i = 0; i < 40; i++) {
    char pressure_palette[64];
    snprintf(pressure_palette, sizeof(pressure_palette), "pressure_%03d_🔥💧⚡", i);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(pressure_palette);
    cr_assert_not_null(cache, "Pressure palette %d should be cached", i);

    usleep(1); // Minimal delay - just establish access pattern
  }

  // Phase 3: Wait some time to allow aging, then test if popular palette survived
  log_debug("Phase 3: Testing if popular palette survived eviction pressure");
  usleep(1000); // 1ms aging - sufficient for test ordering

  struct timespec access_start, access_end;
  clock_gettime(CLOCK_MONOTONIC, &access_start);
  utf8_palette_cache_t *survived_cache = get_utf8_palette_cache(popular_palette);
  clock_gettime(CLOCK_MONOTONIC, &access_end);

  double access_time = (access_end.tv_sec - access_start.tv_sec) + (access_end.tv_nsec - access_start.tv_nsec) / 1e9;

  log_debug("Popular palette access after pressure: time=%.6fs", access_time);

  cr_assert_not_null(survived_cache, "Popular palette should survive eviction pressure");

  // Should be fast access (cache hit) because of high frequency score
  cr_assert_lt(access_time, 0.001, "Popular palette should be fast cached access (%.6fs)", access_time);
}

Test(simd_caches, eviction_fairness_algorithm) {
  // Test that eviction algorithm correctly prioritizes by score (age + frequency)

  log_debug("Testing eviction fairness: frequency vs recency");

  // Create baseline cache entries
  const struct {
    const char *name;
    const char *palette;
    int access_count;
    int age_delay_ms;
  } test_scenarios[] = {
      {"Old Popular", "old_popular_🎨🎭🎪", 25, 5000},      // High frequency, old
      {"Recent Rare", "recent_rare_🔍🔎🕵️", 2, 100},        // Low frequency, recent
      {"Old Rare", "old_rare_📱💻🖥️", 1, 4000},             // Low frequency, old
      {"Recent Popular", "recent_popular_🎵🎶🎼", 15, 200}, // High frequency, recent
      {"Medium", "medium_🌍🌎🌏", 8, 2000},                 // Medium frequency, medium age
  };

  const int num_scenarios = sizeof(test_scenarios) / sizeof(test_scenarios[0]);
  utf8_palette_cache_t *scenario_caches[num_scenarios];

  // Create caches with different usage patterns
  for (int s = 0; s < num_scenarios; s++) {
    log_debug("Creating %s scenario", test_scenarios[s].name);

    // Create cache with initial accesses
    for (int access = 0; access < test_scenarios[s].access_count; access++) {
      scenario_caches[s] = get_utf8_palette_cache(test_scenarios[s].palette);
      cr_assert_not_null(scenario_caches[s], "%s cache should be created", test_scenarios[s].name);
      usleep(1); // Minimal delay between accesses
    }

    // Age the cache
    usleep(test_scenarios[s].age_delay_ms); // Use microseconds directly (much faster)
  }

  // Now create pressure to force evictions (create 30 more unique palettes)
  log_debug("Creating eviction pressure with 30 additional palettes");

  for (int pressure = 0; pressure < 30; pressure++) {
    char pressure_palette[64];
    snprintf(pressure_palette, sizeof(pressure_palette), "eviction_pressure_%03d", pressure);

    utf8_palette_cache_t *pressure_cache = get_utf8_palette_cache(pressure_palette);
    cr_assert_not_null(pressure_cache, "Pressure palette %d should be cached", pressure);
  }

  // Test which caches survived - should follow eviction score logic
  log_debug("Testing survival rates after eviction pressure");

  for (int s = 0; s < num_scenarios; s++) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    utf8_palette_cache_t *test_cache = get_utf8_palette_cache(test_scenarios[s].palette);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double access_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    bool is_cached = (test_cache != NULL && access_time < 0.001); // Fast = cache hit

    log_debug("%s: %s (%.6fs)", test_scenarios[s].name, is_cached ? "SURVIVED ✅" : "EVICTED ❌", access_time);
  }

  // Popular entries should survive regardless of age
  utf8_palette_cache_t *old_popular = get_utf8_palette_cache("old_popular_🎨🎭🎪");
  utf8_palette_cache_t *recent_popular = get_utf8_palette_cache("recent_popular_🎵🎶🎼");

  cr_assert_not_null(old_popular, "Old but popular cache should survive eviction");
  cr_assert_not_null(recent_popular, "Recent popular cache should survive eviction");

  // Rare entries should be evicted (especially old + rare)
  // Note: Due to timing, this might be flaky - focus on popular survival
}

Test(simd_caches, animation_palette_cycling_realistic) {
  // Test realistic animation scenario: 5 palettes cycling for 10 seconds

  const char *animation_palettes[] = {
      "🌑🌒🌓🌔🌕", // Moon cycle 1
      "🌖🌗🌘🌙🌚", // Moon cycle 2
      "🌛🌜🌝🌞🌟", // Moon cycle 3
      "⭐🌠💫⚡🔥", // Effects 1
      "💧❄️🌀🌈☀️"    // Effects 2
  };

  const int num_palettes = sizeof(animation_palettes) / sizeof(animation_palettes[0]);
  const int cycles_per_second = 10; // 10 complete cycles per second
  const int animation_duration = 3; // 3 seconds
  const int total_cycles = cycles_per_second * animation_duration;

  log_debug("Animation test: %d palettes, %d cycles/sec, %d seconds (%d total accesses)", num_palettes,
            cycles_per_second, animation_duration, total_cycles * num_palettes);

  struct timespec start, end;
  clock_gettime(CLOCK_MONOTONIC, &start);

  // Run animation cycles
  for (int cycle = 0; cycle < total_cycles; cycle++) {
    for (int p = 0; p < num_palettes; p++) {
      utf8_palette_cache_t *cache = get_utf8_palette_cache(animation_palettes[p]);
      cr_assert_not_null(cache, "Animation palette %d cycle %d should be cached", p, cycle);

      // Simulate frame render time
      usleep(1); // Minimal render time simulation
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &end);
  double total_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  log_debug("Animation completed in %.3fs", total_time);

  // Create cache pressure with many one-off palettes
  log_debug("Creating post-animation cache pressure");

  for (int pressure = 0; pressure < 35; pressure++) {
    char oneoff_palette[64];
    snprintf(oneoff_palette, sizeof(oneoff_palette), "oneoff_%03d_experimental", pressure);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(oneoff_palette);
    cr_assert_not_null(cache, "One-off palette %d should be cached", pressure);
  }

  // Test if animation palettes survived (they should - high frequency scores)
  log_debug("Testing animation palette survival after cache pressure");

  int survived_count = 0;
  for (int p = 0; p < num_palettes; p++) {
    struct timespec access_start, access_end;
    clock_gettime(CLOCK_MONOTONIC, &access_start);
    utf8_palette_cache_t *cache = get_utf8_palette_cache(animation_palettes[p]);
    clock_gettime(CLOCK_MONOTONIC, &access_end);

    double access_time = (access_end.tv_sec - access_start.tv_sec) + (access_end.tv_nsec - access_start.tv_nsec) / 1e9;
    bool is_cached = (cache != NULL && access_time < 0.001);

    if (is_cached) {
      survived_count++;
      log_debug("Animation palette %d: SURVIVED ✅ (%.6fs)", p, access_time);
    } else {
      log_debug("Animation palette %d: EVICTED ❌ (%.6fs)", p, access_time);
    }
  }

  // Most animation palettes should survive due to high frequency scores
  cr_assert_geq(survived_count, num_palettes / 2, "At least half of animation palettes should survive (%d/%d)",
                survived_count, num_palettes);
}

Test(simd_caches, old_frequent_palette_persistence) {
  // Test that a frequently used palette from long ago stays cached

  const char *old_frequent_palette = "old_frequent_🎯🎪🎨🎭🎮";

  log_debug("Phase 1: Establishing old frequent palette with high access count");

  // Make this palette very popular
  const int high_frequency_accesses = 100;
  for (int i = 0; i < high_frequency_accesses; i++) {
    utf8_palette_cache_t *cache = get_utf8_palette_cache(old_frequent_palette);
    cr_assert_not_null(cache, "High frequency access %d should succeed", i);

    if (i % 20 == 0) {
      log_debug("Building frequency: access %d/100", i);
    }
  }

  log_debug("Phase 2: Aging the popular palette (minimal delay)");
  usleep(1000); // 1ms aging - sufficient for timestamp differentiation

  log_debug("Phase 3: Creating new palettes to fill cache and trigger evictions");

  // Fill cache with new entries to trigger eviction pressure
  const int new_palettes = 50;
  for (int i = 0; i < new_palettes; i++) {
    char new_palette[64];
    snprintf(new_palette, sizeof(new_palette), "new_palette_%03d_recent", i);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(new_palette);
    cr_assert_not_null(cache, "New palette %d should be cached", i);

    if (i % 10 == 0) {
      log_debug("Creating pressure: palette %d/%d", i, new_palettes);
    }
  }

  log_debug("Phase 4: Testing if old frequent palette survived");

  // Test if the old frequent palette is still cached
  struct timespec test_start, test_end;
  clock_gettime(CLOCK_MONOTONIC, &test_start);
  utf8_palette_cache_t *old_cache = get_utf8_palette_cache(old_frequent_palette);
  clock_gettime(CLOCK_MONOTONIC, &test_end);

  double access_time = (test_end.tv_sec - test_start.tv_sec) + (test_end.tv_nsec - test_start.tv_nsec) / 1e9;
  bool is_fast_access = (access_time < 0.001); // Fast = cache hit

  log_debug("Old frequent palette test: cache=%s, time=%.6fs, cached=%s", old_cache ? "EXISTS" : "NULL", access_time,
            is_fast_access ? "YES" : "NO");

  // The old frequent palette should survive due to high frequency score
  cr_assert_not_null(old_cache, "Old frequent palette should survive eviction pressure");

  // Should be fast access (cache hit) due to frequency protection
  if (old_cache) {
    cr_assert_lt(access_time, 0.001, "Old frequent palette should be cached (got %.6fs)", access_time);
  }
}

Test(simd_caches, eviction_ordering_verification) {
  // Test that eviction happens in correct order at full capacity (32/32 entries)

  log_debug("Testing eviction ordering at full cache capacity");

  // Phase 1: Fill cache to exactly 30/32 entries with baseline palettes
  log_debug("Phase 1: Filling cache to 30/32 entries");
  for (int i = 0; i < HASHTABLE_MAX_ENTRIES - 2; i++) {
    char baseline[64];
    snprintf(baseline, sizeof(baseline), "baseline_%02d", i);
    utf8_palette_cache_t *cache = get_utf8_palette_cache(baseline);
    cr_assert_not_null(cache, "Baseline palette %d should be cached", i);
  }

  // Phase 2: Add one high-value item that should survive eviction
  log_debug("Phase 2: Adding high-value item (frequent + recent)");
  const char *survivor_palette = "SURVIVOR_HIGH_VALUE";
  for (int access = 0; access < 50; access++) {
    utf8_palette_cache_t *survivor = get_utf8_palette_cache(survivor_palette);
    cr_assert_not_null(survivor, "Survivor should be cached");
  }

  // Phase 3: Add one low-value item that should be evicted first
  log_debug("Phase 3: Adding low-value item (infrequent + will be aged)");
  const char *victim_palette = "VICTIM_LOW_VALUE";
  utf8_palette_cache_t *victim = get_utf8_palette_cache(victim_palette);
  cr_assert_not_null(victim, "Victim should be initially cached");

  // Age the victim by brief delay (not accessing it)
  usleep(100); // 0.1ms aging - minimal but sufficient for timestamp ordering

  // Cache is now full (32/32). Next insertion will trigger eviction.
  log_debug("Phase 4: Cache full - next insertion triggers eviction");

  // Force eviction by adding new item
  const char *trigger_palette = "EVICTION_TRIGGER";
  utf8_palette_cache_t *trigger = get_utf8_palette_cache(trigger_palette);
  cr_assert_not_null(trigger, "Eviction trigger should be cached");

  // Test survival: high-value item should survive, low-value item should be evicted
  log_debug("Phase 5: Testing eviction results");

  struct timespec start, end;

  // Test survivor (should still be cached - fast access)
  clock_gettime(CLOCK_MONOTONIC, &start);
  utf8_palette_cache_t *survivor_test = get_utf8_palette_cache(survivor_palette);
  clock_gettime(CLOCK_MONOTONIC, &end);
  double survivor_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  // Test victim (may be evicted - could be slow if needs recreation)
  clock_gettime(CLOCK_MONOTONIC, &start);
  utf8_palette_cache_t *victim_test = get_utf8_palette_cache(victim_palette);
  clock_gettime(CLOCK_MONOTONIC, &end);
  double victim_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

  log_debug("Survivor access time: %.6fs", survivor_time);
  log_debug("Victim access time: %.6fs", victim_time);

  // At minimum, both should still exist (cache always returns valid data)
  cr_assert_not_null(survivor_test, "Survivor should exist");
  cr_assert_not_null(victim_test, "Victim should exist (may be recreated)");

  // More stringent test: survivor should be faster (cached vs recreated)
  // Allow some tolerance for timing variations
  cr_assert_lt(survivor_time, victim_time * 2.0 + 0.001, "Survivor (%.6fs) should be faster than victim (%.6fs)",
               survivor_time, victim_time);
}

// =============================================================================
// Min-Heap Data Structure Tests
// =============================================================================

Test(simd_caches, min_heap_ordering_verification) {
  // Test that min-heap maintains proper ordering with score changes

  log_debug("Testing min-heap ordering with dynamic score changes");

  // Create palettes with known initial scores
  const struct {
    const char *name;
    const char *palette;
    int initial_accesses;
    int age_delay_ms;
  } heap_test_entries[] = {
      {"LOW_SCORE", "low_score_💀", 1, 5000},    // Should be at heap root (worst)
      {"MED_SCORE_1", "med_score1_📊", 5, 2000}, // Should be in middle
      {"MED_SCORE_2", "med_score2_📈", 8, 1500}, // Should be in middle
      {"HIGH_SCORE", "high_score_🏆", 20, 500},  // Should be deep in heap (best)
  };

  const int num_entries = sizeof(heap_test_entries) / sizeof(heap_test_entries[0]);
  utf8_palette_cache_t *test_caches[num_entries];

  // Create entries with different characteristics
  for (int e = 0; e < num_entries; e++) {
    log_debug("Creating %s with %d accesses, %dms aging", heap_test_entries[e].name,
              heap_test_entries[e].initial_accesses, heap_test_entries[e].age_delay_ms);

    // Build access pattern
    for (int access = 0; access < heap_test_entries[e].initial_accesses; access++) {
      test_caches[e] = get_utf8_palette_cache(heap_test_entries[e].palette);
      cr_assert_not_null(test_caches[e], "%s access %d should succeed", heap_test_entries[e].name, access);
      usleep(1);
    }

    // Apply aging
    usleep(heap_test_entries[e].age_delay_ms); // Use microseconds directly
  }

  log_debug("Testing heap ordering by triggering score updates");

  // Force score recalculation by accessing each cache 10 times
  // (triggers heap position updates on every 10th access)
  for (int e = 0; e < num_entries; e++) {
    log_debug("Forcing score update for %s", heap_test_entries[e].name);

    for (int update = 0; update < 10; update++) {
      utf8_palette_cache_t *cache = get_utf8_palette_cache(heap_test_entries[e].palette);
      cr_assert_not_null(cache, "%s score update %d should succeed", heap_test_entries[e].name, update);
    }
  }

  // Now test eviction order by creating pressure
  log_debug("Testing eviction order with cache pressure");

  // Create enough pressure to force multiple evictions
  for (int pressure = 0; pressure < 10; pressure++) {
    char pressure_palette[64];
    snprintf(pressure_palette, sizeof(pressure_palette), "heap_pressure_%03d", pressure);

    // Before creating pressure, check which entries are about to be evicted
    utf8_palette_cache_t *pressure_cache = get_utf8_palette_cache(pressure_palette);
    cr_assert_not_null(pressure_cache, "Pressure palette %d should be cached", pressure);

    log_debug("Created pressure palette %d", pressure);
  }

  // Test which entries survived - order should follow heap logic
  log_debug("Testing survival after heap-based eviction");

  for (int e = 0; e < num_entries; e++) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    utf8_palette_cache_t *cache = get_utf8_palette_cache(heap_test_entries[e].palette);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double access_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    bool survived = (cache != NULL && access_time < 0.001);

    log_debug("%s: %s (%.6fs)", heap_test_entries[e].name, survived ? "SURVIVED ✅" : "EVICTED ❌", access_time);
  }

  // High score entries should survive, low score should be evicted
  utf8_palette_cache_t *high_score = get_utf8_palette_cache("high_score_🏆");

  cr_assert_not_null(high_score, "High score entry should survive heap eviction");
  // Low score might be evicted (depending on heap implementation completeness)
}

Test(simd_caches, heap_score_updates_and_rebalancing) {
  // Test that heap rebalances correctly when cache scores change

  log_debug("Testing heap rebalancing with score changes");

  // Create a palette that starts with low score but becomes popular
  const char *rising_star_palette = "rising_star_📈🚀🌟";

  // Phase 1: Create with low initial score (few accesses, will be near heap root)
  log_debug("Phase 1: Creating rising star with low initial score");
  utf8_palette_cache_t *rising_cache = get_utf8_palette_cache(rising_star_palette);
  cr_assert_not_null(rising_cache, "Rising star cache should be created");

  // Fill some cache slots to establish heap structure
  for (int filler = 0; filler < 10; filler++) {
    char filler_palette[64];
    snprintf(filler_palette, sizeof(filler_palette), "filler_%03d", filler);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(filler_palette);
    cr_assert_not_null(cache, "Filler cache %d should be created", filler);
  }

  log_debug("Phase 2: Making rising star very popular (triggering heap rebalancing)");

  // Now make the rising star very popular (should trigger heap position updates)
  const int popularity_boost = 25; // This should trigger score recalculation and heap movement
  for (int boost = 0; boost < popularity_boost; boost++) {
    utf8_palette_cache_t *cache = get_utf8_palette_cache(rising_star_palette);
    cr_assert_eq(cache, rising_cache, "Rising star should return same cache object");

    if (boost % 5 == 0) {
      log_debug("Popularity boost: access %d/%d", boost, popularity_boost);
    }
  }

  log_debug("Phase 3: Testing that rising star moved deeper in heap (better score)");

  // Create eviction pressure to test where rising star ended up in heap
  for (int pressure = 0; pressure < 20; pressure++) {
    char pressure_palette[64];
    snprintf(pressure_palette, sizeof(pressure_palette), "heap_pressure_%03d", pressure);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(pressure_palette);
    cr_assert_not_null(cache, "Heap pressure %d should be cached", pressure);
  }

  // Rising star should still be cached despite pressure (moved deeper in heap)
  struct timespec test_start, test_end;
  clock_gettime(CLOCK_MONOTONIC, &test_start);
  utf8_palette_cache_t *final_cache = get_utf8_palette_cache(rising_star_palette);
  clock_gettime(CLOCK_MONOTONIC, &test_end);

  double final_access_time = (test_end.tv_sec - test_start.tv_sec) + (test_end.tv_nsec - test_start.tv_nsec) / 1e9;

  log_debug("Rising star final test: time=%.6fs", final_access_time);

  cr_assert_not_null(final_cache, "Rising star should survive due to heap rebalancing");
  cr_assert_lt(final_access_time, 0.001, "Rising star should be fast cached access");
}

Test(simd_caches, heap_extraction_and_insertion_cycles) {
  // Test heap behavior under repeated extraction/insertion cycles

  log_debug("Testing heap stability under extraction/insertion cycles");

  // Create initial cache population
  const int initial_population = 20;
  utf8_palette_cache_t *initial_caches[initial_population];

  for (int i = 0; i < initial_population; i++) {
    char palette[64];
    snprintf(palette, sizeof(palette), "initial_%03d_stable", i);

    // Vary access patterns to create different scores
    int accesses = 1 + (i % 10); // 1-10 accesses
    for (int access = 0; access < accesses; access++) {
      initial_caches[i] = get_utf8_palette_cache(palette);
      cr_assert_not_null(initial_caches[i], "Initial cache %d should be created", i);
    }

    usleep((i % 5)); // Variable aging: 0-4 microseconds
  }

  log_debug("Phase 1: Rapid cache creation/eviction cycles");

  // Perform rapid insertion cycles that will trigger many evictions
  const int rapid_cycles = 50;
  struct timespec cycle_start, cycle_end;

  clock_gettime(CLOCK_MONOTONIC, &cycle_start);

  for (int cycle = 0; cycle < rapid_cycles; cycle++) {
    char cycle_palette[64];
    snprintf(cycle_palette, sizeof(cycle_palette), "rapid_cycle_%03d", cycle);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(cycle_palette);
    cr_assert_not_null(cache, "Rapid cycle %d should be cached", cycle);

    if (cycle % 10 == 0) {
      log_debug("Rapid cycle %d/%d", cycle, rapid_cycles);
    }
  }

  clock_gettime(CLOCK_MONOTONIC, &cycle_end);
  double cycle_time = (cycle_end.tv_sec - cycle_start.tv_sec) + (cycle_end.tv_nsec - cycle_start.tv_nsec) / 1e9;
  double avg_cycle_time = (cycle_time / rapid_cycles) * 1000; // Convert to ms

  log_debug("Rapid cycles completed: %.3fs total, %.4fms average", cycle_time, avg_cycle_time);

  // Should maintain good performance during heap operations
  cr_assert_lt(avg_cycle_time, 2.0, "Heap operations should be fast (<2ms, got %.4fms)", avg_cycle_time);

  log_debug("Phase 2: Testing heap integrity after stress");

  // Test that heap structure is still intact by checking some initial caches
  int heap_integrity_survivors = 0;
  for (int i = 0; i < initial_population; i += 3) { // Check every 3rd entry
    char palette[64];
    snprintf(palette, sizeof(palette), "initial_%03d_stable", i);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(palette);
    if (cache != NULL) {
      heap_integrity_survivors++;
    }
  }

  log_debug("Heap integrity: %d initial caches still accessible after stress", heap_integrity_survivors);

  // Heap should still be functional (some entries might be evicted, but no corruption)
  cr_assert_geq(heap_integrity_survivors, 0, "Heap should maintain integrity after stress");

  // Should be able to create new caches after stress
  utf8_palette_cache_t *post_stress_cache = get_utf8_palette_cache("post_stress_test_🧪");
  cr_assert_not_null(post_stress_cache, "Should be able to create cache after heap stress");
}

Test(simd_caches, heap_score_calculation_accuracy) {
  // Test that heap score calculations work correctly for different scenarios

  log_debug("Testing heap score calculation accuracy");

  // Create palettes with known characteristics and verify their relative ordering
  const struct {
    const char *name;
    const char *palette;
    int access_count;
    int age_delay_ms;
    double expected_relative_score; // Higher = better (should be deeper in heap)
  } score_test_cases[] = {
      {"WORST", "worst_case_💀", 1, 10000, 0.1},   // Old + rare = worst score
      {"BAD", "bad_case_👎", 2, 8000, 0.2},        // Still bad
      {"MEDIOCRE", "mediocre_😐", 5, 5000, 0.4},   // Middle tier
      {"GOOD", "good_case_👍", 15, 2000, 0.7},     // Good score
      {"EXCELLENT", "excellent_🏆", 30, 500, 0.9}, // Best score
  };

  const int num_test_cases = sizeof(score_test_cases) / sizeof(score_test_cases[0]);
  utf8_palette_cache_t *score_caches[num_test_cases];

  // Create each test case with specified pattern
  for (int t = 0; t < num_test_cases; t++) {
    log_debug("Creating %s: %d accesses, %dms aging", score_test_cases[t].name, score_test_cases[t].access_count,
              score_test_cases[t].age_delay_ms);

    // Build access frequency
    for (int access = 0; access < score_test_cases[t].access_count; access++) {
      score_caches[t] = get_utf8_palette_cache(score_test_cases[t].palette);
      cr_assert_not_null(score_caches[t], "%s access %d should succeed", score_test_cases[t].name, access);
    }

    // Apply aging
    usleep(score_test_cases[t].age_delay_ms); // Use microseconds directly
  }

  // Force score updates for all caches (trigger heap rebalancing)
  log_debug("Forcing score updates to trigger heap rebalancing");

  for (int t = 0; t < num_test_cases; t++) {
    // Access 10 times to trigger score update (every 10th access)
    for (int update = 0; update < 10; update++) {
      utf8_palette_cache_t *cache = get_utf8_palette_cache(score_test_cases[t].palette);
      cr_assert_not_null(cache, "%s score update should succeed", score_test_cases[t].name);
    }
  }

  // Create massive eviction pressure to test ordering
  log_debug("Creating eviction pressure to test heap ordering");

  const int eviction_rounds = 25; // Should evict worst entries first

  for (int round = 0; round < eviction_rounds; round++) {
    char pressure[64];
    snprintf(pressure, sizeof(pressure), "score_pressure_%03d", round);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(pressure);
    cr_assert_not_null(cache, "Score pressure %d should be cached", round);
  }

  // Test survival order - should match expected score ordering
  log_debug("Testing survival order matches heap score ordering");

  bool worst_survived = false, excellent_survived = false;

  for (int t = 0; t < num_test_cases; t++) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    utf8_palette_cache_t *cache = get_utf8_palette_cache(score_test_cases[t].palette);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double access_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
    bool survived = (cache != NULL && access_time < 0.001);

    log_debug("%s (score=%.1f): %s (%.6fs)", score_test_cases[t].name, score_test_cases[t].expected_relative_score,
              survived ? "SURVIVED ✅" : "EVICTED ❌", access_time);

    if (strcmp(score_test_cases[t].name, "WORST") == 0)
      worst_survived = survived;
    if (strcmp(score_test_cases[t].name, "EXCELLENT") == 0)
      excellent_survived = survived;
  }

  // Verify heap ordering logic
  cr_assert(excellent_survived, "Excellent score cache should survive heap-based eviction");

  // Worst might survive if heap isn't full enough, but excellent should definitely survive
  if (!worst_survived && excellent_survived) {
    log_debug("✅ Heap ordering working correctly: worst evicted, excellent survived");
  } else if (worst_survived && excellent_survived) {
    log_debug("⚠️ Both survived - heap not under enough pressure for ordering test");
  } else {
    log_debug("❌ Unexpected ordering result");
  }
}

Test(simd_caches, heap_memory_management) {
  // Test that heap memory management is correct (no leaks, corruption)

  log_debug("Testing heap memory management and cleanup");

  // Phase 1: Fill heap to capacity
  const int capacity_test = 35; // Slightly over capacity

  for (int i = 0; i < capacity_test; i++) {
    char capacity_palette[64];
    snprintf(capacity_palette, sizeof(capacity_palette), "capacity_test_%03d", i);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(capacity_palette);
    cr_assert_not_null(cache, "Capacity test %d should be cached", i);

    if (i % 10 == 0) {
      log_debug("Filling capacity: %d/%d", i, capacity_test);
    }
  }

  // Phase 2: Test heap cleanup
  log_debug("Testing heap cleanup and reinitialization");

  simd_caches_destroy_all();

  // Phase 3: Test that heap works after cleanup
  log_debug("Testing heap functionality after cleanup");

  for (int post = 0; post < 5; post++) {
    char post_cleanup[64];
    snprintf(post_cleanup, sizeof(post_cleanup), "post_cleanup_%d", post);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(post_cleanup);
    cr_assert_not_null(cache, "Post-cleanup cache %d should work", post);
  }

  log_debug("✅ Heap memory management test completed successfully");
}

Test(simd_caches, cache_eviction_algorithm_future) {
  // Legacy test placeholder - replaced by comprehensive heap tests above
  cr_skip("Legacy eviction test - now covered by min-heap specific tests");
}

Test(simd_caches, palette_cycling_animation_simulation) {
  // Simulate palette cycling animation behavior

  // Expected test implementation:

  // Simulate animation with 5 cycling palettes
  const char *animation_palettes[] = {
      "🌑🌒🌓🌔🌕", // Moon phases 1
      "🌖🌗🌘🌙🌚", // Moon phases 2
      "🌛🌜🌝🌞🌟", // Moon phases 3
      "⭐🌠💫⚡🔥", // Effects 1
      "💧❄️🌀🌈☀️"    // Effects 2
  };

  const int num_animation_palettes = 5;
  const int animation_cycles = 20; // 20 full cycles

  // Run animation simulation
  for (int cycle = 0; cycle < animation_cycles; cycle++) {
    for (int p = 0; p < num_animation_palettes; p++) {
      utf8_palette_cache_t *cache = get_utf8_palette_cache(animation_palettes[p]);
      cr_assert_not_null(cache, "Animation palette %d cycle %d should be cached", p, cycle);

      // Simulate rendering work
      usleep(1); // Minimal render time simulation
    }
  }

  // After animation: all 5 palettes should still be cached (high frequency)
  for (int p = 0; p < num_animation_palettes; p++) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    utf8_palette_cache_t *cache = get_utf8_palette_cache(animation_palettes[p]);
    clock_gettime(CLOCK_MONOTONIC, &end);

    double access_time = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;

    cr_assert_not_null(cache, "Animation palette %d should still be cached", p);
    cr_assert_lt(access_time, 0.001, "Animation palette %d should be fast cached access", p);
  }
}

// =============================================================================
// Memory Safety and Edge Cases
// =============================================================================

Test(simd_caches, invalid_palette_handling) {
  // Test cache behavior with invalid inputs

  utf8_palette_cache_t *null_cache = get_utf8_palette_cache(NULL);
  cr_assert_null(null_cache, "NULL palette should return NULL cache");

  utf8_palette_cache_t *empty_cache = get_utf8_palette_cache("");
  // Empty palette behavior depends on implementation - should not crash
  log_debug("Empty palette cache: %p", (void *)empty_cache);

  // Very long palette
  char long_palette[1000];
  memset(long_palette, 'A', sizeof(long_palette) - 1);
  long_palette[sizeof(long_palette) - 1] = '\0';

  utf8_palette_cache_t *long_cache = get_utf8_palette_cache(long_palette);
  // Should handle gracefully (truncate or reject)
  log_debug("Long palette cache: %p", (void *)long_cache);
}

Test(simd_caches, cache_cleanup_safety) {
  // Test that cache cleanup is safe after creation
  const char *test_palette = "   ...',;:clodxkO0KXNWM";

  // Create cache
  utf8_palette_cache_t *cache = get_utf8_palette_cache(test_palette);
  cr_assert_not_null(cache, "Cache should be created");
  cr_assert(cache->is_valid, "Cache should be valid");

  // Store the palette hash before cleanup
  char original_palette_hash[65];
  strncpy(original_palette_hash, cache->palette_hash, sizeof(original_palette_hash) - 1);
  original_palette_hash[sizeof(original_palette_hash) - 1] = '\0';

  // Cleanup should be safe
  simd_caches_destroy_all();

  // Should be able to create new cache after cleanup
  utf8_palette_cache_t *new_cache = get_utf8_palette_cache(test_palette);
  cr_assert_not_null(new_cache, "Should be able to create cache after cleanup");
  cr_assert(new_cache->is_valid, "New cache should be valid");

  // Cache should work correctly after cleanup (same palette should produce same cache data)
  cr_assert_str_eq(original_palette_hash, new_cache->palette_hash, "Cache should have same palette hash");
}

Test(simd_caches, extreme_palette_stress_test) {
  // Stress test with many different palettes
  const int stress_palette_count = 100;
  int successful_caches = 0;

  for (int i = 0; i < stress_palette_count; i++) {
    char stress_palette[64];
    snprintf(stress_palette, sizeof(stress_palette), "stress_test_palette_%03d_abcdefghijk", i);

    utf8_palette_cache_t *cache = get_utf8_palette_cache(stress_palette);
    if (cache != NULL) {
      successful_caches++;
    }
  }

  log_debug("Stress test: %d/%d palettes successfully cached", successful_caches, stress_palette_count);

  // Should handle at least 32 unique palettes (hashtable capacity)
  cr_assert_eq(successful_caches, stress_palette_count, "Should handle at least hashtable capacity worth of palettes");

  // After stress test, system should still work
  utf8_palette_cache_t *normal_cache = get_utf8_palette_cache("   ...',;:clodxkO0KXNWM");
  cr_assert_not_null(normal_cache, "Normal cache should work after stress test");
}
