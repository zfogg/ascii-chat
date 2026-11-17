#include <iterator>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <string.h>
#include <math.h>
#include <time.h>

#include <ascii-chat/audio/audio.h>
#include <ascii-chat/buffer.h> // For buffer_system_init
#include <ascii-chat/tests/logging.h>

// Use the enhanced macro to create complete test suite with basic quiet logging
// =============================================================================
// Audio Initialization Tests
// =============================================================================

TEST_CASE("audio/initialization_and_cleanup", "[audio]") {
  audio_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  // Test basic audio system initialization
  int result = audio_init(&ctx);
  // May fail if no audio device available - that's OK
  if (result == 0) {
    REQUIRE(ctx.initialized);
  }

  // Even if initialization fails (no audio device), cleanup should work
  audio_destroy(&ctx);

  // Multiple cleanup calls should be safe
  audio_destroy(&ctx);
}

TEST_CASE("audio/multiple_init_cleanup_cycles", "[audio]") {
  // Test multiple init/cleanup cycles
  for (int i = 0; i < 3; i++) {
    audio_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    audio_init(&ctx);
    // Should succeed or fail consistently
    audio_destroy(&ctx);
  }
}

TEST_CASE("audio/discard_pending_audio_on_consumer_read", "[audio]") {
  buffer_system_init();
  audio_ring_buffer_t *rb = audio_ring_buffer_create_for_capture();
  REQUIRE((rb) != nullptr);

  float queued[4] = {0.25f, -0.25f, 0.5f, -0.5f};
  float readback[4] = {0};
  REQUIRE((audio_ring_buffer_write(rb, queued, 4)) == (ASCIICHAT_OK));

  audio_ring_buffer_discard_pending(rb);
  REQUIRE((audio_ring_buffer_read(rb, readback, 4)) == (0));

  REQUIRE((audio_ring_buffer_write(rb, queued, 4)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_read(rb, readback, 4)) == (4));
  for (size_t i = 0; i < 4; i++) {
    REQUIRE((readback[i]) == Catch::Approx(queued[i]));
  }
  audio_ring_buffer_destroy(rb);
}

TEST_CASE("audio/flush_playback_buffers_discards_queued_audio", "[audio]") {
  buffer_system_init();
  audio_context_t ctx = {0};
  ctx.initialized = true;
  ctx.playback_buffer = audio_ring_buffer_create();
  ctx.processed_playback_rb = audio_ring_buffer_create();
  ctx.render_buffer = audio_ring_buffer_create();
  ctx.raw_render_rb = audio_ring_buffer_create();
  REQUIRE((ctx.playback_buffer) != nullptr);
  REQUIRE((ctx.processed_playback_rb) != nullptr);
  REQUIRE((ctx.render_buffer) != nullptr);
  REQUIRE((ctx.raw_render_rb) != nullptr);

  const float queued[] = {0.25f, -0.25f, 0.5f, -0.5f};
  float readback[sizeof(queued) / sizeof(queued[0])] = {0};
  REQUIRE((audio_ring_buffer_write(ctx.playback_buffer, queued, 4)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_write(ctx.processed_playback_rb, queued, 4)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_write(ctx.render_buffer, queued, 4)) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_write(ctx.raw_render_rb, queued, 4)) == (ASCIICHAT_OK));

  audio_flush_playback_buffers(&ctx);

  REQUIRE((audio_ring_buffer_read(ctx.playback_buffer, readback, 4)) == (0));
  REQUIRE((audio_ring_buffer_read(ctx.processed_playback_rb, readback, 4)) == (0));
  REQUIRE((audio_ring_buffer_read(ctx.render_buffer, readback, 4)) == (0));
  REQUIRE((audio_ring_buffer_read(ctx.raw_render_rb, readback, 4)) == (0));
  audio_ring_buffer_destroy(ctx.playback_buffer);
  audio_ring_buffer_destroy(ctx.processed_playback_rb);
  audio_ring_buffer_destroy(ctx.render_buffer);
  audio_ring_buffer_destroy(ctx.raw_render_rb);
}

// =============================================================================
// Parameterized Tests for Audio Ring Buffer Operations
// =============================================================================

// Test case structure for ring buffer write/read operations
typedef struct {
  size_t write_size;
  size_t read_size;
  const char *description;
  bool should_succeed;
} ringbuffer_operation_test_case_t;

