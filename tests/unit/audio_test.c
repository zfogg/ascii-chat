#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "common.h"
#include "audio.h"

void setup_audio_quiet_logging(void);
void restore_audio_logging(void);

TestSuite(audio, .init = setup_audio_quiet_logging, .fini = restore_audio_logging);

void setup_audio_quiet_logging(void) {
    log_set_level(LOG_FATAL);
}

void restore_audio_logging(void) {
    log_set_level(LOG_DEBUG);
}

// =============================================================================
// Audio Initialization Tests
// =============================================================================

Test(audio, initialization_and_cleanup) {
    audio_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    // Test basic audio system initialization
    int result = audio_init(&ctx);
    // May fail if no audio device available - that's OK
    if (result == 0) {
        cr_assert(ctx.initialized, "Context should be marked as initialized");
    }

    // Even if initialization fails (no audio device), cleanup should work
    audio_destroy(&ctx);

    // Multiple cleanup calls should be safe
    audio_destroy(&ctx);
}

Test(audio, multiple_init_cleanup_cycles) {
    // Test multiple init/cleanup cycles
    for (int i = 0; i < 3; i++) {
        audio_context_t ctx;
        memset(&ctx, 0, sizeof(ctx));

        int result = audio_init(&ctx);
        // Should succeed or fail consistently
        audio_destroy(&ctx);
    }
}

// =============================================================================
// Ringbuffer Tests
// =============================================================================

Test(audio, ringbuffer_basic_operations) {
    audio_ring_buffer_t *rb = audio_ring_buffer_create();
    cr_assert_not_null(rb, "Ringbuffer creation should succeed");

    // Test empty buffer properties
    cr_assert_eq(audio_ring_buffer_available_read(rb), 0, "Empty buffer should have 0 read space");
    cr_assert_gt(audio_ring_buffer_available_write(rb), 0, "Empty buffer should have write space");

    audio_ring_buffer_destroy(rb);
}

Test(audio, ringbuffer_write_read) {
    audio_ring_buffer_t *rb = audio_ring_buffer_create();
    cr_assert_not_null(rb, "Ringbuffer creation should succeed");

    float test_data[100];
    float read_data[100];

    // Fill test data with sine wave
    for (int i = 0; i < 100; i++) {
        test_data[i] = sinf(2.0f * M_PI * 440.0f * i / 44100.0f);
    }

    // Write data
    int written = audio_ring_buffer_write(rb, test_data, 100);
    cr_assert_gt(written, 0, "Should write some samples");
    cr_assert_gt(audio_ring_buffer_available_read(rb), 0, "Should have samples available to read");

    // Read data back
    int read = audio_ring_buffer_read(rb, read_data, written);
    cr_assert_eq(read, written, "Should read all written samples");

    // Verify data integrity for what was actually written/read
    for (int i = 0; i < read; i++) {
        cr_assert_float_eq(read_data[i], test_data[i], 0.0001f,
                          "Sample %d should match (expected %.6f, got %.6f)",
                          i, test_data[i], read_data[i]);
    }

    audio_ring_buffer_destroy(rb);
}

Test(audio, ringbuffer_overflow_behavior) {
    audio_ring_buffer_t *rb = audio_ring_buffer_create();
    cr_assert_not_null(rb, "Ringbuffer creation should succeed");

    float test_data[2000]; // More than likely buffer capacity

    for (int i = 0; i < 2000; i++) {
        test_data[i] = i * 0.001f;
    }

    int available_space = audio_ring_buffer_available_write(rb);
    cr_assert_gt(available_space, 0, "Buffer should have write space");

    // Try to write more than buffer capacity
    int written = audio_ring_buffer_write(rb, test_data, 2000);
    cr_assert_leq(written, available_space, "Should not write more than available space");
    cr_assert_geq(written, 0, "Should not return negative");

    audio_ring_buffer_destroy(rb);
}

