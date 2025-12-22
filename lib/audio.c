
/**
 * @file audio.c
 * @ingroup audio
 * @brief 🔊 Audio capture and playback using PortAudio with buffer management
 */

#include "audio.h"
#include "common.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "buffer_pool.h"
#include "options.h"
#include "platform/init.h" // For static_mutex_t
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef _WIN32
#include <unistd.h> // For dup, dup2, close, STDERR_FILENO
#include <fcntl.h>  // For O_WRONLY
#endif

// PortAudio initialization reference counter
// Tracks how many audio contexts are using PortAudio to avoid conflicts
static unsigned int g_pa_init_refcount = 0;
static static_mutex_t g_pa_refcount_mutex = STATIC_MUTEX_INIT;

static int input_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
  (void)outputBuffer;
  (void)timeInfo;
  (void)statusFlags;

  audio_context_t *ctx = (audio_context_t *)userData;
  const float *input = (const float *)inputBuffer;

  if (input != NULL && ctx->capture_buffer != NULL) {
    // Log some sample values for debugging
    static int callback_count = 0;
    callback_count++;

    // Log every 100 callbacks to avoid spam
    if (callback_count % 100 == 0) {
      // Calculate RMS (root mean square) to measure audio level
      float sum_squares = 0.0f;
      size_t total_samples = framesPerBuffer * AUDIO_CHANNELS;
      for (size_t i = 0; i < total_samples; i++) {
        sum_squares += input[i] * input[i];
      }
      float rms = sqrtf(sum_squares / total_samples);

      // Log first few samples and RMS
      log_debug_every(10000000,
                      "Audio input callback #%d: frames=%lu, channels=%d, first_samples=[%.4f, %.4f, %.4f], RMS=%.6f",
                      callback_count, framesPerBuffer, AUDIO_CHANNELS, input[0], input[1], input[2], rms);
    }

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

  static uint64_t callback_count = 0;
  callback_count++;

  if (output != NULL) {
    if (ctx->playback_buffer != NULL) {
      int samples_read = audio_ring_buffer_read(ctx->playback_buffer, output, framesPerBuffer * AUDIO_CHANNELS);

      // Debug: Log every 1000 callbacks (~10 seconds at 256 frames/callback, 44.1kHz)
      if (callback_count % 1000 == 0) {
        size_t available = audio_ring_buffer_available_read(ctx->playback_buffer);
        log_debug("Audio output callback #%llu: samples_read=%d/%lu, buffer_available=%zu", callback_count,
                  samples_read, framesPerBuffer * AUDIO_CHANNELS, available);
      }

      if (samples_read < (int)(framesPerBuffer * AUDIO_CHANNELS)) {
        SAFE_MEMSET(output + samples_read, (framesPerBuffer * AUDIO_CHANNELS - samples_read) * sizeof(float), 0,
                    (framesPerBuffer * AUDIO_CHANNELS - samples_read) * sizeof(float));
      }
    } else {
      log_warn_every(10000000, "Audio output callback: playback_buffer is NULL!");
      SAFE_MEMSET(output, framesPerBuffer * AUDIO_CHANNELS * sizeof(float), 0,
                  framesPerBuffer * AUDIO_CHANNELS * sizeof(float));
    }
  }

  return paContinue;
}

