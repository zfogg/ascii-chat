/**
 * @file ascii_simd_integration_test.c
 * @brief SIMD ASCII rendering integration tests
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>

#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/rgba/video_frame.h>

TestSuite(ascii_simd_integration);

// =============================================================================
// Frame Buffer + Output Buffer Integration
// =============================================================================

Test(ascii_simd_integration, frame_buffer_with_output_buffer) {
  video_frame_buffer_t *vfb = video_frame_buffer_create("integration_test");
  ascii_output_buffer_t output;

  cr_assert_not_null(vfb, "Should create frame buffer");

  // Create output buffer
  asciichat_error_t result = ascii_output_buffer_create(&output, 80, 24);
  cr_assert_eq(result, ASCIICHAT_OK, "Should create output buffer");

  // Write frame to buffer
  video_frame_t *frame = video_frame_begin_write(vfb);
  frame->width = 640;
  frame->height = 480;
  frame->sequence_number = 1;
  video_frame_commit(vfb);

  // Verify frame was written
  const video_frame_t *read_frame = video_frame_get_latest(vfb);
  cr_assert_not_null(read_frame, "Should get frame after commit");

  // Cleanup
  ascii_output_buffer_destroy(&output);
  video_frame_buffer_destroy(vfb);
}

Test(ascii_simd_integration, multiple_clients_concurrent) {
  int num_clients = 3;
  video_frame_buffer_t *buffers[num_clients];
  ascii_output_buffer_t outputs[num_clients];

  // Create frame buffers and output buffers for each client
  for (int i = 0; i < num_clients; i++) {
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "client_%d", i);
    buffers[i] = video_frame_buffer_create(client_id);
    cr_assert_not_null(buffers[i], "Should create buffer for client %d", i);

    asciichat_error_t result = ascii_output_buffer_create(&outputs[i], 80, 24);
    cr_assert_eq(result, ASCIICHAT_OK, "Should create output for client %d", i);
  }

  // Write frames from all clients
  for (int i = 0; i < num_clients; i++) {
    video_frame_t *frame = video_frame_begin_write(buffers[i]);
    frame->width = 320 + i * 100;
    frame->height = 240 + i * 50;
    frame->sequence_number = 1;
    video_frame_commit(buffers[i]);
  }

  // Verify all frames available
  for (int i = 0; i < num_clients; i++) {
    const video_frame_t *frame = video_frame_get_latest(buffers[i]);
    cr_assert_not_null(frame, "Client %d should have frame", i);
    cr_assert_eq(frame->width, 320 + i * 100, "Client %d width should match", i);
  }

  // Cleanup
  for (int i = 0; i < num_clients; i++) {
    ascii_output_buffer_destroy(&outputs[i]);
    video_frame_buffer_destroy(buffers[i]);
  }
}

Test(ascii_simd_integration, frame_swap_integration) {
  simple_frame_swap_t *sfs = simple_frame_swap_create();
  ascii_output_buffer_t output;

  cr_assert_not_null(sfs, "Should create frame swap");

  asciichat_error_t result = ascii_output_buffer_create(&output, 80, 24);
  cr_assert_eq(result, ASCIICHAT_OK, "Should create output buffer");

  // Create test data
  uint8_t data[2048];
  memset(data, 200, sizeof(data));

  // Update frame swap
  simple_frame_swap_update(sfs, data, sizeof(data));

  // Get frame
  const video_frame_t *frame = simple_frame_swap_get(sfs);
  cr_assert_not_null(frame, "Should get frame from swap");

  // Cleanup
  ascii_output_buffer_destroy(&output);
  simple_frame_swap_destroy(sfs);
}

Test(ascii_simd_integration, streaming_sequence) {
  video_frame_buffer_t *vfb = video_frame_buffer_create("stream_test");
  ascii_output_buffer_t output;

  asciichat_error_t result = ascii_output_buffer_create(&output, 80, 24);
  cr_assert_eq(result, ASCIICHAT_OK, "Should create output buffer");

  // Simulate frame stream
  for (int frame_num = 0; frame_num < 10; frame_num++) {
    video_frame_t *frame = video_frame_begin_write(vfb);
    frame->width = 640;
    frame->height = 480;
    frame->sequence_number = frame_num + 1;
    video_frame_commit(vfb);

    const video_frame_t *latest = video_frame_get_latest(vfb);
    cr_assert_not_null(latest, "Should have frame %d", frame_num);
    cr_assert_eq(latest->sequence_number, frame_num + 1, "Sequence should match");
  }

  // Cleanup
  ascii_output_buffer_destroy(&output);
  video_frame_buffer_destroy(vfb);
}

Test(ascii_simd_integration, quality_metrics_collection) {
  video_frame_buffer_t *vfb = video_frame_buffer_create("quality_test");

  // Write multiple frames
  for (int i = 0; i < 10; i++) {
    video_frame_t *frame = video_frame_begin_write(vfb);
    frame->sequence_number = i + 1;
    frame->encoding_time_ns = 16666667;
    video_frame_commit(vfb);
  }

  // Get statistics
  video_frame_stats_t stats;
  video_frame_get_stats(vfb, &stats);

  cr_assert_eq(stats.total_frames, 10, "Should have 10 frames");
  cr_assert_eq(stats.dropped_frames, 0, "Should have no dropped frames");

  video_frame_buffer_destroy(vfb);
}

Test(ascii_simd_integration, multiple_resolutions) {
  uint32_t resolutions[][2] = {
      {320, 240},
      {640, 480},
      {800, 600},
      {1280, 720},
  };

  ascii_output_buffer_t outputs[4];

  for (size_t i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++) {
    asciichat_error_t result = ascii_output_buffer_create(&outputs[i], 80, 24);
    cr_assert_eq(result, ASCIICHAT_OK, "Should create output for resolution %zu", i);
  }

  for (size_t i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); i++) {
    ascii_output_buffer_destroy(&outputs[i]);
  }
}
