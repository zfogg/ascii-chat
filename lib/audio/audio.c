
/**
 * @file audio.c
 * @ingroup audio
 * @brief 🔊 Audio capture and playback using PortAudio with buffer management
 */

#include "audio/audio.h"
#include "audio/client_audio_pipeline.h"
#include "common.h"
#include "util/endian.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "buffer_pool.h"
#include "options.h"
#include "platform/init.h" // For static_mutex_t
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>

#ifndef _WIN32
#include <unistd.h> // For dup, dup2, close, STDERR_FILENO
#include <fcntl.h>  // For O_WRONLY
#endif

// PortAudio initialization reference counter
// Tracks how many audio contexts are using PortAudio to avoid conflicts
static unsigned int g_pa_init_refcount = 0;
static static_mutex_t g_pa_refcount_mutex = STATIC_MUTEX_INIT;

/**
 * Full-duplex callback - handles BOTH input and output in one callback.
 *
 * This is the PROFESSIONAL approach for AEC3:
 * - Render and capture happen at the EXACT same instant
 * - No ring buffers for AEC3, no timing mismatch
 * - AEC3 processing done inline with perfect synchronization
 */
static int duplex_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
                           void *userData) {
  (void)timeInfo;

  audio_context_t *ctx = (audio_context_t *)userData;
  const float *input = (const float *)inputBuffer;
  float *output = (float *)outputBuffer;
  size_t num_samples = framesPerBuffer * AUDIO_CHANNELS;

  // Silence on shutdown
  if (atomic_load(&ctx->shutting_down)) {
    if (output) {
      SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
    }
    return paContinue;
  }

  // Log status flags
  if (statusFlags != 0) {
    if (statusFlags & paOutputUnderflow) {
      log_warn_every(LOG_RATE_FAST, "PortAudio output underflow");
    }
    if (statusFlags & paInputOverflow) {
      log_warn_every(LOG_RATE_FAST, "PortAudio input overflow");
    }
  }

  // STEP 1: Read from jitter buffer → output (speaker)
  size_t samples_read = 0;
  if (output && ctx->playback_buffer) {
    samples_read = audio_ring_buffer_read(ctx->playback_buffer, output, num_samples);
    if (samples_read == 0) {
      SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
    }
  } else if (output) {
    SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
  }

  // STEP 2: Process AEC3 inline - render and capture at EXACT same time
  if (ctx->audio_pipeline && input && output) {
    // Allocate processed buffer on stack
    float *processed = (float *)alloca(num_samples * sizeof(float));

    // This does: AnalyzeRender(output) + ProcessCapture(input) + filters + compressor
    // All in one call, perfect synchronization, no ring buffer
    client_audio_pipeline_process_duplex(
        (client_audio_pipeline_t *)ctx->audio_pipeline, // NOLINT(readability-suspicious-call-argument)
        output,                                         // render_samples (what's playing to speakers)
        (int)num_samples,                               // render_count
        input,                                          // capture_samples (microphone input)
        (int)num_samples,                               // capture_count
        processed                                       // processed_output (processed capture)
    );

    // Write processed capture to ring buffer for encoding thread
    if (ctx->capture_buffer) {
      audio_ring_buffer_write(ctx->capture_buffer, processed, (int)num_samples);
    }
  } else if (input && ctx->capture_buffer) {
    // No pipeline - write raw capture
    audio_ring_buffer_write(ctx->capture_buffer, input, (int)num_samples);
  }

  return paContinue;
}

/**
 * Simple linear interpolation resampler.
 * Resamples from src_rate to dst_rate using linear interpolation.
 * See audio.h for full documentation.
 */
