#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Mono streaming STFT processor. One owner thread per instance. */
typedef struct spectral_processor spectral_processor_t;
typedef struct {
  int sample_rate;
  int fft_size; /**< 512, 1024, or 2048; latency is exactly fft_size samples. */
  bool noise_gate;
  bool compressor;
  bool adaptive_eq;
  bool pitch_correct; /**< Optional monophonic correction to nearest equal-tempered semitone. */
  bool auto_gain;
  float reduction_db;
  float crossovers[3];
  float thresholds_db[4];
  float ratios[4];
} spectral_config_t;

spectral_config_t spectral_default_config(int sample_rate);
asciichat_error_t spectral_create(const spectral_config_t *config, spectral_processor_t **out);
void spectral_destroy(spectral_processor_t *processor);
void spectral_reset(spectral_processor_t *processor);
/** Arbitrary block sizes and in-place operation; no allocation or locks. */
asciichat_error_t spectral_process(spectral_processor_t *processor, const float *input, float *output, size_t count);
/** Latest input spectrum, normalized to sinusoidal peak amplitude; owner thread only. */
float spectral_pitch_hz(const spectral_processor_t *processor);
const float *spectral_spectrum(const spectral_processor_t *processor, size_t *bins);

#ifdef __cplusplus
}
#endif
