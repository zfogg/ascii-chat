#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/video/webcam/frame_clock.h>

TEST_CASE("webcam_clock/detects_buffered_frames_and_catchup_without_hiding_backlog", "[webcam_clock]") {
  webcam_frame_clock_t clock = {0};
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 1000000, 1000000000), 0);
  // A 60 Hz camera consumed at 5 Hz accumulates queued samples.
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 1166667, 1200000000), 183333300);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 1333334, 1201000000), 167666600);
  // Draining reaches the current image without resetting away the old delay.
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 3000000, 1202000000), 2000000);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 5000000, 1402000000), 2000000);
}

TEST_CASE("webcam_clock/faster_delivery_and_clock_restarts_establish_a_new_origin", "[webcam_clock]") {
  webcam_frame_clock_t clock = {0};
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, -100000, 1000000000), 0);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 100000, 1010000000), 0);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 200000, 1025000000), 5000000);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 0, 1030000000), 0);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 10000, 100), 0);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 20000, 1000100), 0);
}

TEST_CASE("webcam_clock/flushing_a_drifting_clock_allows_new_frames_again", "[webcam_clock]") {
  webcam_frame_clock_t clock = {0};
  webcam_frame_clock_age(&clock, 0, 1000000000);
  for (unsigned i = 1; i <= 200; ++i)
    webcam_frame_clock_age(&clock, i * INT64_C(100000), 1000000000 + i * UINT64_C(20000000));
  ASCIICHAT_REQUIRE_GT(webcam_frame_clock_age(&clock, 20100000, 5020000000), 33000000);
  // The caller flushes queued reader samples before establishing a new origin.
  webcam_frame_clock_reset(&clock);
  ASCIICHAT_REQUIRE_NOT(clock.initialized);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 20200000, 5040000000), 0);
  ASCIICHAT_REQUIRE_EQ(webcam_frame_clock_age(&clock, 20400000, 5060000000), 0);
}