void resample_linear(const float *src, size_t src_samples, float *dst, size_t dst_samples, double src_rate,
                     double dst_rate) {
  if (src_samples == 0 || dst_samples == 0) {
    SAFE_MEMSET(dst, dst_samples * sizeof(float), 0, dst_samples * sizeof(float));
    return;
  }

  double ratio = src_rate / dst_rate;

  for (size_t i = 0; i < dst_samples; i++) {
    double src_pos = (double)i * ratio;
    size_t idx0 = (size_t)src_pos;
    size_t idx1 = idx0 + 1;
    double frac = src_pos - (double)idx0;

    // Clamp indices to valid range
    if (idx0 >= src_samples)
      idx0 = src_samples - 1;
    if (idx1 >= src_samples)
      idx1 = src_samples - 1;

    // Linear interpolation
    dst[i] = (float)((1.0 - frac) * src[idx0] + frac * src[idx1]);
  }
}

/**
 * Separate output callback - handles playback only.
 * Used when full-duplex mode is unavailable (e.g., different sample rates).
 * Resamples from internal 48kHz to output device rate if needed.
 * Copies render samples to render_buffer for AEC3 reference.
 */
static int output_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
                           void *userData) {
  (void)inputBuffer;
  (void)timeInfo;

  audio_context_t *ctx = (audio_context_t *)userData;
  float *output = (float *)outputBuffer;
  size_t num_output_samples = framesPerBuffer * AUDIO_CHANNELS;

  // Silence on shutdown
  if (atomic_load(&ctx->shutting_down)) {
    if (output) {
      SAFE_MEMSET(output, num_output_samples * sizeof(float), 0, num_output_samples * sizeof(float));
    }
    return paContinue;
  }

  if (statusFlags & paOutputUnderflow) {
    log_warn_every(LOG_RATE_FAST, "PortAudio output underflow (separate stream)");
  }

  // Read from playback buffer → output (speaker)
  if (output && ctx->playback_buffer) {
    // Check if we need to resample (buffer is at sample_rate, output is at output_device_rate)
    bool needs_resample =
        (ctx->output_device_rate > 0 && ctx->sample_rate > 0 && ctx->output_device_rate != ctx->sample_rate);

    if (needs_resample) {
      // Calculate how many samples we need from the 48kHz buffer to produce num_output_samples at output rate
      double ratio = ctx->sample_rate / ctx->output_device_rate;                 // e.g., 48000/44100 = 1.088
      size_t num_src_samples = (size_t)((double)num_output_samples * ratio) + 2; // +2 for interpolation safety

      // Read from buffer at internal sample rate
      float *src_buffer = (float *)alloca(num_src_samples * sizeof(float));
      size_t samples_read = audio_ring_buffer_read(ctx->playback_buffer, src_buffer, num_src_samples);

      if (samples_read == 0) {
        SAFE_MEMSET(output, num_output_samples * sizeof(float), 0, num_output_samples * sizeof(float));
      } else {
        // Resample from 48kHz to output device rate
        resample_linear(src_buffer, samples_read, output, num_output_samples, ctx->sample_rate,
                        ctx->output_device_rate);
      }

      // Copy resampled output to render buffer for AEC3 reference
      // (AEC3 needs to know what's actually playing to speakers)
      if (ctx->render_buffer) {
        audio_ring_buffer_write(ctx->render_buffer, output, (int)num_output_samples);
      }
    } else {
      // No resampling needed - direct read
      size_t samples_read = audio_ring_buffer_read(ctx->playback_buffer, output, num_output_samples);
      if (samples_read == 0) {
        SAFE_MEMSET(output, num_output_samples * sizeof(float), 0, num_output_samples * sizeof(float));
      }

      // Copy to render buffer for AEC3 reference
      if (ctx->render_buffer) {
        audio_ring_buffer_write(ctx->render_buffer, output, (int)num_output_samples);
      }
    }
  } else if (output) {
    SAFE_MEMSET(output, num_output_samples * sizeof(float), 0, num_output_samples * sizeof(float));
  }

  return paContinue;
}

