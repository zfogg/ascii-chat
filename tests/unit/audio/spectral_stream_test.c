#include <criterion/criterion.h>
#include <ascii-chat/audio/spectral.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <math.h>
#include <string.h>

TestSuite(spectral_stream);

Test(spectral_stream, bypass_preserves_signal_latency_and_arbitrary_block_boundaries) {
  const int sizes[] = {512, 1024, 2048};
  for (size_t v = 0; v < 3; ++v) {
    int n = sizes[v], count = n * 8;
    spectral_config_t config = spectral_default_config(48000);
    config.fft_size = n;
    config.noise_gate = false;
    spectral_processor_t *whole = NULL, *chunked = NULL;
    cr_assert_eq(spectral_create(&config, &whole), ASCIICHAT_OK);
    cr_assert_eq(spectral_create(&config, &chunked), ASCIICHAT_OK);
    float *input = SAFE_MALLOC((size_t)count * sizeof(float), float *);
    float *output = SAFE_CALLOC((size_t)count, sizeof(float), float *);
    float *inplace = SAFE_MALLOC((size_t)count * sizeof(float), float *);
    for (int i = 0; i < count; ++i)
      input[i] = 0.5f * sinf(6.283185307f * 32 * i / n);
    memcpy(inplace, input, (size_t)count * sizeof(float));
    cr_assert_eq(spectral_process(whole, input, output, (size_t)count), ASCIICHAT_OK);
    for (int i = 0; i < count; i += 137) {
      size_t length = (size_t)(count - i < 137 ? count - i : 137);
      cr_assert_eq(spectral_process(chunked, inplace + i, inplace + i, length), ASCIICHAT_OK);
    }
    for (int i = 0; i < count; ++i) {
      cr_assert_float_eq(output[i], inplace[i], 0.00001f);
      if (i >= 2 * n)
        cr_assert_float_eq(output[i], input[i - n], 0.0001f);
    }
    size_t bins = 0;
    const float *spectrum = spectral_spectrum(whole, &bins);
    cr_assert_eq(bins, (size_t)n / 2 + 1);
    cr_assert_float_eq(spectrum[32], 0.5f, 0.005f);
    spectral_reset(whole);
    memset(input, 0, (size_t)count * sizeof(float));
    cr_assert_eq(spectral_process(whole, input, output, (size_t)count), ASCIICHAT_OK);
    for (int i = 0; i < count; ++i)
      cr_assert_float_eq(output[i], 0, 0.00001f);
    spectral_destroy(whole);
    spectral_destroy(chunked);
    SAFE_FREE(input);
    SAFE_FREE(output);
    SAFE_FREE(inplace);
  }
}

Test(spectral_stream, processing_detects_pitch_and_keeps_all_effects_finite) {
  spectral_config_t config = spectral_default_config(48000);
  config.fft_size = 2048;
  config.compressor = config.adaptive_eq = config.pitch_correct = config.auto_gain = true;
  spectral_processor_t *processor = NULL;
  cr_assert_eq(spectral_create(&config, &processor), ASCIICHAT_OK);
  float input[480], output[480];
  double energy = 0;
  for (int block = 0; block < 80; ++block) {
    for (int i = 0; i < 480; ++i)
      input[i] = 0.5f * sinf(6.283185307f * 450 * (block * 480 + i) / 48000.0f);
    cr_assert_eq(spectral_process(processor, input, output, 480), ASCIICHAT_OK);
    for (int i = 0; i < 480; ++i) {
      cr_assert(isfinite(output[i]));
      cr_assert_lt(fabsf(output[i]), 2.0f);
      if (block > 20)
        energy += output[i] * output[i];
    }
  }
  cr_assert_gt(energy, 0.01);
  cr_assert_float_eq(spectral_pitch_hz(processor), 450, 3);
  spectral_reset(processor);
  cr_assert_eq(spectral_pitch_hz(processor), 0);
  input[0] = NAN;
  input[1] = INFINITY;
  input[2] = -INFINITY;
  cr_assert_eq(spectral_process(processor, input, output, 480), ASCIICHAT_OK);
  for (int i = 0; i < 480; ++i)
    cr_assert(isfinite(output[i]));
  spectral_destroy(processor);
}

Test(spectral_stream, invalid_configuration_and_buffers_are_rejected) {
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
    cr_assert_eq(spectral_create(&config, &processor), ERROR_INVALID_PARAM);
    cr_assert_null(processor);
  }
  spectral_processor_t *processor = NULL;
  cr_assert_eq(spectral_create(&good, &processor), ASCIICHAT_OK);
  float sample = 0;
  cr_assert_eq(spectral_process(processor, NULL, NULL, 0), ASCIICHAT_OK);
  cr_assert_eq(spectral_process(processor, NULL, &sample, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(spectral_process(NULL, &sample, &sample, 1), ERROR_INVALID_PARAM);
  spectral_destroy(processor);
  spectral_destroy(NULL);
  spectral_reset(NULL);
  size_t bins = 10;
  cr_assert_null(spectral_spectrum(NULL, &bins));
  cr_assert_eq(bins, 0);
}