Test(audio, ringbuffer_wrap_around) {
    audio_ring_buffer_t *rb = audio_ring_buffer_create();
    cr_assert_not_null(rb, "Ringbuffer creation should succeed");

    float data1[256], data2[256], read_data[256];

    // Fill test data
    for (int i = 0; i < 256; i++) {
        data1[i] = i;
        data2[i] = 1000 + i;
    }

    // Write first batch
    int written1 = audio_ring_buffer_write(rb, data1, 256);
    cr_assert_gt(written1, 0, "Should write first batch");

    // Read partial data to make space
    int read1 = audio_ring_buffer_read(rb, read_data, written1 / 2);
    cr_assert_gt(read1, 0, "Should read partial data");

    // Write second batch (may wrap around internally)
    int written2 = audio_ring_buffer_write(rb, data2, 256);
    cr_assert_geq(written2, 0, "Second write should not fail");

    // Verify we can still read remaining data
    int remaining = audio_ring_buffer_available_read(rb);
    cr_assert_gt(remaining, 0, "Should have data available after operations");

    audio_ring_buffer_destroy(rb);
}

Test(audio, ringbuffer_concurrent_access_simulation) {
    audio_ring_buffer_t *rb = audio_ring_buffer_create();
    cr_assert_not_null(rb, "Ringbuffer creation should succeed");

    float write_data[50], read_data[30];

    // Simulate producer/consumer pattern
    for (int cycle = 0; cycle < 10; cycle++) {
        // Producer writes
        for (int i = 0; i < 50; i++) {
            write_data[i] = cycle * 100 + i;
        }
        int written = audio_ring_buffer_write(rb, write_data, 50);
        cr_assert_geq(written, 0, "Should write some data in cycle %d (wrote %d)", cycle, written);

        // Consumer reads (less than written to build up buffer)
        if (audio_ring_buffer_available_read(rb) >= 30) {
            int read = audio_ring_buffer_read(rb, read_data, 30);
            cr_assert_geq(read, 0, "Should read samples in cycle %d (read %d)", cycle, read);
        }
    }

    audio_ring_buffer_destroy(rb);
}

// =============================================================================
// Audio Context and Streaming Tests
// =============================================================================

Test(audio, audio_context_operations) {
    audio_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    // Test initialization
    int result = audio_init(&ctx);
    if (result != 0) {
        // Audio may not be available in test environment - skip remaining tests
        return;
    }

    cr_assert(ctx.initialized, "Context should be initialized");
    cr_assert_not_null(ctx.capture_buffer, "Should have capture buffer");
    cr_assert_not_null(ctx.playback_buffer, "Should have playback buffer");

    // Test cleanup
    audio_destroy(&ctx);
    cr_assert(ctx.initialized == false, "Context should be marked uninitialized");
}

Test(audio, audio_sample_buffer_operations) {
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
    cr_assert_geq(written, 0, "Should write samples successfully or return 0");

    // Test reading samples (may return 0 if no input available)
    int read = audio_read_samples(&ctx, read_samples, 256);
    cr_assert_geq(read, 0, "Should read samples or return 0");

    audio_destroy(&ctx);
}

// =============================================================================
// Audio Capture and Playback Tests
// =============================================================================

Test(audio, audio_capture_operations) {
    audio_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (audio_init(&ctx) != 0) {
        return; // Skip if no audio available
    }

    // Test starting and stopping capture
    int result = audio_start_capture(&ctx);
    // May fail if no input device - that's OK

    if (result == 0) {
        cr_assert(ctx.recording, "Should be marked as recording");

        result = audio_stop_capture(&ctx);
        cr_assert_eq(result, 0, "Should stop capture successfully");
        cr_assert(ctx.recording == false, "Should not be marked as recording");
    }

    audio_destroy(&ctx);
}

