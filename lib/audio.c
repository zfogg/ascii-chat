#include "audio.h"
#include "common.h"
#include "buffer_pool.h"
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

  if (output != NULL) {
    if (ctx->playback_buffer != NULL) {
      int samples_read = audio_ring_buffer_read(ctx->playback_buffer, output, framesPerBuffer * AUDIO_CHANNELS);
      if (samples_read < (int)(framesPerBuffer * AUDIO_CHANNELS)) {
        memset(output + samples_read, 0, (framesPerBuffer * AUDIO_CHANNELS - samples_read) * sizeof(float));
      }
    } else {
      memset(output, 0, framesPerBuffer * AUDIO_CHANNELS * sizeof(float));
    }
  }

  return paContinue;
}

audio_ring_buffer_t *audio_ring_buffer_create(void) {
  size_t rb_size = sizeof(audio_ring_buffer_t);
  audio_ring_buffer_t *rb = (audio_ring_buffer_t *)buffer_pool_alloc(rb_size);

  if (!rb) {
    log_error("Failed to allocate audio ring buffer from buffer pool");
    return NULL;
  }

  memset(rb->data, 0, sizeof(rb->data));
  rb->write_index = 0;
  rb->read_index = 0;

  if (mutex_init(&rb->mutex) != 0) {
    log_error("Failed to initialize audio ring buffer mutex");
    buffer_pool_free(rb, sizeof(audio_ring_buffer_t));
    return NULL;
  }

  return rb;
}

void audio_ring_buffer_destroy(audio_ring_buffer_t *rb) {
  if (!rb)
    return;

  mutex_destroy(&rb->mutex);
  buffer_pool_free(rb, sizeof(audio_ring_buffer_t));
}

int audio_ring_buffer_write(audio_ring_buffer_t *rb, const float *data, int samples) {
  if (!rb || !data || samples <= 0)
    return 0;

  // Validate samples doesn't exceed our buffer size
  if (samples > AUDIO_RING_BUFFER_SIZE) {
    log_error("Attempted to write %d samples, but buffer size is only %d", samples, AUDIO_RING_BUFFER_SIZE);
    return 0;
  }

  mutex_lock(&rb->mutex);

  int available = audio_ring_buffer_available_write(rb);

  // If we need more space than available, drop old samples by advancing read index
  if (samples > available) {
    int samples_to_drop = samples - available;
    rb->read_index = (rb->read_index + samples_to_drop) % AUDIO_RING_BUFFER_SIZE;
    // Now we have enough space to write all samples
  }

  // Write all samples (we've made room if needed)
  int write_idx = rb->write_index;
  int remaining = AUDIO_RING_BUFFER_SIZE - write_idx;

  if (samples <= remaining) {
    // Can copy in one chunk
    memcpy(&rb->data[write_idx], data, samples * sizeof(float));
  } else {
    // Need to wrap around - copy in two chunks
    memcpy(&rb->data[write_idx], data, remaining * sizeof(float));
    memcpy(&rb->data[0], &data[remaining], (samples - remaining) * sizeof(float));
  }

  rb->write_index = (write_idx + samples) % AUDIO_RING_BUFFER_SIZE;

  mutex_unlock(&rb->mutex);
  return samples; // Always return that we wrote all samples
}

int audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, int samples) {
  if (!rb || !data || samples <= 0)
    return 0;

  mutex_lock(&rb->mutex);

  int available = audio_ring_buffer_available_read(rb);
  int to_read = (samples > available) ? available : samples;

  // Optimize: copy in chunks instead of one sample at a time
  int read_idx = rb->read_index;
  int remaining = AUDIO_RING_BUFFER_SIZE - read_idx;

  if (to_read <= remaining) {
    // Can copy in one chunk
    memcpy(data, &rb->data[read_idx], to_read * sizeof(float));
  } else {
    // Need to wrap around - copy in two chunks
    memcpy(data, &rb->data[read_idx], remaining * sizeof(float));
    memcpy(&data[remaining], &rb->data[0], (to_read - remaining) * sizeof(float));
  }

  rb->read_index = (read_idx + to_read) % AUDIO_RING_BUFFER_SIZE;

  mutex_unlock(&rb->mutex);
  return to_read;
}

int audio_ring_buffer_available_read(audio_ring_buffer_t *rb) {
  if (!rb)
    return 0;

  // Note: This function must be called with mutex already locked
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

  if (mutex_init(&ctx->state_mutex) != 0) {
    log_error("Failed to initialize audio context mutex");
    return -1;
  }

  PaError err = Pa_Initialize();
  if (err != paNoError) {
    log_error("Failed to initialize PortAudio: %s", Pa_GetErrorText(err));
    mutex_destroy(&ctx->state_mutex);
    return -1;
  }

  ctx->capture_buffer = audio_ring_buffer_create();
  if (!ctx->capture_buffer) {
    log_error("Failed to create capture buffer");
    Pa_Terminate();
    mutex_destroy(&ctx->state_mutex);
    return -1;
  }

  ctx->playback_buffer = audio_ring_buffer_create();
  if (!ctx->playback_buffer) {
    log_error("Failed to create playback buffer");
    audio_ring_buffer_destroy(ctx->capture_buffer);
    Pa_Terminate();
    mutex_destroy(&ctx->state_mutex);
    return -1;
  }

  ctx->initialized = true;
  log_info("Audio system initialized successfully");
  return 0;
}

