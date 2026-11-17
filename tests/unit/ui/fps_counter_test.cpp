#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/ui/fps_counter.h>

TEST_CASE("fps_counter/startup_and_rates", "[fps_counter] [unit]") {
  fps_counter_t *counter = fps_counter_create();
  REQUIRE((counter) != nullptr);
  REQUIRE((fps_counter_get_at(counter, 0)) == (0));
  const unsigned rates[] = {1, 9, 10, 60, 99, 100, 144};
  for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
    fps_counter_reset(counter);
    uint64_t step = 1000000000ULL / rates[r];
    for (unsigned i = 0; i < 70; i++)
      fps_counter_tick_at(counter, i * step);
    REQUIRE((fps_counter_get_at(counter, 69 * step)) == Catch::Approx(rates[r]));
  }
  fps_counter_destroy(counter);
}

TEST_CASE("fps_counter/inactivity_and_clock_reset", "[fps_counter] [unit]") {
  fps_counter_t *counter = fps_counter_create();
  fps_counter_tick_at(counter, 0);
  fps_counter_tick_at(counter, 10000000);
  REQUIRE((fps_counter_get_at(counter, 10000000)) == Catch::Approx(100));
  REQUIRE((fps_counter_get_at(counter, 1010000000)) < (2));
  REQUIRE((fps_counter_get_at(counter, 2010000000)) == (0));
  fps_counter_tick_at(counter, 3000000000);
  REQUIRE((fps_counter_get_at(counter, 3000000000)) == (0));
  fps_counter_tick_at(counter, 3010000000);
  REQUIRE((fps_counter_get_at(counter, 3010000000)) == Catch::Approx(100));
  fps_counter_tick_at(counter, 1);
  REQUIRE((fps_counter_get_at(counter, 1)) == (0));
  fps_counter_tick_at(counter, 1);
  REQUIRE((fps_counter_get_at(counter, 1)) == (0));
  fps_counter_reset(counter);
  REQUIRE((fps_counter_get_at(counter, 1)) == (0));
  fps_counter_destroy(counter);
}

TEST_CASE("fps_counter/presentation_counts_only_complete_frames", "[fps_counter] [unit]") {
  fps_counter_t *counter = fps_counter_create();
  // Two writes are one presentation, and a failed second write invalidates it.
  for (unsigned i = 0; i < 5; i++) {
    fps_counter_frame_begin(counter, i != 3);
    if (i != 2) {
      fps_counter_write_begin(counter);
      fps_counter_write_end(counter, true);
      fps_counter_write_begin(counter);
      fps_counter_write_end(counter, i != 1);
    }
    fps_counter_frame_end(counter, i * 10000000ULL);
  }
  REQUIRE((fps_counter_get_at(counter, 40000000)) == Catch::Approx(25));
  // Writes outside a presentation (including the overlay) must not count.
  fps_counter_write_begin(counter);
  fps_counter_write_end(counter, true);
  fps_counter_frame_end(counter, 50000000);
  REQUIRE((fps_counter_get_at(counter, 50000000)) == Catch::Approx(20));
  REQUIRE(fps_counter_set_visible(counter, true));
  fps_counter_reset(counter);
  REQUIRE(!(fps_counter_set_visible(counter, true)));
  REQUIRE(fps_counter_set_visible(counter, false));
  fps_counter_destroy(counter);
}

TEST_CASE("fps_counter/acquired_frame_ids_exclude_redraws_and_failed_presentations", "[fps_counter] [unit]") {
  fps_counter_t *counter = fps_counter_create();
  for (unsigned i = 0; i < 3; ++i) {
    REQUIRE(fps_counter_frame_begin_id(counter, i + 1));
    fps_counter_write_begin(counter);
    fps_counter_write_end(counter, true);
    fps_counter_frame_end(counter, i * 100000000ULL);
    // Identical pixels in a new acquired frame still count; reusing an ID does not.
    REQUIRE(!(fps_counter_frame_begin_id(counter, i + 1)));
    fps_counter_write_begin(counter);
    fps_counter_write_end(counter, true);
    fps_counter_frame_end(counter, i * 100000000ULL + 1000000ULL);
  }
  REQUIRE((fps_counter_get_at(counter, 200000000ULL)) == Catch::Approx(10));
  REQUIRE(!(fps_counter_frame_begin_id(counter, 0)));
  fps_counter_frame_end(counter, 202000000ULL);
  REQUIRE(fps_counter_frame_begin_id(counter, 4));
  fps_counter_write_begin(counter);
  fps_counter_write_end(counter, false);
  fps_counter_frame_end(counter, 203000000ULL);
  REQUIRE(fps_counter_frame_begin_id(counter, 4));
  fps_counter_write_begin(counter);
  fps_counter_write_end(counter, true);
  fps_counter_frame_end(counter, 300000000ULL);
  REQUIRE((fps_counter_get_at(counter, 300000000ULL)) == Catch::Approx(10));
  fps_counter_destroy(counter);
}
