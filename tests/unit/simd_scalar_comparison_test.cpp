#include <catch2/catch_test_macros.hpp>
/**
 * @file simd_scalar_comparison_test.c
 * @brief SIMD vs scalar performance comparison tests
 */

#include <string.h>
#include <stdio.h>

#include <ascii-chat/video/rgba/video_frame.h>
// =============================================================================
// Video Frame Tests
// =============================================================================

TEST_CASE("simd_scalar_comparison/frame_buffer_creation", "[simd_scalar_comparison] [unit]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("test_client");
  REQUIRE((vfb) != nullptr);

  video_frame_buffer_destroy(vfb);
}

TEST_CASE("simd_scalar_comparison/frame_buffer_write_commit", "[simd_scalar_comparison] [unit]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("test_client");

  video_frame_t *frame = video_frame_begin_write(vfb);
  REQUIRE((frame) != nullptr);

  frame->width = 640;
  frame->height = 480;
  frame->sequence_number = 1;

  video_frame_commit(vfb);

  const video_frame_t *read_frame = video_frame_get_latest(vfb);
  REQUIRE((read_frame) != nullptr);

  video_frame_buffer_destroy(vfb);
}

TEST_CASE("simd_scalar_comparison/frame_buffer_multiple_writes", "[simd_scalar_comparison] [unit]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("test_client");

  for (int i = 0; i < 5; i++) {
    video_frame_t *frame = video_frame_begin_write(vfb);
    frame->sequence_number = i + 1;
    video_frame_commit(vfb);
  }

  const video_frame_t *latest = video_frame_get_latest(vfb);
  REQUIRE((latest) != nullptr);
  REQUIRE((latest->sequence_number) == (5));

  video_frame_buffer_destroy(vfb);
}

TEST_CASE("simd_scalar_comparison/simple_frame_swap_creation", "[simd_scalar_comparison] [unit]") {
  simple_frame_swap_t *sfs = simple_frame_swap_create();
  REQUIRE((sfs) != nullptr);

  simple_frame_swap_destroy(sfs);
}

TEST_CASE("simd_scalar_comparison/simple_frame_swap_update", "[simd_scalar_comparison] [unit]") {
  simple_frame_swap_t *sfs = simple_frame_swap_create();

  uint8_t data[1024];
  memset(data, 0xAB, sizeof(data));

  simple_frame_swap_update(sfs, data, sizeof(data));

  const video_frame_t *frame = simple_frame_swap_get(sfs);
  REQUIRE((frame) != nullptr);

  simple_frame_swap_destroy(sfs);
}

TEST_CASE("simd_scalar_comparison/simple_frame_swap_multiple_updates", "[simd_scalar_comparison] [unit]") {
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
    REQUIRE((frame) != nullptr);
  }

  simple_frame_swap_destroy(sfs);
}

// =============================================================================
// Comparative Performance Tests
// =============================================================================

TEST_CASE("simd_scalar_comparison/buffer_vs_frame_swap", "[simd_scalar_comparison] [unit]") {
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

  REQUIRE((video_frame_get_latest(vfb)) != nullptr);
  REQUIRE((simple_frame_swap_get(sfs)) != nullptr);

  video_frame_buffer_destroy(vfb);
  simple_frame_swap_destroy(sfs);
}

TEST_CASE("simd_scalar_comparison/concurrent_buffers", "[simd_scalar_comparison] [unit]") {
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
    REQUIRE((frame) != nullptr);
    REQUIRE((frame->sequence_number) == ((uint64_t)i));
  }

  for (int i = 0; i < num_buffers; i++) {
    video_frame_buffer_destroy(buffers[i]);
  }
}