void audio_destroy(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    log_info("DEBUG: audio_destroy early return - ctx=%p, initialized=%s", ctx,
             ctx ? (ctx->initialized ? "true" : "false") : "NULL");
    return;
  }

  log_info("DEBUG: audio_destroy starting - about to lock mutex");
  mutex_lock(&ctx->state_mutex);
  log_info("DEBUG: audio_destroy acquired mutex lock");

  if (ctx->recording) {
    audio_stop_capture(ctx);
  }

  if (ctx->playing) {
    audio_stop_playback(ctx);
  }

  audio_ring_buffer_destroy(ctx->capture_buffer);
  audio_ring_buffer_destroy(ctx->playback_buffer);

  log_info("DEBUG: audio_destroy about to call Pa_Terminate()");
  Pa_Terminate();
  log_info("DEBUG: audio_destroy Pa_Terminate() completed");
  ctx->initialized = false;

  log_info("DEBUG: audio_destroy about to unlock mutex");
  mutex_unlock(&ctx->state_mutex);
  log_info("DEBUG: audio_destroy unlocked mutex, about to destroy");
  mutex_destroy(&ctx->state_mutex);
  log_info("DEBUG: audio_destroy mutex destroyed");

  log_info("Audio system destroyed");
}

int audio_start_capture(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    log_error("Audio context not initialized");
    return -1;
  }

  mutex_lock(&ctx->state_mutex);

  if (ctx->recording) {
    mutex_unlock(&ctx->state_mutex);
    return 0;
  }

  PaStreamParameters inputParameters;
  inputParameters.device = Pa_GetDefaultInputDevice();
  if (inputParameters.device == paNoDevice) {
    log_error("No default input device available");
    mutex_unlock(&ctx->state_mutex);
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
    mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  err = Pa_StartStream(ctx->input_stream);
  if (err != paNoError) {
    log_error("Failed to start input stream: %s", Pa_GetErrorText(err));
    Pa_CloseStream(ctx->input_stream);
    ctx->input_stream = NULL;
    mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  // Set real-time priority for better audio performance
  audio_set_realtime_priority();

  ctx->recording = true;
  mutex_unlock(&ctx->state_mutex);

  log_info("Audio capture started");
  return 0;
}

int audio_stop_capture(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->recording) {
    return 0;
  }

  mutex_lock(&ctx->state_mutex);

  if (ctx->input_stream) {
    Pa_StopStream(ctx->input_stream);
    Pa_CloseStream(ctx->input_stream);
    ctx->input_stream = NULL;
  }

  ctx->recording = false;
  mutex_unlock(&ctx->state_mutex);

  log_info("Audio capture stopped");
  return 0;
}

int audio_start_playback(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    log_error("Audio context not initialized");
    return -1;
  }

  mutex_lock(&ctx->state_mutex);

  if (ctx->playing) {
    mutex_unlock(&ctx->state_mutex);
    return 0;
  }

  PaStreamParameters outputParameters;
  outputParameters.device = Pa_GetDefaultOutputDevice();
  if (outputParameters.device == paNoDevice) {
    log_error("No default output device available");
    mutex_unlock(&ctx->state_mutex);
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
    mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  err = Pa_StartStream(ctx->output_stream);
  if (err != paNoError) {
    log_error("Failed to start output stream: %s", Pa_GetErrorText(err));
    Pa_CloseStream(ctx->output_stream);
    ctx->output_stream = NULL;
    mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  // Set real-time priority for better audio performance
  audio_set_realtime_priority();

  ctx->playing = true;
  mutex_unlock(&ctx->state_mutex);

  log_info("Audio playback started");
  return 0;
}

int audio_stop_playback(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->playing) {
    return 0;
  }

  mutex_lock(&ctx->state_mutex);

  if (ctx->output_stream) {
    Pa_StopStream(ctx->output_stream);
    Pa_CloseStream(ctx->output_stream);
    ctx->output_stream = NULL;
  }

  ctx->playing = false;
  mutex_unlock(&ctx->state_mutex);

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

int audio_set_realtime_priority(void) {
#ifdef __linux__
  struct sched_param param;
  int policy = SCHED_FIFO;

  // Set high priority for real-time scheduling
  param.sched_priority = 80; // High priority (1-99 range)

  // Try to set real-time scheduling for current thread
#ifndef _WIN32
  if (pthread_setschedparam(ascii_thread_self(), policy, &param) != 0) {
    log_error(
        "Failed to set real-time thread priority (try running with elevated privileges or configuring rtprio limits)");
    return -1;
  }
#else
  // Windows thread priority setting is handled differently
  // TODO: Implement Windows-specific thread priority setting
  (void)policy;
  (void)param;
#endif

  log_info("✓ Audio thread real-time priority set to %d with SCHED_FIFO", param.sched_priority);
  return 0;
#elif defined(__APPLE__)
  // macOS: Use thread_policy_set for real-time scheduling
  thread_time_constraint_policy_data_t policy;
  policy.period = 0;
  policy.computation = 5000; // 5ms computation time
  policy.constraint = 10000; // 10ms constraint
  policy.preemptible = 0;    // Not preemptible

  kern_return_t result = thread_policy_set(mach_thread_self(), THREAD_TIME_CONSTRAINT_POLICY, (thread_policy_t)&policy,
                                           THREAD_TIME_CONSTRAINT_POLICY_COUNT);

  if (result != KERN_SUCCESS) {
    log_error("Failed to set real-time thread priority on macOS");
    return -1;
  }

  log_info("✓ Audio thread real-time priority set on macOS");
  return 0;
#else
  log_info("Real-time thread priority not implemented for this platform");
  return 0;
#endif
}