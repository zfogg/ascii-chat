#include "audio.h"
#include <ascii-chat/audio/processor.h>
#include <emscripten.h>

/** Browser AEC runs at microphone capture; this API owns the remaining DSP. */
audio_processor_t *browser_audio_processor_create(int sample_rate, bool bypass) {
  audio_processor_config_t config = audio_processor_default_config(sample_rate);
  if (bypass) {
    config.spectral_enabled = false;
    config.flags = (audio_processor_flags_t){0};
  }
  audio_processor_t *processor = NULL;
  if (audio_processor_create(&config, &processor) != ASCIICHAT_OK)
    return NULL;
  return processor;
}

/** Stereo microphone enhancement keeps independent filters and linked dynamics. */
audio_processor_t *browser_audio_processor_create_stereo(int sample_rate, bool bypass) {
  audio_processor_config_t config = audio_processor_default_config(sample_rate);
  config.channels = 2;
  if (bypass) {
    config.spectral_enabled = false;
    config.flags = (audio_processor_flags_t){0};
  }
  audio_processor_t *processor = NULL;
  if (audio_processor_create(&config, &processor) != ASCIICHAT_OK)
    return NULL;
  return processor;
}

asciichat_error_t browser_audio_processor_process(audio_processor_t *processor, float *samples, size_t count) {
  return audio_processor_process(processor, samples, samples, count);
}

void browser_audio_processor_destroy(audio_processor_t *processor) {
  audio_processor_destroy(processor);
}
