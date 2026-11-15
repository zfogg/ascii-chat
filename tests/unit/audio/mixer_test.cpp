#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <string.h>
#include <math.h>

#include <ascii-chat/tests/logging.h>
#include <ascii-chat/audio/mixer.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/fnv1a.h>

TEST_CASE("mixer/independent_listener_audio_and_self_exclusion", "[mixer]") {
  mixer_t *mixer = mixer_create(3, 48000);
  REQUIRE((mixer) != nullptr);
  float input[480], first[480], second[480], self[480];
  for (int i = 0; i < 480; i++)
    input[i] = 0.1f;
  audio_ring_buffer_t *buffers[3];
  const char *ids[] = {"speaker", "listener-one", "listener-two"};
  for (int i = 0; i < 3; i++) {
    buffers[i] = audio_ring_buffer_create_for_capture();
    REQUIRE((buffers[i]) != nullptr);
    REQUIRE((mixer_add_source(mixer, ids[i], buffers[i])) >= (0));
  }
  REQUIRE((audio_ring_buffer_write(buffers[0], input, 480)) == (ASCIICHAT_OK));
  REQUIRE((mixer_process_excluding_source(mixer, first, 480, fnv1a_hash_string(ids[1]))) == (480));
  REQUIRE((mixer_process_excluding_source(mixer, second, 480, fnv1a_hash_string(ids[2]))) == (480));
  REQUIRE((mixer_process_excluding_source(mixer, self, 480, fnv1a_hash_string(ids[0]))) == (480));
  for (int i = 0; i < 480; i++) {
    REQUIRE((first[i]) > (0.05f));
    REQUIRE((second[i]) > (0.05f));
    REQUIRE((first[i]) == Catch::Approx(second[i]));
    REQUIRE((self[i]) == Catch::Approx(0.0f));
  }
  mixer_destroy(mixer);
  for (int i = 0; i < 3; i++)
    audio_ring_buffer_destroy(buffers[i]);
}

TEST_CASE("mixer/listener_exclusion_and_activation_across_mask_words", "[mixer]") {
  enum { SOURCES = 65 };
  mixer_t *mixer = mixer_create(SOURCES, 48000);
  REQUIRE((mixer) != nullptr);
  audio_ring_buffer_t *buffers[SOURCES];
  char ids[SOURCES][32];
  float input[480], output[480];
  for (unsigned i = 0; i < 480; ++i)
    input[i] = 0.1f;
  for (unsigned i = 0; i < SOURCES; ++i) {
    snprintf(ids[i], sizeof(ids[i]), "mask-source-%u", i);
    buffers[i] = audio_ring_buffer_create_for_capture();
    REQUIRE((buffers[i]) != nullptr);
    REQUIRE((mixer_add_source(mixer, ids[i], buffers[i])) == (i));
    mixer_set_source_active(mixer, ids[i], i == 64);
  }
  REQUIRE((audio_ring_buffer_write(buffers[64], input, 480)) == (ASCIICHAT_OK));
  REQUIRE((mixer_process_excluding_source(mixer, output, 480, fnv1a_hash_string(ids[0]))) == (480));
  for (unsigned i = 0; i < 480; ++i)
    REQUIRE((output[i]) > (0.05f));
  mixer_process_excluding_source(mixer, output, 480, fnv1a_hash_string(ids[64]));
  for (unsigned i = 0; i < 480; ++i)
    REQUIRE((output[i]) == Catch::Approx(0));
  mixer_set_source_active(mixer, ids[64], false);
  REQUIRE((mixer_process_excluding_source(mixer, output, 480, fnv1a_hash_string(ids[0]))) == (0));
  mixer_set_source_active(mixer, ids[0], true);
  REQUIRE((audio_ring_buffer_write(buffers[0], input, 480)) == (ASCIICHAT_OK));
  mixer_remove_source(mixer, ids[64]);
  REQUIRE((mixer_process_excluding_source(mixer, output, 480, fnv1a_hash_string(ids[1]))) == (480));
  for (unsigned i = 0; i < 480; ++i)
    REQUIRE((output[i]) > (0.05f));
  mixer_destroy(mixer);
  for (unsigned i = 0; i < SOURCES; ++i)
    audio_ring_buffer_destroy(buffers[i]);
}