audio_ring_buffer_t *audio_ring_buffer_create(void) {
  size_t rb_size = sizeof(audio_ring_buffer_t);
  audio_ring_buffer_t *rb = (audio_ring_buffer_t *)buffer_pool_alloc(rb_size);

  if (!rb) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate audio ring buffer from buffer pool");
    return NULL;
  }

  SAFE_MEMSET(rb->data, sizeof(rb->data), 0, sizeof(rb->data));
  rb->write_index = 0;
  rb->read_index = 0;
  rb->jitter_buffer_filled = false;

  if (mutex_init(&rb->mutex) != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize audio ring buffer mutex");
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

asciichat_error_t audio_ring_buffer_write(audio_ring_buffer_t *rb, const float *data, int samples) {
  if (!rb || !data || samples <= 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: rb=%p, data=%p, samples=%d", rb, data, samples);

  // Validate samples doesn't exceed our buffer size
  if (samples > AUDIO_RING_BUFFER_SIZE) {
    return SET_ERRNO(ERROR_BUFFER, "Attempted to write %d samples, but buffer size is only %d", samples,
                     AUDIO_RING_BUFFER_SIZE);
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
    SAFE_MEMCPY(&rb->data[write_idx], samples * sizeof(float), data, samples * sizeof(float));
  } else {
    // Need to wrap around - copy in two chunks
    SAFE_MEMCPY(&rb->data[write_idx], remaining * sizeof(float), data, remaining * sizeof(float));
    SAFE_MEMCPY(&rb->data[0], (samples - remaining) * sizeof(float), &data[remaining],
                (samples - remaining) * sizeof(float));
  }

  rb->write_index = (write_idx + samples) % AUDIO_RING_BUFFER_SIZE;

  // Check if jitter buffer threshold has been reached
  if (!rb->jitter_buffer_filled) {
    int available_to_read = audio_ring_buffer_available_read(rb);
    if (available_to_read >= AUDIO_JITTER_BUFFER_THRESHOLD) {
      rb->jitter_buffer_filled = true;
    }
  }

  mutex_unlock(&rb->mutex);
  return ASCIICHAT_OK; // Success
}

size_t audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, size_t samples) {
  if (!rb || !data || samples <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: rb=%p, data=%p, samples=%d", rb, data, samples);
    return 0; // Return 0 samples read on error
  }

  mutex_lock(&rb->mutex);

  // Jitter buffer: don't read until initial fill threshold is reached
  if (!rb->jitter_buffer_filled) {
    mutex_unlock(&rb->mutex);
    return 0; // Return silence until buffer is filled
  }

  size_t available = audio_ring_buffer_available_read(rb);
  size_t to_read = (samples > available) ? available : samples;

  // Optimize: copy in chunks instead of one sample at a time
  int read_idx = rb->read_index;
  size_t remaining = AUDIO_RING_BUFFER_SIZE - read_idx;

  if (to_read <= remaining) {
    // Can copy in one chunk
    SAFE_MEMCPY(data, to_read * sizeof(float), &rb->data[read_idx], to_read * sizeof(float));
  } else {
    // Need to wrap around - copy in two chunks
    SAFE_MEMCPY(data, remaining * sizeof(float), &rb->data[read_idx], remaining * sizeof(float));
    SAFE_MEMCPY(&data[remaining], (to_read - remaining) * sizeof(float), &rb->data[0],
                (to_read - remaining) * sizeof(float));
  }

  rb->read_index = (read_idx + to_read) % AUDIO_RING_BUFFER_SIZE;

  mutex_unlock(&rb->mutex);
  return to_read; // Return number of samples actually read
}

size_t audio_ring_buffer_available_read(audio_ring_buffer_t *rb) {
  if (!rb)
    return 0;

  // NOTE: This function is safe to call without the mutex for approximate values.
  // The volatile indices provide atomic reads on aligned 32-bit integers.
  // For exact values during concurrent modification, hold rb->mutex first.
  int write_idx = rb->write_index;
  int read_idx = rb->read_index;

  if (write_idx >= read_idx) {
    return write_idx - read_idx;
  }

  return AUDIO_RING_BUFFER_SIZE - read_idx + write_idx;
}

size_t audio_ring_buffer_available_write(audio_ring_buffer_t *rb) {
  if (!rb) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: rb is NULL");
    return 0;
  }

  return AUDIO_RING_BUFFER_SIZE - audio_ring_buffer_available_read(rb) - 1;
}

