#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

typedef struct audio_recording audio_recording_t;
typedef enum {
  AUDIO_RECORDING_MIC,
  AUDIO_RECORDING_MEDIA,
  AUDIO_RECORDING_REMOTE,
  AUDIO_RECORDING_SOURCE_COUNT
} audio_recording_source_t;

/* One session recording subscribes to PCM copies; playback and capture retain ownership. */
asciichat_error_t audio_recording_create(audio_recording_t **out);
void audio_recording_destroy(audio_recording_t *recording);
void audio_recording_start(audio_recording_t *recording, uint64_t epoch_ns);
void audio_recording_submit(audio_recording_source_t source, const float *samples, size_t count, uint64_t start_ns);
/* Reads a complete interval, filling missing inputs with silence and clipping the final mix. */
void audio_recording_read(audio_recording_t *recording, float *samples, size_t count);
