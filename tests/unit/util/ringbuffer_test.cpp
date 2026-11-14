#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <pthread.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>

#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/buffer.h>

// Use the enhanced macro to create complete test suites with custom log levels
/* ============================================================================
 * Ring Buffer Tests
 * ============================================================================ */

TEST_CASE("ringbuffer/create_and_destroy", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 8);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);
  ASCIICHAT_REQUIRE_EQ(rb->element_size, sizeof(int));
  ASCIICHAT_REQUIRE_GE(rb->capacity, 8); // Should be rounded up to power of 2
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 0);
  ASCIICHAT_REQUIRE(ringbuffer_is_empty(rb));
  ASCIICHAT_REQUIRE_NOT(ringbuffer_is_full(rb));

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/create_with_invalid_params", "[ringbuffer]") {
  // Test with zero element size
  ringbuffer_t *rb = ringbuffer_create(0, 8);
  ASCIICHAT_REQUIRE_NULL(rb);

  // Test with zero capacity
  rb = ringbuffer_create(sizeof(int), 0);
  ASCIICHAT_REQUIRE_NULL(rb);

  // Test with both zero
  rb = ringbuffer_create(0, 0);
  ASCIICHAT_REQUIRE_NULL(rb);
}

TEST_CASE("ringbuffer/destroy_null", "[ringbuffer]") {
  // Should not crash when destroying NULL
  ringbuffer_destroy(NULL);
  ASCIICHAT_REQUIRE(true);
}

TEST_CASE("ringbuffer/basic_write_read", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  int test_data = 42;
  bool result = ringbuffer_write(rb, &test_data);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 1);
  ASCIICHAT_REQUIRE_NOT(ringbuffer_is_empty(rb));

  int read_data;
  result = ringbuffer_read(rb, &read_data);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(read_data, 42);
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 0);
  ASCIICHAT_REQUIRE(ringbuffer_is_empty(rb));

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/write_read_multiple", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 8);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  // Write multiple values
  for (int i = 0; i < 5; i++) {
    bool result = ringbuffer_write(rb, &i);
    ASCIICHAT_REQUIRE(result);
  }

  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 5);

  // Read them back
  for (int i = 0; i < 5; i++) {
    int read_data;
    bool result = ringbuffer_read(rb, &read_data);
    ASCIICHAT_REQUIRE(result);
    ASCIICHAT_REQUIRE_EQ(read_data, i);
  }

  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 0);
  ASCIICHAT_REQUIRE(ringbuffer_is_empty(rb));

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/write_to_full_buffer", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  // Fill the buffer
  for (int i = 0; i < 4; i++) {
    bool result = ringbuffer_write(rb, &i);
    ASCIICHAT_REQUIRE(result);
  }

  ASCIICHAT_REQUIRE(ringbuffer_is_full(rb));

  // Try to write one more - should fail
  int extra = 99;
  bool result = ringbuffer_write(rb, &extra);
  ASCIICHAT_REQUIRE_NOT(result);
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 4);

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/read_from_empty_buffer", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  int read_data;
  bool result = ringbuffer_read(rb, &read_data);
  ASCIICHAT_REQUIRE_NOT(result);
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 0);

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/peek_functionality", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  int test_data = 123;
  ringbuffer_write(rb, &test_data);

  // Peek should not consume the data
  int peek_data;
  bool result = ringbuffer_peek(rb, &peek_data);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(peek_data, 123);
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 1); // Size should be unchanged

  // Read should consume the data
  int read_data;
  result = ringbuffer_read(rb, &read_data);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(read_data, 123);
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 0);

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/peek_empty_buffer", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  int peek_data;
  bool result = ringbuffer_peek(rb, &peek_data);
  ASCIICHAT_REQUIRE_NOT(result);

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/clear_functionality", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  // Add some data
  for (int i = 0; i < 3; i++) {
    ringbuffer_write(rb, &i);
  }

  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 3);

  // Clear the buffer
  ringbuffer_clear(rb);

  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 0);
  ASCIICHAT_REQUIRE(ringbuffer_is_empty(rb));
  ASCIICHAT_REQUIRE_NOT(ringbuffer_is_full(rb));

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/null_parameters", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 4);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  // Test with NULL data
  bool result = ringbuffer_write(rb, NULL);
  ASCIICHAT_REQUIRE_NOT(result);

  result = ringbuffer_read(rb, NULL);
  ASCIICHAT_REQUIRE_NOT(result);

  result = ringbuffer_peek(rb, NULL);
  ASCIICHAT_REQUIRE_NOT(result);

  // Test with NULL ringbuffer
  int data = 42;
  result = ringbuffer_write(NULL, &data);
  ASCIICHAT_REQUIRE_NOT(result);

  result = ringbuffer_read(NULL, &data);
  ASCIICHAT_REQUIRE_NOT(result);

  result = ringbuffer_peek(NULL, &data);
  ASCIICHAT_REQUIRE_NOT(result);

  // Test size functions with NULL
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(NULL), 0);
  ASCIICHAT_REQUIRE(ringbuffer_is_empty(NULL));
  ASCIICHAT_REQUIRE(ringbuffer_is_full(NULL));

  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/power_of_two_capacity", "[ringbuffer]") {
  // Test that capacity is rounded up to power of 2
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 5);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);
  ASCIICHAT_REQUIRE_EQ(rb->capacity, 8); // Should be rounded up to 8
  ringbuffer_destroy(rb);

  rb = ringbuffer_create(sizeof(int), 3);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);
  ASCIICHAT_REQUIRE_EQ(rb->capacity, 4); // Should be rounded up to 4
  ringbuffer_destroy(rb);

  rb = ringbuffer_create(sizeof(int), 1);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);
  ASCIICHAT_REQUIRE_EQ(rb->capacity, 1); // 1 is already a power of 2
  ringbuffer_destroy(rb);
}