asciichat_error_t audio_init(audio_context_t *ctx) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx is NULL");
  }

  SAFE_MEMSET(ctx, sizeof(audio_context_t), 0, sizeof(audio_context_t));

  if (mutex_init(&ctx->state_mutex) != 0) {
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize audio context mutex");
  }

  // Initialize PortAudio with reference counting
  static_mutex_lock(&g_pa_refcount_mutex);
  if (g_pa_init_refcount == 0) {
    // Suppress PortAudio backend probe errors (ALSA/JACK/OSS warnings)
    // These are harmless - PortAudio tries multiple backends until one works
    int stderr_fd_backup = -1;
    int devnull_fd = -1;
#ifndef _WIN32
    stderr_fd_backup = dup(STDERR_FILENO);
    devnull_fd = platform_open("/dev/null", O_WRONLY, 0);
    if (stderr_fd_backup >= 0 && devnull_fd >= 0) {
      dup2(devnull_fd, STDERR_FILENO);
    }
#endif

    PaError err = Pa_Initialize();

    // Restore stderr IMMEDIATELY so real errors are visible
#ifndef _WIN32
    if (stderr_fd_backup >= 0) {
      dup2(stderr_fd_backup, STDERR_FILENO);
      close(stderr_fd_backup);
    }
    if (devnull_fd >= 0) {
      close(devnull_fd);
    }
#endif

    if (err != paNoError) {
      static_mutex_unlock(&g_pa_refcount_mutex);
      mutex_destroy(&ctx->state_mutex);
      // stderr is restored, so this error will be visible
      return SET_ERRNO(ERROR_AUDIO, "Failed to initialize PortAudio: %s", Pa_GetErrorText(err));
    }

    log_debug("PortAudio initialized successfully (probe warnings suppressed)");
  }
  g_pa_init_refcount++;
  static_mutex_unlock(&g_pa_refcount_mutex);

  // Enumerate all audio devices for debugging
  int numDevices = Pa_GetDeviceCount();
  const size_t max_device_info_size = 4096; // Limit total device info size
  char device_names[max_device_info_size];
  int offset = 0;
  for (int i = 0; i < numDevices && offset < (int)sizeof(device_names) - 256; i++) {
    const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(i);
    if (deviceInfo && deviceInfo->name) {
      int remaining = sizeof(device_names) - offset;
      if (remaining < 256)
        break;

      int len = snprintf(&device_names[offset], remaining,
                         "\n  Device %d: %s (inputs=%d, outputs=%d, sample_rate=%.0f Hz)%s%s", i, deviceInfo->name,
                         deviceInfo->maxInputChannels, deviceInfo->maxOutputChannels, deviceInfo->defaultSampleRate,
                         (i == Pa_GetDefaultInputDevice()) ? " [DEFAULT INPUT]" : "",
                         (i == Pa_GetDefaultOutputDevice()) ? " [DEFAULT OUTPUT]" : "");
      if (len > 0 && len < remaining) {
        offset += len;
      } else {
        // Buffer full or error - stop here
        break;
      }
    }
  }
  device_names[offset] = '\0';
  if (offset > 0) {
    log_debug("PortAudio found %d audio devices:%s", numDevices, device_names);
  } else {
    log_warn("PortAudio found no audio devices");
  }

  ctx->capture_buffer = audio_ring_buffer_create();
  if (!ctx->capture_buffer) {
    // Decrement refcount and terminate if this was the only context
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create capture buffer");
  }

  ctx->playback_buffer = audio_ring_buffer_create();
  if (!ctx->playback_buffer) {
    audio_ring_buffer_destroy(ctx->capture_buffer);
    // Decrement refcount and terminate if this was the only context
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create playback buffer");
  }

  ctx->initialized = true;
  log_info("Audio system initialized successfully");
  return ASCIICHAT_OK;
}

void audio_destroy(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  mutex_lock(&ctx->state_mutex);

  if (ctx->recording) {
    audio_stop_capture(ctx);
  }

  if (ctx->playing) {
    audio_stop_playback(ctx);
  }

  audio_ring_buffer_destroy(ctx->capture_buffer);
  audio_ring_buffer_destroy(ctx->playback_buffer);

  // Terminate PortAudio only when last context is destroyed
  static_mutex_lock(&g_pa_refcount_mutex);
  if (g_pa_init_refcount > 0) {
    g_pa_init_refcount--;
    if (g_pa_init_refcount == 0) {
      Pa_Terminate();
    }
  }
  static_mutex_unlock(&g_pa_refcount_mutex);

  ctx->initialized = false;

  mutex_unlock(&ctx->state_mutex);
  mutex_destroy(&ctx->state_mutex);

  log_info("Audio system destroyed");
}