static ringbuffer_operation_test_case_t ringbuffer_operation_cases[] = {{100, 50, "Normal write/read", true},
                                                                        {256, 256, "Equal write/read", true},
                                                                        {50, 100, "Read more than written", false},
                                                                        {0, 50, "Read from empty", false},
                                                                        {1000, 500, "Large buffer operations", true},
                                                                        {1, 1, "Single sample", true},
                                                                        {512, 0, "Write only", true},
                                                                        {0, 0, "Zero operations", true}};

TEST_CASE("audio/ringbuffer_operations", "[audio]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(ringbuffer_operation_cases), std::end(ringbuffer_operation_cases)));
  auto *tc = &generated_case;
  buffer_system_init(); // Initialize buffer domain for ring buffer allocation

  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  // Prepare test data
  float *write_data = NULL;
  float *read_data = NULL;

  if (tc->write_size > 0) {
    write_data = SAFE_MALLOC(tc->write_size * sizeof(float), float *);
    for (size_t i = 0; i < tc->write_size; i++) {
      write_data[i] = (float)i * 0.1f;
    }
  }

  if (tc->read_size > 0) {
    read_data = SAFE_MALLOC(tc->read_size * sizeof(float), float *);
  }

  // Test write operation
  int written = 0;
  if (tc->write_size > 0) {
    written = audio_ring_buffer_write(rb, write_data, tc->write_size);
    REQUIRE((written) >= (0));
    REQUIRE((written) <= ((int)tc->write_size));
  }

  // Test read operation
  int read = 0;
  if (tc->read_size > 0) {
    read = audio_ring_buffer_read(rb, read_data, tc->read_size);
    REQUIRE((read) >= (0));
    REQUIRE((read) <= (written));

    if (tc->should_succeed && read > 0) {
      // Verify data integrity for what was actually read
      for (int i = 0; i < read; i++) {
        REQUIRE((read_data[i]) == Catch::Approx(write_data[i]));
      }
    }
  }

  // Clean up
  if (write_data)
    SAFE_FREE(write_data);
  if (read_data)
    SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}

// Test case structure for ring buffer capacity scenarios
typedef struct {
  size_t buffer_size;
  size_t write_cycles;
  size_t read_cycles;
  const char *description;
} ringbuffer_capacity_test_case_t;

static ringbuffer_capacity_test_case_t ringbuffer_capacity_cases[] = {
    {100, 1, 1, "Single write/read cycle"},   {100, 5, 3, "Multiple write cycles, fewer reads"}, {100, 3, 5, "Multiple read cycles, fewer writes"},
    {100, 10, 10, "Equal write/read cycles"}, {256, 2, 2, "Larger buffer, two cycles"},          {50, 20, 20, "Small buffer, many cycles"}};

TEST_CASE("audio/ringbuffer_capacity_scenarios", "[audio]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(ringbuffer_capacity_cases), std::end(ringbuffer_capacity_cases)));
  auto *tc = &generated_case;
  buffer_system_init(); // Initialize buffer domain for ring buffer allocation

  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  float *write_data = NULL;
  float *read_data = NULL;

  write_data = SAFE_MALLOC(tc->buffer_size * sizeof(float), float *);
  read_data = SAFE_MALLOC(tc->buffer_size * sizeof(float), float *);

  // Fill test data
  for (size_t i = 0; i < tc->buffer_size; i++) {
    write_data[i] = (float)i * 0.01f;
  }

  int total_written = 0;
  int total_read = 0;

  // Perform write cycles
  for (size_t cycle = 0; cycle < tc->write_cycles; cycle++) {
    int written = audio_ring_buffer_write(rb, write_data, tc->buffer_size);
    REQUIRE((written) >= (0));
    total_written += written;
  }

  // Perform read cycles
  for (size_t cycle = 0; cycle < tc->read_cycles; cycle++) {
    int read = audio_ring_buffer_read(rb, read_data, tc->buffer_size);
    REQUIRE((read) >= (0));
    total_read += read;
  }

  // Verify we didn't read more than we wrote
  REQUIRE((total_read) <= (total_written));

  // Clean up
  SAFE_FREE(write_data);
  SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}