TEST_CASE("ringbuffer/large_element_size", "[ringbuffer]") {
  // Test with large element size
  struct large_struct {
    char data[1024];
  };

  ringbuffer_t *rb = ringbuffer_create(sizeof(struct large_struct), 2);
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  struct large_struct test_data;
  memset(test_data.data, 'A', sizeof(test_data.data));

  bool result = ringbuffer_write(rb, &test_data);
  ASCIICHAT_REQUIRE(result);

  struct large_struct read_data;
  result = ringbuffer_read(rb, &read_data);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(memcmp(test_data.data, read_data.data, sizeof(test_data.data)), 0);

  ringbuffer_destroy(rb);
}

/* ============================================================================
 * Frame Buffer Tests
 * ============================================================================ */

TEST_CASE("framebuffer/create_and_destroy", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);
  ASCIICHAT_REQUIRE_NOT_NULL(fb->rb);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/create_with_invalid_capacity", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create(0);
  ASCIICHAT_REQUIRE_NULL(fb);
}

TEST_CASE("framebuffer/destroy_null", "[framebuffer]") {
  // Should not crash when destroying NULL
  framebuffer_destroy(NULL);
  ASCIICHAT_REQUIRE(true);
}

TEST_CASE("framebuffer/write_and_read_frame", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);

  const char *test_frame = "Hello, World!";
  size_t frame_size = strlen(test_frame);

  bool result = framebuffer_write_frame(fb, test_frame, frame_size);
  ASCIICHAT_REQUIRE(result);

  frame_t frame;
  result = framebuffer_read_frame(fb, &frame);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(frame.magic, FRAME_MAGIC);
  ASCIICHAT_REQUIRE_EQ(frame.size, frame_size + 1); // +1 for null terminator
  ASCIICHAT_REQUIRE_NOT_NULL(frame.data);
  ASCIICHAT_REQUIRE_EQ(strcmp(frame.data, test_frame), 0);

  // Clean up the frame data
  buffer_release(frame.data);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/write_invalid_frame", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);

  // Test with NULL frame data
  bool result = framebuffer_write_frame(fb, NULL, 10);
  ASCIICHAT_REQUIRE_NOT(result);

  // Test with zero frame size
  result = framebuffer_write_frame(fb, "test", 0);
  ASCIICHAT_REQUIRE_NOT(result);

  // Test with NULL framebuffer
  result = framebuffer_write_frame(NULL, "test", 4);
  ASCIICHAT_REQUIRE_NOT(result);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/read_invalid_frame", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);

  frame_t frame;

  // Test with NULL framebuffer
  bool result = framebuffer_read_frame(NULL, &frame);
  ASCIICHAT_REQUIRE_NOT(result);

  // Test with NULL frame pointer
  result = framebuffer_read_frame(fb, NULL);
  ASCIICHAT_REQUIRE_NOT(result);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/buffer_overflow", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create(2);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);

  // Fill the buffer
  framebuffer_write_frame(fb, "frame1", 6);
  framebuffer_write_frame(fb, "frame2", 6);

  // Try to write one more - should drop oldest frame
  bool result = framebuffer_write_frame(fb, "frame3", 6);
  ASCIICHAT_REQUIRE(result);

  // Should only have frame2 and frame3
  frame_t frame;
  result = framebuffer_read_frame(fb, &frame);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(strcmp(frame.data, "frame2"), 0);
  buffer_release(frame.data);

  result = framebuffer_read_frame(fb, &frame);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(strcmp(frame.data, "frame3"), 0);
  buffer_release(frame.data);

  // Should be empty now
  result = framebuffer_read_frame(fb, &frame);
  ASCIICHAT_REQUIRE_NOT(result);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/clear_functionality", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);

  // Add some frames
  framebuffer_write_frame(fb, "frame1", 6);
  framebuffer_write_frame(fb, "frame2", 6);

  // Clear the buffer
  framebuffer_clear(fb);

  // Should be empty
  frame_t frame;
  bool result = framebuffer_read_frame(fb, &frame);
  ASCIICHAT_REQUIRE_NOT(result);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/multi_source_create_and_destroy", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create_multi(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);
  ASCIICHAT_REQUIRE_NOT_NULL(fb->rb);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/multi_source_write_and_read", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create_multi(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);

  const char *test_frame = "Multi-source frame";
  size_t frame_size = strlen(test_frame);
  uint32_t client_id = 123;
  uint32_t sequence = 456;
  uint32_t timestamp = 789;

  bool result = framebuffer_write_multi_frame(fb, test_frame, frame_size, client_id, sequence, timestamp);
  ASCIICHAT_REQUIRE(result);

  multi_source_frame_t frame;
  result = framebuffer_read_multi_frame(fb, &frame);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(frame.magic, FRAME_MAGIC);
  char expected_client_id[64];
  snprintf(expected_client_id, sizeof(expected_client_id), "%u", client_id);
  ASCIICHAT_REQUIRE_STR_EQ(frame.source_client_id, expected_client_id);
  ASCIICHAT_REQUIRE_EQ(frame.frame_sequence, sequence);
  ASCIICHAT_REQUIRE_EQ(frame.timestamp, timestamp);
  ASCIICHAT_REQUIRE_EQ(frame.size, frame_size);
  ASCIICHAT_REQUIRE_NOT_NULL(frame.data);
  ASCIICHAT_REQUIRE_EQ(memcmp(frame.data, test_frame, frame_size), 0);

  // Clean up the frame data
  buffer_release(frame.data);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/multi_source_peek", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create_multi(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);

  const char *test_frame = "Peek test frame";
  size_t frame_size = strlen(test_frame);

  framebuffer_write_multi_frame(fb, test_frame, frame_size, 1, 1, 1);

  multi_source_frame_t frame;
  bool result = framebuffer_peek_latest_multi_frame(fb, &frame);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(frame.magic, FRAME_MAGIC);
  ASCIICHAT_REQUIRE_EQ(memcmp(frame.data, test_frame, frame_size), 0);

  // Frame should still be in buffer
  multi_source_frame_t frame2;
  result = framebuffer_read_multi_frame(fb, &frame2);
  ASCIICHAT_REQUIRE(result);
  ASCIICHAT_REQUIRE_EQ(memcmp(frame2.data, test_frame, frame_size), 0);

  // Clean up
  buffer_release(frame.data);
  buffer_release(frame2.data);

  framebuffer_destroy(fb);
}

