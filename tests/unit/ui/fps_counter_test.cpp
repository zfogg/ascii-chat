#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/ui/fps_counter.h>

TEST_CASE("fps_counter/startup_and_rates", "[fps_counter]") {
  fps_counter_t *counter = fps_counter_create();
  ASCIICHAT_REQUIRE_NOT_NULL(counter);
  ASCIICHAT_REQUIRE_EQ(fps_counter_get_at(counter, 0), 0);
  const unsigned rates[] = {1, 9, 10, 60, 99, 100, 144};
  for (unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
    fps_counter_reset(counter);
    uint64_t step = 1000000000ULL / rates[r];
    for (unsigned i = 0; i < 70; i++)
      fps_counter_tick_at(counter, i * step);
    ASCIICHAT_REQUIRE_FLOAT_EQ(fps_counter_get_at(counter, 69 * step), rates[r], 0.01);
  }
  fps_counter_destroy(counter);
}

TEST_CASE("fps_counter/inactivity_and_clock_reset", "[fps_counter]") {
  fps_counter_t *counter = fps_counter_create();
  fps_counter_tick_at(counter, 0);
  fps_counter_tick_at(counter, 10000000);
  ASCIICHAT_REQUIRE_FLOAT_EQ(fps_counter_get_at(counter, 10000000), 100, 0.01);
  ASCIICHAT_REQUIRE_LT(fps_counter_get_at(counter, 1010000000), 2);
  ASCIICHAT_REQUIRE_EQ(fps_counter_get_at(counter, 2010000000), 0);
  fps_counter_tick_at(counter, 3000000000);
  ASCIICHAT_REQUIRE_EQ(fps_counter_get_at(counter, 3000000000), 0);
  fps_counter_tick_at(counter, 3010000000);
  ASCIICHAT_REQUIRE_FLOAT_EQ(fps_counter_get_at(counter, 3010000000), 100, 0.01);
  fps_counter_tick_at(counter, 1);
  ASCIICHAT_REQUIRE_EQ(fps_counter_get_at(counter, 1), 0);
  fps_counter_tick_at(counter, 1);
  ASCIICHAT_REQUIRE_EQ(fps_counter_get_at(counter, 1), 0);
  fps_counter_reset(counter);
  ASCIICHAT_REQUIRE_EQ(fps_counter_get_at(counter, 1), 0);
  fps_counter_destroy(counter);
}

TEST_CASE("fps_counter/presentation_counts_only_complete_frames", "[fps_counter]") {
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
  ASCIICHAT_REQUIRE_FLOAT_EQ(fps_counter_get_at(counter, 40000000), 25, 0.01);
  // Writes outside a presentation (including the overlay) must not count.
  fps_counter_write_begin(counter);
  fps_counter_write_end(counter, true);
  fps_counter_frame_end(counter, 50000000);
  ASCIICHAT_REQUIRE_FLOAT_EQ(fps_counter_get_at(counter, 50000000), 20, 0.01);
  ASCIICHAT_REQUIRE(fps_counter_set_visible(counter, true));
  fps_counter_reset(counter);
  ASCIICHAT_REQUIRE_NOT(fps_counter_set_visible(counter, true));
  ASCIICHAT_REQUIRE(fps_counter_set_visible(counter, false));
  fps_counter_destroy(counter);
}

TEST_CASE("fps_counter/acquired_frame_ids_exclude_redraws_and_failed_presentations", "[fps_counter]") {
  fps_counter_t *counter = fps_counter_create();
  for (unsigned i = 0; i < 3; ++i) {
    ASCIICHAT_REQUIRE(fps_counter_frame_begin_id(counter, i + 1));
    fps_counter_write_begin(counter);
    fps_counter_write_end(counter, true);
    fps_counter_frame_end(counter, i * 100000000ULL);
    // Identical pixels in a new acquired frame still count; reusing an ID does not.
    ASCIICHAT_REQUIRE_NOT(fps_counter_frame_begin_id(counter, i + 1));
    fps_counter_write_begin(counter);
    fps_counter_write_end(counter, true);
    fps_counter_frame_end(counter, i * 100000000ULL + 1000000ULL);
  }
  ASCIICHAT_REQUIRE_FLOAT_EQ(fps_counter_get_at(counter, 200000000ULL), 10, 0.01);
  ASCIICHAT_REQUIRE_NOT(fps_counter_frame_begin_id(counter, 0));
  fps_counter_frame_end(counter, 202000000ULL);
  ASCIICHAT_REQUIRE(fps_counter_frame_begin_id(counter, 4));
  fps_counter_write_begin(counter);
  fps_counter_write_end(counter, false);
  fps_counter_frame_end(counter, 203000000ULL);
  ASCIICHAT_REQUIRE(fps_counter_frame_begin_id(counter, 4));
  fps_counter_write_begin(counter);
  fps_counter_write_end(counter, true);
  fps_counter_frame_end(counter, 300000000ULL);
  ASCIICHAT_REQUIRE_FLOAT_EQ(fps_counter_get_at(counter, 300000000ULL), 10, 0.01);
  fps_counter_destroy(counter);
}