// =============================================================================
// Ringbuffer Tests
// =============================================================================

TEST_CASE("audio/ringbuffer_roundtrip_property", "[audio]") {
  int sample_count = GENERATE(10, 50, 100, 256, 512, 1024);

  // Use capture buffer which disables jitter buffering for basic roundtrip testing
  audio_ring_buffer_t *rb = audio_ring_buffer_create_for_capture();
  if (!(rb != NULL))
    SKIP("assumption not satisfied");

  float *test_data = SAFE_MALLOC(sample_count * sizeof(float), float *);
  float *read_data = SAFE_MALLOC(sample_count * sizeof(float), float *);
  if (!(test_data != NULL && read_data != NULL))
    SKIP("assumption not satisfied");

  // Fill test data with sine wave
  for (int i = 0; i < sample_count; i++) {
    test_data[i] = sinf(2.0f * M_PI * 440.0f * i / 44100.0f);
  }

  // Write data
  asciichat_error_t result = audio_ring_buffer_write(rb, test_data, sample_count);
  REQUIRE((result) == (ASCIICHAT_OK));

  // Read data back
  int read = audio_ring_buffer_read(rb, read_data, sample_count);
  REQUIRE((read) == (sample_count));

  // PROPERTY: Roundtrip must preserve data
  for (int i = 0; i < read; i++) {
    REQUIRE((read_data[i]) == Catch::Approx(test_data[i]));
  }

  SAFE_FREE(test_data);
  SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}

TEST_CASE("audio/ringbuffer_basic_operations", "[audio]") {
  buffer_system_init(); // Initialize buffer domain for ring buffer allocation

  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  // Test empty buffer properties
  REQUIRE((audio_ring_buffer_available_read(rb)) == (0));
  REQUIRE((audio_ring_buffer_available_write(rb)) > (0));

  audio_ring_buffer_destroy(rb);
}

TEST_CASE("audio/ringbuffer_write_read", "[audio]") {
  buffer_system_init(); // Initialize buffer domain for ring buffer allocation

  // Use capture buffer which disables jitter buffering for basic read/write testing
  audio_ring_buffer_t *rb = audio_ring_buffer_create_for_capture();
  REQUIRE((rb) != nullptr);

  // Test basic write/read functionality without jitter buffer overhead
  const int num_samples = 3000;
  float *test_data = SAFE_MALLOC(num_samples * sizeof(float), float *);
  float *read_data = SAFE_MALLOC(num_samples * sizeof(float), float *);

  // Fill test data with sine wave
  for (int i = 0; i < num_samples; i++) {
    test_data[i] = sinf(2.0f * M_PI * 440.0f * i / 44100.0f);
  }

  // Write data (this will fill the jitter buffer)
  asciichat_error_t write_result = audio_ring_buffer_write(rb, test_data, num_samples);
  REQUIRE((write_result) == (ASCIICHAT_OK));
  REQUIRE((audio_ring_buffer_available_read(rb)) > (0));

  // Read data back (read returns number of samples read)
  int read = audio_ring_buffer_read(rb, read_data, num_samples);
  REQUIRE((read) == (num_samples));

  // Verify data integrity
  for (int i = 0; i < read; i++) {
    REQUIRE((read_data[i]) == Catch::Approx(test_data[i]));
  }

  SAFE_FREE(test_data);
  SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}

TEST_CASE("audio/ringbuffer_overflow_property", "[audio]") {
  int write_size = GENERATE(500, 1000, 2000, 4000, 8000);

  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  if (!(rb != NULL))
    SKIP("assumption not satisfied");

  float *test_data = SAFE_MALLOC(write_size * sizeof(float), float *);
  if (!(test_data != NULL))
    SKIP("assumption not satisfied");

  for (int i = 0; i < write_size; i++) {
    test_data[i] = i * 0.001f;
  }

  int available_space = audio_ring_buffer_available_write(rb);
  REQUIRE((available_space) > (0));

  // PROPERTY: Write should not exceed available space
  int written = audio_ring_buffer_write(rb, test_data, write_size);
  REQUIRE((written) <= (available_space));
  REQUIRE((written) >= (0));

  SAFE_FREE(test_data);
  audio_ring_buffer_destroy(rb);
}