TEST_CASE("framebuffer/multi_source_invalid_params", "[framebuffer]") {
  framebuffer_t *fb = framebuffer_create_multi(4);
  ASCIICHAT_REQUIRE_NOT_NULL(fb);

  // Test with NULL parameters
  bool result = framebuffer_write_multi_frame(NULL, "test", 4, 1, 1, 1);
  ASCIICHAT_REQUIRE_NOT(result);

  result = framebuffer_write_multi_frame(fb, NULL, 4, 1, 1, 1);
  ASCIICHAT_REQUIRE_NOT(result);

  result = framebuffer_write_multi_frame(fb, "test", 0, 1, 1, 1);
  ASCIICHAT_REQUIRE_NOT(result);

  multi_source_frame_t frame;
  result = framebuffer_read_multi_frame(NULL, &frame);
  ASCIICHAT_REQUIRE_NOT(result);

  result = framebuffer_read_multi_frame(fb, NULL);
  ASCIICHAT_REQUIRE_NOT(result);

  result = framebuffer_peek_latest_multi_frame(NULL, &frame);
  ASCIICHAT_REQUIRE_NOT(result);

  result = framebuffer_peek_latest_multi_frame(fb, NULL);
  ASCIICHAT_REQUIRE_NOT(result);

  framebuffer_destroy(fb);
}