Test(audio, audio_playback_operations) {
    audio_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (audio_init(&ctx) != 0) {
        return; // Skip if no audio available
    }

    // Test starting and stopping playback
    int result = audio_start_playback(&ctx);
    // May fail if no output device - that's OK

    if (result == 0) {
        cr_assert(ctx.playing, "Should be marked as playing");

        result = audio_stop_playback(&ctx);
        cr_assert_eq(result, 0, "Should stop playback successfully");
        cr_assert(ctx.playing == false, "Should not be marked as playing");
    }

    audio_destroy(&ctx);
}

// =============================================================================
// Audio Constants and Configuration Tests
// =============================================================================

Test(audio, audio_constants) {
    // Test that audio constants are sensible
    cr_assert_eq(AUDIO_SAMPLE_RATE, 44100, "Sample rate should be 44.1kHz");
    cr_assert_eq(AUDIO_CHANNELS, 1, "Should be mono audio");
    cr_assert_gt(AUDIO_FRAMES_PER_BUFFER, 0, "Frame buffer size should be positive");
    cr_assert_leq(AUDIO_FRAMES_PER_BUFFER, 2048, "Frame buffer should be reasonable size");

    // Verify buffer size calculation
    cr_assert_eq(AUDIO_BUFFER_SIZE, AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS,
                "Buffer size should match frames × channels");
}

Test(audio, audio_realtime_priority) {
    // Test realtime priority setting (may fail on some systems)
    int result = audio_set_realtime_priority();
    // Result may be 0 (success) or non-zero (failed) - both are OK
    // This just tests that the function doesn't crash
    (void)result; // Suppress unused variable warning
}

// =============================================================================
// Audio Ring Buffer Advanced Tests
// =============================================================================

Test(audio, audio_ring_buffer_stress_test) {
    audio_ring_buffer_t *rb = audio_ring_buffer_create();
    cr_assert_not_null(rb, "Ringbuffer creation should succeed");

    float test_data[64];
    float read_data[64];

    // Generate test pattern
    for (int i = 0; i < 64; i++) {
        test_data[i] = sinf(2.0f * M_PI * 440.0f * i / 44100.0f);
    }

    // Perform many write/read cycles to stress test
    for (int cycle = 0; cycle < 100; cycle++) {
        int written = audio_ring_buffer_write(rb, test_data, 64);
        cr_assert_geq(written, 0, "Write should not fail in cycle %d", cycle);

        if (written > 0) {
            int read = audio_ring_buffer_read(rb, read_data, written);
            cr_assert_eq(read, written, "Should read what was written in cycle %d", cycle);
        }
    }

    audio_ring_buffer_destroy(rb);
}

Test(audio, audio_ring_buffer_partial_operations) {
    audio_ring_buffer_t *rb = audio_ring_buffer_create();
    cr_assert_not_null(rb, "Ringbuffer creation should succeed");

    float large_data[1024];

    // Fill with test pattern
    for (int i = 0; i < 1024; i++) {
        large_data[i] = i * 0.001f;
    }

    // Try to write large amount - may only write partial
    int written = audio_ring_buffer_write(rb, large_data, 1024);
    cr_assert_geq(written, 0, "Should handle large writes gracefully");

    // If something was written, we should be able to read it back
    if (written > 0) {
        float *read_data;
        SAFE_MALLOC(read_data, written * sizeof(float), float *);

        int read = audio_ring_buffer_read(rb, read_data, written);
        cr_assert_eq(read, written, "Should read back all written data");

        // Verify data integrity
        for (int i = 0; i < read; i++) {
            cr_assert_float_eq(read_data[i], large_data[i], 0.0001f,
                             "Sample %d should match", i);
        }

        free(read_data);
    }

    audio_ring_buffer_destroy(rb);
}

// =============================================================================
// Audio Integration Tests
// =============================================================================