TEST_CASE("audio/ringbuffer_overflow_behavior", "[audio]") {
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  float test_data[2000]; // More than likely buffer capacity

  for (int i = 0; i < 2000; i++) {
    test_data[i] = i * 0.001f;
  }

  int available_space = audio_ring_buffer_available_write(rb);
  REQUIRE((available_space) > (0));

  // Try to write more than buffer capacity
  int written = audio_ring_buffer_write(rb, test_data, 2000);
  REQUIRE((written) <= (available_space));
  REQUIRE((written) >= (0));

  audio_ring_buffer_destroy(rb);
}

TEST_CASE("audio/ringbuffer_wrap_around", "[audio]") {
  // Use capture buffer which disables jitter buffering for basic wrap-around testing
  audio_ring_buffer_t *rb = audio_ring_buffer_create_for_capture();
  REQUIRE((rb) != nullptr);

  // Test wrap-around behavior without jitter buffer overhead
  const int batch_size = 3000;
  float *data1 = SAFE_MALLOC(batch_size * sizeof(float), float *);
  float *data2 = SAFE_MALLOC(batch_size * sizeof(float), float *);
  float *read_data = SAFE_MALLOC(batch_size * sizeof(float), float *);

  // Fill test data
  for (int i = 0; i < batch_size; i++) {
    data1[i] = (float)i;
    data2[i] = 1000.0f + i;
  }

  // Write first batch (fills jitter buffer)
  asciichat_error_t write_result1 = audio_ring_buffer_write(rb, data1, batch_size);
  REQUIRE((write_result1) == (ASCIICHAT_OK));

  // Now jitter buffer is filled, we can read
  // Read partial data to make space
  int read1 = audio_ring_buffer_read(rb, read_data, batch_size / 2);
  REQUIRE((read1) > (0));

  // Write second batch (may wrap around internally)
  asciichat_error_t write_result2 = audio_ring_buffer_write(rb, data2, batch_size);
  REQUIRE((write_result2) == (ASCIICHAT_OK));

  // Verify we can still read remaining data
  int remaining = audio_ring_buffer_available_read(rb);
  REQUIRE((remaining) > (0));

  SAFE_FREE(data1);
  SAFE_FREE(data2);
  SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}

TEST_CASE("audio/ringbuffer_concurrent_access_simulation", "[audio]") {
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  float write_data[50], read_data[30];

  // Simulate producer/consumer pattern
  for (int cycle = 0; cycle < 10; cycle++) {
    // Producer writes
    for (int i = 0; i < 50; i++) {
      write_data[i] = cycle * 100 + i;
    }
    int written = audio_ring_buffer_write(rb, write_data, 50);
    REQUIRE((written) >= (0));

    // Consumer reads (less than written to build up buffer)
    if (audio_ring_buffer_available_read(rb) >= 30) {
      int read = audio_ring_buffer_read(rb, read_data, 30);
      REQUIRE((read) >= (0));
    }
  }

  audio_ring_buffer_destroy(rb);
}

// =============================================================================
// Audio Context and Streaming Tests
// =============================================================================

TEST_CASE("audio/audio_context_operations", "[audio]") {
  audio_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  // Test initialization
  int result = audio_init(&ctx);
  if (result != 0) {
    // Audio may not be available in test environment - skip remaining tests
    return;
  }

  REQUIRE(ctx.initialized);
  REQUIRE((ctx.capture_buffer) != nullptr);
  REQUIRE((ctx.playback_buffer) != nullptr);

  // Test cleanup
  audio_destroy(&ctx);
  REQUIRE(ctx.initialized == false);
}

TEST_CASE("audio/audio_sample_buffer_operations", "[audio]") {
  audio_context_t ctx;
  memset(&ctx, 0, sizeof(ctx));

  if (audio_init(&ctx) != 0) {
    return; // Skip if no audio available
  }

  float test_samples[256];
  float read_samples[256];

  // Generate test sine wave
  for (int i = 0; i < 256; i++) {
    test_samples[i] = sinf(2.0f * M_PI * 440.0f * i / 44100.0f);
  }

  // Test writing samples to playback buffer
  int written = audio_write_samples(&ctx, test_samples, 256);
  REQUIRE((written) >= (0));

  // Test reading samples (may return 0 if no input available)
  int read = audio_read_samples(&ctx, read_samples, 256);
  REQUIRE((read) >= (0));

  audio_destroy(&ctx);
}

