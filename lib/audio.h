#pragma once

#include <stdbool.h>
#include "platform.h"
#include <portaudio.h>

#ifdef __linux__
#include <sched.h>
#include <sys/resource.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <mach/thread_policy.h>
#include <mach/kern_return.h>
#endif

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_FRAMES_PER_BUFFER 256 // Reduced for lower latency
#define AUDIO_CHANNELS 1
#define AUDIO_BUFFER_SIZE (AUDIO_FRAMES_PER_BUFFER * AUDIO_CHANNELS)
// Include ringbuffer.h to get the audio_ring_buffer_t type
#include "ringbuffer.h"

// Note: AUDIO_RING_BUFFER_SIZE is defined in ringbuffer.h

typedef struct {
  PaStream *input_stream;
  PaStream *output_stream;
  audio_ring_buffer_t *capture_buffer;
  audio_ring_buffer_t *playback_buffer;
  bool initialized;
  bool recording;
  bool playing;
  mutex_t state_mutex;
} audio_context_t;

int audio_init(audio_context_t *ctx);
void audio_destroy(audio_context_t *ctx);

int audio_start_capture(audio_context_t *ctx);
int audio_stop_capture(audio_context_t *ctx);

int audio_start_playback(audio_context_t *ctx);
int audio_stop_playback(audio_context_t *ctx);

int audio_read_samples(audio_context_t *ctx, float *buffer, int num_samples);
int audio_write_samples(audio_context_t *ctx, const float *buffer, int num_samples);

int audio_set_realtime_priority(void);

audio_ring_buffer_t *audio_ring_buffer_create(void);
void audio_ring_buffer_destroy(audio_ring_buffer_t *rb);
int audio_ring_buffer_write(audio_ring_buffer_t *rb, const float *data, int samples);
int audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, int samples);
int audio_ring_buffer_available_read(audio_ring_buffer_t *rb);
int audio_ring_buffer_available_write(audio_ring_buffer_t *rb);
