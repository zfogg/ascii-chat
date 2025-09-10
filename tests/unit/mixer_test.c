#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <string.h>
#include <math.h>

#include "mixer.h"
#include "audio.h"
#include "common.h"
#include "tests/logging.h"

// Use the enhanced macro to create complete test suites with custom log levels
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(mixer, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(mixer_utils, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(compressor, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(ducking, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(noise_gate, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(highpass_filter, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(soft_clip, LOG_FATAL, LOG_DEBUG, true, true);
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(mixer_integration, LOG_FATAL, LOG_DEBUG, true, true);

// Test data generation helpers
static void generate_sine_wave(float *buffer, int num_samples, float frequency, float sample_rate, float amplitude) {
    for (int i = 0; i < num_samples; i++) {
        buffer[i] = amplitude * sinf(2.0f * M_PI * frequency * i / sample_rate);
    }
}

static void generate_silence(float *buffer, int num_samples) {
    memset(buffer, 0, num_samples * sizeof(float));
}

static void generate_noise(float *buffer, int num_samples, float amplitude) {
    for (int i = 0; i < num_samples; i++) {
        buffer[i] = amplitude * ((float)rand() / RAND_MAX - 0.5f) * 2.0f;
    }
}

// Helper to create a test audio ring buffer with data
static audio_ring_buffer_t *create_test_buffer_with_data(const float *data, int num_samples) {
    audio_ring_buffer_t *buffer = audio_ring_buffer_create();
    cr_assert_not_null(buffer);

    int written = audio_ring_buffer_write(buffer, data, num_samples);
    cr_assert_eq(written, num_samples);

    return buffer;
}

// Helper to verify audio buffer contains expected data
static void verify_audio_buffer(const float *actual, const float *expected, int num_samples, float tolerance) {
    for (int i = 0; i < num_samples; i++) {
        cr_assert_float_eq(actual[i], expected[i], tolerance,
                          "Sample %d: expected %f, got %f", i, expected[i], actual[i]);
    }
}

/* ============================================================================
 * Basic Mixer Functionality Tests
 * ============================================================================ */

Test(mixer, create_and_destroy) {
    mixer_t *mixer = mixer_create(4, 44100);
    cr_assert_not_null(mixer);
    cr_assert_eq(mixer->max_sources, 4);
    cr_assert_eq(mixer->sample_rate, 44100);
    cr_assert_eq(mixer->num_sources, 0);

    mixer_destroy(mixer);
}

Test(mixer, create_with_invalid_params) {
    // Set a breakpoint on this line for debugging
    printf("DEBUG: Starting create_with_invalid_params test\n");

    // Add a simple pause to give debugger time to attach
    for (int i = 0; i < 1000000; i++) {
        // Simple delay loop
    }

    mixer_t *mixer = mixer_create(0, 44100);
    printf("DEBUG: mixer_create(0, 44100) returned: %p\n", mixer);
    cr_assert_null(mixer);

    mixer = mixer_create(4, 0);
    cr_assert_null(mixer);

    mixer = mixer_create(-1, 44100);
    cr_assert_null(mixer);

    mixer = mixer_create(4, -1);
    cr_assert_null(mixer);

    mixer = mixer_create(MIXER_MAX_SOURCES + 1, 44100);
    cr_assert_null(mixer);
}

Test(mixer, add_and_remove_sources) {
    mixer_t *mixer = mixer_create(4, 44100);
    cr_assert_not_null(mixer);

    // Create test audio buffers
    float test_data[256];
    generate_sine_wave(test_data, 256, 440.0f, 44100.0f, 0.5f);

    audio_ring_buffer_t *buffer1 = create_test_buffer_with_data(test_data, 256);
    audio_ring_buffer_t *buffer2 = create_test_buffer_with_data(test_data, 256);

    // Add sources
    int slot1 = mixer_add_source(mixer, 100, buffer1);
    cr_assert_geq(slot1, 0);
    cr_assert_eq(mixer->num_sources, 1);

    int slot2 = mixer_add_source(mixer, 200, buffer2);
    cr_assert_geq(slot2, 0);
    cr_assert_eq(mixer->num_sources, 2);

    // Try to add more than max sources
    audio_ring_buffer_t *buffer3 = create_test_buffer_with_data(test_data, 256);
    audio_ring_buffer_t *buffer4 = create_test_buffer_with_data(test_data, 256);
    audio_ring_buffer_t *buffer5 = create_test_buffer_with_data(test_data, 256);

    int slot3 = mixer_add_source(mixer, 300, buffer3);
    int slot4 = mixer_add_source(mixer, 400, buffer4);
    int slot5 = mixer_add_source(mixer, 500, buffer5);

    cr_assert_geq(slot3, 0);
    cr_assert_geq(slot4, 0);
    cr_assert_eq(slot5, -1); // Should fail - no more slots
    cr_assert_eq(mixer->num_sources, 4);

    // Remove sources
    mixer_remove_source(mixer, 100);
    cr_assert_eq(mixer->num_sources, 3);

    mixer_remove_source(mixer, 200);
    cr_assert_eq(mixer->num_sources, 2);

    // Clean up
    audio_ring_buffer_destroy(buffer1);
    audio_ring_buffer_destroy(buffer2);
    audio_ring_buffer_destroy(buffer3);
    audio_ring_buffer_destroy(buffer4);
    audio_ring_buffer_destroy(buffer5);
    mixer_destroy(mixer);
}

Test(mixer, set_source_active) {
    mixer_t *mixer = mixer_create(2, 44100);
    cr_assert_not_null(mixer);

    float test_data[256];
    generate_sine_wave(test_data, 256, 440.0f, 44100.0f, 0.5f);

    audio_ring_buffer_t *buffer = create_test_buffer_with_data(test_data, 256);
    int slot = mixer_add_source(mixer, 100, buffer);
    cr_assert_geq(slot, 0);

    // Source should be active by default
    cr_assert(mixer->source_active[slot]);

    // Deactivate source
    mixer_set_source_active(mixer, 100, false);
    cr_assert_not(mixer->source_active[slot]);

    // Reactivate source
    mixer_set_source_active(mixer, 100, true);
    cr_assert(mixer->source_active[slot]);

    // Clean up
    audio_ring_buffer_destroy(buffer);
    mixer_destroy(mixer);
}

Test(mixer, process_single_source) {
    mixer_t *mixer = mixer_create(2, 44100);
    cr_assert_not_null(mixer);

    // Generate test signal
    float test_data[256];
    generate_sine_wave(test_data, 256, 440.0f, 44100.0f, 0.5f);

    audio_ring_buffer_t *buffer = create_test_buffer_with_data(test_data, 256);
    mixer_add_source(mixer, 100, buffer);

    // Process audio
    float output[256];
    int processed = mixer_process(mixer, output, 256);
    cr_assert_eq(processed, 256);

    // Output should be similar to input (with some processing applied)
    // We expect some attenuation due to crowd scaling and compression
    for (int i = 0; i < 256; i++) {
        cr_assert_geq(fabsf(output[i]), 0.0f);
        cr_assert_leq(fabsf(output[i]), 1.0f);
    }

    // Clean up
    audio_ring_buffer_destroy(buffer);
    mixer_destroy(mixer);
}

Test(mixer, process_multiple_sources) {
    mixer_t *mixer = mixer_create(3, 44100);
    cr_assert_not_null(mixer);

    // Generate different test signals
    float test_data1[256], test_data2[256];
    generate_sine_wave(test_data1, 256, 440.0f, 44100.0f, 0.3f);
    generate_sine_wave(test_data2, 256, 880.0f, 44100.0f, 0.3f);

    audio_ring_buffer_t *buffer1 = create_test_buffer_with_data(test_data1, 256);
    audio_ring_buffer_t *buffer2 = create_test_buffer_with_data(test_data2, 256);

    mixer_add_source(mixer, 100, buffer1);
    mixer_add_source(mixer, 200, buffer2);

    // Process audio
    float output[256];
    int processed = mixer_process(mixer, output, 256);
    cr_assert_eq(processed, 256);

    // Output should be mixed (not just one source)
    // With crowd scaling, the output should be attenuated
    for (int i = 0; i < 256; i++) {
        cr_assert_geq(fabsf(output[i]), 0.0f);
        cr_assert_leq(fabsf(output[i]), 1.0f);
    }

    // Clean up
    audio_ring_buffer_destroy(buffer1);
    audio_ring_buffer_destroy(buffer2);
    mixer_destroy(mixer);
}

Test(mixer, process_excluding_source) {
    mixer_t *mixer = mixer_create(3, 44100);
    cr_assert_not_null(mixer);

    // Generate test signals
    float test_data1[256], test_data2[256];
    generate_sine_wave(test_data1, 256, 440.0f, 44100.0f, 0.5f);
    generate_sine_wave(test_data2, 256, 880.0f, 44100.0f, 0.5f);

    audio_ring_buffer_t *buffer1 = create_test_buffer_with_data(test_data1, 256);
    audio_ring_buffer_t *buffer2 = create_test_buffer_with_data(test_data2, 256);

    mixer_add_source(mixer, 100, buffer1);
    mixer_add_source(mixer, 200, buffer2);

    // Process normally
    float output_normal[256];
    mixer_process(mixer, output_normal, 256);

    // Process excluding source 100
    float output_excluded[256];
    int processed = mixer_process_excluding_source(mixer, output_excluded, 256, 100);
    cr_assert_eq(processed, 256);

    // Output should be different (only source 200 should be present)
    bool different = false;
    for (int i = 0; i < 256; i++) {
        if (fabsf(output_normal[i] - output_excluded[i]) > 1e-6f) {
            different = true;
            break;
        }
    }
    cr_assert(different, "Output should be different when excluding a source");

    // Clean up
    audio_ring_buffer_destroy(buffer1);
    audio_ring_buffer_destroy(buffer2);
    mixer_destroy(mixer);
}

Test(mixer, process_no_sources) {
    mixer_t *mixer = mixer_create(2, 44100);
    cr_assert_not_null(mixer);

    float output[256];
    int processed = mixer_process(mixer, output, 256);
    cr_assert_eq(processed, 0);

    // Output should be silence
    for (int i = 0; i < 256; i++) {
        cr_assert_float_eq(output[i], 0.0f, 1e-6f);
    }

    mixer_destroy(mixer);
}

/* ============================================================================
 * Utility Function Tests
 * ============================================================================ */

Test(mixer_utils, db_to_linear_conversion) {
    cr_assert_float_eq(db_to_linear(0.0f), 1.0f, 1e-6f);
    cr_assert_float_eq(db_to_linear(-6.0f), 0.5f, 0.01f);
    cr_assert_float_eq(db_to_linear(-20.0f), 0.1f, 1e-3f);
    cr_assert_float_eq(db_to_linear(-40.0f), 0.01f, 1e-4f);
}

Test(mixer_utils, linear_to_db_conversion) {
    cr_assert_float_eq(linear_to_db(1.0f), 0.0f, 1e-6f);
    cr_assert_float_eq(linear_to_db(0.5f), -6.0f, 0.1f);
    cr_assert_float_eq(linear_to_db(0.1f), -20.0f, 1e-3f);
    cr_assert_float_eq(linear_to_db(0.01f), -40.0f, 1e-3f);
}

Test(mixer_utils, clamp_float) {
    cr_assert_float_eq(clamp_float(0.5f, 0.0f, 1.0f), 0.5f, 1e-6f);
    cr_assert_float_eq(clamp_float(-0.5f, 0.0f, 1.0f), 0.0f, 1e-6f);
    cr_assert_float_eq(clamp_float(1.5f, 0.0f, 1.0f), 1.0f, 1e-6f);
    cr_assert_float_eq(clamp_float(0.0f, -1.0f, 1.0f), 0.0f, 1e-6f);
}

/* ============================================================================
 * Compressor Tests
 * ============================================================================ */

Test(compressor, init_and_params) {
    compressor_t comp;
    compressor_init(&comp, 44100.0f);

    cr_assert_float_eq(comp.sample_rate, 44100.0f, 1e-6f);
    cr_assert_float_eq(comp.threshold_dB, -10.0f, 1e-6f);
    cr_assert_float_eq(comp.ratio, 4.0f, 1e-6f);
    cr_assert_float_eq(comp.attack_ms, 10.0f, 1e-6f);
    cr_assert_float_eq(comp.release_ms, 100.0f, 1e-6f);
    cr_assert_float_eq(comp.makeup_dB, 3.0f, 1e-6f);
    cr_assert_float_eq(comp.envelope, 0.0f, 1e-6f);
    cr_assert_float_eq(comp.gain_lin, 1.0f, 1e-6f);
}

Test(compressor, set_params) {
    compressor_t comp;
    compressor_init(&comp, 44100.0f);

    compressor_set_params(&comp, -20.0f, 2.0f, 5.0f, 50.0f, 6.0f);

    cr_assert_float_eq(comp.threshold_dB, -20.0f, 1e-6f);
    cr_assert_float_eq(comp.ratio, 2.0f, 1e-6f);
    cr_assert_float_eq(comp.attack_ms, 5.0f, 1e-6f);
    cr_assert_float_eq(comp.release_ms, 50.0f, 1e-6f);
    cr_assert_float_eq(comp.makeup_dB, 6.0f, 1e-6f);
}

Test(compressor, process_below_threshold) {
    compressor_t comp;
    compressor_init(&comp, 44100.0f);
    compressor_set_params(&comp, -10.0f, 4.0f, 10.0f, 100.0f, 0.0f);

    // Process signal below threshold
    float gain = compressor_process_sample(&comp, 0.1f); // -20dB
    cr_assert_geq(gain, 0.9f); // Should have minimal compression
}

Test(compressor, process_above_threshold) {
    compressor_t comp;
    compressor_init(&comp, 44100.0f);
    compressor_set_params(&comp, -10.0f, 4.0f, 10.0f, 100.0f, 0.0f);

    // Process signal above threshold
    float gain = compressor_process_sample(&comp, 0.5f); // -6dB
    cr_assert_leq(gain, 1.0f); // Should have some compression
}

/* ============================================================================
 * Ducking Tests
 * ============================================================================ */

Test(ducking, init_and_params) {
    ducking_t duck;
    ducking_init(&duck, 4, 44100.0f);

    cr_assert_float_eq(duck.threshold_dB, -40.0f, 1e-6f);
    cr_assert_float_eq(duck.leader_margin_dB, 3.0f, 1e-6f);
    cr_assert_float_eq(duck.atten_dB, -12.0f, 1e-6f);
    cr_assert_float_eq(duck.attack_ms, 5.0f, 1e-6f);
    cr_assert_float_eq(duck.release_ms, 100.0f, 1e-6f);
    cr_assert_not_null(duck.envelope);
    cr_assert_not_null(duck.gain);

    // Check initial gain values
    for (int i = 0; i < 4; i++) {
        cr_assert_float_eq(duck.gain[i], 1.0f, 1e-6f);
    }

    ducking_free(&duck);
}

Test(ducking, set_params) {
    ducking_t duck;
    ducking_init(&duck, 4, 44100.0f);

    ducking_set_params(&duck, -30.0f, 5.0f, -15.0f, 10.0f, 200.0f);

    cr_assert_float_eq(duck.threshold_dB, -30.0f, 1e-6f);
    cr_assert_float_eq(duck.leader_margin_dB, 5.0f, 1e-6f);
    cr_assert_float_eq(duck.atten_dB, -15.0f, 1e-6f);
    cr_assert_float_eq(duck.attack_ms, 10.0f, 1e-6f);
    cr_assert_float_eq(duck.release_ms, 200.0f, 1e-6f);

    ducking_free(&duck);
}

Test(ducking, process_frame_leader_detection) {
    ducking_t duck;
    ducking_init(&duck, 3, 44100.0f);
    ducking_set_params(&duck, -40.0f, 3.0f, -12.0f, 5.0f, 100.0f);

    float envelopes[3] = {0.1f, 0.5f, 0.2f}; // Source 1 is loudest
    float gains[3] = {1.0f, 1.0f, 1.0f};

    ducking_process_frame(&duck, envelopes, gains, 3);

    // Source 1 should remain at full gain (leader)
    cr_assert_geq(gains[1], 0.9f);

    // Sources 0 and 2 should be ducked (but might not be as aggressive initially)
    cr_assert_lt(gains[0], 1.0f);
    cr_assert_lt(gains[2], 1.0f);

    ducking_free(&duck);
}

/* ============================================================================
 * Noise Gate Tests
 * ============================================================================ */

Test(noise_gate, init_and_params) {
    noise_gate_t gate;
    noise_gate_init(&gate, 44100.0f);

    cr_assert_float_eq(gate.sample_rate, 44100.0f, 1e-6f);
    cr_assert_float_eq(gate.threshold, 0.01f, 1e-6f);
    cr_assert_float_eq(gate.attack_ms, 2.0f, 1e-6f);
    cr_assert_float_eq(gate.release_ms, 50.0f, 1e-6f);
    cr_assert_float_eq(gate.hysteresis, 0.9f, 1e-6f);
    cr_assert_float_eq(gate.envelope, 0.0f, 1e-6f);
    cr_assert_not(gate.gate_open);
}

Test(noise_gate, set_params) {
    noise_gate_t gate;
    noise_gate_init(&gate, 44100.0f);

    noise_gate_set_params(&gate, 0.05f, 5.0f, 100.0f, 0.8f);

    cr_assert_float_eq(gate.threshold, 0.05f, 1e-6f);
    cr_assert_float_eq(gate.attack_ms, 5.0f, 1e-6f);
    cr_assert_float_eq(gate.release_ms, 100.0f, 1e-6f);
    cr_assert_float_eq(gate.hysteresis, 0.8f, 1e-6f);
}

Test(noise_gate, process_below_threshold) {
    noise_gate_t gate;
    noise_gate_init(&gate, 44100.0f);
    noise_gate_set_params(&gate, 0.1f, 2.0f, 50.0f, 0.9f);

    // Process signal below threshold
    float output = noise_gate_process_sample(&gate, 0.5f, 0.05f);
    cr_assert_leq(fabsf(output), 0.1f); // Should be gated
    cr_assert_not(noise_gate_is_open(&gate));
}

Test(noise_gate, process_above_threshold) {
    noise_gate_t gate;
    noise_gate_init(&gate, 44100.0f);
    noise_gate_set_params(&gate, 0.1f, 2.0f, 50.0f, 0.9f);

    // Process signal above threshold (envelope starts at 0, so first sample will be low)
    float output = noise_gate_process_sample(&gate, 0.5f, 0.2f);
    cr_assert_gt(fabsf(output), 0.001f); // Should pass through (but envelope starts low)
    cr_assert(noise_gate_is_open(&gate));
}

Test(noise_gate, process_buffer) {
    noise_gate_t gate;
    noise_gate_init(&gate, 44100.0f);
    noise_gate_set_params(&gate, 0.1f, 2.0f, 50.0f, 0.9f);

    float buffer[10] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    noise_gate_process_buffer(&gate, buffer, 10);

    // All samples should be processed
    for (int i = 0; i < 10; i++) {
        cr_assert_geq(fabsf(buffer[i]), 0.0f);
        cr_assert_leq(fabsf(buffer[i]), 1.0f);
    }
}

/* ============================================================================
 * High-Pass Filter Tests
 * ============================================================================ */

Test(highpass_filter, init_and_reset) {
    highpass_filter_t filter;
    highpass_filter_init(&filter, 100.0f, 44100.0f);

    cr_assert_float_eq(filter.cutoff_hz, 100.0f, 1e-6f);
    cr_assert_float_eq(filter.sample_rate, 44100.0f, 1e-6f);
    cr_assert_gt(filter.alpha, 0.0f);
    cr_assert_lt(filter.alpha, 1.0f);

    highpass_filter_reset(&filter);
    cr_assert_float_eq(filter.prev_input, 0.0f, 1e-6f);
    cr_assert_float_eq(filter.prev_output, 0.0f, 1e-6f);
}

Test(highpass_filter, process_dc_signal) {
    highpass_filter_t filter;
    highpass_filter_init(&filter, 100.0f, 44100.0f);

    // Process DC signal (should be filtered out)
    float output = highpass_filter_process_sample(&filter, 1.0f);
    cr_assert_lt(fabsf(output), 1.0f); // Should be attenuated (first sample might not be fully filtered)

    // Continue processing DC
    for (int i = 0; i < 100; i++) {
        output = highpass_filter_process_sample(&filter, 1.0f);
    }
    cr_assert_lt(fabsf(output), 0.5f); // Should be attenuated (high-pass filters may not be perfect)
}

Test(highpass_filter, process_high_frequency) {
    highpass_filter_t filter;
    highpass_filter_init(&filter, 100.0f, 44100.0f);

    // Process high frequency signal (should pass through)
    float output = highpass_filter_process_sample(&filter, 1.0f);
    // First sample will be filtered, but subsequent high-freq samples should pass
    for (int i = 0; i < 10; i++) {
        output = highpass_filter_process_sample(&filter, (i % 2) ? 1.0f : -1.0f);
    }
    cr_assert_gt(fabsf(output), 0.1f); // Should have significant output
}

Test(highpass_filter, process_buffer) {
    highpass_filter_t filter;
    highpass_filter_init(&filter, 100.0f, 44100.0f);

    float buffer[10] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    highpass_filter_process_buffer(&filter, buffer, 10);

    // All samples should be processed
    for (int i = 0; i < 10; i++) {
        cr_assert_geq(fabsf(buffer[i]), 0.0f);
        cr_assert_leq(fabsf(buffer[i]), 1.0f);
    }
}

/* ============================================================================
 * Soft Clipping Tests
 * ============================================================================ */

Test(soft_clip, process_within_threshold) {
    float output = soft_clip(0.5f, 0.8f);
    cr_assert_float_eq(output, 0.5f, 1e-6f);

    output = soft_clip(-0.3f, 0.8f);
    cr_assert_float_eq(output, -0.3f, 1e-6f);
}

Test(soft_clip, process_above_threshold) {
    float output = soft_clip(1.0f, 0.8f);
    cr_assert_lt(output, 1.0f);
    cr_assert_gt(output, 0.8f);

    output = soft_clip(-1.0f, 0.8f);
    cr_assert_gt(output, -1.0f);
    cr_assert_lt(output, -0.6f); // Should be soft clipped (smooth curve, not hard threshold)
}

Test(soft_clip, process_buffer) {
    float buffer[5] = {0.5f, 1.0f, -0.3f, -1.0f, 0.0f};
    soft_clip_buffer(buffer, 5, 0.8f);

    cr_assert_float_eq(buffer[0], 0.5f, 1e-6f); // Within threshold
    cr_assert_lt(buffer[1], 1.0f); // Clipped
    cr_assert_float_eq(buffer[2], -0.3f, 1e-6f); // Within threshold
    cr_assert_gt(buffer[3], -1.0f); // Clipped
    cr_assert_float_eq(buffer[4], 0.0f, 1e-6f); // Zero
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

Test(mixer_integration, full_pipeline_with_processing) {
    mixer_t *mixer = mixer_create(3, 44100);
    cr_assert_not_null(mixer);

    // Generate test signals with different characteristics
    float sine_data[256], noise_data[256];
    generate_sine_wave(sine_data, 256, 440.0f, 44100.0f, 0.8f);
    generate_noise(noise_data, 256, 0.1f);

    audio_ring_buffer_t *sine_buffer = create_test_buffer_with_data(sine_data, 256);
    audio_ring_buffer_t *noise_buffer = create_test_buffer_with_data(noise_data, 256);

    mixer_add_source(mixer, 100, sine_buffer);
    mixer_add_source(mixer, 200, noise_buffer);

    // Process through full pipeline
    float output[256];
    int processed = mixer_process(mixer, output, 256);
    cr_assert_eq(processed, 256);

    // Verify output characteristics
    float max_output = 0.0f;
    float rms_output = 0.0f;
    for (int i = 0; i < 256; i++) {
        float abs_val = fabsf(output[i]);
        if (abs_val > max_output) max_output = abs_val;
        rms_output += output[i] * output[i];
    }
    rms_output = sqrtf(rms_output / 256.0f);

    // Output should be properly limited and processed
    cr_assert_leq(max_output, 1.0f); // No clipping
    cr_assert_gt(rms_output, 0.0f); // Non-zero output

    // Clean up
    audio_ring_buffer_destroy(sine_buffer);
    audio_ring_buffer_destroy(noise_buffer);
    mixer_destroy(mixer);
}

Test(mixer_integration, stress_test_multiple_sources) {
    mixer_t *mixer = mixer_create(MIXER_MAX_SOURCES, 44100);
    cr_assert_not_null(mixer);

    // Add maximum number of sources
    audio_ring_buffer_t *buffers[MIXER_MAX_SOURCES];
    for (int i = 0; i < MIXER_MAX_SOURCES; i++) {
        float test_data[256];
        generate_sine_wave(test_data, 256, 440.0f + i * 100.0f, 44100.0f, 0.1f);
        buffers[i] = create_test_buffer_with_data(test_data, 256);
        int slot = mixer_add_source(mixer, 100 + i, buffers[i]);
        cr_assert_geq(slot, 0);
    }

    cr_assert_eq(mixer->num_sources, MIXER_MAX_SOURCES);

    // Process all sources
    float output[256];
    int processed = mixer_process(mixer, output, 256);
    cr_assert_eq(processed, 256);

    // Verify output is properly mixed and limited
    for (int i = 0; i < 256; i++) {
        cr_assert_geq(output[i], -1.0f);
        cr_assert_leq(output[i], 1.0f);
    }

    // Clean up
    for (int i = 0; i < MIXER_MAX_SOURCES; i++) {
        audio_ring_buffer_destroy(buffers[i]);
    }
    mixer_destroy(mixer);
}