// =============================================================================
// Audio Capture and Playback Tests
// =============================================================================

TEST_CASE("audio/audio_duplex_operations", "[audio]") {
  SKIP("test skipped");
}

// =============================================================================
// Audio Constants and Configuration Tests
// =============================================================================

TEST_CASE("audio/audio_constants", "[audio]") {
  // Test that audio constants are sensible
  REQUIRE((AUDIO_SAMPLE_RATE) == (48000));
  REQUIRE((AUDIO_CHANNELS) == (2));
  REQUIRE((AUDIO_FRAMES_PER_BUFFER) > (0));
  REQUIRE((AUDIO_FRAMES_PER_BUFFER) <= (2048));

  // Verify buffer size calculation
  REQUIRE((AUDIO_BUFFER_SIZE) == (AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS));
}

TEST_CASE("audio/audio_realtime_priority", "[audio]") {
  // Test realtime priority setting (may fail in Docker/containers without privileges)
  asciichat_error_t result = audio_set_realtime_priority();

#if defined(__linux__)
  // In Docker/CI environments without CAP_SYS_NICE, this will fail
  // Skip the test if we can't set realtime priority (expected in containers)
  if (result != ASCIICHAT_OK) {
    SKIP("test skipped");
  }
  REQUIRE((result) == (ASCIICHAT_OK));
#elif defined(__APPLE__)
  REQUIRE((result) == (ASCIICHAT_OK));
#elif defined(_WIN32)
  log_warn("THIS DOESN'T ACTUALLY WORK");
  REQUIRE((result) == (ASCIICHAT_OK));
#else
  REQUIRE((result) != (ASCIICHAT_OK));
#endif
}

// =============================================================================
// Audio Ring Buffer Advanced Tests
// =============================================================================

TEST_CASE("audio/audio_ring_buffer_stress_test", "[audio]") {
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  float test_data[64];
  float read_data[64];

  // Generate test pattern
  for (int i = 0; i < 64; i++) {
    test_data[i] = sinf(2.0f * M_PI * 440.0f * i / 44100.0f);
  }

  // Perform many write/read cycles to stress test
  for (int cycle = 0; cycle < 100; cycle++) {
    int written = audio_ring_buffer_write(rb, test_data, 64);
    REQUIRE((written) >= (0));

    if (written > 0) {
      int read = audio_ring_buffer_read(rb, read_data, written);
      REQUIRE((read) == (written));
    }
  }

  audio_ring_buffer_destroy(rb);
}

TEST_CASE("audio/audio_ring_buffer_partial_operations", "[audio]") {
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  float large_data[1024];

  // Fill with test pattern
  for (int i = 0; i < 1024; i++) {
    large_data[i] = i * 0.001f;
  }

  // Try to write large amount - may only write partial
  int written = audio_ring_buffer_write(rb, large_data, 1024);
  REQUIRE((written) >= (0));

  // If something was written, we should be able to read it back
  if (written > 0) {
    float *read_data;
    read_data = SAFE_MALLOC(written * sizeof(float), float *);

    int read = audio_ring_buffer_read(rb, read_data, written);
    REQUIRE((read) == (written));

    // Verify data integrity
    for (int i = 0; i < read; i++) {
      REQUIRE((read_data[i]) == Catch::Approx(large_data[i]));
    }

    SAFE_FREE(read_data);
  }

  audio_ring_buffer_destroy(rb);
}

// =============================================================================
// Audio Integration Tests
// =============================================================================

TEST_CASE("audio/audio_context_integration", "[audio]") {
  SKIP("test skipped");
}

TEST_CASE("audio/audio_buffer_size_consistency", "[audio]") {
  // Test that audio constants are consistent
  REQUIRE((AUDIO_BUFFER_SIZE) > (0));
  REQUIRE((AUDIO_BUFFER_SIZE) == (AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS));

  // Test reasonable buffer sizes for real-time audio
  REQUIRE((AUDIO_FRAMES_PER_BUFFER) >= (64));
  REQUIRE((AUDIO_FRAMES_PER_BUFFER) <= (4096));

  // Test that sample rate is reasonable
  REQUIRE((AUDIO_SAMPLE_RATE) >= (8000));
  REQUIRE((AUDIO_SAMPLE_RATE) <= (192000));
}

