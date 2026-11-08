/** Browser-facing C interface implemented in audio.c. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct audio_processor audio_processor_t;

#ifdef __cplusplus
extern "C" {
#endif

audio_processor_t *wasm_audio_processor_create(int sample_rate, bool bypass);
audio_processor_t *wasm_audio_processor_create_stereo(int sample_rate, bool bypass);
asciichat_error_t wasm_audio_processor_process(audio_processor_t *processor, float *samples, size_t count);
void wasm_audio_processor_destroy(audio_processor_t *processor);

#ifdef __cplusplus
}
#endif