asciichat_error_t audio_start_capture(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, initialized=%d", ctx, ctx ? ctx->initialized : 0);
  }

  mutex_lock(&ctx->state_mutex);

  if (ctx->recording) {
    mutex_unlock(&ctx->state_mutex);
    return ASCIICHAT_OK;
  }

  PaStreamParameters inputParameters;

  // Use specified device or default
  if (opt_audio_device >= 0) {
    // User specified a device index
    inputParameters.device = opt_audio_device;
    if (inputParameters.device >= Pa_GetDeviceCount()) {
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_AUDIO, "Audio device index %d out of range (max %d)", opt_audio_device,
                       Pa_GetDeviceCount() - 1);
    }
  } else {
    // Use default device
    inputParameters.device = Pa_GetDefaultInputDevice();
  }

  if (inputParameters.device == paNoDevice) {
    mutex_unlock(&ctx->state_mutex);
    return SET_ERRNO(ERROR_AUDIO, "No input device available");
  }

  const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo(inputParameters.device);
  log_info("Opening audio input device %d: %s (%d channels, %.0f Hz)%s", inputParameters.device, deviceInfo->name,
           deviceInfo->maxInputChannels, deviceInfo->defaultSampleRate, (opt_audio_device < 0) ? " [DEFAULT]" : "");

  inputParameters.channelCount = AUDIO_CHANNELS;
  inputParameters.sampleFormat = paFloat32;
  inputParameters.suggestedLatency = deviceInfo->defaultLowInputLatency; // Low latency for real-time
  inputParameters.hostApiSpecificStreamInfo = NULL;

  PaError err = Pa_OpenStream(&ctx->input_stream, &inputParameters, NULL, AUDIO_SAMPLE_RATE, AUDIO_FRAMES_PER_BUFFER,
                              paClipOff, input_callback, ctx); // Disable clipping for raw samples

  if (err != paNoError) {
    SET_ERRNO(ERROR_AUDIO, "Failed to open input stream: %s", Pa_GetErrorText(err));
    mutex_unlock(&ctx->state_mutex);
    return ERROR_AUDIO;
  }

  err = Pa_StartStream(ctx->input_stream);
  if (err != paNoError) {
    Pa_CloseStream(ctx->input_stream);
    ctx->input_stream = NULL;
    mutex_unlock(&ctx->state_mutex);
    return SET_ERRNO(ERROR_AUDIO, "Failed to start input stream: %s", Pa_GetErrorText(err));
  }

  // Set real-time priority for better audio performance
  audio_set_realtime_priority();

  ctx->recording = true;
  mutex_unlock(&ctx->state_mutex);

  log_info("Audio capture started");
  return ASCIICHAT_OK;
}

asciichat_error_t audio_stop_capture(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->recording) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, initialized=%d, recording=%d", ctx,
                     ctx ? ctx->initialized : 0, ctx ? ctx->recording : 0);
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
  return ASCIICHAT_OK;
}

asciichat_error_t audio_start_playback(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, initialized=%d", ctx, ctx ? ctx->initialized : 0);
  }

  mutex_lock(&ctx->state_mutex);

  if (ctx->playing) {
    mutex_unlock(&ctx->state_mutex);
    return 0;
  }

  PaStreamParameters outputParameters;
  outputParameters.device = Pa_GetDefaultOutputDevice();
  if (outputParameters.device == paNoDevice) {
    SET_ERRNO(ERROR_AUDIO, "No default output device available");
    mutex_unlock(&ctx->state_mutex);
    return -1;
  }

  outputParameters.channelCount = AUDIO_CHANNELS;
  outputParameters.sampleFormat = paFloat32;
  outputParameters.suggestedLatency =
      Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency; // Low latency for real-time
  outputParameters.hostApiSpecificStreamInfo = NULL;

  PaError err = Pa_OpenStream(&ctx->output_stream, NULL, &outputParameters, AUDIO_SAMPLE_RATE, AUDIO_FRAMES_PER_BUFFER,
                              paClipOff, output_callback, ctx); // Disable clipping for raw samples

  if (err != paNoError) {
    mutex_unlock(&ctx->state_mutex);
    return SET_ERRNO(ERROR_AUDIO, "Failed to open output stream: %s", Pa_GetErrorText(err));
  }

  err = Pa_StartStream(ctx->output_stream);
  if (err != paNoError) {
    Pa_CloseStream(ctx->output_stream);
    ctx->output_stream = NULL;
    mutex_unlock(&ctx->state_mutex);
    return SET_ERRNO(ERROR_AUDIO, "Failed to start output stream: %s", Pa_GetErrorText(err));
  }

  // Set real-time priority for better audio performance
  audio_set_realtime_priority();

  ctx->playing = true;
  mutex_unlock(&ctx->state_mutex);

  log_info("Audio playback started");
  return ASCIICHAT_OK;
}