// =============================================================================
// Error Handling and Edge Cases
// =============================================================================

TEST_CASE("audio/null_pointer_handling", "[audio]") {
  audio_context_t ctx;
  float samples[100];

  // Test audio functions with NULL context
  int result = audio_read_samples(NULL, samples, 100);
  REQUIRE((result) != (100));

  result = audio_write_samples(NULL, samples, 100);
  REQUIRE((result) != (100));

  // Test with NULL buffer
  memset(&ctx, 0, sizeof(ctx));
  if (audio_init(&ctx) == 0) {
    result = audio_read_samples(&ctx, NULL, 100);
    REQUIRE((result) != (100));

    result = audio_write_samples(&ctx, NULL, 100);
    REQUIRE((result) != (100));

    audio_destroy(&ctx);
  }
}

TEST_CASE("audio/zero_sample_count_handling", "[audio]") {
  audio_context_t ctx;
  float samples[100];

  memset(&ctx, 0, sizeof(ctx));
  if (audio_init(&ctx) == 0) {
    // Functions should handle zero sample count gracefully
    asciichat_error_t result = audio_read_samples(&ctx, samples, 0);
    REQUIRE(result != ASCIICHAT_OK);

    result = audio_write_samples(&ctx, samples, 0);
    REQUIRE(result != ASCIICHAT_OK);

    audio_destroy(&ctx);
  }
}

TEST_CASE("audio/ringbuffer_edge_cases", "[audio]") {
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  float samples[100];

  // Test with NULL data pointer
  asciichat_error_t result = audio_ring_buffer_write(rb, NULL, 100);
  REQUIRE(result != ASCIICHAT_OK);

  int read_result = audio_ring_buffer_read(rb, NULL, 100);
  REQUIRE((read_result) == (0));

  // Test with zero sample count
  result = audio_ring_buffer_write(rb, samples, 0);
  REQUIRE(result != ASCIICHAT_OK);

  read_result = audio_ring_buffer_read(rb, samples, 0);
  REQUIRE((read_result) == (0));

  audio_ring_buffer_destroy(rb);
}

// =============================================================================
// Parameterized Tests for Audio Operations
// =============================================================================

// Test case structure for audio buffer size tests
typedef struct {
  size_t buffer_size;
  const char *description;
} audio_buffer_test_case_t;

static audio_buffer_test_case_t audio_buffer_cases[] = {{64, "Small buffer"}, {256, "Medium buffer"}, {1024, "Large buffer"}, {4096, "Very large buffer"}};

TEST_CASE("audio/buffer_sizes", "[audio]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(audio_buffer_cases), std::end(audio_buffer_cases)));
  auto *tc = &generated_case;
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  float *test_data = SAFE_MALLOC(tc->buffer_size * sizeof(float), float *);
  float *read_data = SAFE_MALLOC(tc->buffer_size * sizeof(float), float *);
  REQUIRE((test_data) != nullptr);
  REQUIRE((read_data) != nullptr);

  // Fill with test pattern
  for (size_t i = 0; i < tc->buffer_size; i++) {
    test_data[i] = sinf(2.0f * M_PI * 440.0f * i / 44100.0f);
  }

  int written = audio_ring_buffer_write(rb, test_data, tc->buffer_size);
  REQUIRE((written) >= (0));

  if (written > 0) {
    int read = audio_ring_buffer_read(rb, read_data, written);
    REQUIRE((read) == (written));

    // Verify data integrity for what was actually written/read
    for (int i = 0; i < read; i++) {
      REQUIRE((read_data[i]) == Catch::Approx(test_data[i]));
    }
  }

  SAFE_FREE(test_data);
  SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}

// Test case structure for audio sample rate tests
typedef struct {
  float frequency;
  const char *description;
} audio_frequency_test_case_t;

static audio_frequency_test_case_t audio_frequency_cases[] = {{440.0f, "A4 note"},    {880.0f, "A5 note"},       {220.0f, "A3 note"},
                                                              {1000.0f, "1kHz tone"}, {100.0f, "Low frequency"}, {10000.0f, "High frequency"}};

