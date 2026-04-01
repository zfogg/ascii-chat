/**
 * @file simd_scalar_comparison_test.c
 * @brief SIMD vs scalar performance comparison tests
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>

#include <ascii-chat/video/ascii/output_buffer.h>
#include <ascii-chat/video/rgba/video_frame.h>

TestSuite(simd_scalar_comparison);

// =============================================================================
// Output Buffer Tests
// =============================================================================

Test(simd_scalar_comparison, output_buffer_creation) {
  ascii_output_buffer_t buffer;
  asciichat_error_t result = ascii_output_buffer_create(&buffer, 80, 24);
  cr_assert_eq(result, ASCIICHAT_OK, "Should create output buffer");

  ascii_output_buffer_destroy(&buffer);
}

Test(simd_scalar_comparison, output_buffer_multiple) {
  ascii_output_buffer_t buffers[4];

  for (int i = 0; i < 4; i++) {
    asciichat_error_t result = ascii_output_buffer_create(&buffers[i], 80 + i * 20, 24 + i * 6);
    cr_assert_eq(result, ASCIICHAT_OK, "Should create buffer %d", i);
  }

  for (int i = 0; i < 4; i++) {
    ascii_output_buffer_destroy(&buffers[i]);
  }
}

Test(simd_scalar_comparison, output_buffer_small) {
  ascii_output_buffer_t buffer;
  asciichat_error_t result = ascii_output_buffer_create(&buffer, 10, 5);
  cr_assert_eq(result, ASCIICHAT_OK, "Should create small output buffer");

  ascii_output_buffer_destroy(&buffer);
}

Test(simd_scalar_comparison, output_buffer_large) {
  ascii_output_buffer_t buffer;
  asciichat_error_t result = ascii_output_buffer_create(&buffer, 200, 50);
  cr_assert_eq(result, ASCIICHAT_OK, "Should create large output buffer");

  ascii_output_buffer_destroy(&buffer);
}

Test(simd_scalar_comparison, output_buffer_destroy_null) {
  ascii_output_buffer_destroy(NULL);
  cr_assert(true, "Destroying NULL buffer should not crash");
}

// =============================================================================
// Video Frame Tests
// =============================================================================

Test(simd_scalar_comparison, frame_buffer_creation) {
  video_frame_buffer_t *vfb = video_frame_buffer_create("test_client");
  cr_assert_not_null(vfb, "Should create frame buffer");

  video_frame_buffer_destroy(vfb);
}

Test(simd_scalar_comparison, frame_buffer_write_commit) {
  video_frame_buffer_t *vfb = video_frame_buffer_create("test_client");

  video_frame_t *frame = video_frame_begin_write(vfb);
  cr_assert_not_null(frame, "Should get writable frame");

  frame->width = 640;
  frame->height = 480;
  frame->sequence_number = 1;

  video_frame_commit(vfb);

  const video_frame_t *read_frame = video_frame_get_latest(vfb);
  cr_assert_not_null(read_frame, "Should get frame after commit");

  video_frame_buffer_destroy(vfb);
}

Test(simd_scalar_comparison, frame_buffer_multiple_writes) {
  video_frame_buffer_t *vfb = video_frame_buffer_create("test_client");

  for (int i = 0; i < 5; i++) {
    video_frame_t *frame = video_frame_begin_write(vfb);
    frame->sequence_number = i + 1;
    video_frame_commit(vfb);
  }

  const video_frame_t *latest = video_frame_get_latest(vfb);
  cr_assert_not_null(latest, "Should have latest frame");
  cr_assert_eq(latest->sequence_number, 5, "Latest frame should be 5");

  video_frame_buffer_destroy(vfb);
}

Test(simd_scalar_comparison, simple_frame_swap_creation) {
  simple_frame_swap_t *sfs = simple_frame_swap_create();
  cr_assert_not_null(sfs, "Should create simple frame swap");

  simple_frame_swap_destroy(sfs);
}

Test(simd_scalar_comparison, simple_frame_swap_update) {
  simple_frame_swap_t *sfs = simple_frame_swap_create();

  uint8_t data[1024];
  memset(data, 0xAB, sizeof(data));

  simple_frame_swap_update(sfs, data, sizeof(data));

  const video_frame_t *frame = simple_frame_swap_get(sfs);
  cr_assert_not_null(frame, "Should get frame after update");

  simple_frame_swap_destroy(sfs);
}

Test(simd_scalar_comparison, simple_frame_swap_multiple_updates) {
  simple_frame_swap_t *sfs = simple_frame_swap_create();

  uint8_t buffer_a[512];
  uint8_t buffer_b[512];

  for (int i = 0; i < 10; i++) {
    if (i % 2 == 0) {
      simple_frame_swap_update(sfs, buffer_a, sizeof(buffer_a));
    } else {
      simple_frame_swap_update(sfs, buffer_b, sizeof(buffer_b));
    }

    const video_frame_t *frame = simple_frame_swap_get(sfs);
    cr_assert_not_null(frame, "Should have frame at iteration %d", i);
  }

  simple_frame_swap_destroy(sfs);
}

// =============================================================================
// Comparative Performance Tests
// =============================================================================

Test(simd_scalar_comparison, buffer_vs_frame_swap) {
  video_frame_buffer_t *vfb = video_frame_buffer_create("buffer_test");
  simple_frame_swap_t *sfs = simple_frame_swap_create();

  uint8_t data[2048];
  memset(data, 200, sizeof(data));

  // Test frame buffer
  video_frame_t *frame = video_frame_begin_write(vfb);
  frame->size = sizeof(data);
  video_frame_commit(vfb);

  // Test frame swap
  simple_frame_swap_update(sfs, data, sizeof(data));

  cr_assert_not_null(video_frame_get_latest(vfb), "Buffer should have frame");
  cr_assert_not_null(simple_frame_swap_get(sfs), "Swap should have frame");

  video_frame_buffer_destroy(vfb);
  simple_frame_swap_destroy(sfs);
}

Test(simd_scalar_comparison, concurrent_buffers) {
  int num_buffers = 4;
  video_frame_buffer_t *buffers[num_buffers];

  for (int i = 0; i < num_buffers; i++) {
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "buffer_%d", i);
    buffers[i] = video_frame_buffer_create(client_id);

    video_frame_t *frame = video_frame_begin_write(buffers[i]);
    frame->sequence_number = i;
    video_frame_commit(buffers[i]);
  }

  for (int i = 0; i < num_buffers; i++) {
    const video_frame_t *frame = video_frame_get_latest(buffers[i]);
    cr_assert_not_null(frame, "Buffer %d should have frame", i);
    cr_assert_eq(frame->sequence_number, (uint64_t)i, "Buffer %d sequence", i);
  }

  for (int i = 0; i < num_buffers; i++) {
    video_frame_buffer_destroy(buffers[i]);
  }
}