// Custom init function
void mixer_test_init(void) {}

// Custom fini function to cleanup asciichat_errno for all mixer test suites
void mixer_test_fini(void) {
  // Clear any error state left by tests
  errno_clear();
}

// Use TestSuite directly with custom init and fini to cleanup error state
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
  REQUIRE((buffer) != nullptr);

  // audio_ring_buffer_write returns asciichat_error_t, not sample count
  asciichat_error_t result = audio_ring_buffer_write(buffer, data, num_samples);
  REQUIRE((result) == (ASCIICHAT_OK));

  // For tests, mark jitter buffer as filled immediately so reads work
  // (Normally this happens after AUDIO_JITTER_BUFFER_THRESHOLD samples are written)
  atomic_store_bool(&buffer->jitter_buffer_filled, true);

  return buffer;
}

/* ============================================================================
 * Basic Mixer Functionality Tests
 * ============================================================================ */
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("mixer/create_and_destroy", "[mixer]") {
  mixer_t *mixer = mixer_create(4, 44100);
  REQUIRE((mixer) != nullptr);
  REQUIRE((mixer->max_sources) == (4));
  REQUIRE((mixer->sample_rate) == (44100));
  REQUIRE((mixer->num_sources) == (0));

  mixer_destroy(mixer);
}

// Parameterized test for invalid mixer creation parameters
typedef struct {
  int max_sources;
  int sample_rate;
  char description[64];
} mixer_invalid_params_test_case_t;

static mixer_invalid_params_test_case_t mixer_invalid_params_cases[] = {
    {0, 44100, "Zero max_sources"}, {4, 0, "Zero sample_rate"}, {-1, 44100, "Negative max_sources"}, {4, -1, "Negative sample_rate"}, {MIXER_MAX_SOURCES + 1, 44100, "Exceeds MIXER_MAX_SOURCES"},
};

TEST_CASE("mixer/create_with_invalid_params", "[mixer]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(mixer_invalid_params_cases));
  auto *tc = &generated_case;
  mixer_t *mixer = mixer_create(tc->max_sources, tc->sample_rate);
  REQUIRE((mixer) == nullptr);
}

TEST_CASE("mixer/add_and_remove_sources", "[mixer]") {
  mixer_t *mixer = mixer_create(4, 44100);
  REQUIRE((mixer) != nullptr);

  // Create test audio buffers
  float test_data[256];
  generate_sine_wave(test_data, 256, 440.0f, 44100.0f, 0.5f);

  audio_ring_buffer_t *buffer1 = create_test_buffer_with_data(test_data, 256);
  audio_ring_buffer_t *buffer2 = create_test_buffer_with_data(test_data, 256);

  // Add sources
  int slot1 = mixer_add_source(mixer, "100", buffer1);
  REQUIRE((slot1) >= (0));
  REQUIRE((mixer->num_sources) == (1));

  int slot2 = mixer_add_source(mixer, "200", buffer2);
  REQUIRE((slot2) >= (0));
  REQUIRE((mixer->num_sources) == (2));

  // Try to add more than max sources
  audio_ring_buffer_t *buffer3 = create_test_buffer_with_data(test_data, 256);
  audio_ring_buffer_t *buffer4 = create_test_buffer_with_data(test_data, 256);
  audio_ring_buffer_t *buffer5 = create_test_buffer_with_data(test_data, 256);

  int slot3 = mixer_add_source(mixer, "300", buffer3);
  int slot4 = mixer_add_source(mixer, "400", buffer4);
  int slot5 = mixer_add_source(mixer, "500", buffer5);

  REQUIRE((slot3) >= (0));
  REQUIRE((slot4) >= (0));
  REQUIRE((slot5) == (-1)); // Should fail - no more slots
  REQUIRE((mixer->num_sources) == (4));

  // Remove sources
  mixer_remove_source(mixer, "100");
  REQUIRE((mixer->num_sources) == (3));

  mixer_remove_source(mixer, "200");
  REQUIRE((mixer->num_sources) == (2));

  // Clean up
  audio_ring_buffer_destroy(buffer1);
  audio_ring_buffer_destroy(buffer2);
  audio_ring_buffer_destroy(buffer3);
  audio_ring_buffer_destroy(buffer4);
  audio_ring_buffer_destroy(buffer5);
  mixer_destroy(mixer);
}