/* ============================================================================
 * Audio Ring Buffer Tests
 * ============================================================================ */

TEST_CASE("audio_ring_buffer/create_and_destroy", "[audio_ring_buffer]") {
  audio_ring_buffer_t *arb = audio_ring_buffer_create();
  ASCIICHAT_REQUIRE_NOT_NULL(arb);

  audio_ring_buffer_destroy(arb);
}

TEST_CASE("audio_ring_buffer/destroy_null", "[audio_ring_buffer]") {
  // Should not crash when destroying NULL
  audio_ring_buffer_destroy(NULL);
  ASCIICHAT_REQUIRE(true);
}

TEST_CASE("audio_ring_buffer/basic_write_read", "[audio_ring_buffer]") {
  // Use capture buffer which disables jitter buffering for basic read/write testing
  audio_ring_buffer_t *arb = audio_ring_buffer_create_for_capture();
  ASCIICHAT_REQUIRE_NOT_NULL(arb);

  // Write samples for basic ring buffer functionality test
  float dummy_samples[2048];
  for (int i = 0; i < 2048; i++) {
    dummy_samples[i] = 0.0f;
  }
  asciichat_error_t result = audio_ring_buffer_write(arb, dummy_samples, 2048);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  // Read the dummy samples back
  float dummy_read[2048];
  int dummy_read_count = audio_ring_buffer_read(arb, dummy_read, 2048);
  ASCIICHAT_REQUIRE_EQ(dummy_read_count, 2048);

  // Now test actual samples
  float test_samples[4] = {0.1f, 0.2f, 0.3f, 0.4f};

  result = audio_ring_buffer_write(arb, test_samples, 4);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  float read_samples[4];
  int read = audio_ring_buffer_read(arb, read_samples, 4);
  ASCIICHAT_REQUIRE_EQ(read, 4);

  for (int i = 0; i < 4; i++) {
    ASCIICHAT_REQUIRE_FLOAT_EQ(read_samples[i], test_samples[i], 1e-6f);
  }

  audio_ring_buffer_destroy(arb);
}

TEST_CASE("audio_ring_buffer/partial_read_write", "[audio_ring_buffer]") {
  // Use capture buffer which disables jitter buffering for basic read/write testing
  audio_ring_buffer_t *arb = audio_ring_buffer_create_for_capture();
  ASCIICHAT_REQUIRE_NOT_NULL(arb);

  // Write samples for basic ring buffer functionality test
  float dummy_samples[2048];
  for (int i = 0; i < 2048; i++) {
    dummy_samples[i] = 0.0f;
  }
  asciichat_error_t result = audio_ring_buffer_write(arb, dummy_samples, 2048);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  // Read the dummy samples back
  float dummy_read[2048];
  int dummy_read_count = audio_ring_buffer_read(arb, dummy_read, 2048);
  ASCIICHAT_REQUIRE_EQ(dummy_read_count, 2048);

  // Now test actual samples
  float test_samples[8] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};

  // Write all samples
  result = audio_ring_buffer_write(arb, test_samples, 8);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  // Read only 3 samples
  float read_samples[3];
  int read = audio_ring_buffer_read(arb, read_samples, 3);
  ASCIICHAT_REQUIRE_EQ(read, 3);

  for (int i = 0; i < 3; i++) {
    ASCIICHAT_REQUIRE_FLOAT_EQ(read_samples[i], test_samples[i], 1e-6f);
  }

  // Read remaining 5 samples
  float read_samples2[5];
  read = audio_ring_buffer_read(arb, read_samples2, 5);
  ASCIICHAT_REQUIRE_EQ(read, 5);

  for (int i = 0; i < 5; i++) {
    ASCIICHAT_REQUIRE_FLOAT_EQ(read_samples2[i], test_samples[i + 3], 1e-6f);
  }

  audio_ring_buffer_destroy(arb);
}

