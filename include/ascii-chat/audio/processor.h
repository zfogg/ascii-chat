#pragma once

#include <ascii-chat/audio/spectral.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Streaming mono PCM enhancement. Each instance has one processing owner. */
typedef struct audio_processor audio_processor_t;
typedef struct {
  bool agc, compressor, noise_gate, highpass, lowpass;
} audio_processor_flags_t;

typedef struct {
  int sample_rate;
  audio_processor_flags_t flags;
  bool spectral_enabled;
  spectral_config_t spectral;
  int agc_level, agc_max_gain;
  float highpass_hz, lowpass_hz;
  float comp_threshold_db, comp_ratio, comp_makeup_db;
  uint64_t comp_attack_ns, comp_release_ns;
  float gate_threshold, gate_hysteresis;
  uint64_t gate_attack_ns, gate_release_ns;
} audio_processor_config_t;

/** Microphone defaults shared by native and browser hosts; no global options. */
audio_processor_config_t audio_processor_default_config(int sample_rate);
asciichat_error_t audio_processor_create(const audio_processor_config_t *config, audio_processor_t **out);
/** Arbitrary block sizes, in-place buffers, and no allocations or locks. AEC precedes this operation. */
asciichat_error_t audio_processor_process(audio_processor_t *processor, const float *input, float *output,
                                          size_t count);
void audio_processor_set_flags(audio_processor_t *processor, audio_processor_flags_t flags);
void audio_processor_reset(audio_processor_t *processor);
void audio_processor_destroy(audio_processor_t *processor);

#ifdef __cplusplus
}
#endif