/**
 * Separate input callback - handles capture only.
 * Used when full-duplex mode is unavailable (e.g., different sample rates).
 * Gets render reference from render_buffer for AEC3 processing.
 * Resamples render from output_device_rate to input_device_rate if needed.
 */
static int input_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
  (void)outputBuffer;
  (void)timeInfo;

  audio_context_t *ctx = (audio_context_t *)userData;
  const float *input = (const float *)inputBuffer;
  size_t num_samples = framesPerBuffer * AUDIO_CHANNELS;

  // Silence on shutdown
  if (atomic_load(&ctx->shutting_down)) {
    return paContinue;
  }

  if (statusFlags & paInputOverflow) {
    log_warn_every(LOG_RATE_FAST, "PortAudio input overflow (separate stream)");
  }

  // Process AEC3 with render reference from render_buffer
  if (ctx->audio_pipeline && input && ctx->render_buffer) {
    // Check if we need to resample render (render_buffer is at output_device_rate, input is at input_device_rate)
    bool needs_resample = (ctx->output_device_rate > 0 && ctx->input_device_rate > 0 &&
                           ctx->output_device_rate != ctx->input_device_rate);

    float *render = (float *)alloca(num_samples * sizeof(float));

    if (needs_resample) {
      // Render buffer is at output_device_rate, we need samples at input_device_rate
      // Calculate how many samples to read from render buffer
      double ratio = ctx->output_device_rate / ctx->input_device_rate; // e.g., 44100/48000 = 0.919
      size_t num_render_samples = (size_t)((double)num_samples * ratio) + 2;

      float *render_raw = (float *)alloca(num_render_samples * sizeof(float));
      size_t render_read = audio_ring_buffer_read(ctx->render_buffer, render_raw, num_render_samples);

      if (render_read == 0) {
        SAFE_MEMSET(render, num_samples * sizeof(float), 0, num_samples * sizeof(float));
      } else {
        // Resample from output_device_rate to input_device_rate
        resample_linear(render_raw, render_read, render, num_samples, ctx->output_device_rate, ctx->input_device_rate);
      }
    } else {
      // No resampling needed - direct read
      size_t render_samples = audio_ring_buffer_read(ctx->render_buffer, render, num_samples);
      if (render_samples < num_samples) {
        // Not enough render samples - zero-pad
        SAFE_MEMSET(render + render_samples, (num_samples - render_samples) * sizeof(float), 0,
                    (num_samples - render_samples) * sizeof(float));
      }
    }

    // Process through AEC3
    float *processed = (float *)alloca(num_samples * sizeof(float));
    client_audio_pipeline_process_duplex((client_audio_pipeline_t *)ctx->audio_pipeline, render,
                                         (int)num_samples,        // render = what's playing to speakers
                                         input, (int)num_samples, // capture = microphone input
                                         processed                // output = processed capture
    );

    // Write processed capture to ring buffer for encoding thread
    if (ctx->capture_buffer) {
      audio_ring_buffer_write(ctx->capture_buffer, processed, (int)num_samples);
    }
  } else if (input && ctx->capture_buffer) {
    // No pipeline - write raw capture
    audio_ring_buffer_write(ctx->capture_buffer, input, (int)num_samples);
  }

  return paContinue;
}

// Forward declaration for internal helper function
static audio_ring_buffer_t *audio_ring_buffer_create_internal(bool jitter_buffer_enabled);