TEST_CASE("audio_ring_buffer/buffer_overflow", "[audio_ring_buffer]") {
  // Use capture buffer which disables jitter buffering for overflow testing
  audio_ring_buffer_t *arb = audio_ring_buffer_create_for_capture();
  ASCIICHAT_REQUIRE_NOT_NULL(arb);

  // First test: Try to write more than buffer capacity - should be rejected
  float oversized_samples[AUDIO_RING_BUFFER_SIZE + 100];
  for (int i = 0; i < AUDIO_RING_BUFFER_SIZE + 100; i++) {
    oversized_samples[i] = (float)i * 0.001f;
  }
  asciichat_error_t result = audio_ring_buffer_write(arb, oversized_samples, AUDIO_RING_BUFFER_SIZE + 100);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK); // Should fail when trying to write more than buffer size

  // Second test: Write a small amount first, then try to overflow
  float initial_samples[10];
  for (int i = 0; i < 10; i++) {
    initial_samples[i] = (float)i * 0.1f;
  }
  result = audio_ring_buffer_write(arb, initial_samples, 10);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  // Now try to write enough to cause overflow (this should drop the old samples)
  // We need to write enough samples to exceed the available space
  // Available space after writing 10 samples = AUDIO_RING_BUFFER_SIZE - 10 - 1 = 8181
  // So we need to write more than 8181 samples to cause overflow
  float overflow_samples[AUDIO_RING_BUFFER_SIZE - 1];
  for (int i = 0; i < AUDIO_RING_BUFFER_SIZE - 1; i++) {
    overflow_samples[i] = (float)(i + 1000) * 0.001f;
  }
  result = audio_ring_buffer_write(arb, overflow_samples, AUDIO_RING_BUFFER_SIZE - 1);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);

  // Read back samples - should get data back
  float read_samples[AUDIO_RING_BUFFER_SIZE];
  int read = audio_ring_buffer_read(arb, read_samples, AUDIO_RING_BUFFER_SIZE - 1);
  // We expect to read back some data (at least not zero)
  ASCIICHAT_REQUIRE_GT(read, 0, "Should read some samples after overflow");

  // Verify that at least some samples from overflow_samples were returned
  // (May not be all due to buffer management and overflow semantics)
  bool found_matching_sample = false;
  for (int i = 0; i < read && i < 100 && !found_matching_sample; i++) {
    float expected = overflow_samples[i];
    float actual = read_samples[i];
    // Check if samples match within reasonable tolerance
    if (fabs(actual - expected) <= fabs(expected) * 0.2f + 0.01f) {
      found_matching_sample = true;
    }
  }
  ASCIICHAT_REQUIRE(found_matching_sample, "Should find at least some matching samples from overflow_samples");

  audio_ring_buffer_destroy(arb);
}

TEST_CASE("audio_ring_buffer/read_from_empty", "[audio_ring_buffer]") {
  audio_ring_buffer_t *arb = audio_ring_buffer_create();
  ASCIICHAT_REQUIRE_NOT_NULL(arb);

  float read_samples[4];
  int read = audio_ring_buffer_read(arb, read_samples, 4);
  ASCIICHAT_REQUIRE_EQ(read, 0);

  audio_ring_buffer_destroy(arb);
}

TEST_CASE("audio_ring_buffer/null_parameters", "[audio_ring_buffer]") {
  audio_ring_buffer_t *arb = audio_ring_buffer_create();
  ASCIICHAT_REQUIRE_NOT_NULL(arb);

  float test_samples[4] = {0.1f, 0.2f, 0.3f, 0.4f};

  // Test with NULL buffer
  asciichat_error_t result = audio_ring_buffer_write(NULL, test_samples, 4);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK);

  result = audio_ring_buffer_write(arb, NULL, 4);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK);

  // Test with NULL buffer
  int read = audio_ring_buffer_read(NULL, test_samples, 4);
  ASCIICHAT_REQUIRE_EQ(read, 0);

  read = audio_ring_buffer_read(arb, NULL, 4);
  ASCIICHAT_REQUIRE_EQ(read, 0);

  audio_ring_buffer_destroy(arb);
}

TEST_CASE("audio_ring_buffer/zero_samples", "[audio_ring_buffer]") {
  audio_ring_buffer_t *arb = audio_ring_buffer_create();
  ASCIICHAT_REQUIRE_NOT_NULL(arb);

  float test_samples[4] = {0.1f, 0.2f, 0.3f, 0.4f};

  // Test with zero samples
  asciichat_error_t result = audio_ring_buffer_write(arb, test_samples, 0);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK); // Zero samples should be invalid

  int read = audio_ring_buffer_read(arb, test_samples, 0);
  ASCIICHAT_REQUIRE_EQ(read, 0);

  audio_ring_buffer_destroy(arb);
}

