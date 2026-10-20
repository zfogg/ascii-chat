#include <criterion/criterion.h>
#include <ascii-chat/video/webcam/frame_clock.h>

Test(webcam_clock, detects_buffered_frames_and_catchup_without_hiding_backlog) {
  webcam_frame_clock_t clock = {0};
  cr_assert_eq(webcam_frame_clock_age(&clock, 1000000, 1000000000), 0);
  // A 60 Hz camera consumed at 5 Hz accumulates queued samples.
  cr_assert_eq(webcam_frame_clock_age(&clock, 1166667, 1200000000), 183333300);
  cr_assert_eq(webcam_frame_clock_age(&clock, 1333334, 1201000000), 167666600);
  // Draining reaches the current image without resetting away the old delay.
  cr_assert_eq(webcam_frame_clock_age(&clock, 3000000, 1202000000), 2000000);
  cr_assert_eq(webcam_frame_clock_age(&clock, 5000000, 1402000000), 2000000);
}

Test(webcam_clock, faster_delivery_and_clock_restarts_establish_a_new_origin) {
  webcam_frame_clock_t clock = {0};
  cr_assert_eq(webcam_frame_clock_age(&clock, -100000, 1000000000), 0);
  cr_assert_eq(webcam_frame_clock_age(&clock, 100000, 1010000000), 0);
  cr_assert_eq(webcam_frame_clock_age(&clock, 200000, 1025000000), 5000000);
  cr_assert_eq(webcam_frame_clock_age(&clock, 0, 1030000000), 0);
  cr_assert_eq(webcam_frame_clock_age(&clock, 10000, 100), 0);
  cr_assert_eq(webcam_frame_clock_age(&clock, 20000, 1000100), 0);
}