static audio_ring_buffer_t *audio_ring_buffer_create_internal(bool jitter_buffer_enabled) {
  size_t rb_size = sizeof(audio_ring_buffer_t);
  audio_ring_buffer_t *rb = (audio_ring_buffer_t *)buffer_pool_alloc(rb_size);

  if (!rb) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate audio ring buffer from buffer pool");
    return NULL;
  }

  SAFE_MEMSET(rb->data, sizeof(rb->data), 0, sizeof(rb->data));
  rb->write_index = 0;
  rb->read_index = 0;
  // For capture buffers (jitter_buffer_enabled=false), mark as already filled to bypass jitter logic
  // For playback buffers (jitter_buffer_enabled=true), start unfilled to wait for threshold
  rb->jitter_buffer_filled = !jitter_buffer_enabled;
  rb->crossfade_samples_remaining = 0;
  rb->crossfade_fade_in = false;
  rb->last_sample = 0.0f;
  rb->underrun_count = 0;
  rb->jitter_buffer_enabled = jitter_buffer_enabled;

  if (mutex_init(&rb->mutex) != 0) {
    SET_ERRNO(ERROR_THREAD, "Failed to initialize audio ring buffer mutex");
    buffer_pool_free(rb, sizeof(audio_ring_buffer_t));
    return NULL;
  }

  return rb;
}

audio_ring_buffer_t *audio_ring_buffer_create(void) {
  return audio_ring_buffer_create_internal(true); // Default: enable jitter buffering for playback
}

audio_ring_buffer_t *audio_ring_buffer_create_for_capture(void) {
  return audio_ring_buffer_create_internal(false); // Disable jitter buffering for capture
}

void audio_ring_buffer_destroy(audio_ring_buffer_t *rb) {
  if (!rb)
    return;

  mutex_destroy(&rb->mutex);
  buffer_pool_free(rb, sizeof(audio_ring_buffer_t));
}

void audio_ring_buffer_clear(audio_ring_buffer_t *rb) {
  if (!rb)
    return;

  mutex_lock(&rb->mutex);
  // Reset buffer to empty state (no audio to play = silence at shutdown)
  rb->write_index = 0;
  rb->read_index = 0;
  rb->last_sample = 0.0f;
  // Clear the actual data to zeros to prevent any stale audio
  SAFE_MEMSET(rb->data, sizeof(rb->data), 0, sizeof(rb->data));
  mutex_unlock(&rb->mutex);
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
  // Note: Removed aggressive buffer discard logic that was causing clicks.
  // Instead, we let the normal overflow handling below drop incoming samples
  // when the buffer is full. This causes less audible artifacts than discarding
  // large chunks of buffered audio.

  // Now write the new samples
  int samples_to_write = samples;
  if (samples > available) {
    // Still not enough space after cleanup - drop some incoming samples
    int samples_dropped = samples - available;
    samples_to_write = available;
    log_warn_every(LOG_RATE_FAST, "Audio buffer overflow: dropping %d of %d incoming samples (buffer_used=%d/%d)",
                   samples_dropped, samples, AUDIO_RING_BUFFER_SIZE - available, AUDIO_RING_BUFFER_SIZE);
  }

  // Write only the samples that fit (preserves existing data integrity)
  if (samples_to_write > 0) {
    int write_idx = rb->write_index;
    int remaining = AUDIO_RING_BUFFER_SIZE - write_idx;

    if (samples_to_write <= remaining) {
      // Can copy in one chunk
      SAFE_MEMCPY(&rb->data[write_idx], samples_to_write * sizeof(float), data, samples_to_write * sizeof(float));
    } else {
      // Need to wrap around - copy in two chunks
      SAFE_MEMCPY(&rb->data[write_idx], remaining * sizeof(float), data, remaining * sizeof(float));
      SAFE_MEMCPY(&rb->data[0], (samples_to_write - remaining) * sizeof(float), &data[remaining],
                  (samples_to_write - remaining) * sizeof(float));
    }

    rb->write_index = (write_idx + samples_to_write) % AUDIO_RING_BUFFER_SIZE;
  }

  // Note: jitter buffer fill check is now done in read function for better control

  mutex_unlock(&rb->mutex);
  return ASCIICHAT_OK; // Success
}

