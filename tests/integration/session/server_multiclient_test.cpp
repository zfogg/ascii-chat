/**
 * @file server_multiclient_test.c
 * @brief Server multi-client integration tests
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <stdint.h>

#include <ascii-chat/video/rgba/video_frame.h>
// =============================================================================
// Video Frame Buffer Creation Tests
// =============================================================================

TEST_CASE("server_multiclient/create_frame_buffer", "[server_multiclient]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("client_1");
  ASCIICHAT_REQUIRE_NOT_NULL(vfb, "Should create frame buffer");

  video_frame_buffer_destroy(vfb);
}

TEST_CASE("server_multiclient/create_multiple_frame_buffers", "[server_multiclient]") {
  int num_clients = 4;
  video_frame_buffer_t *buffers[num_clients];

  for (int i = 0; i < num_clients; i++) {
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "client_%d", i);
    buffers[i] = video_frame_buffer_create(client_id);
    ASCIICHAT_REQUIRE_NOT_NULL(buffers[i], "Should create buffer for client %d", i);
  }

  for (int i = 0; i < num_clients; i++) {
    video_frame_buffer_destroy(buffers[i]);
  }
}

TEST_CASE("server_multiclient/destroy_null_buffer", "[server_multiclient]") {
  video_frame_buffer_destroy(NULL);
  ASCIICHAT_REQUIRE(true, "Destroying NULL buffer should not crash");
}

// =============================================================================
// Frame Writing Tests
// =============================================================================

TEST_CASE("server_multiclient/begin_write_and_commit", "[server_multiclient]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("client_1");

  video_frame_t *frame = video_frame_begin_write(vfb);
  ASCIICHAT_REQUIRE_NOT_NULL(frame, "Should get writable frame");

  frame->width = 640;
  frame->height = 480;
  frame->size = 640 * 480 * 4;
  frame->sequence_number = 1;

  video_frame_commit(vfb);

  video_frame_buffer_destroy(vfb);
}

TEST_CASE("server_multiclient/multiple_writes", "[server_multiclient]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("client_1");

  // Write multiple frames
  for (int i = 0; i < 5; i++) {
    video_frame_t *frame = video_frame_begin_write(vfb);
    ASCIICHAT_REQUIRE_NOT_NULL(frame, "Should get writable frame on iteration %d", i);

    frame->width = 640 + i;
    frame->height = 480 + i;
    frame->size = (640 + i) * (480 + i) * 4;
    frame->sequence_number = i + 1;

    video_frame_commit(vfb);
  }

  video_frame_buffer_destroy(vfb);
}

// =============================================================================
// Frame Reading Tests
// =============================================================================

TEST_CASE("server_multiclient/get_latest_frame", "[server_multiclient]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("client_1");

  // Write a frame
  video_frame_t *write_frame = video_frame_begin_write(vfb);
  write_frame->width = 640;
  write_frame->height = 480;
  write_frame->sequence_number = 1;
  video_frame_commit(vfb);

  // Read the frame
  const video_frame_t *read_frame = video_frame_get_latest(vfb);
  ASCIICHAT_REQUIRE_NOT_NULL(read_frame, "Should get latest frame after write");
  ASCIICHAT_REQUIRE_EQ(read_frame->width, 640, "Frame width should match");
  ASCIICHAT_REQUIRE_EQ(read_frame->height, 480, "Frame height should match");

  video_frame_buffer_destroy(vfb);
}

TEST_CASE("server_multiclient/no_frame_initially", "[server_multiclient]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("client_1");

  // No write yet, should return NULL
  const video_frame_t *frame = video_frame_get_latest(vfb);
  ASCIICHAT_REQUIRE_NULL(frame, "Should return NULL when no frame written");

  video_frame_buffer_destroy(vfb);
}

TEST_CASE("server_multiclient/latest_frame_wins", "[server_multiclient]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("client_1");

  // Write first frame
  video_frame_t *frame1 = video_frame_begin_write(vfb);
  frame1->sequence_number = 1;
  video_frame_commit(vfb);

  // Write second frame (overwrites first)
  video_frame_t *frame2 = video_frame_begin_write(vfb);
  frame2->sequence_number = 2;
  video_frame_commit(vfb);

  // Should get second frame
  const video_frame_t *latest = video_frame_get_latest(vfb);
  ASCIICHAT_REQUIRE_NOT_NULL(latest, "Should get frame");
  ASCIICHAT_REQUIRE_EQ(latest->sequence_number, 2, "Should have second frame");

  video_frame_buffer_destroy(vfb);
}

// =============================================================================
// Frame Statistics Tests
// =============================================================================

TEST_CASE("server_multiclient/get_frame_stats", "[server_multiclient]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("client_1");

  // Write frames
  for (int i = 0; i < 3; i++) {
    video_frame_t *frame = video_frame_begin_write(vfb);
    frame->sequence_number = i + 1;
    video_frame_commit(vfb);
    ASCIICHAT_REQUIRE_NOT_NULL(video_frame_get_latest(vfb), "Should consume committed frame %d", i + 1);
  }

  // Get stats
  video_frame_stats_t stats;
  video_frame_get_stats(vfb, &stats);

  ASCIICHAT_REQUIRE_EQ(stats.total_frames, 3, "Should have 3 total frames");
  ASCIICHAT_REQUIRE_EQ(stats.dropped_frames, 0, "Should have no dropped frames");

  video_frame_buffer_destroy(vfb);
}

TEST_CASE("server_multiclient/stats_initially_zero", "[server_multiclient]") {
  video_frame_buffer_t *vfb = video_frame_buffer_create("client_1");

  video_frame_stats_t stats;
  video_frame_get_stats(vfb, &stats);

  ASCIICHAT_REQUIRE_EQ(stats.total_frames, 0, "Should start with 0 frames");
  ASCIICHAT_REQUIRE_EQ(stats.dropped_frames, 0, "Should start with 0 dropped");

  video_frame_buffer_destroy(vfb);
}

// =============================================================================
// Simple Frame Swap Tests
// =============================================================================

TEST_CASE("server_multiclient/create_simple_frame_swap", "[server_multiclient]") {
  simple_frame_swap_t *sfs = simple_frame_swap_create();
  ASCIICHAT_REQUIRE_NOT_NULL(sfs, "Should create simple frame swap");

  simple_frame_swap_destroy(sfs);
}

TEST_CASE("server_multiclient/simple_frame_swap_update", "[server_multiclient]") {
  simple_frame_swap_t *sfs = simple_frame_swap_create();

  uint8_t data[1024];
  memset(data, 0xAB, sizeof(data));

  simple_frame_swap_update(sfs, data, sizeof(data));

  const video_frame_t *frame = simple_frame_swap_get(sfs);
  ASCIICHAT_REQUIRE_NOT_NULL(frame, "Should get frame after update");
  ASCIICHAT_REQUIRE_EQ(frame->size, sizeof(data), "Frame size should match");

  simple_frame_swap_destroy(sfs);
}

TEST_CASE("server_multiclient/simple_frame_swap_null_initially", "[server_multiclient]") {
  simple_frame_swap_t *sfs = simple_frame_swap_create();

  const video_frame_t *frame = simple_frame_swap_get(sfs);
  ASCIICHAT_REQUIRE_NULL(frame, "Should return NULL initially");

  simple_frame_swap_destroy(sfs);
}

TEST_CASE("server_multiclient/simple_frame_swap_multiple_updates", "[server_multiclient]") {
  simple_frame_swap_t *sfs = simple_frame_swap_create();

  // Alternate between two buffers
  uint8_t buffer_a[512];
  uint8_t buffer_b[512];

  memset(buffer_a, 0xAA, sizeof(buffer_a));
  memset(buffer_b, 0xBB, sizeof(buffer_b));

  for (int i = 0; i < 5; i++) {
    if (i % 2 == 0) {
      simple_frame_swap_update(sfs, buffer_a, sizeof(buffer_a));
    } else {
      simple_frame_swap_update(sfs, buffer_b, sizeof(buffer_b));
    }

    const video_frame_t *frame = simple_frame_swap_get(sfs);
    ASCIICHAT_REQUIRE_NOT_NULL(frame, "Should have frame at iteration %d", i);
  }

  simple_frame_swap_destroy(sfs);
}

// =============================================================================
// Multi-Client Scenario Tests
// =============================================================================

TEST_CASE("server_multiclient/multiclient_concurrent_writes", "[server_multiclient]") {
  int num_clients = 4;
  video_frame_buffer_t *buffers[num_clients];

  // Create buffers for multiple clients
  for (int i = 0; i < num_clients; i++) {
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "client_%d", i);
    buffers[i] = video_frame_buffer_create(client_id);
  }

  // Simulate concurrent writes from each client
  for (int round = 0; round < 3; round++) {
    for (int i = 0; i < num_clients; i++) {
      video_frame_t *frame = video_frame_begin_write(buffers[i]);
      ASCIICHAT_REQUIRE_NOT_NULL(frame, "Should write frame for client %d round %d", i, round);

      frame->width = 640 + i;
      frame->height = 480 + i;
      frame->sequence_number = round * num_clients + i;

      video_frame_commit(buffers[i]);
    }
  }

  // Verify all clients have latest frames
  for (int i = 0; i < num_clients; i++) {
    const video_frame_t *frame = video_frame_get_latest(buffers[i]);
    ASCIICHAT_REQUIRE_NOT_NULL(frame, "Client %d should have frame", i);
    ASCIICHAT_REQUIRE_EQ(frame->width, 640 + i, "Client %d width should match", i);
  }

  // Cleanup
  for (int i = 0; i < num_clients; i++) {
    video_frame_buffer_destroy(buffers[i]);
  }
}

TEST_CASE("server_multiclient/multiclient_stats_aggregation", "[server_multiclient]") {
  int num_clients = 3;
  video_frame_buffer_t *buffers[num_clients];

  for (int i = 0; i < num_clients; i++) {
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "client_%d", i);
    buffers[i] = video_frame_buffer_create(client_id);

    // Write frames for each client
    for (int j = 0; j < i + 2; j++) {
      video_frame_t *frame = video_frame_begin_write(buffers[i]);
      frame->sequence_number = j;
      video_frame_commit(buffers[i]);
    }
  }

  // Verify stats for each client
  for (int i = 0; i < num_clients; i++) {
    video_frame_stats_t stats;
    video_frame_get_stats(buffers[i], &stats);
    ASCIICHAT_REQUIRE_EQ(stats.total_frames, i + 2, "Client %d should have %d frames", i, i + 2);
  }

  // Cleanup
  for (int i = 0; i < num_clients; i++) {
    video_frame_buffer_destroy(buffers[i]);
  }
}
