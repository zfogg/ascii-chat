#include "audio.h"
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static int input_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
  (void)outputBuffer;
  (void)timeInfo;
  (void)statusFlags;

  audio_context_t *ctx = (audio_context_t *)userData;
  const float *input = (const float *)inputBuffer;

  if (input != NULL && ctx->capture_buffer != NULL) {
    audio_ring_buffer_write(ctx->capture_buffer, input, framesPerBuffer * AUDIO_CHANNELS);
  }

  return paContinue;
}

static int output_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
                           void *userData) {
  (void)inputBuffer;
  (void)timeInfo;
  (void)statusFlags;

  audio_context_t *ctx = (audio_context_t *)userData;
  float *output = (float *)outputBuffer;

  if (output != NULL && ctx->playback_buffer != NULL) {
    int samples_read = audio_ring_buffer_read(ctx->playback_buffer, output, framesPerBuffer * AUDIO_CHANNELS);
    if (samples_read < (int)(framesPerBuffer * AUDIO_CHANNELS)) {
      memset(output + samples_read, 0, (framesPerBuffer * AUDIO_CHANNELS - samples_read) * sizeof(float));
    }
  } else {
    memset(output, 0, framesPerBuffer * AUDIO_CHANNELS * sizeof(float));
  }

  return paContinue;
}

audio_ring_buffer_t *audio_ring_buffer_create(void) {
  audio_ring_buffer_t *rb;
  SAFE_MALLOC(rb, sizeof(audio_ring_buffer_t), audio_ring_buffer_t *);

  if (!rb) {
    log_error("Failed to allocate audio ring buffer");
    return NULL;
  }

  memset(rb->data, 0, sizeof(rb->data));
  rb->write_index = 0;
  rb->read_index = 0;

  if (pthread_mutex_init(&rb->mutex, NULL) != 0) {
    log_error("Failed to initialize audio ring buffer mutex");
    free(rb);
    return NULL;
  }

  return rb;
}

void audio_ring_buffer_destroy(audio_ring_buffer_t *rb) {
  if (!rb)
    return;

  pthread_mutex_destroy(&rb->mutex);
  free(rb);
}

int audio_ring_buffer_write(audio_ring_buffer_t *rb, const float *data, int samples) {
  if (!rb || !data || samples <= 0)
    return 0;

  pthread_mutex_lock(&rb->mutex);

  int available = audio_ring_buffer_available_write(rb);
  int to_write = (samples > available) ? available : samples;

  for (int i = 0; i < to_write; i++) {
    rb->data[rb->write_index] = data[i];
    rb->write_index = (rb->write_index + 1) % AUDIO_RING_BUFFER_SIZE;
  }

  pthread_mutex_unlock(&rb->mutex);
  return to_write;
}

int audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, int samples) {
  if (!rb || !data || samples <= 0)
    return 0;

  pthread_mutex_lock(&rb->mutex);

  int available = audio_ring_buffer_available_read(rb);
  int to_read = (samples > available) ? available : samples;

  for (int i = 0; i < to_read; i++) {
    data[i] = rb->data[rb->read_index];
    rb->read_index = (rb->read_index + 1) % AUDIO_RING_BUFFER_SIZE;
  }

  pthread_mutex_unlock(&rb->mutex);
  return to_read;
}

int audio_ring_buffer_available_read(audio_ring_buffer_t *rb) {
  if (!rb)
    return 0;

  int write_idx = rb->write_index;
  int read_idx = rb->read_index;

  if (write_idx >= read_idx) {
    return write_idx - read_idx;
  } else {
    return AUDIO_RING_BUFFER_SIZE - read_idx + write_idx;
  }
}

int audio_ring_buffer_available_write(audio_ring_buffer_t *rb) {
  if (!rb)
    return 0;

  return AUDIO_RING_BUFFER_SIZE - audio_ring_buffer_available_read(rb) - 1;
}

int audio_init(audio_context_t *ctx) {
  if (!ctx) {
    log_error("NULL audio context");
    return -1;
  }

  memset(ctx, 0, sizeof(audio_context_t));

  if (pthread_mutex_init(&ctx->state_mutex, NULL) != 0) {
    log_error("Failed to initialize audio context mutex");
    return -1;
  }

  PaError err = Pa_Initialize();
  if (err != paNoError) {
    log_error("Failed to initialize PortAudio: %s", Pa_GetErrorText(err));
    pthread_mutex_destroy(&ctx->state_mutex);
    return -1;
  }

  ctx->capture_buffer = audio_ring_buffer_create();
  if (!ctx->capture_buffer) {
    log_error("Failed to create capture buffer");
    Pa_Terminate();
    pthread_mutex_destroy(&ctx->state_mutex);
    return -1;
  }

  ctx->playback_buffer = audio_ring_buffer_create();
  if (!ctx->playback_buffer) {
    log_error("Failed to create playback buffer");
    audio_ring_buffer_destroy(ctx->capture_buffer);
    Pa_Terminate();
    pthread_mutex_destroy(&ctx->state_mutex);
    return -1;
  }

  ctx->initialized = true;
  log_info("Audio system initialized successfully");
  return 0;
}