asciichat_error_t audio_stop_playback(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized || !ctx->playing) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, initialized=%d, playing=%d", ctx,
                     ctx ? ctx->initialized : 0, ctx ? ctx->playing : 0);
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
  return ASCIICHAT_OK;
}

asciichat_error_t audio_read_samples(audio_context_t *ctx, float *buffer, int num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples <= 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, buffer=%p, num_samples=%d", ctx, buffer,
                     num_samples);
  }

  // audio_ring_buffer_read now returns number of samples read, not error code
  int samples_read = audio_ring_buffer_read(ctx->capture_buffer, buffer, num_samples);
  return (samples_read >= 0) ? ASCIICHAT_OK : ERROR_AUDIO;
}

asciichat_error_t audio_write_samples(audio_context_t *ctx, const float *buffer, int num_samples) {
  if (!ctx || !ctx->initialized || !buffer || num_samples <= 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, buffer=%p, num_samples=%d", ctx, buffer,
                     num_samples);
  }

  // Debug: Log writes to playback buffer
  static uint64_t write_count = 0;
  write_count++;
  if (write_count % 100 == 0) {
    size_t available_before = audio_ring_buffer_available_read(ctx->playback_buffer);
    log_debug("audio_write_samples #%llu: writing %d samples, buffer_available_before=%zu", write_count, num_samples,
              available_before);
  }

  asciichat_error_t result = audio_ring_buffer_write(ctx->playback_buffer, buffer, num_samples);

  if (write_count % 100 == 0) {
    size_t available_after = audio_ring_buffer_available_read(ctx->playback_buffer);
    log_debug("audio_write_samples #%llu: after write, buffer_available_after=%zu, result=%d", write_count,
              available_after, result);
  }

  return result;
}