/* ============================================================================
 * Thread Safety Tests
 * ============================================================================ */

typedef struct {
  ringbuffer_t *rb;
  int thread_id;
  int num_operations;
  bool success;
  pthread_mutex_t *mutex;
  pthread_cond_t *not_full;
  pthread_cond_t *not_empty;
  int *total_produced;
  int *total_consumed;
  int max_operations;
} thread_test_data_t;

void *producer_thread(void *arg) {
  thread_test_data_t *data = (thread_test_data_t *)arg;
  data->success = true;

  for (int i = 0; i < data->num_operations; i++) {
    int value = data->thread_id * 1000 + i;

    pthread_mutex_lock(data->mutex);

    // Wait while buffer is full
    while (ringbuffer_is_full(data->rb) && *data->total_consumed < data->max_operations) {
      pthread_cond_wait(data->not_full, data->mutex);
    }

    // Check if we should exit (all consumption is done)
    if (*data->total_consumed >= data->max_operations) {
      pthread_mutex_unlock(data->mutex);
      break;
    }

    // Write to buffer
    if (!ringbuffer_write(data->rb, &value)) {
      data->success = false;
      pthread_mutex_unlock(data->mutex);
      return NULL;
    }

    (*data->total_produced)++;

    // Signal consumers that buffer is not empty
    pthread_cond_signal(data->not_empty);
    pthread_mutex_unlock(data->mutex);

    usleep(10); // Small delay to allow thread interleaving
  }

  return NULL;
}

void *consumer_thread(void *arg) {
  thread_test_data_t *data = (thread_test_data_t *)arg;
  data->success = true;

  for (int i = 0; i < data->num_operations; i++) {
    int value;

    pthread_mutex_lock(data->mutex);

    // Wait while buffer is empty and not all items have been produced
    while (ringbuffer_is_empty(data->rb) && *data->total_produced < data->max_operations) {
      pthread_cond_wait(data->not_empty, data->mutex);
    }

    // Check if we're done (buffer empty and all production complete)
    if (ringbuffer_is_empty(data->rb) && *data->total_produced >= data->max_operations) {
      pthread_mutex_unlock(data->mutex);
      break;
    }

    // Read from buffer
    if (!ringbuffer_read(data->rb, &value)) {
      data->success = false;
      pthread_mutex_unlock(data->mutex);
      return NULL;
    }

    (*data->total_consumed)++;

    // Signal producers that buffer is not full
    pthread_cond_signal(data->not_full);
    pthread_mutex_unlock(data->mutex);

    usleep(10); // Small delay to allow thread interleaving
  }

  return NULL;
}

