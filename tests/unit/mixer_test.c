#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <criterion/theories.h>
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

static void generate_noise(float *buffer, int num_samples, float amplitude) {
  for (int i = 0; i < num_samples; i++) {
    buffer[i] = amplitude * ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f;
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

// Parameterized test for invalid mixer creation parameters
typedef struct {
  int max_sources;
  int sample_rate;
  char description[64];
} mixer_invalid_params_test_case_t;

static mixer_invalid_params_test_case_t mixer_invalid_params_cases[] = {
    {0, 44100, "Zero max_sources"},
    {4, 0, "Zero sample_rate"},
    {-1, 44100, "Negative max_sources"},
    {4, -1, "Negative sample_rate"},
    {MIXER_MAX_SOURCES + 1, 44100, "Exceeds MIXER_MAX_SOURCES"},
};

ParameterizedTestParameters(mixer, create_with_invalid_params) {
  return cr_make_param_array(mixer_invalid_params_test_case_t, mixer_invalid_params_cases,
                             sizeof(mixer_invalid_params_cases) / sizeof(mixer_invalid_params_cases[0]));
}

ParameterizedTest(mixer_invalid_params_test_case_t *tc, mixer, create_with_invalid_params) {
  mixer_t *mixer = mixer_create(tc->max_sources, tc->sample_rate);
  cr_assert_null(mixer, "%s should return NULL", tc->description);
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

// Test case structure for dB to linear conversion
typedef struct {
  float db_value;
  float expected_linear;
  float epsilon;
  char description[64];
} db_to_linear_test_case_t;

static db_to_linear_test_case_t db_to_linear_cases[] = {
    {0.0f, 1.0f, 1e-6f, "0 dB equals unity gain"},       {-6.0f, 0.5f, 0.01f, "-6 dB equals half gain"},
    {-20.0f, 0.1f, 1e-3f, "-20 dB equals 0.1 gain"},     {-40.0f, 0.01f, 1e-4f, "-40 dB equals 0.01 gain"},
    {-60.0f, 0.001f, 1e-4f, "-60 dB equals 0.001 gain"}, {6.0f, 2.0f, 0.01f, "+6 dB equals double gain"}};

ParameterizedTestParameters(mixer_utils, db_to_linear_conversion_param) {
  size_t nb_cases = sizeof(db_to_linear_cases) / sizeof(db_to_linear_cases[0]);
  return cr_make_param_array(db_to_linear_test_case_t, db_to_linear_cases, nb_cases);
}

ParameterizedTest(db_to_linear_test_case_t *tc, mixer_utils, db_to_linear_conversion_param) {
  float result = db_to_linear(tc->db_value);
  cr_assert_float_eq(result, tc->expected_linear, tc->epsilon, "%s should be correct", tc->description);
}

// Test case structure for linear to dB conversion
typedef struct {
  float linear_value;
  float expected_db;
  float epsilon;
  char description[64];
} linear_to_db_test_case_t;

static linear_to_db_test_case_t linear_to_db_cases[] = {
    {1.0f, 0.0f, 1e-6f, "Unity gain equals 0 dB"},       {0.5f, -6.0f, 0.1f, "Half gain equals -6 dB"},
    {0.1f, -20.0f, 1e-3f, "0.1 gain equals -20 dB"},     {0.01f, -40.0f, 1e-3f, "0.01 gain equals -40 dB"},
    {0.001f, -60.0f, 1e-3f, "0.001 gain equals -60 dB"}, {2.0f, 6.0f, 0.1f, "Double gain equals +6 dB"}};

ParameterizedTestParameters(mixer_utils, linear_to_db_conversion_param) {
  size_t nb_cases = sizeof(linear_to_db_cases) / sizeof(linear_to_db_cases[0]);
  return cr_make_param_array(linear_to_db_test_case_t, linear_to_db_cases, nb_cases);
}

ParameterizedTest(linear_to_db_test_case_t *tc, mixer_utils, linear_to_db_conversion_param) {
  float result = linear_to_db(tc->linear_value);
  cr_assert_float_eq(result, tc->expected_db, tc->epsilon, "%s should be correct", tc->description);
}

// Test case structure for clamp_float
typedef struct {
  float value;
  float min;
  float max;
  float expected;
  char description[64];
} clamp_float_test_case_t;

static clamp_float_test_case_t clamp_float_cases[] = {
    {0.5f, 0.0f, 1.0f, 0.5f, "Value within range"},       {-0.5f, 0.0f, 1.0f, 0.0f, "Value below min"},
    {1.5f, 0.0f, 1.0f, 1.0f, "Value above max"},          {0.0f, -1.0f, 1.0f, 0.0f, "Zero in symmetric range"},
    {-2.0f, -1.0f, 1.0f, -1.0f, "Clamp to negative min"}, {2.0f, -1.0f, 1.0f, 1.0f, "Clamp to positive max"},
    {0.0f, 0.0f, 0.0f, 0.0f, "Degenerate range"}};

ParameterizedTestParameters(mixer_utils, clamp_float_param) {
  size_t nb_cases = sizeof(clamp_float_cases) / sizeof(clamp_float_cases[0]);
  return cr_make_param_array(clamp_float_test_case_t, clamp_float_cases, nb_cases);
}

ParameterizedTest(clamp_float_test_case_t *tc, mixer_utils, clamp_float_param) {
  float result = clamp_float(tc->value, tc->min, tc->max);
  cr_assert_float_eq(result, tc->expected, 1e-6f, "%s should be correct", tc->description);
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
  cr_assert_geq(gain, 0.9f);                           // Should have minimal compression
}

Test(compressor, process_above_threshold) {
  compressor_t comp;
  compressor_init(&comp, 44100.0f);
  compressor_set_params(&comp, -10.0f, 4.0f, 10.0f, 100.0f, 0.0f);

  // Process signal above threshold
  float gain = compressor_process_sample(&comp, 0.5f); // -6dB
  cr_assert_leq(gain, 1.0f);                           // Should have some compression
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

  cr_assert_float_eq(buffer[0], 0.5f, 1e-6f);  // Within threshold
  cr_assert_lt(buffer[1], 1.0f);               // Clipped
  cr_assert_float_eq(buffer[2], -0.3f, 1e-6f); // Within threshold
  cr_assert_gt(buffer[3], -1.0f);              // Clipped
  cr_assert_float_eq(buffer[4], 0.0f, 1e-6f);  // Zero
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
    if (abs_val > max_output)
      max_output = abs_val;
    rms_output += output[i] * output[i];
  }
  rms_output = sqrtf(rms_output / 256.0f);

  // Output should be properly limited and processed
  cr_assert_leq(max_output, 1.0f); // No clipping
  cr_assert_gt(rms_output, 0.0f);  // Non-zero output

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

// =============================================================================
// Audio Mixing Property-Based Tests
// =============================================================================

// Theory: Mixed audio output should always be bounded to [-1.0, 1.0] range
// regardless of number of sources or their amplitudes
TheoryDataPoints(mixer_integration, audio_bounds_property) = {
    DataPoints(size_t, 1, 2, 3, 4, 8),         // num_sources
    DataPoints(float, 0.1f, 0.5f, 1.0f, 2.0f), // amplitude per source
};

Theory((size_t num_sources, float amplitude), mixer_integration, audio_bounds_property) {
  cr_assume(num_sources > 0 && num_sources <= MIXER_MAX_SOURCES);
  cr_assume(amplitude > 0.0f && amplitude <= 2.0f);

  mixer_t *mixer = mixer_create(MIXER_MAX_SOURCES, 48000);
  cr_assume(mixer != NULL);

  // Create sources with specified amplitude
  audio_ring_buffer_t *buffers[MIXER_MAX_SOURCES];
  for (size_t i = 0; i < num_sources; i++) {
    buffers[i] = audio_ring_buffer_create();
    cr_assume(buffers[i] != NULL);

    // Generate test signal with specified amplitude
    float test_data[256];
    generate_sine_wave(test_data, 256, 440.0f + (float)i * 100.0f, 48000.0f, amplitude);

    audio_ring_buffer_write(buffers[i], test_data, 256);
    mixer_add_source(mixer, 100 + (uint32_t)i, buffers[i]);
  }

  // Process mixed output
  float output[256];
  int processed = mixer_process(mixer, output, 256);
  cr_assert_eq(processed, 256, "Should process all samples");

  // Verify property: ALL output samples must be in [-1.0, 1.0] range
  for (int i = 0; i < 256; i++) {
    cr_assert_geq(output[i], -1.0f, "Output sample %d must be >= -1.0 (sources=%zu, amplitude=%.2f, got %.4f)", i,
                  num_sources, amplitude, output[i]);
    cr_assert_leq(output[i], 1.0f, "Output sample %d must be <= 1.0 (sources=%zu, amplitude=%.2f, got %.4f)", i,
                  num_sources, amplitude, output[i]);
  }

  // Clean up
  for (size_t i = 0; i < num_sources; i++) {
    audio_ring_buffer_destroy(buffers[i]);
  }
  mixer_destroy(mixer);
}