size_t audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, size_t samples) {
  if (!rb || !data || samples <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: rb=%p, data=%p, samples=%d", rb, data, samples);
    return 0; // Return 0 samples read on error
  }

  mutex_lock(&rb->mutex);

  size_t available = audio_ring_buffer_available_read(rb);

  // Jitter buffer: don't read until initial fill threshold is reached
  // (only for playback buffers - capture buffers have jitter_buffer_enabled = false)
  if (!rb->jitter_buffer_filled && rb->jitter_buffer_enabled) {
    // First, check if we're in the middle of a fade-out that needs to continue
    // This happens when fade-out spans multiple buffer reads
    if (!rb->crossfade_fade_in && rb->crossfade_samples_remaining > 0) {
      // Continue fade-out from where we left off
      int fade_start = AUDIO_CROSSFADE_SAMPLES - rb->crossfade_samples_remaining;
      size_t fade_samples =
          (samples < (size_t)rb->crossfade_samples_remaining) ? samples : (size_t)rb->crossfade_samples_remaining;
      float last = rb->last_sample;
      for (size_t i = 0; i < fade_samples; i++) {
        float fade_factor = 1.0f - ((float)(fade_start + (int)i) / (float)AUDIO_CROSSFADE_SAMPLES);
        data[i] = last * fade_factor;
      }
      // Fill rest with silence
      for (size_t i = fade_samples; i < samples; i++) {
        data[i] = 0.0f;
      }
      rb->crossfade_samples_remaining -= (int)fade_samples;
      if (rb->crossfade_samples_remaining <= 0) {
        rb->last_sample = 0.0f;
      }

      mutex_unlock(&rb->mutex);
      return samples; // Return full buffer (with continued fade-out)
    }

    // Check if we've accumulated enough samples to start playback
    if (available >= AUDIO_JITTER_BUFFER_THRESHOLD) {
      rb->jitter_buffer_filled = true;
      rb->crossfade_samples_remaining = AUDIO_CROSSFADE_SAMPLES;
      rb->crossfade_fade_in = true;
      log_info("Jitter buffer filled (%zu samples), starting playback with fade-in", available);
    } else {
      // Log buffer fill progress every second
      log_debug_every(1000000, "Jitter buffer filling: %zu/%d samples (%.1f%%)", available,
                      AUDIO_JITTER_BUFFER_THRESHOLD, (100.0f * available) / AUDIO_JITTER_BUFFER_THRESHOLD);
      mutex_unlock(&rb->mutex);
      return 0; // Return silence until buffer is filled
    }
  }

  // Periodic buffer health logging (every 5 seconds when healthy)
  static unsigned int health_log_counter = 0;
  if (++health_log_counter % 250 == 0) { // ~5 seconds at 50Hz callback rate
    log_debug("Buffer health: %zu/%d samples (%.1f%%), underruns=%u", available, AUDIO_RING_BUFFER_SIZE,
              (100.0f * available) / AUDIO_RING_BUFFER_SIZE, rb->underrun_count);
  }

  // Low buffer handling: DON'T pause playback - continue reading what's available
  // and fill the rest with silence. Pausing causes a feedback loop where:
  // 1. Underrun -> pause reading -> buffer overflows from incoming samples
  // 2. Threshold reached -> resume reading -> drains too fast -> underrun again
  //
  // Instead: always consume samples to prevent overflow, use silence for missing data
  if (rb->jitter_buffer_enabled && available < AUDIO_JITTER_LOW_WATER_MARK) {
    rb->underrun_count++;
    log_warn_every(LOG_RATE_FAST,
                   "Audio buffer low #%u: only %zu samples available (low water mark: %d), padding with silence",
                   rb->underrun_count, available, AUDIO_JITTER_LOW_WATER_MARK);
    // Don't set jitter_buffer_filled = false - keep reading to prevent overflow
  }

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

  rb->read_index = (read_idx + (int)to_read) % AUDIO_RING_BUFFER_SIZE;

  // Apply fade-in if recovering from underrun
  if (rb->crossfade_fade_in && rb->crossfade_samples_remaining > 0) {
    int fade_start = AUDIO_CROSSFADE_SAMPLES - rb->crossfade_samples_remaining;
    size_t fade_samples =
        (to_read < (size_t)rb->crossfade_samples_remaining) ? to_read : (size_t)rb->crossfade_samples_remaining;

    for (size_t i = 0; i < fade_samples; i++) {
      float fade_factor = (float)(fade_start + (int)i + 1) / (float)AUDIO_CROSSFADE_SAMPLES;
      data[i] *= fade_factor;
    }

    rb->crossfade_samples_remaining -= (int)fade_samples;
    if (rb->crossfade_samples_remaining <= 0) {
      rb->crossfade_fade_in = false;
      log_debug("Audio fade-in complete");
    }
  }

  // Save last sample for potential fade-out
  // Note: only update if we actually read some data
  if (to_read > 0) {
    rb->last_sample = data[to_read - 1];
  }

  // Fill any remaining samples with pure silence if we couldn't read enough
  // NOTE: Previous code applied fade-out from last sample, but this created
  // audible "little extra sounds in the gaps" during frequent underruns.
  // Pure silence is less disruptive than artificial fade artifacts.
  if (to_read < samples) {
    size_t silence_samples = samples - to_read;
    SAFE_MEMSET(data + to_read, silence_samples * sizeof(float), 0, silence_samples * sizeof(float));
  }

  mutex_unlock(&rb->mutex);
  return samples; // Always return full buffer (with silence padding if needed)
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

  // Create capture buffer WITHOUT jitter buffering (PortAudio writes directly from microphone)
  ctx->capture_buffer = audio_ring_buffer_create_for_capture();
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
  atomic_store(&ctx->shutting_down, false);
  log_info("Audio system initialized successfully");
  return ASCIICHAT_OK;
}