void audio_destroy(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized)
    return;

  pthread_mutex_lock(&ctx->state_mutex);

  if (ctx->recording) {
    audio_stop_capture(ctx);
  }

  if (ctx->playing) {
    audio_stop_playback(ctx);
  }

  audio_ring_buffer_destroy(ctx->capture_buffer);
  audio_ring_buffer_destroy(ctx->playback_buffer);

  Pa_Terminate();
  ctx->initialized = false;

  pthread_mutex_unlock(&ctx->state_mutex);
  pthread_mutex_destroy(&ctx->state_mutex);

  log_info("Audio system destroyed");
}

int audio_start_capture(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    log_error("Audio context not initialized");
    return -1;
  }

  pthread_mutex_lock(&ctx->state_mutex);

  if (ctx->recording) {
    pthread_mutex_unlock(&ctx->state_mutex);
    return 0;
  }

  PaStreamParameters inputParameters;
  inputParameters.device = Pa_GetDefaultInputDevice();
  if (inputParameters.device == paNoDevice) {
    log_error("No default input device available");
    pthread_mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  inputParameters.channelCount = AUDIO_CHANNELS;
  inputParameters.sampleFormat = paFloat32;
  inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
  inputParameters.hostApiSpecificStreamInfo = NULL;

  PaError err = Pa_OpenStream(&ctx->input_stream, &inputParameters, NULL, AUDIO_SAMPLE_RATE, AUDIO_FRAMES_PER_BUFFER,
                              paClipOff, input_callback, ctx);

  if (err != paNoError) {
    log_error("Failed to open input stream: %s", Pa_GetErrorText(err));
    pthread_mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  err = Pa_StartStream(ctx->input_stream);
  if (err != paNoError) {
    log_error("Failed to start input stream: %s", Pa_GetErrorText(err));
    Pa_CloseStream(ctx->input_stream);
    ctx->input_stream = NULL;
    pthread_mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  ctx->recording = true;
  pthread_mutex_unlock(&ctx->state_mutex);

  log_info("Audio capture started");
  return 0;
}

int audio_stop_capture(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->recording) {
    return 0;
  }

  pthread_mutex_lock(&ctx->state_mutex);

  if (ctx->input_stream) {
    Pa_StopStream(ctx->input_stream);
    Pa_CloseStream(ctx->input_stream);
    ctx->input_stream = NULL;
  }

  ctx->recording = false;
  pthread_mutex_unlock(&ctx->state_mutex);

  log_info("Audio capture stopped");
  return 0;
}

int audio_start_playback(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    log_error("Audio context not initialized");
    return -1;
  }

  pthread_mutex_lock(&ctx->state_mutex);

  if (ctx->playing) {
    pthread_mutex_unlock(&ctx->state_mutex);
    return 0;
  }

  PaStreamParameters outputParameters;
  outputParameters.device = Pa_GetDefaultOutputDevice();
  if (outputParameters.device == paNoDevice) {
    log_error("No default output device available");
    pthread_mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  outputParameters.channelCount = AUDIO_CHANNELS;
  outputParameters.sampleFormat = paFloat32;
  outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;

  PaError err = Pa_OpenStream(&ctx->output_stream, NULL, &outputParameters, AUDIO_SAMPLE_RATE, AUDIO_FRAMES_PER_BUFFER,
                              paClipOff, output_callback, ctx);

  if (err != paNoError) {
    log_error("Failed to open output stream: %s", Pa_GetErrorText(err));
    pthread_mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  err = Pa_StartStream(ctx->output_stream);
  if (err != paNoError) {
    log_error("Failed to start output stream: %s", Pa_GetErrorText(err));
    Pa_CloseStream(ctx->output_stream);
    ctx->output_stream = NULL;
    pthread_mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  ctx->playing = true;
  pthread_mutex_unlock(&ctx->state_mutex);

  log_info("Audio playback started");
  return 0;
}

int audio_stop_playback(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->playing) {
    return 0;
  }

  pthread_mutex_lock(&ctx->state_mutex);

  if (ctx->output_stream) {
    Pa_StopStream(ctx->output_stream);
    Pa_CloseStream(ctx->output_stream);
    ctx->output_stream = NULL;
  }

  ctx->playing = false;
  pthread_mutex_unlock(&ctx->state_mutex);

  log_info("Audio playback stopped");
  return 0;
}

int audio_read_samples(audio_context_t *ctx, float *buffer, int num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples <= 0) {
    return 0;
  }

  return audio_ring_buffer_read(ctx->capture_buffer, buffer, num_samples);
}

int audio_write_samples(audio_context_t *ctx, const float *buffer, int num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples <= 0) {
    return 0;
  }

  return audio_ring_buffer_write(ctx->playback_buffer, buffer, num_samples);
}