Test(audio, audio_context_integration) {
    audio_context_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    if (audio_init(&ctx) != 0) {
        return; // Skip if no audio available
    }

    // Test that both capture and playback can be started together
    int capture_result = audio_start_capture(&ctx);
    int playback_result = audio_start_playback(&ctx);

    // Both may fail if no devices available - that's OK
    if (capture_result == 0 && playback_result == 0) {
        cr_assert(ctx.recording, "Should be recording");
        cr_assert(ctx.playing, "Should be playing");

        // Test stopping both
        audio_stop_capture(&ctx);
        audio_stop_playback(&ctx);

        cr_assert(ctx.recording == false, "Should not be recording");
        cr_assert(ctx.playing == false, "Should not be playing");
    }

    audio_destroy(&ctx);
}

Test(audio, audio_buffer_size_consistency) {
    // Test that audio constants are consistent
    cr_assert_gt(AUDIO_BUFFER_SIZE, 0, "Buffer size should be positive");
    cr_assert_eq(AUDIO_BUFFER_SIZE, AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS,
                "Buffer size should equal frames × channels");

    // Test reasonable buffer sizes for real-time audio
    cr_assert_geq(AUDIO_FRAMES_PER_BUFFER, 64, "Frame buffer should be at least 64 samples");
    cr_assert_leq(AUDIO_FRAMES_PER_BUFFER, 4096, "Frame buffer should not exceed 4096 samples");

    // Test that sample rate is reasonable
    cr_assert_geq(AUDIO_SAMPLE_RATE, 8000, "Sample rate should be at least 8kHz");
    cr_assert_leq(AUDIO_SAMPLE_RATE, 192000, "Sample rate should not exceed 192kHz");
}

// =============================================================================
// Error Handling and Edge Cases
// =============================================================================

Test(audio, null_pointer_handling) {
    audio_context_t ctx;
    float samples[100];

    // Test audio functions with NULL context
    int result = audio_read_samples(NULL, samples, 100);
    cr_assert_neq(result, 100, "Reading with NULL context should fail or return 0");

    result = audio_write_samples(NULL, samples, 100);
    cr_assert_neq(result, 100, "Writing with NULL context should fail or return 0");

    // Test with NULL buffer
    memset(&ctx, 0, sizeof(ctx));
    if (audio_init(&ctx) == 0) {
        result = audio_read_samples(&ctx, NULL, 100);
        cr_assert_neq(result, 100, "Reading with NULL buffer should fail");

        result = audio_write_samples(&ctx, NULL, 100);
        cr_assert_neq(result, 100, "Writing with NULL buffer should fail");

        audio_destroy(&ctx);
    }
}

Test(audio, zero_sample_count_handling) {
    audio_context_t ctx;
    float samples[100];

    memset(&ctx, 0, sizeof(ctx));
    if (audio_init(&ctx) == 0) {
        // Functions should handle zero sample count gracefully
        int result = audio_read_samples(&ctx, samples, 0);
        cr_assert_eq(result, 0, "Zero sample read should return 0");

        result = audio_write_samples(&ctx, samples, 0);
        cr_assert_eq(result, 0, "Zero sample write should return 0");

        audio_destroy(&ctx);
    }
}

Test(audio, ringbuffer_edge_cases) {
    audio_ring_buffer_t *rb = audio_ring_buffer_create();
    cr_assert_not_null(rb, "Ringbuffer creation should succeed");

    float samples[100];

    // Test with NULL data pointer
    int result = audio_ring_buffer_write(rb, NULL, 100);
    cr_assert_leq(result, 0, "Writing NULL data should fail or return 0");

    result = audio_ring_buffer_read(rb, NULL, 100);
    cr_assert_leq(result, 0, "Reading to NULL buffer should fail or return 0");

    // Test with zero sample count
    result = audio_ring_buffer_write(rb, samples, 0);
    cr_assert_eq(result, 0, "Writing zero samples should return 0");

    result = audio_ring_buffer_read(rb, samples, 0);
    cr_assert_eq(result, 0, "Reading zero samples should return 0");

    audio_ring_buffer_destroy(rb);
}