void audio_destroy(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  // Stop duplex stream if running
  if (ctx->running) {
    audio_stop_duplex(ctx);
  }

  mutex_lock(&ctx->state_mutex);

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

void audio_set_pipeline(audio_context_t *ctx, void *pipeline) {
  if (!ctx)
    return;
  ctx->audio_pipeline = pipeline;
}

asciichat_error_t audio_start_duplex(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Audio context not initialized");
  }

  mutex_lock(&ctx->state_mutex);

  // Already running?
  if (ctx->duplex_stream || ctx->input_stream || ctx->output_stream) {
    mutex_unlock(&ctx->state_mutex);
    return ASCIICHAT_OK;
  }

  // Setup input parameters
  PaStreamParameters inputParams;
  if (opt_microphone_index >= 0) {
    inputParams.device = opt_microphone_index;
  } else {
    inputParams.device = Pa_GetDefaultInputDevice();
  }

  if (inputParams.device == paNoDevice) {
    mutex_unlock(&ctx->state_mutex);
    return SET_ERRNO(ERROR_AUDIO, "No input device available");
  }

  const PaDeviceInfo *inputInfo = Pa_GetDeviceInfo(inputParams.device);
  if (!inputInfo) {
    mutex_unlock(&ctx->state_mutex);
    return SET_ERRNO(ERROR_AUDIO, "Input device info not found");
  }

  inputParams.channelCount = AUDIO_CHANNELS;
  inputParams.sampleFormat = paFloat32;
  inputParams.suggestedLatency = inputInfo->defaultLowInputLatency;
  inputParams.hostApiSpecificStreamInfo = NULL;

  // Setup output parameters
  PaStreamParameters outputParams;
  if (opt_speakers_index >= 0) {
    outputParams.device = opt_speakers_index;
  } else {
    outputParams.device = Pa_GetDefaultOutputDevice();
  }

  if (outputParams.device == paNoDevice) {
    mutex_unlock(&ctx->state_mutex);
    return SET_ERRNO(ERROR_AUDIO, "No output device available");
  }

  const PaDeviceInfo *outputInfo = Pa_GetDeviceInfo(outputParams.device);
  if (!outputInfo) {
    mutex_unlock(&ctx->state_mutex);
    return SET_ERRNO(ERROR_AUDIO, "Output device info not found");
  }

  outputParams.channelCount = AUDIO_CHANNELS;
  outputParams.sampleFormat = paFloat32;
  outputParams.suggestedLatency = outputInfo->defaultLowOutputLatency;
  outputParams.hostApiSpecificStreamInfo = NULL;

  // Store device rates for diagnostics
  ctx->input_device_rate = inputInfo->defaultSampleRate;
  ctx->output_device_rate = outputInfo->defaultSampleRate;

  log_info("Opening audio:");
  log_info("  Input:  %s (%.0f Hz)", inputInfo->name, inputInfo->defaultSampleRate);
  log_info("  Output: %s (%.0f Hz)", outputInfo->name, outputInfo->defaultSampleRate);

  // Check if sample rates differ - ALSA full-duplex doesn't handle this well
  bool rates_differ = (inputInfo->defaultSampleRate != outputInfo->defaultSampleRate);
  bool try_separate = rates_differ;
  PaError err = paNoError;

  if (!try_separate) {
    // Try full-duplex first (preferred - perfect AEC3 timing)
    err = Pa_OpenStream(&ctx->duplex_stream, &inputParams, &outputParams, AUDIO_SAMPLE_RATE, AUDIO_FRAMES_PER_BUFFER,
                        paClipOff, duplex_callback, ctx);

    if (err == paNoError) {
      err = Pa_StartStream(ctx->duplex_stream);
      if (err != paNoError) {
        Pa_CloseStream(ctx->duplex_stream);
        ctx->duplex_stream = NULL;
        log_warn("Full-duplex stream failed to start: %s", Pa_GetErrorText(err));
        try_separate = true;
      }
    } else {
      log_warn("Full-duplex stream failed to open: %s", Pa_GetErrorText(err));
      try_separate = true;
    }
  }

  if (try_separate) {
    // Fall back to separate streams (needed when sample rates differ)
    log_info("Using separate input/output streams (sample rates differ: %.0f vs %.0f Hz)", inputInfo->defaultSampleRate,
             outputInfo->defaultSampleRate);
    log_info("  Will resample: buffer at %.0f Hz → output at %.0f Hz", (double)AUDIO_SAMPLE_RATE,
             outputInfo->defaultSampleRate);

    // Store the internal sample rate (buffer rate)
    ctx->sample_rate = AUDIO_SAMPLE_RATE;

    // Create render buffer for AEC3 reference synchronization
    ctx->render_buffer = audio_ring_buffer_create_for_capture();
    if (!ctx->render_buffer) {
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_MEMORY, "Failed to create render buffer");
    }

    // Open output stream at NATIVE device rate - we'll resample from 48kHz buffer in callback
    err = Pa_OpenStream(&ctx->output_stream, NULL, &outputParams, outputInfo->defaultSampleRate,
                        AUDIO_FRAMES_PER_BUFFER, paClipOff, output_callback, ctx);
    if (err != paNoError) {
      audio_ring_buffer_destroy(ctx->render_buffer);
      ctx->render_buffer = NULL;
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_AUDIO, "Failed to open output stream: %s", Pa_GetErrorText(err));
    }

    // Open input stream at NATIVE device rate (typically 48kHz which matches our internal rate)
    err = Pa_OpenStream(&ctx->input_stream, &inputParams, NULL, inputInfo->defaultSampleRate, AUDIO_FRAMES_PER_BUFFER,
                        paClipOff, input_callback, ctx);
    if (err != paNoError) {
      Pa_CloseStream(ctx->output_stream);
      ctx->output_stream = NULL;
      audio_ring_buffer_destroy(ctx->render_buffer);
      ctx->render_buffer = NULL;
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_AUDIO, "Failed to open input stream: %s", Pa_GetErrorText(err));
    }

    // Start both streams
    err = Pa_StartStream(ctx->output_stream);
    if (err != paNoError) {
      Pa_CloseStream(ctx->input_stream);
      Pa_CloseStream(ctx->output_stream);
      ctx->input_stream = NULL;
      ctx->output_stream = NULL;
      audio_ring_buffer_destroy(ctx->render_buffer);
      ctx->render_buffer = NULL;
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_AUDIO, "Failed to start output stream: %s", Pa_GetErrorText(err));
    }

    err = Pa_StartStream(ctx->input_stream);
    if (err != paNoError) {
      Pa_StopStream(ctx->output_stream);
      Pa_CloseStream(ctx->input_stream);
      Pa_CloseStream(ctx->output_stream);
      ctx->input_stream = NULL;
      ctx->output_stream = NULL;
      audio_ring_buffer_destroy(ctx->render_buffer);
      ctx->render_buffer = NULL;
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_AUDIO, "Failed to start input stream: %s", Pa_GetErrorText(err));
    }

    ctx->separate_streams = true;
    log_info("Separate streams started successfully");
  } else {
    ctx->separate_streams = false;
    log_info("Full-duplex stream started (single callback, perfect AEC3 timing)");
  }

  audio_set_realtime_priority();

  ctx->running = true;
  ctx->sample_rate = AUDIO_SAMPLE_RATE;
  mutex_unlock(&ctx->state_mutex);

  return ASCIICHAT_OK;
}