// Internal helper to list audio devices (input or output)
static asciichat_error_t audio_list_devices_internal(audio_device_info_t **out_devices, unsigned int *out_count,
                                                     bool list_inputs) {
  if (!out_devices || !out_count) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "audio_list_devices: invalid parameters");
  }

  *out_devices = NULL;
  *out_count = 0;

  // Check if PortAudio is already initialized by another audio context
  static_mutex_lock(&g_pa_refcount_mutex);
  bool pa_was_initialized = (g_pa_init_refcount > 0);

  // Initialize PortAudio only if not already initialized
  if (!pa_was_initialized) {
    // Suppress PortAudio backend probe errors (ALSA/JACK/OSS warnings)
    // These are harmless - PortAudio tries multiple backends until one works
    int stderr_fd_backup = -1;
    int devnull_fd = -1;
#ifndef _WIN32
    stderr_fd_backup = dup(STDERR_FILENO);
    devnull_fd = platform_open("/dev/null", O_WRONLY, 0);
    if (stderr_fd_backup >= 0 && devnull_fd >= 0) {
      dup2(devnull_fd, STDERR_FILENO);
    }
#endif

    PaError err = Pa_Initialize();

    // Restore stderr
#ifndef _WIN32
    if (stderr_fd_backup >= 0) {
      dup2(stderr_fd_backup, STDERR_FILENO);
      close(stderr_fd_backup);
    }
    if (devnull_fd >= 0) {
      close(devnull_fd);
    }
#endif

    if (err != paNoError) {
      static_mutex_unlock(&g_pa_refcount_mutex);
      return SET_ERRNO(ERROR_AUDIO, "Failed to initialize PortAudio: %s", Pa_GetErrorText(err));
    }
    g_pa_init_refcount = 1; // Set refcount to 1 for temporary initialization
  } else {
    // PortAudio is already initialized - increment refcount to prevent
    // termination while we're using it
    g_pa_init_refcount++;
  }
  static_mutex_unlock(&g_pa_refcount_mutex);

  int num_devices = Pa_GetDeviceCount();
  if (num_devices < 0) {
    // Decrement refcount and terminate only if we initialized PortAudio ourselves
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (!pa_was_initialized && g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    return SET_ERRNO(ERROR_AUDIO, "Failed to get device count: %s", Pa_GetErrorText(num_devices));
  }

  if (num_devices == 0) {
    // Decrement refcount and terminate only if we initialized PortAudio ourselves
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (!pa_was_initialized && g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    return ASCIICHAT_OK; // No devices found
  }

  // Get default device indices
  PaDeviceIndex default_input = Pa_GetDefaultInputDevice();
  PaDeviceIndex default_output = Pa_GetDefaultOutputDevice();

  // First pass: count matching devices
  unsigned int device_count = 0;
  for (int i = 0; i < num_devices; i++) {
    const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
    if (info) {
      bool matches = list_inputs ? (info->maxInputChannels > 0) : (info->maxOutputChannels > 0);
      if (matches) {
        device_count++;
      }
    }
  }

  if (device_count == 0) {
    // Decrement refcount and terminate only if we initialized PortAudio ourselves
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (!pa_was_initialized && g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    return ASCIICHAT_OK; // No matching devices
  }

  // Allocate device array
  audio_device_info_t *devices = SAFE_CALLOC(device_count, sizeof(audio_device_info_t), audio_device_info_t *);
  if (!devices) {
    // Decrement refcount and terminate only if we initialized PortAudio ourselves
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (!pa_was_initialized && g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate audio device info array");
  }

  // Second pass: populate device info
  unsigned int idx = 0;
  for (int i = 0; i < num_devices && idx < device_count; i++) {
    const PaDeviceInfo *info = Pa_GetDeviceInfo(i);
    if (!info)
      continue;

    bool match = list_inputs ? (info->maxInputChannels > 0) : (info->maxOutputChannels > 0);
    if (!match)
      continue;

    devices[idx].index = i;
    if (info->name) {
      SAFE_STRNCPY(devices[idx].name, info->name, AUDIO_DEVICE_NAME_MAX);
    } else {
      SAFE_STRNCPY(devices[idx].name, "<Unknown>", AUDIO_DEVICE_NAME_MAX);
    }
    devices[idx].max_input_channels = info->maxInputChannels;
    devices[idx].max_output_channels = info->maxOutputChannels;
    devices[idx].default_sample_rate = info->defaultSampleRate;
    devices[idx].is_default_input = (i == default_input);
    devices[idx].is_default_output = (i == default_output);
    idx++;
  }

  // Decrement refcount and terminate only if we initialized PortAudio ourselves
  static_mutex_lock(&g_pa_refcount_mutex);
  if (g_pa_init_refcount > 0) {
    g_pa_init_refcount--;
    if (!pa_was_initialized && g_pa_init_refcount == 0) {
      Pa_Terminate();
    }
  }
  static_mutex_unlock(&g_pa_refcount_mutex);

  *out_devices = devices;
  *out_count = idx;
  return ASCIICHAT_OK;
}

asciichat_error_t audio_list_input_devices(audio_device_info_t **out_devices, unsigned int *out_count) {
  return audio_list_devices_internal(out_devices, out_count, true);
}

asciichat_error_t audio_list_output_devices(audio_device_info_t **out_devices, unsigned int *out_count) {
  return audio_list_devices_internal(out_devices, out_count, false);
}

void audio_free_device_list(audio_device_info_t *devices) {
  SAFE_FREE(devices);
}

asciichat_error_t audio_set_realtime_priority(void) {
#if defined(__linux__)
  struct sched_param param;
  int policy = SCHED_FIFO;
  // Set high priority for real-time scheduling
  param.sched_priority = 80; // High priority (1-99 range)
  // Try to set real-time scheduling for current thread
  if (pthread_setschedparam(ascii_thread_self(), policy, &param) != 0) {
    return SET_ERRNO_SYS(
        ERROR_THREAD,
        "Failed to set real-time thread priority (try running with elevated privileges or configuring rtprio limits)");
  }
  log_info("✓ Audio thread real-time priority set to %d with SCHED_FIFO", param.sched_priority);
  return ASCIICHAT_OK;

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
    return SET_ERRNO(ERROR_THREAD, "Failed to set real-time thread priority on macOS");
  }
  log_info("✓ Audio thread real-time priority set on macOS");
  return ASCIICHAT_OK;

#elif defined(_WIN32)
  // Windows thread priority setting is handled differently
  // TODO: Implement Windows-specific thread priority setting
  log_warn("Setting a real-time thread priority is not yet implemented for the Windows platform");
  return ASCIICHAT_OK;

#else
  return SET_ERRNO(ERROR_THREAD, "Setting a real-time thread priority is not available for this platform");
#endif
}
