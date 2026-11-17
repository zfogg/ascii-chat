#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/audio/spectral.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
#include <string.h>
TEST_CASE("spectral_stream/bypass_preserves_signal_latency_and_arbitrary_block_boundaries", "[spectral_stream]") {
  const int sizes[] = {512, 1024, 2048};
  for (size_t v = 0; v < 3; ++v) {
    int n = sizes[v], count = n * 8;
    spectral_config_t config = spectral_default_config(48000);
    config.fft_size = n;
    config.noise_gate = false;
    spectral_processor_t *whole = NULL, *chunked = NULL;
    REQUIRE((spectral_create(&config, &whole)) == (ASCIICHAT_OK));
    REQUIRE((spectral_create(&config, &chunked)) == (ASCIICHAT_OK));
    float *input = SAFE_MALLOC((size_t)count * sizeof(float), float *);
    float *output = SAFE_CALLOC((size_t)count, sizeof(float), float *);
    float *inplace = SAFE_MALLOC((size_t)count * sizeof(float), float *);
    for (int i = 0; i < count; ++i)
      input[i] = 0.5f * sinf(6.283185307f * 32 * i / n);
    memcpy(inplace, input, (size_t)count * sizeof(float));
    REQUIRE((spectral_process(whole, input, output, (size_t)count)) == (ASCIICHAT_OK));
    for (int i = 0; i < count; i += 137) {
      size_t length = (size_t)(count - i < 137 ? count - i : 137);
      REQUIRE((spectral_process(chunked, inplace + i, inplace + i, length)) == (ASCIICHAT_OK));
    }
    for (int i = 0; i < count; ++i) {
      REQUIRE((output[i]) == Catch::Approx(inplace[i]));
      if (i >= 2 * n)
        REQUIRE((output[i]) == Catch::Approx(input[i - n]));
    }
    size_t bins = 0;
    const float *spectrum = spectral_spectrum(whole, &bins);
    REQUIRE((bins) == ((size_t)n / 2 + 1));
    REQUIRE((spectrum[32]) == Catch::Approx(0.5f));
    spectral_reset(whole);
    memset(input, 0, (size_t)count * sizeof(float));
    REQUIRE((spectral_process(whole, input, output, (size_t)count)) == (ASCIICHAT_OK));
    for (int i = 0; i < count; ++i)
      REQUIRE((output[i]) == Catch::Approx(0));
    spectral_destroy(whole);
    spectral_destroy(chunked);
    SAFE_FREE(input);
    SAFE_FREE(output);
    SAFE_FREE(inplace);
  }
}

TEST_CASE("spectral_stream/processing_detects_pitch_and_keeps_all_effects_finite", "[spectral_stream]") {
  spectral_config_t config = spectral_default_config(48000);
  config.fft_size = 2048;
  config.compressor = config.adaptive_eq = config.pitch_correct = config.auto_gain = true;
  spectral_processor_t *processor = NULL;
  REQUIRE((spectral_create(&config, &processor)) == (ASCIICHAT_OK));
  float input[480], output[480];
  double energy = 0;
  for (int block = 0; block < 80; ++block) {
    for (int i = 0; i < 480; ++i)
      input[i] = 0.5f * sinf(6.283185307f * 450 * (block * 480 + i) / 48000.0f);
    REQUIRE((spectral_process(processor, input, output, 480)) == (ASCIICHAT_OK));
    for (int i = 0; i < 480; ++i) {
      REQUIRE(isfinite(output[i]));
      REQUIRE((fabsf(output[i])) < (2.0f));
      if (block > 20)
        energy += output[i] * output[i];
    }
  }
  REQUIRE((energy) > (0.01));
  REQUIRE((spectral_pitch_hz(processor)) == Catch::Approx(450));
  spectral_reset(processor);
  REQUIRE((spectral_pitch_hz(processor)) == (0));
  input[0] = NAN;
  input[1] = INFINITY;
  input[2] = -INFINITY;
  REQUIRE((spectral_process(processor, input, output, 480)) == (ASCIICHAT_OK));
  for (int i = 0; i < 480; ++i)
    REQUIRE(isfinite(output[i]));
  spectral_destroy(processor);
}

TEST_CASE("spectral_stream/invalid_configuration_and_buffers_are_rejected", "[spectral_stream]") {
  spectral_config_t good = spectral_default_config(48000);
  for (int i = 0; i < 7; ++i) {
    spectral_config_t config = good;
    switch (i) {
    case 0:
      config.sample_rate = 8000;
      break;
    case 1:
      config.fft_size = 1000;
      break;
    case 2:
      config.reduction_db = NAN;
      break;
    case 3:
      config.ratios[0] = 0.5f;
      break;
    case 4:
      config.thresholds_db[1] = 1;
      break;
    case 5:
      config.crossovers[1] = config.crossovers[0];
      break;
    case 6:
      config.crossovers[2] = 25000;
      break;
    }
    spectral_processor_t *processor = NULL;
    REQUIRE((spectral_create(&config, &processor)) == (ERROR_INVALID_PARAM));
    REQUIRE((processor) == nullptr);
  }
  spectral_processor_t *processor = NULL;
  REQUIRE((spectral_create(&good, &processor)) == (ASCIICHAT_OK));
  float sample = 0;
  REQUIRE((spectral_process(processor, NULL, NULL, 0)) == (ASCIICHAT_OK));
  REQUIRE((spectral_process(processor, NULL, &sample, 1)) == (ERROR_INVALID_PARAM));
  REQUIRE((spectral_process(NULL, &sample, &sample, 1)) == (ERROR_INVALID_PARAM));
  spectral_destroy(processor);
  spectral_destroy(NULL);
  spectral_reset(NULL);
  size_t bins = 10;
  REQUIRE((spectral_spectrum(NULL, &bins)) == nullptr);
  REQUIRE((bins) == (0));
}
