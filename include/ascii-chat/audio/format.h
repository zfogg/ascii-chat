#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Internal PCM is interleaved stereo. Counts passed to PCM APIs are float samples. */
#define AUDIO_PCM_CHANNELS 2
#define AUDIO_PCM_RATE 48000
#define AUDIO_PCM_SAMPLES_PER_SECOND (AUDIO_PCM_RATE * AUDIO_PCM_CHANNELS)
#define AUDIO_DEVICE_MAX_CHANNELS 64

#ifdef __cplusplus
extern "C" {
#endif

/** Route a zero-based mono input or adjacent stereo pair to internal PCM.
 * A negative first_channel downmixes all input channels to centered mono.
 * Input and output must not overlap. No allocation or locking is performed.
 */
asciichat_error_t audio_route_capture(const float *input, size_t frames, unsigned channels, int first_channel,
                                      unsigned selected_channels, float *output);
/** Convert interleaved mono/stereo PCM. In-place stereo-to-mono is supported. */
asciichat_error_t audio_convert_channels(const float *input, size_t frames, unsigned input_channels, float *output,
                                         unsigned output_channels);

typedef struct {
  double phase;
  float previous[AUDIO_PCM_CHANNELS];
  bool has_previous;
} audio_resampler_t;

/** Stateful interleaved resampling; capacities and return value are frames.
 * On insufficient capacity the state and output are left unchanged.
 */
size_t audio_resample_stereo(audio_resampler_t *state, const float *input, size_t frames, double input_rate,
                             float *output, size_t capacity, double output_rate);

#ifdef __cplusplus
}
#endif