TEST_CASE("audio/frequency_tests", "[audio]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(audio_frequency_cases), std::end(audio_frequency_cases)));
  auto *tc = &generated_case;
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  const size_t sample_count = 256;
  float *test_data = SAFE_MALLOC(sample_count * sizeof(float), float *);
  float *read_data = SAFE_MALLOC(sample_count * sizeof(float), float *);
  REQUIRE((test_data) != nullptr);
  REQUIRE((read_data) != nullptr);

  // Generate sine wave at specified frequency
  for (size_t i = 0; i < sample_count; i++) {
    test_data[i] = sinf(2.0f * M_PI * tc->frequency * i / AUDIO_SAMPLE_RATE);
  }

  int written = audio_ring_buffer_write(rb, test_data, sample_count);
  REQUIRE((written) >= (0));

  if (written > 0) {
    int read = audio_ring_buffer_read(rb, read_data, written);
    REQUIRE((read) == (written));
  }

  SAFE_FREE(test_data);
  SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}

// Test case structure for audio stress tests
typedef struct {
  int num_cycles;
  const char *description;
} audio_stress_test_case_t;

static audio_stress_test_case_t audio_stress_cases[] = {{10, "Light stress test"}, {50, "Medium stress test"}, {100, "Heavy stress test"}, {500, "Intensive stress test"}};

TEST_CASE("audio/stress_tests", "[audio]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(audio_stress_cases), std::end(audio_stress_cases)));
  auto *tc = &generated_case;
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  const size_t sample_count = 64;
  float *test_data = SAFE_MALLOC(sample_count * sizeof(float), float *);
  float *read_data = SAFE_MALLOC(sample_count * sizeof(float), float *);
  REQUIRE((test_data) != nullptr);
  REQUIRE((read_data) != nullptr);

  // Generate test pattern
  for (size_t i = 0; i < sample_count; i++) {
    test_data[i] = sinf(2.0f * M_PI * 440.0f * i / AUDIO_SAMPLE_RATE);
  }

  // Perform many write/read cycles
  for (int cycle = 0; cycle < tc->num_cycles; cycle++) {
    int written = audio_ring_buffer_write(rb, test_data, sample_count);
    REQUIRE((written) >= (0));

    if (written > 0) {
      int read = audio_ring_buffer_read(rb, read_data, written);
      REQUIRE((read) == (written));
    }
  }

  SAFE_FREE(test_data);
  SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}

// Test case structure for audio edge case tests
typedef struct {
  size_t write_size;
  size_t read_size;
  const char *description;
} audio_edge_test_case_t;

static audio_edge_test_case_t audio_edge_cases[] = {{0, 0, "Zero size operations"},         {1, 1, "Single sample"},
                                                    {10, 5, "Write more than read"},        {5, 10, "Read more than available"},
                                                    {1000, 100, "Large write, small read"}, {100, 1000, "Small write, large read"}};

TEST_CASE("audio/edge_cases", "[audio]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(std::begin(audio_edge_cases), std::end(audio_edge_cases)));
  auto *tc = &generated_case;
  audio_ring_buffer_t *rb = audio_ring_buffer_create();
  REQUIRE((rb) != nullptr);

  float *write_data = NULL;
  float *read_data = NULL;

  if (tc->write_size > 0) {
    write_data = SAFE_MALLOC(tc->write_size * sizeof(float), float *);
    REQUIRE((write_data) != nullptr);

    // Fill with test pattern
    for (size_t i = 0; i < tc->write_size; i++) {
      write_data[i] = i * 0.001f;
    }
  }

  if (tc->read_size > 0) {
    read_data = SAFE_MALLOC(tc->read_size * sizeof(float), float *);
    REQUIRE((read_data) != nullptr);
  }

  // Test write operation
  int written = 0;
  if (tc->write_size > 0) {
    written = audio_ring_buffer_write(rb, write_data, tc->write_size);
    REQUIRE((written) >= (0));
  }

  // Test read operation
  int read = 0;
  if (tc->read_size > 0 && written > 0) {
    read = audio_ring_buffer_read(rb, read_data, tc->read_size);
    REQUIRE((read) >= (0));
    REQUIRE((read) <= (written));
  }

  SAFE_FREE(write_data);
  SAFE_FREE(read_data);
  audio_ring_buffer_destroy(rb);
}