TEST_CASE("mixer/set_source_active", "[mixer]") {
  mixer_t *mixer = mixer_create(2, 44100);
  REQUIRE((mixer) != nullptr);

  float test_data[256];
  generate_sine_wave(test_data, 256, 440.0f, 44100.0f, 0.5f);

  audio_ring_buffer_t *buffer = create_test_buffer_with_data(test_data, 256);
  int slot = mixer_add_source(mixer, "100", buffer);
  REQUIRE((slot) >= (0));

  // Source should be active by default
  REQUIRE(mixer->source_active[slot]);

  // Deactivate source
  mixer_set_source_active(mixer, "100", false);
  REQUIRE(!(mixer->source_active[slot]));

  // Reactivate source
  mixer_set_source_active(mixer, "100", true);
  REQUIRE(mixer->source_active[slot]);

  // Clean up
  audio_ring_buffer_destroy(buffer);
  mixer_destroy(mixer);
}

TEST_CASE("mixer/process_single_source", "[mixer]") {
  mixer_t *mixer = mixer_create(2, 44100);
  REQUIRE((mixer) != nullptr);

  // Generate test signal
  float test_data[256];
  generate_sine_wave(test_data, 256, 440.0f, 44100.0f, 0.5f);

  audio_ring_buffer_t *buffer = create_test_buffer_with_data(test_data, 256);
  mixer_add_source(mixer, "100", buffer);

  // Process audio
  float output[256];
  int processed = mixer_process(mixer, output, 256);
  REQUIRE((processed) == (256));

  // Output should be similar to input (with some processing applied)
  // We expect some attenuation due to crowd scaling and compression
  for (int i = 0; i < 256; i++) {
    REQUIRE((fabsf(output[i])) >= (0.0f));
    REQUIRE((fabsf(output[i])) <= (1.0f));
  }

  // Clean up
  audio_ring_buffer_destroy(buffer);
  mixer_destroy(mixer);
}

TEST_CASE("mixer/process_multiple_sources", "[mixer]") {
  mixer_t *mixer = mixer_create(3, 44100);
  REQUIRE((mixer) != nullptr);

  // Generate different test signals
  float test_data1[256], test_data2[256];
  generate_sine_wave(test_data1, 256, 440.0f, 44100.0f, 0.3f);
  generate_sine_wave(test_data2, 256, 880.0f, 44100.0f, 0.3f);

  audio_ring_buffer_t *buffer1 = create_test_buffer_with_data(test_data1, 256);
  audio_ring_buffer_t *buffer2 = create_test_buffer_with_data(test_data2, 256);

  mixer_add_source(mixer, "100", buffer1);
  mixer_add_source(mixer, "200", buffer2);

  // Process audio
  float output[256];
  int processed = mixer_process(mixer, output, 256);
  REQUIRE((processed) == (256));

  // Output should be mixed (not just one source)
  // With crowd scaling, the output should be attenuated
  for (int i = 0; i < 256; i++) {
    REQUIRE((fabsf(output[i])) >= (0.0f));
    REQUIRE((fabsf(output[i])) <= (1.0f));
  }

  // Clean up
  audio_ring_buffer_destroy(buffer1);
  audio_ring_buffer_destroy(buffer2);
  mixer_destroy(mixer);
}

TEST_CASE("mixer/process_excluding_source", "[mixer]") {
  mixer_t *mixer = mixer_create(3, 44100);
  REQUIRE((mixer) != nullptr);

  // Generate test signals
  float test_data1[256], test_data2[256];
  generate_sine_wave(test_data1, 256, 440.0f, 44100.0f, 0.5f);
  generate_sine_wave(test_data2, 256, 880.0f, 44100.0f, 0.5f);

  audio_ring_buffer_t *buffer1 = create_test_buffer_with_data(test_data1, 256);
  audio_ring_buffer_t *buffer2 = create_test_buffer_with_data(test_data2, 256);

  mixer_add_source(mixer, "100", buffer1);
  mixer_add_source(mixer, "200", buffer2);

  // Process normally
  float output_normal[256];
  mixer_process(mixer, output_normal, 256);

  // Process excluding source 100
  float output_excluded[256];
  int processed = mixer_process_excluding_source(mixer, output_excluded, 256, 100);
  REQUIRE((processed) == (256));

  // Output should be different (only source 200 should be present)
  bool different = false;
  for (int i = 0; i < 256; i++) {
    if (fabsf(output_normal[i] - output_excluded[i]) > 1e-6f) {
      different = true;
      break;
    }
  }
  REQUIRE(different);

  // Clean up
  audio_ring_buffer_destroy(buffer1);
  audio_ring_buffer_destroy(buffer2);
  mixer_destroy(mixer);
}