asciichat_error_t audio_stop_duplex(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Audio context not initialized");
  }

  atomic_store(&ctx->shutting_down, true);

  if (ctx->playback_buffer) {
    audio_ring_buffer_clear(ctx->playback_buffer);
  }

  Pa_Sleep(50); // Let callbacks drain

  mutex_lock(&ctx->state_mutex);

  if (ctx->duplex_stream) {
    Pa_StopStream(ctx->duplex_stream);
    Pa_CloseStream(ctx->duplex_stream);
    ctx->duplex_stream = NULL;
  }

  // Stop separate streams if used
  if (ctx->input_stream) {
    Pa_StopStream(ctx->input_stream);
    Pa_CloseStream(ctx->input_stream);
    ctx->input_stream = NULL;
  }

  if (ctx->output_stream) {
    Pa_StopStream(ctx->output_stream);
    Pa_CloseStream(ctx->output_stream);
    ctx->output_stream = NULL;
  }

  // Cleanup render buffer
  if (ctx->render_buffer) {
    audio_ring_buffer_destroy(ctx->render_buffer);
    ctx->render_buffer = NULL;
  }

  ctx->running = false;
  ctx->separate_streams = false;
  mutex_unlock(&ctx->state_mutex);

  log_info("Audio stopped");
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

  // Don't accept new audio data during shutdown - this prevents garbage/beeps
  if (atomic_load(&ctx->shutting_down)) {
    return ASCIICHAT_OK; // Silently discard
  }

  asciichat_error_t result = audio_ring_buffer_write(ctx->playback_buffer, buffer, num_samples);

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

asciichat_error_t audio_dequantize_samples(const uint8_t *samples_ptr, uint32_t total_samples, float *out_samples) {
  if (!samples_ptr || !out_samples || total_samples == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for audio dequantization");
  }

  for (uint32_t i = 0; i < total_samples; i++) {
    uint32_t network_sample;
    // Use memcpy to safely handle potential misalignment from packet header
    memcpy(&network_sample, samples_ptr + i * sizeof(uint32_t), sizeof(uint32_t));
    int32_t scaled = (int32_t)NET_TO_HOST_U32(network_sample);
    out_samples[i] = (float)scaled / 2147483647.0f;
  }

  return ASCIICHAT_OK;
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