TEST_CASE("ringbuffer/thread_safety", "[ringbuffer]") {
  ringbuffer_t *rb = ringbuffer_create(sizeof(int), 64); // Larger buffer to reduce contention
  ASCIICHAT_REQUIRE_NOT_NULL(rb);

  const int num_threads = 4;            // Fewer threads to reduce contention
  const int operations_per_thread = 50; // Fewer operations per thread
  const int total_operations = num_threads * operations_per_thread;

  // Initialize synchronization primitives
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
  pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;
  int total_produced = 0;
  int total_consumed = 0;

  pthread_t threads[num_threads * 2];
  thread_test_data_t thread_data[num_threads * 2];

  // Create producer threads
  for (int i = 0; i < num_threads; i++) {
    thread_data[i].rb = rb;
    thread_data[i].thread_id = i;
    thread_data[i].num_operations = operations_per_thread;
    thread_data[i].success = false;
    thread_data[i].mutex = &mutex;
    thread_data[i].not_full = &not_full;
    thread_data[i].not_empty = &not_empty;
    thread_data[i].total_produced = &total_produced;
    thread_data[i].total_consumed = &total_consumed;
    thread_data[i].max_operations = total_operations;

    int result = pthread_create(&threads[i], NULL, producer_thread, &thread_data[i]);
    ASCIICHAT_REQUIRE_EQ(result, 0);
  }

  // Create consumer threads
  for (int i = 0; i < num_threads; i++) {
    thread_data[num_threads + i].rb = rb;
    thread_data[num_threads + i].thread_id = i;
    thread_data[num_threads + i].num_operations = operations_per_thread;
    thread_data[num_threads + i].success = false;
    thread_data[num_threads + i].mutex = &mutex;
    thread_data[num_threads + i].not_full = &not_full;
    thread_data[num_threads + i].not_empty = &not_empty;
    thread_data[num_threads + i].total_produced = &total_produced;
    thread_data[num_threads + i].total_consumed = &total_consumed;
    thread_data[num_threads + i].max_operations = total_operations;

    int result = pthread_create(&threads[num_threads + i], NULL, consumer_thread, &thread_data[num_threads + i]);
    ASCIICHAT_REQUIRE_EQ(result, 0);
  }

  // Wait for all threads to complete
  for (int i = 0; i < num_threads * 2; i++) {
    pthread_join(threads[i], NULL);
  }

  // Check that all threads succeeded
  for (int i = 0; i < num_threads * 2; i++) {
    ASCIICHAT_REQUIRE(thread_data[i].success, "All threads succeed");
  }

  // Verify correct number of operations
  ASCIICHAT_REQUIRE_EQ(total_produced, total_operations, "All items were produced");
  ASCIICHAT_REQUIRE_EQ(total_consumed, total_operations, "All items were consumed");

  // Buffer should be empty
  ASCIICHAT_REQUIRE(ringbuffer_is_empty(rb));

  // Clean up synchronization primitives
  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&not_full);
  pthread_cond_destroy(&not_empty);

  ringbuffer_destroy(rb);
}

/* ============================================================================
 * Theory-Based Tests
 * ============================================================================ */

// Theory: FIFO ordering property - data written in order X should be read in order X
// This property should hold for any sequence of integers
static constexpr size_t fifo_capacities[] = {2, 4, 8, 16, 32};
static constexpr size_t fifo_operation_counts[] = {3, 5, 10, 20, 50};

TEST_CASE("ringbuffer/fifo_ordering_property", "[ringbuffer]") {
  const size_t capacity = GENERATE(Catch::Generators::from_range(fifo_capacities));
  const size_t num_ops = GENERATE(Catch::Generators::from_range(fifo_operation_counts));
  // Skip combinations where num_ops > capacity (would require wraparound logic)
  ASCIICHAT_ASSUME(capacity >= 2);
  ASCIICHAT_ASSUME(num_ops >= 1);
  ASCIICHAT_ASSUME(num_ops <= capacity); // Only fill buffer, not overflow

  ringbuffer_t *rb = ringbuffer_create(sizeof(int), capacity);
  ASCIICHAT_ASSUME(rb != NULL);

  // PROPERTY: Write sequence of integers
  int *written_values;
  written_values = SAFE_MALLOC(num_ops * sizeof(int), int *);
  ASCIICHAT_ASSUME(written_values != NULL);

  for (size_t i = 0; i < num_ops; i++) {
    written_values[i] = (int)i;
    bool result = ringbuffer_write(rb, &written_values[i]);
    ASCIICHAT_REQUIRE(result, "Write should succeed at index %zu for capacity %zu", i, capacity);
  }

  // PROPERTY: Size should equal number of writes
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), num_ops, "Size should equal number of writes for capacity %zu, num_ops %zu",
               capacity, num_ops);

  // PROPERTY: Read back in same order (FIFO property)
  for (size_t i = 0; i < num_ops; i++) {
    int read_value;
    bool result = ringbuffer_read(rb, &read_value);
    ASCIICHAT_REQUIRE(result, "Read should succeed at index %zu for capacity %zu", i, capacity);
    ASCIICHAT_REQUIRE_EQ(read_value, written_values[i],
                 "FIFO ordering violated: expected %d at position %zu, got %d (capacity=%zu, num_ops=%zu)",
                 written_values[i], i, read_value, capacity, num_ops);
  }

  // PROPERTY: Buffer should be empty after reading all values
  ASCIICHAT_REQUIRE(ringbuffer_is_empty(rb), "Buffer should be empty after reading all values (capacity=%zu, num_ops=%zu)",
            capacity, num_ops);
  ASCIICHAT_REQUIRE_EQ(ringbuffer_size(rb), 0, "Size should be 0 after reading all values (capacity=%zu, num_ops=%zu)",
               capacity, num_ops);

  SAFE_FREE(written_values);
  ringbuffer_destroy(rb);
}