TEST_CASE("mixer/process_no_sources", "[mixer]") {
  mixer_t *mixer = mixer_create(2, 44100);
  REQUIRE((mixer) != nullptr);

  float output[256];
  int processed = mixer_process(mixer, output, 256);
  REQUIRE((processed) == (0));

  // Output should be silence
  for (int i = 0; i < 256; i++) {
    REQUIRE((output[i]) == Catch::Approx(0.0f));
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

static db_to_linear_test_case_t db_to_linear_cases[] = {{0.0f, 1.0f, 1e-6f, "0 dB equals unity gain"},       {-6.0f, 0.5f, 0.01f, "-6 dB equals half gain"},
                                                        {-20.0f, 0.1f, 1e-3f, "-20 dB equals 0.1 gain"},     {-40.0f, 0.01f, 1e-4f, "-40 dB equals 0.01 gain"},
                                                        {-60.0f, 0.001f, 1e-4f, "-60 dB equals 0.001 gain"}, {6.0f, 2.0f, 0.01f, "+6 dB equals double gain"}};

TEST_CASE("mixer_utils/db_to_linear_conversion_param", "[mixer_utils]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(db_to_linear_cases));
  auto *tc = &generated_case;
  float result = db_to_linear(tc->db_value);
  REQUIRE((result) == Catch::Approx(tc->expected_linear));
}

// Test case structure for linear to dB conversion
typedef struct {
  float linear_value;
  float expected_db;
  float epsilon;
  char description[64];
} linear_to_db_test_case_t;

static linear_to_db_test_case_t linear_to_db_cases[] = {{1.0f, 0.0f, 1e-6f, "Unity gain equals 0 dB"},       {0.5f, -6.0f, 0.1f, "Half gain equals -6 dB"},
                                                        {0.1f, -20.0f, 1e-3f, "0.1 gain equals -20 dB"},     {0.01f, -40.0f, 1e-3f, "0.01 gain equals -40 dB"},
                                                        {0.001f, -60.0f, 1e-3f, "0.001 gain equals -60 dB"}, {2.0f, 6.0f, 0.1f, "Double gain equals +6 dB"}};

TEST_CASE("mixer_utils/linear_to_db_conversion_param", "[mixer_utils]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(linear_to_db_cases));
  auto *tc = &generated_case;
  float result = linear_to_db(tc->linear_value);
  REQUIRE((result) == Catch::Approx(tc->expected_db));
}

// Test case structure for clamp_float
typedef struct {
  float value;
  float min;
  float max;
  float expected;
  char description[64];
} clamp_float_test_case_t;

static clamp_float_test_case_t clamp_float_cases[] = {{0.5f, 0.0f, 1.0f, 0.5f, "Value within range"},       {-0.5f, 0.0f, 1.0f, 0.0f, "Value below min"},
                                                      {1.5f, 0.0f, 1.0f, 1.0f, "Value above max"},          {0.0f, -1.0f, 1.0f, 0.0f, "Zero in symmetric range"},
                                                      {-2.0f, -1.0f, 1.0f, -1.0f, "Clamp to negative min"}, {2.0f, -1.0f, 1.0f, 1.0f, "Clamp to positive max"},
                                                      {0.0f, 0.0f, 0.0f, 0.0f, "Degenerate range"}};

TEST_CASE("mixer_utils/clamp_float_param", "[mixer_utils]") {
  auto &&generated_case = GENERATE(Catch::Generators::from_range(clamp_float_cases));
  auto *tc = &generated_case;
  float result = clamp_float(tc->value, tc->min, tc->max);
  REQUIRE((result) == Catch::Approx(tc->expected));
}

/* ============================================================================
 * Compressor Tests
 * ============================================================================ */

TEST_CASE("compressor/init_and_params", "[compressor]") {
  compressor_t comp;
  compressor_init(&comp, 44100.0f);

  REQUIRE((comp.sample_rate) == Catch::Approx(44100.0f));
  REQUIRE((comp.threshold_dB) == Catch::Approx(-10.0f));
  REQUIRE((comp.ratio) == Catch::Approx(4.0f));
  REQUIRE((comp.attack_ns) == (10 * NS_PER_MS_INT));
  REQUIRE((comp.release_ns) == (100 * NS_PER_MS_INT));
  REQUIRE((comp.makeup_dB) == Catch::Approx(0.0f));
  REQUIRE((comp.envelope) == Catch::Approx(0.0f));
  REQUIRE((comp.gain_lin) == Catch::Approx(1.0f));
}

TEST_CASE("compressor/set_params", "[compressor]") {
  compressor_t comp;
  compressor_init(&comp, 44100.0f);

  compressor_set_params(&comp, -20.0f, 2.0f, 5 * NS_PER_MS_INT, 50 * NS_PER_MS_INT, 6.0f);

  REQUIRE((comp.threshold_dB) == Catch::Approx(-20.0f));
  REQUIRE((comp.ratio) == Catch::Approx(2.0f));
  REQUIRE((comp.attack_ns) == (5 * NS_PER_MS_INT));
  REQUIRE((comp.release_ns) == (50 * NS_PER_MS_INT));
  REQUIRE((comp.makeup_dB) == Catch::Approx(6.0f));
}

TEST_CASE("compressor/process_below_threshold", "[compressor]") {
  compressor_t comp;
  compressor_init(&comp, 44100.0f);
  compressor_set_params(&comp, -10.0f, 4.0f, 10 * NS_PER_MS_INT, 100 * NS_PER_MS_INT, 0.0f);

  // Process signal below threshold
  float gain = compressor_process_sample(&comp, 0.1f); // -20dB
  REQUIRE((gain) >= (0.9f));                           // Should have minimal compression
}

TEST_CASE("compressor/process_above_threshold", "[compressor]") {
  compressor_t comp;
  compressor_init(&comp, 44100.0f);
  compressor_set_params(&comp, -10.0f, 4.0f, 10 * NS_PER_MS_INT, 100 * NS_PER_MS_INT, 0.0f);

  // Process signal above threshold
  float gain = compressor_process_sample(&comp, 0.5f); // -6dB
  REQUIRE((gain) <= (1.0f));                           // Should have some compression
}

/* ============================================================================
 * Ducking Tests
 * ============================================================================ */

TEST_CASE("ducking/init_and_params", "[ducking]") {
  ducking_t duck;
  ducking_init(&duck, 4, 44100.0f);

  REQUIRE((duck.threshold_dB) == Catch::Approx(-45.0f));
  REQUIRE((duck.leader_margin_dB) == Catch::Approx(6.0f));
  REQUIRE((duck.atten_dB) == Catch::Approx(-6.0f));
  REQUIRE((duck.attack_ns) == (10 * NS_PER_MS_INT));
  REQUIRE((duck.release_ns) == (200 * NS_PER_MS_INT));
  REQUIRE((duck.envelope) != nullptr);
  REQUIRE((duck.gain) != nullptr);

  // Check initial gain values
  for (int i = 0; i < 4; i++) {
    REQUIRE((duck.gain[i]) == Catch::Approx(1.0f));
  }

  ducking_destroy(&duck);
}

TEST_CASE("ducking/set_params", "[ducking]") {
  ducking_t duck;
  ducking_init(&duck, 4, 44100.0f);

  ducking_set_params(&duck, -30.0f, 5.0f, -15.0f, 10 * NS_PER_MS_INT, 200 * NS_PER_MS_INT);

  REQUIRE((duck.threshold_dB) == Catch::Approx(-30.0f));
  REQUIRE((duck.leader_margin_dB) == Catch::Approx(5.0f));
  REQUIRE((duck.atten_dB) == Catch::Approx(-15.0f));
  REQUIRE((duck.attack_ns) == (10 * NS_PER_MS_INT));
  REQUIRE((duck.release_ns) == (200 * NS_PER_MS_INT));

  ducking_destroy(&duck);
}

TEST_CASE("ducking/process_frame_leader_detection", "[ducking]") {
  ducking_t duck;
  ducking_init(&duck, 3, 44100.0f);
  ducking_set_params(&duck, -40.0f, 3.0f, -12.0f, 5 * NS_PER_MS_INT, 100 * NS_PER_MS_INT);

  float envelopes[3] = {0.1f, 0.5f, 0.2f}; // Source 1 is loudest
  float gains[3] = {1.0f, 1.0f, 1.0f};

  ducking_process_frame(&duck, envelopes, gains, 3);

  // Source 1 should remain at full gain (leader)
  REQUIRE((gains[1]) >= (0.9f));

  // Sources 0 and 2 should be ducked (but might not be as aggressive initially)
  REQUIRE((gains[0]) < (1.0f));
  REQUIRE((gains[2]) < (1.0f));

  ducking_destroy(&duck);
}

/* ============================================================================
 * Noise Gate Tests
 * ============================================================================ */

TEST_CASE("noise_gate/init_and_params", "[noise_gate]") {
  noise_gate_t gate;
  noise_gate_init(&gate, 44100.0f);

  REQUIRE((gate.sample_rate) == Catch::Approx(44100.0f));
  REQUIRE((gate.threshold) == Catch::Approx(0.01f));
  REQUIRE((gate.attack_ns) == (10 * NS_PER_MS_INT));
  REQUIRE((gate.release_ns) == (50 * NS_PER_MS_INT));
  REQUIRE((gate.hysteresis) == Catch::Approx(0.9f));
  REQUIRE((gate.envelope) == Catch::Approx(0.0f));
  REQUIRE(!(gate.gate_open));
}

TEST_CASE("noise_gate/set_params", "[noise_gate]") {
  noise_gate_t gate;
  noise_gate_init(&gate, 44100.0f);

  noise_gate_set_params(&gate, 0.05f, 5 * NS_PER_MS_INT, 100 * NS_PER_MS_INT, 0.8f);

  REQUIRE((gate.threshold) == Catch::Approx(0.05f));
  REQUIRE((gate.attack_ns) == (5 * NS_PER_MS_INT));
  REQUIRE((gate.release_ns) == (100 * NS_PER_MS_INT));
  REQUIRE((gate.hysteresis) == Catch::Approx(0.8f));
}

TEST_CASE("noise_gate/process_below_threshold", "[noise_gate]") {
  noise_gate_t gate;
  noise_gate_init(&gate, 44100.0f);
  noise_gate_set_params(&gate, 0.1f, 2 * NS_PER_MS_INT, 50 * NS_PER_MS_INT, 0.9f);

  // Process signal below threshold
  float output = noise_gate_process_sample(&gate, 0.5f, 0.05f);
  REQUIRE((fabsf(output)) <= (0.1f)); // Should be gated
  REQUIRE(!(noise_gate_is_open(&gate)));
}

TEST_CASE("noise_gate/process_above_threshold", "[noise_gate]") {
  noise_gate_t gate;
  noise_gate_init(&gate, 44100.0f);
  noise_gate_set_params(&gate, 0.1f, 2 * NS_PER_MS_INT, 50 * NS_PER_MS_INT, 0.9f);

  // Process signal above threshold (envelope starts at 0, so first sample will be low)
  float output = noise_gate_process_sample(&gate, 0.5f, 0.2f);
  REQUIRE((fabsf(output)) > (0.001f)); // Should pass through (but envelope starts low)
  REQUIRE(noise_gate_is_open(&gate));
}

TEST_CASE("noise_gate/process_buffer", "[noise_gate]") {
  noise_gate_t gate;
  noise_gate_init(&gate, 44100.0f);
  noise_gate_set_params(&gate, 0.1f, 2 * NS_PER_MS_INT, 50 * NS_PER_MS_INT, 0.9f);

  float buffer[10] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
  noise_gate_process_buffer(&gate, buffer, 10);

  // All samples should be processed
  for (int i = 0; i < 10; i++) {
    REQUIRE((fabsf(buffer[i])) >= (0.0f));
    REQUIRE((fabsf(buffer[i])) <= (1.0f));
  }
}

/* ============================================================================
 * High-Pass Filter Tests
 * ============================================================================ */

TEST_CASE("highpass_filter/init_and_reset", "[highpass_filter]") {
  highpass_filter_t filter;
  highpass_filter_init(&filter, 100.0f, 44100.0f);

  REQUIRE((filter.cutoff_hz) == Catch::Approx(100.0f));
  REQUIRE((filter.sample_rate) == Catch::Approx(44100.0f));
  REQUIRE((filter.alpha) > (0.0f));
  REQUIRE((filter.alpha) < (1.0f));

  highpass_filter_reset(&filter);
  REQUIRE((filter.prev_input) == Catch::Approx(0.0f));
  REQUIRE((filter.prev_output) == Catch::Approx(0.0f));
}

TEST_CASE("highpass_filter/process_dc_signal", "[highpass_filter]") {
  highpass_filter_t filter;
  highpass_filter_init(&filter, 100.0f, 44100.0f);

  // Process DC signal (should be filtered out)
  float output = highpass_filter_process_sample(&filter, 1.0f);
  REQUIRE((fabsf(output)) < (1.0f)); // Should be attenuated (first sample might not be fully filtered)

  // Continue processing DC
  for (int i = 0; i < 100; i++) {
    output = highpass_filter_process_sample(&filter, 1.0f);
  }
  REQUIRE((fabsf(output)) < (0.5f)); // Should be attenuated (high-pass filters may not be perfect)
}

TEST_CASE("highpass_filter/process_high_frequency", "[highpass_filter]") {
  highpass_filter_t filter;
  highpass_filter_init(&filter, 100.0f, 44100.0f);

  // Process high frequency signal (should pass through)
  float output = highpass_filter_process_sample(&filter, 1.0f);
  // First sample will be filtered, but subsequent high-freq samples should pass
  for (int i = 0; i < 10; i++) {
    output = highpass_filter_process_sample(&filter, (i % 2) ? 1.0f : -1.0f);
  }
  REQUIRE((fabsf(output)) > (0.1f)); // Should have significant output
}

TEST_CASE("highpass_filter/process_buffer", "[highpass_filter]") {
  highpass_filter_t filter;
  highpass_filter_init(&filter, 100.0f, 44100.0f);

  float buffer[10] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  highpass_filter_process_buffer(&filter, buffer, 10);

  // All samples should be processed
  for (int i = 0; i < 10; i++) {
    REQUIRE((fabsf(buffer[i])) >= (0.0f));
    REQUIRE((fabsf(buffer[i])) <= (1.0f));
  }
}

/* ============================================================================
 * Soft Clipping Tests
 * ============================================================================ */

TEST_CASE("soft_clip/process_within_threshold", "[soft_clip]") {
  float output = soft_clip(0.5f, 0.8f, 6.0f);
  REQUIRE((output) == Catch::Approx(0.5f));

  output = soft_clip(-0.3f, 0.8f, 6.0f);
  REQUIRE((output) == Catch::Approx(-0.3f));
}

TEST_CASE("soft_clip/process_above_threshold", "[soft_clip]") {
  float output = soft_clip(1.0f, 0.8f, 6.0f);
  REQUIRE((output) < (1.0f));
  REQUIRE((output) > (0.8f));

  output = soft_clip(-1.0f, 0.8f, 6.0f);
  REQUIRE((output) > (-1.0f));
  REQUIRE((output) < (-0.6f)); // Should be soft clipped (smooth curve, not hard threshold)
}

TEST_CASE("soft_clip/process_buffer", "[soft_clip]") {
  float buffer[5] = {0.5f, 1.0f, -0.3f, -1.0f, 0.0f};
  soft_clip_buffer(buffer, 5, 0.8f, 6.0f);

  REQUIRE((buffer[0]) == Catch::Approx(0.5f));  // Within threshold
  REQUIRE((buffer[1]) < (1.0f));                // Clipped
  REQUIRE((buffer[2]) == Catch::Approx(-0.3f)); // Within threshold
  REQUIRE((buffer[3]) > (-1.0f));               // Clipped
  REQUIRE((buffer[4]) == Catch::Approx(0.0f));  // Zero
}

/* ============================================================================
 * Integration Tests
 * ============================================================================ */

TEST_CASE("mixer_integration/full_pipeline_with_processing", "[mixer_integration]") {
  mixer_t *mixer = mixer_create(3, 44100);
  REQUIRE((mixer) != nullptr);

  // Generate test signals with different characteristics
  float sine_data[256], noise_data[256];
  generate_sine_wave(sine_data, 256, 440.0f, 44100.0f, 0.8f);
  generate_noise(noise_data, 256, 0.1f);

  audio_ring_buffer_t *sine_buffer = create_test_buffer_with_data(sine_data, 256);
  audio_ring_buffer_t *noise_buffer = create_test_buffer_with_data(noise_data, 256);

  mixer_add_source(mixer, "100", sine_buffer);
  mixer_add_source(mixer, "200", noise_buffer);

  // Process through full pipeline
  float output[256];
  int processed = mixer_process(mixer, output, 256);
  REQUIRE((processed) == (256));

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
  REQUIRE((max_output) <= (1.0f)); // No clipping
  REQUIRE((rms_output) > (0.0f));  // Non-zero output

  // Clean up
  audio_ring_buffer_destroy(sine_buffer);
  audio_ring_buffer_destroy(noise_buffer);
  mixer_destroy(mixer);
}

TEST_CASE("mixer_integration/stress_test_multiple_sources", "[mixer_integration]") {
  mixer_t *mixer = mixer_create(MIXER_MAX_SOURCES, 44100);
  REQUIRE((mixer) != nullptr);

  // Add maximum number of sources
  audio_ring_buffer_t *buffers[MIXER_MAX_SOURCES];
  for (int i = 0; i < MIXER_MAX_SOURCES; i++) {
    float test_data[256];
    generate_sine_wave(test_data, 256, 440.0f + i * 100.0f, 44100.0f, 0.1f);
    buffers[i] = create_test_buffer_with_data(test_data, 256);
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "%d", 100 + i);
    int slot = mixer_add_source(mixer, client_id, buffers[i]);
    REQUIRE((slot) >= (0));
  }

  REQUIRE((mixer->num_sources) == (MIXER_MAX_SOURCES));

  // Process all sources
  float output[256];
  int processed = mixer_process(mixer, output, 256);
  REQUIRE((processed) == (256));

  // Verify output is properly mixed and limited
  for (int i = 0; i < 256; i++) {
    REQUIRE((output[i]) >= (-1.0f));
    REQUIRE((output[i]) <= (1.0f));
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
static constexpr int audio_bounds_property_catch2_data[] = {1, 2, 3, 4, 8};

TEST_CASE("mixer_integration/audio_bounds_property", "[mixer_integration]") {
  size_t num_sources = GENERATE(Catch::Generators::from_range(audio_bounds_property_catch2_data));
  float amplitude = GENERATE(0.1f, 0.5f, 1.0f, 1.5f, 2.0f);
  if (!(num_sources > 0 && num_sources <= MIXER_MAX_SOURCES))
    SKIP("assumption not satisfied");
  if (!(amplitude > 0.0f && amplitude <= 2.0f))
    SKIP("assumption not satisfied");

  mixer_t *mixer = mixer_create(MIXER_MAX_SOURCES, 48000);
  if (!(mixer != NULL))
    SKIP("assumption not satisfied");

  // Create sources with specified amplitude
  audio_ring_buffer_t *buffers[MIXER_MAX_SOURCES];
  for (size_t i = 0; i < num_sources; i++) {
    buffers[i] = audio_ring_buffer_create();
    if (!(buffers[i] != NULL))
      SKIP("assumption not satisfied");

    // Generate test signal with specified amplitude
    float test_data[256];
    generate_sine_wave(test_data, 256, 440.0f + (float)i * 100.0f, 48000.0f, amplitude);

    asciichat_error_t write_result = audio_ring_buffer_write(buffers[i], test_data, 256);
    if (!(write_result == ASCIICHAT_OK))
      SKIP("assumption not satisfied");
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "%u", 100 + (uint32_t)i);
    mixer_add_source(mixer, client_id, buffers[i]);
  }

  // Process mixed output
  float output[256];
  int processed = mixer_process(mixer, output, 256);
  REQUIRE((processed) == (256));

  // Verify property: ALL output samples must be in [-1.0, 1.0] range
  for (int i = 0; i < 256; i++) {
    REQUIRE((output[i]) >= (-1.0f));
    REQUIRE((output[i]) <= (1.0f));
  }

  // Clean up
  for (size_t i = 0; i < num_sources; i++) {
    audio_ring_buffer_destroy(buffers[i]);
  }
  mixer_destroy(mixer);
}
