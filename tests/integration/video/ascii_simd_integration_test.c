/**
 * @file ascii_simd_integration_test.c
 * @brief SIMD ASCII rendering integration tests
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>

#include <ascii-chat/video/rgba/video_frame.h>

TestSuite(ascii_simd_integration);

Test(ascii_simd_integration, quality_metrics_collection) {
  video_frame_buffer_t *vfb = video_frame_buffer_create("quality_test");

  // Write multiple frames and consume them to avoid dropping
  for (int i = 0; i < 10; i++) {
    video_frame_t *frame = video_frame_begin_write(vfb);
    frame->sequence_number = i + 1;
    frame->encoding_time_ns = 16666667;
    video_frame_commit(vfb);

    // Consume the frame to prevent drops
    const video_frame_t *read_frame = video_frame_get_latest(vfb);
    (void)read_frame;  // Suppress unused variable warning
  }

  // Get statistics
  video_frame_stats_t stats;
  video_frame_get_stats(vfb, &stats);

  cr_assert_eq(stats.total_frames, 10, "Should have 10 frames");
  cr_assert_eq(stats.dropped_frames, 0, "Should have no dropped frames");

  video_frame_buffer_destroy(vfb);
}
