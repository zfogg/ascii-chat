
/**
 * @file audio.c
 * @ingroup audio
 * @brief 🔊 Audio capture and playback using PortAudio with buffer management
 */

#include "audio/audio.h"
#include "audio/client_audio_pipeline.h"
#include "util/endian.h"
#include "common.h"
#include "util/endian.h"
#include "asciichat_errno.h" // For asciichat_errno system
#include "buffer_pool.h"
#include "options/options.h"
#include "platform/init.h"  // For static_mutex_t
#include "network/packet.h" // For audio_batch_packet_t
#include "log/logging.h"    // For log_* macros
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>

#ifdef _WIN32
#include <malloc.h> // For _alloca on Windows
#define alloca _alloca
#else
#include <unistd.h> // For dup, dup2, close, STDERR_FILENO
#include <fcntl.h>  // For O_WRONLY
#endif

// PortAudio initialization reference counter
// Tracks how many audio contexts are using PortAudio to avoid conflicts
static unsigned int g_pa_init_refcount = 0;
static static_mutex_t g_pa_refcount_mutex = STATIC_MUTEX_INIT;

// Worker thread batch size (in frames, not samples)
// 480 frames = 10ms at 48kHz, matches AUDIO_FRAMES_PER_BUFFER
#define WORKER_BATCH_FRAMES 480
#define WORKER_BATCH_SAMPLES (WORKER_BATCH_FRAMES * AUDIO_CHANNELS)
#define WORKER_TIMEOUT_MS 10 // Wake up every 10ms to check for audio

/**
 * @brief Audio worker thread for heavy processing
 *
 * This thread handles ALL computationally expensive audio operations that
 * cannot be done in real-time PortAudio callbacks (<2ms requirement):
 * - WebRTC AEC3 echo cancellation (50-80ms on Raspberry Pi!)
 * - Audio resampling with floating-point math
 * - RMS calculations with sqrt()
 * - Any filtering or analysis
 *
 * Architecture:
 * 1. Wait for signal from callbacks (or timeout after 10ms)
 * 2. Batch-read raw audio from ring buffers
 * 3. Process through AEC3 + filters (can take 50-80ms, that's OK!)
 * 4. Resample if needed (separate streams mode)
 * 5. Write processed audio to output buffers
 *
 * Timing:
 * - Callbacks: <2ms (lock-free ring buffer ops only)
 * - Worker: 50-80ms OK (non-real-time thread)
 * - Condition variable provides efficient signaling
 *
 * @param arg Pointer to audio_context_t
 * @return NULL on exit
 */
static void *audio_worker_thread(void *arg) {
  audio_context_t *ctx = (audio_context_t *)arg;
  log_info("Audio worker thread started (batch size: %d frames = %d samples)", WORKER_BATCH_FRAMES,
           WORKER_BATCH_SAMPLES);

  while (true) {
    // Wait for signal from callback or timeout
    mutex_lock(&ctx->worker_mutex);
    int wait_result = cond_timedwait(&ctx->worker_cond, &ctx->worker_mutex, WORKER_TIMEOUT_MS);
    mutex_unlock(&ctx->worker_mutex);

    // Check shutdown flag
    if (atomic_load(&ctx->worker_should_stop)) {
      log_debug("Worker thread received shutdown signal");
      break;
    }

    // Skip processing if we timed out and no data available
    size_t capture_available = audio_ring_buffer_available_read(ctx->raw_capture_rb);
    size_t render_available = audio_ring_buffer_available_read(ctx->raw_render_rb);
    size_t playback_available = audio_ring_buffer_available_read(ctx->playback_buffer);

    if (wait_result != 0 && capture_available == 0 && playback_available == 0) {
      // Timeout with no data - continue waiting
      continue;
    }

    // STEP 1: Process capture path (mic → AEC3 → encoder)
    if (capture_available >= WORKER_BATCH_SAMPLES || render_available >= WORKER_BATCH_SAMPLES) {
      // Read raw capture and render samples from callbacks
      size_t capture_read =
          audio_ring_buffer_read(ctx->raw_capture_rb, ctx->worker_capture_batch, WORKER_BATCH_SAMPLES);
      size_t render_read = audio_ring_buffer_read(ctx->raw_render_rb, ctx->worker_render_batch, WORKER_BATCH_SAMPLES);

      if (capture_read > 0 && render_read > 0) {
        // Process through AEC3 if pipeline is available
        if (ctx->audio_pipeline) {
          // AEC3 processing can take 50-80ms on Raspberry Pi - that's OK here!
          // Output is written back to worker_capture_batch (in-place processing)
          client_audio_pipeline_process_duplex(ctx->audio_pipeline, ctx->worker_render_batch, (int)render_read,
                                               ctx->worker_capture_batch, (int)capture_read,
                                               ctx->worker_capture_batch); // Process in-place
        }

        // TODO: Add optional resampling here if input device rate != 48kHz
        // For now, assume 48kHz (most common for professional audio)

        // Write processed capture to encoder buffer
        audio_ring_buffer_write(ctx->capture_buffer, ctx->worker_capture_batch, (int)capture_read);

        log_debug_every(1000000, "Worker processed %zu capture samples (AEC3 applied)", capture_read);
      }
    }

    // STEP 2: Process playback path (network → worker → speakers)
    if (playback_available >= WORKER_BATCH_SAMPLES) {
      // Read decoded audio from network
      size_t playback_read =
          audio_ring_buffer_read(ctx->playback_buffer, ctx->worker_playback_batch, WORKER_BATCH_SAMPLES);

      if (playback_read > 0) {
        // TODO: Add optional resampling here if output device rate != 48kHz
        // TODO: Add optional filtering/equalization/ducking here

        // Write to processed playback buffer for callback
        audio_ring_buffer_write(ctx->processed_playback_rb, ctx->worker_playback_batch, (int)playback_read);

        log_debug_every(1000000, "Worker processed %zu playback samples", playback_read);
      }
    }
  }

  log_info("Audio worker thread exiting");
  return NULL;
}

/**
 * Full-duplex callback - handles BOTH input and output in one callback.
 *
 * NEW ARCHITECTURE (Real-Time Safe - Worker Thread Design):
 * This callback is now MINIMAL - just lock-free ring buffer copies:
 * 1. Read processed playback → speakers (~0.5ms)
 * 2. Copy raw mic → worker (~0.5ms)
 * 3. Copy raw speaker → worker (~0.5ms)
 * 4. Signal worker (non-blocking, ~0.1ms)
 * TOTAL: ~1.6ms ✓ (was 50-80ms with inline AEC3!)
 *
 * Heavy processing (AEC3, RMS, resampling) moved to audio_worker_thread().
 * Worker runs non-real-time, can take 50-80ms without blocking callback.
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

  // Log status flags (rate-limited to avoid spam)
  if (statusFlags != 0) {
    if (statusFlags & paOutputUnderflow) {
      log_warn_every(LOG_RATE_FAST, "PortAudio output underflow");
    }
    if (statusFlags & paInputOverflow) {
      log_warn_every(LOG_RATE_FAST, "PortAudio input overflow");
    }
  }

  // STEP 1: Read processed playback from worker → speakers (~0.5ms)
  if (output && ctx->processed_playback_rb) {
    size_t samples_read = audio_ring_buffer_read(ctx->processed_playback_rb, output, num_samples);
    if (samples_read < num_samples) {
      // Fill remaining with silence if underrun
      SAFE_MEMSET(output + samples_read, (num_samples - samples_read) * sizeof(float), 0,
                  (num_samples - samples_read) * sizeof(float));
      log_debug_every(1000000, "Playback underrun: got %zu/%zu samples", samples_read, num_samples);
    }
  } else if (output) {
    SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
  }

  // STEP 2: Copy raw mic samples → worker for AEC3 processing (~0.5ms)
  if (input && ctx->raw_capture_rb) {
    audio_ring_buffer_write(ctx->raw_capture_rb, input, (int)num_samples);
  }

  // STEP 3: Copy raw speaker samples → worker for AEC3 reference (~0.5ms)
  // This is CRITICAL for AEC3 - worker needs exact render signal at same time as capture
  if (output && ctx->raw_render_rb) {
    audio_ring_buffer_write(ctx->raw_render_rb, output, (int)num_samples);
  }

  // STEP 4: Signal worker thread (non-blocking, ~0.1ms)
  // Worker wakes up, processes batch, writes back to processed buffers
  cond_signal(&ctx->worker_cond);

  return paContinue;
}

/**
 * Simple linear interpolation resampler.
 * Resamples from src_rate to dst_rate using linear interpolation.
 *
 * @param src Source samples at src_rate
 * @param src_samples Number of source samples
 * @param dst Destination buffer at dst_rate
 * @param dst_samples Number of destination samples to produce
 * @param src_rate Source sample rate (e.g., 48000)
 * @param dst_rate Destination sample rate (e.g., 44100)
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
 * Separate output callback - handles playback only (separate streams mode).
 *
 * NEW ARCHITECTURE (Real-Time Safe):
 * - Read processed playback from worker → speakers (~0.5ms)
 * - Copy to render buffer for input callback (~0.5ms)
 * - Signal worker (~0.1ms)
 * TOTAL: ~1.1ms ✓
 *
 * Resampling (if needed) is handled by worker thread, not here.
 */
static int output_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
                           void *userData) {
  (void)inputBuffer;
  (void)timeInfo;

  audio_context_t *ctx = (audio_context_t *)userData;
  float *output = (float *)outputBuffer;
  size_t num_samples = framesPerBuffer * AUDIO_CHANNELS;

  // Silence on shutdown
  if (atomic_load(&ctx->shutting_down)) {
    if (output) {
      SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
    }
    return paContinue;
  }

  if (statusFlags & paOutputUnderflow) {
    log_warn_every(LOG_RATE_FAST, "PortAudio output underflow (separate stream)");
  }

  // STEP 1: Read processed playback from worker → speakers
  if (output && ctx->processed_playback_rb) {
    size_t samples_read = audio_ring_buffer_read(ctx->processed_playback_rb, output, num_samples);
    if (samples_read < num_samples) {
      SAFE_MEMSET(output + samples_read, (num_samples - samples_read) * sizeof(float), 0,
                  (num_samples - samples_read) * sizeof(float));
    }

    // STEP 2: Copy to render buffer for input callback (AEC3 reference)
    if (ctx->render_buffer) {
      audio_ring_buffer_write(ctx->render_buffer, output, (int)samples_read);
    }
  } else if (output) {
    SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
  }

  // STEP 3: Signal worker
  cond_signal(&ctx->worker_cond);

  return paContinue;
}

/**
 * Separate input callback - handles capture only (separate streams mode).
 *
 * NEW ARCHITECTURE (Real-Time Safe):
 * - Copy raw mic → worker (~0.5ms)
 * - Read render reference from render_buffer (~0.5ms)
 * - Copy render → worker (~0.5ms)
 * - Signal worker (~0.1ms)
 * TOTAL: ~1.6ms ✓
 *
 * AEC3 processing (with render reference) is handled by worker thread.
 * No alloca, no static buffers, no resampling, no sqrt() - all in worker.
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

  // STEP 1: Copy raw mic samples → worker for AEC3 processing
  if (input && ctx->raw_capture_rb) {
    audio_ring_buffer_write(ctx->raw_capture_rb, input, (int)num_samples);
  }

  // STEP 2: Read render reference from render_buffer and copy to worker
  // (render_buffer is written by output_callback, read here for synchronization)
  if (ctx->render_buffer && ctx->raw_render_rb) {
    // Worker will handle the AEC3 processing using this render reference
    size_t render_available = audio_ring_buffer_available_read(ctx->render_buffer);
    if (render_available >= num_samples) {
      // Read exactly what we need
      float render_temp[AUDIO_BUFFER_SIZE]; // Stack allocation OK - fixed small size
      size_t render_read = audio_ring_buffer_read(ctx->render_buffer, render_temp, num_samples);
      if (render_read > 0) {
        audio_ring_buffer_write(ctx->raw_render_rb, render_temp, (int)render_read);
      }
    }
  }

  // STEP 3: Signal worker thread
  cond_signal(&ctx->worker_cond);

  return paContinue;
}

// Forward declaration for internal helper function
static audio_ring_buffer_t *audio_ring_buffer_create_internal(bool jitter_buffer_enabled);

static audio_ring_buffer_t *audio_ring_buffer_create_internal(bool jitter_buffer_enabled) {
  size_t rb_size = sizeof(audio_ring_buffer_t);
  audio_ring_buffer_t *rb = (audio_ring_buffer_t *)buffer_pool_alloc(NULL, rb_size);

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
    buffer_pool_free(NULL, rb, sizeof(audio_ring_buffer_t));
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
  buffer_pool_free(NULL, rb, sizeof(audio_ring_buffer_t));
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

  // LOCK-FREE: Load indices with proper memory ordering
  // - Load our own write_index with relaxed (no sync needed with ourselves)
  // - Load reader's read_index with acquire (see reader's updates to free space)
  unsigned int write_idx = atomic_load_explicit(&rb->write_index, memory_order_relaxed);
  unsigned int read_idx = atomic_load_explicit(&rb->read_index, memory_order_acquire);

  // Calculate current buffer level (how many samples are buffered)
  int buffer_level;
  if (write_idx >= read_idx) {
    buffer_level = (int)(write_idx - read_idx);
  } else {
    buffer_level = AUDIO_RING_BUFFER_SIZE - (int)(read_idx - write_idx);
  }
  int available = AUDIO_RING_BUFFER_SIZE - buffer_level;

  // HIGH WATER MARK: Drop OLD samples to prevent latency accumulation
  // This is critical for real-time audio - we always want the NEWEST data
  // ALWAYS apply high-water-mark on write, regardless of jitter_buffer_enabled
  // jitter_buffer_enabled only controls READ side (whether to wait for threshold)
  // On WRITE side, we ALWAYS want to drop old samples to bound latency
  if (buffer_level + samples > AUDIO_JITTER_HIGH_WATER_MARK) {
    // Calculate how many old samples to drop to bring buffer to target level
    int excess = (buffer_level + samples) - AUDIO_JITTER_TARGET_LEVEL;
    if (excess > 0) {
      // Advance read_index to drop old samples
      // Note: This is safe because the reader checks for underrun and handles it gracefully
      unsigned int new_read_idx = (read_idx + (unsigned int)excess) % AUDIO_RING_BUFFER_SIZE;
      atomic_store_explicit(&rb->read_index, new_read_idx, memory_order_release);

      log_warn_every(LOG_RATE_FAST,
                     "Audio buffer high water mark exceeded: dropping %d OLD samples to reduce latency "
                     "(buffer was %d, target %d)",
                     excess, buffer_level, AUDIO_JITTER_TARGET_LEVEL);

      // Recalculate available space after dropping old samples
      read_idx = new_read_idx;
      buffer_level = AUDIO_JITTER_TARGET_LEVEL - samples;
      if (buffer_level < 0)
        buffer_level = 0;
      available = AUDIO_RING_BUFFER_SIZE - buffer_level;
    }
  }

  // Now write the new samples - should always have enough space after above
  int samples_to_write = samples;
  if (samples > available) {
    // This should rarely happen after the high water mark logic above
    int samples_dropped = samples - available;
    samples_to_write = available;
    log_warn_every(LOG_RATE_FAST, "Audio buffer overflow: dropping %d of %d incoming samples (buffer_used=%d/%d)",
                   samples_dropped, samples, AUDIO_RING_BUFFER_SIZE - available, AUDIO_RING_BUFFER_SIZE);
  }

  // Write only the samples that fit (preserves existing data integrity)
  if (samples_to_write > 0) {
    int remaining = AUDIO_RING_BUFFER_SIZE - (int)write_idx;

    if (samples_to_write <= remaining) {
      // Can copy in one chunk
      SAFE_MEMCPY(&rb->data[write_idx], samples_to_write * sizeof(float), data, samples_to_write * sizeof(float));
    } else {
      // Need to wrap around - copy in two chunks
      SAFE_MEMCPY(&rb->data[write_idx], remaining * sizeof(float), data, remaining * sizeof(float));
      SAFE_MEMCPY(&rb->data[0], (samples_to_write - remaining) * sizeof(float), &data[remaining],
                  (samples_to_write - remaining) * sizeof(float));
    }

    // LOCK-FREE: Store new write_index with release ordering
    // This ensures all data writes above are visible before the index update
    unsigned int new_write_idx = (write_idx + (unsigned int)samples_to_write) % AUDIO_RING_BUFFER_SIZE;
    atomic_store_explicit(&rb->write_index, new_write_idx, memory_order_release);
  }

  // Note: jitter buffer fill check is now done in read function for better control

  return ASCIICHAT_OK; // Success
}

size_t audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, size_t samples) {
  if (!rb || !data || samples <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: rb=%p, data=%p, samples=%d", rb, data, samples);
    return 0; // Return 0 samples read on error
  }

  // LOCK-FREE: Load indices with proper memory ordering
  // - Load writer's write_index with acquire (see writer's data updates)
  // - Load our own read_index with relaxed (no sync needed with ourselves)
  unsigned int write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);
  unsigned int read_idx = atomic_load_explicit(&rb->read_index, memory_order_relaxed);

  // Calculate available samples
  size_t available;
  if (write_idx >= read_idx) {
    available = write_idx - read_idx;
  } else {
    available = AUDIO_RING_BUFFER_SIZE - read_idx + write_idx;
  }

  // LOCK-FREE: Load jitter buffer state with acquire ordering
  bool jitter_filled = atomic_load_explicit(&rb->jitter_buffer_filled, memory_order_acquire);
  int crossfade_remaining = atomic_load_explicit(&rb->crossfade_samples_remaining, memory_order_acquire);
  bool fade_in = atomic_load_explicit(&rb->crossfade_fade_in, memory_order_acquire);

  // Jitter buffer: don't read until initial fill threshold is reached
  // (only for playback buffers - capture buffers have jitter_buffer_enabled = false)
  if (!jitter_filled && rb->jitter_buffer_enabled) {
    // First, check if we're in the middle of a fade-out that needs to continue
    // This happens when fade-out spans multiple buffer reads
    if (!fade_in && crossfade_remaining > 0) {
      // Continue fade-out from where we left off
      int fade_start = AUDIO_CROSSFADE_SAMPLES - crossfade_remaining;
      size_t fade_samples = (samples < (size_t)crossfade_remaining) ? samples : (size_t)crossfade_remaining;
      float last = rb->last_sample; // NOT atomic - only written by reader
      for (size_t i = 0; i < fade_samples; i++) {
        float fade_factor = 1.0f - ((float)(fade_start + (int)i) / (float)AUDIO_CROSSFADE_SAMPLES);
        data[i] = last * fade_factor;
      }
      // Fill rest with silence
      for (size_t i = fade_samples; i < samples; i++) {
        data[i] = 0.0f;
      }
      // Update crossfade state atomically
      atomic_store_explicit(&rb->crossfade_samples_remaining, crossfade_remaining - (int)fade_samples,
                            memory_order_release);
      if (crossfade_remaining - (int)fade_samples <= 0) {
        rb->last_sample = 0.0f;
      }

      return samples; // Return full buffer (with continued fade-out)
    }

    // Check if we've accumulated enough samples to start playback
    if (available >= AUDIO_JITTER_BUFFER_THRESHOLD) {
      atomic_store_explicit(&rb->jitter_buffer_filled, true, memory_order_release);
      atomic_store_explicit(&rb->crossfade_samples_remaining, AUDIO_CROSSFADE_SAMPLES, memory_order_release);
      atomic_store_explicit(&rb->crossfade_fade_in, true, memory_order_release);
      log_info("Jitter buffer filled (%zu samples), starting playback with fade-in", available);
      // Reload state for processing below
      jitter_filled = true;
      crossfade_remaining = AUDIO_CROSSFADE_SAMPLES;
      fade_in = true;
    } else {
      // Log buffer fill progress every second
      log_debug_every(1000000, "Jitter buffer filling: %zu/%d samples (%.1f%%)", available,
                      AUDIO_JITTER_BUFFER_THRESHOLD, (100.0f * available) / AUDIO_JITTER_BUFFER_THRESHOLD);
      return 0; // Return silence until buffer is filled
    }
  }

  // Periodic buffer health logging (every 5 seconds when healthy)
  static unsigned int health_log_counter = 0;
  if (++health_log_counter % 250 == 0) { // ~5 seconds at 50Hz callback rate
    unsigned int underruns = atomic_load_explicit(&rb->underrun_count, memory_order_relaxed);
    log_debug("Buffer health: %zu/%d samples (%.1f%%), underruns=%u", available, AUDIO_RING_BUFFER_SIZE,
              (100.0f * available) / AUDIO_RING_BUFFER_SIZE, underruns);
  }

  // Low buffer handling: DON'T pause playback - continue reading what's available
  // and fill the rest with silence. Pausing causes a feedback loop where:
  // 1. Underrun -> pause reading -> buffer overflows from incoming samples
  // 2. Threshold reached -> resume reading -> drains too fast -> underrun again
  //
  // Instead: always consume samples to prevent overflow, use silence for missing data
  if (rb->jitter_buffer_enabled && available < AUDIO_JITTER_LOW_WATER_MARK) {
    unsigned int underrun_count = atomic_fetch_add_explicit(&rb->underrun_count, 1, memory_order_relaxed) + 1;
    log_warn_every(LOG_RATE_FAST,
                   "Audio buffer low #%u: only %zu samples available (low water mark: %d), padding with silence",
                   underrun_count, available, AUDIO_JITTER_LOW_WATER_MARK);
    // Don't set jitter_buffer_filled = false - keep reading to prevent overflow
  }

  size_t to_read = (samples > available) ? available : samples;

  // Optimize: copy in chunks instead of one sample at a time
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

  // LOCK-FREE: Store new read_index with release ordering
  // This ensures all data reads above complete before the index update
  unsigned int new_read_idx = (read_idx + (unsigned int)to_read) % AUDIO_RING_BUFFER_SIZE;
  atomic_store_explicit(&rb->read_index, new_read_idx, memory_order_release);

  // Apply fade-in if recovering from underrun
  if (fade_in && crossfade_remaining > 0) {
    int fade_start = AUDIO_CROSSFADE_SAMPLES - crossfade_remaining;
    size_t fade_samples = (to_read < (size_t)crossfade_remaining) ? to_read : (size_t)crossfade_remaining;

    for (size_t i = 0; i < fade_samples; i++) {
      float fade_factor = (float)(fade_start + (int)i + 1) / (float)AUDIO_CROSSFADE_SAMPLES;
      data[i] *= fade_factor;
    }

    int new_crossfade_remaining = crossfade_remaining - (int)fade_samples;
    atomic_store_explicit(&rb->crossfade_samples_remaining, new_crossfade_remaining, memory_order_release);
    if (new_crossfade_remaining <= 0) {
      atomic_store_explicit(&rb->crossfade_fade_in, false, memory_order_release);
      log_debug("Audio fade-in complete");
    }
  }

  // Save last sample for potential fade-out
  // Note: only update if we actually read some data
  // This is NOT atomic - only the reader thread writes this
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

  return samples; // Always return full buffer (with silence padding if needed)
}

/**
 * @brief Peek at available samples without consuming them (for AEC3 render signal)
 *
 * This function reads samples from the jitter buffer WITHOUT advancing the read_index.
 * Used to feed audio to AEC3 for echo cancellation even during jitter buffer fill period.
 *
 * @param rb Ring buffer to peek from
 * @param data Output buffer for samples
 * @param samples Number of samples to peek
 * @return Number of samples actually peeked (may be less than requested)
 */
size_t audio_ring_buffer_peek(audio_ring_buffer_t *rb, float *data, size_t samples) {
  if (!rb || !data || samples <= 0) {
    return 0;
  }

  // LOCK-FREE: Load indices with proper memory ordering
  unsigned int write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);
  unsigned int read_idx = atomic_load_explicit(&rb->read_index, memory_order_relaxed);

  // Calculate available samples
  size_t available;
  if (write_idx >= read_idx) {
    available = write_idx - read_idx;
  } else {
    available = AUDIO_RING_BUFFER_SIZE - read_idx + write_idx;
  }

  size_t to_peek = (samples > available) ? available : samples;

  if (to_peek == 0) {
    return 0;
  }

  // Copy samples in chunks (handle wraparound)
  size_t first_chunk = (read_idx + to_peek <= AUDIO_RING_BUFFER_SIZE) ? to_peek : (AUDIO_RING_BUFFER_SIZE - read_idx);

  SAFE_MEMCPY(data, first_chunk * sizeof(float), rb->data + read_idx, first_chunk * sizeof(float));

  if (first_chunk < to_peek) {
    // Wraparound: copy second chunk from beginning of buffer
    size_t second_chunk = to_peek - first_chunk;
    SAFE_MEMCPY(data + first_chunk, second_chunk * sizeof(float), rb->data, second_chunk * sizeof(float));
  }

  return to_peek;
}

size_t audio_ring_buffer_available_read(audio_ring_buffer_t *rb) {
  if (!rb)
    return 0;

  // LOCK-FREE: Load indices with proper memory ordering
  // Use acquire for write_index to see writer's updates
  // Use relaxed for read_index (our own index)
  unsigned int write_idx = atomic_load_explicit(&rb->write_index, memory_order_acquire);
  unsigned int read_idx = atomic_load_explicit(&rb->read_index, memory_order_relaxed);

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
    platform_stderr_redirect_handle_t stderr_handle = platform_stderr_redirect_to_null();

    PaError err = Pa_Initialize();

    // Restore stderr before checking errors
    platform_stderr_restore(stderr_handle);

    if (err != paNoError) {
      static_mutex_unlock(&g_pa_refcount_mutex);
      mutex_destroy(&ctx->state_mutex);
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

  // Create new ring buffers for worker thread architecture
  ctx->raw_capture_rb = audio_ring_buffer_create_for_capture();
  if (!ctx->raw_capture_rb) {
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create raw capture buffer");
  }

  ctx->raw_render_rb = audio_ring_buffer_create_for_capture();
  if (!ctx->raw_render_rb) {
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create raw render buffer");
  }

  ctx->processed_playback_rb = audio_ring_buffer_create();
  if (!ctx->processed_playback_rb) {
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create processed playback buffer");
  }

  // Initialize worker thread infrastructure
  if (mutex_init(&ctx->worker_mutex) != 0) {
    audio_ring_buffer_destroy(ctx->processed_playback_rb);
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize worker mutex");
  }

  if (cond_init(&ctx->worker_cond) != 0) {
    mutex_destroy(&ctx->worker_mutex);
    audio_ring_buffer_destroy(ctx->processed_playback_rb);
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize worker condition variable");
  }

  // Allocate pre-allocated worker buffers (avoid malloc in worker loop)
  ctx->worker_capture_batch = SAFE_MALLOC(WORKER_BATCH_SAMPLES * sizeof(float), float *);
  if (!ctx->worker_capture_batch) {
    cond_destroy(&ctx->worker_cond);
    mutex_destroy(&ctx->worker_mutex);
    audio_ring_buffer_destroy(ctx->processed_playback_rb);
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate worker capture batch buffer");
  }

  ctx->worker_render_batch = SAFE_MALLOC(WORKER_BATCH_SAMPLES * sizeof(float), float *);
  if (!ctx->worker_render_batch) {
    SAFE_FREE(ctx->worker_capture_batch);
    cond_destroy(&ctx->worker_cond);
    mutex_destroy(&ctx->worker_mutex);
    audio_ring_buffer_destroy(ctx->processed_playback_rb);
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate worker render batch buffer");
  }

  ctx->worker_playback_batch = SAFE_MALLOC(WORKER_BATCH_SAMPLES * sizeof(float), float *);
  if (!ctx->worker_playback_batch) {
    SAFE_FREE(ctx->worker_render_batch);
    SAFE_FREE(ctx->worker_capture_batch);
    cond_destroy(&ctx->worker_cond);
    mutex_destroy(&ctx->worker_mutex);
    audio_ring_buffer_destroy(ctx->processed_playback_rb);
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    static_mutex_lock(&g_pa_refcount_mutex);
    if (g_pa_init_refcount > 0) {
      g_pa_init_refcount--;
      if (g_pa_init_refcount == 0) {
        Pa_Terminate();
      }
    }
    static_mutex_unlock(&g_pa_refcount_mutex);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate worker playback batch buffer");
  }

  // Initialize worker thread state (thread will be started in audio_start_duplex)
  ctx->worker_running = false;
  atomic_store(&ctx->worker_should_stop, false);

  ctx->initialized = true;
  atomic_store(&ctx->shutting_down, false);
  log_info("Audio system initialized successfully (worker thread architecture enabled)");
  return ASCIICHAT_OK;
}

void audio_destroy(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  // Stop duplex stream if running (this also stops the worker thread)
  if (ctx->running) {
    audio_stop_duplex(ctx);
  }

  // Ensure worker thread is stopped even if streams weren't running
  if (ctx->worker_running) {
    log_debug("Stopping worker thread during audio_destroy");
    atomic_store(&ctx->worker_should_stop, true);
    cond_signal(&ctx->worker_cond); // Wake up worker if waiting
    asciichat_thread_join(&ctx->worker_thread, NULL);
    ctx->worker_running = false;
  }

  mutex_lock(&ctx->state_mutex);

  // Destroy all ring buffers (old + new)
  audio_ring_buffer_destroy(ctx->capture_buffer);
  audio_ring_buffer_destroy(ctx->playback_buffer);
  audio_ring_buffer_destroy(ctx->raw_capture_rb);
  audio_ring_buffer_destroy(ctx->raw_render_rb);
  audio_ring_buffer_destroy(ctx->processed_playback_rb);
  audio_ring_buffer_destroy(ctx->render_buffer); // May be NULL, that's OK

  // Free pre-allocated worker buffers
  SAFE_FREE(ctx->worker_capture_batch);
  SAFE_FREE(ctx->worker_render_batch);
  SAFE_FREE(ctx->worker_playback_batch);

  // Destroy worker synchronization primitives
  cond_destroy(&ctx->worker_cond);
  mutex_destroy(&ctx->worker_mutex);

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

  log_info("Audio system destroyed (worker thread architecture)");
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
  if (GET_OPTION(microphone_index) >= 0) {
    inputParams.device = GET_OPTION(microphone_index);
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
  const PaDeviceInfo *outputInfo = NULL;
  bool has_output = false;

  if (GET_OPTION(speakers_index) >= 0) {
    outputParams.device = GET_OPTION(speakers_index);
  } else {
    outputParams.device = Pa_GetDefaultOutputDevice();
  }

  if (outputParams.device != paNoDevice) {
    outputInfo = Pa_GetDeviceInfo(outputParams.device);
    if (outputInfo) {
      has_output = true;
      outputParams.channelCount = AUDIO_CHANNELS;
      outputParams.sampleFormat = paFloat32;
      outputParams.suggestedLatency = outputInfo->defaultLowOutputLatency;
      outputParams.hostApiSpecificStreamInfo = NULL;
    } else {
      log_warn("Output device info not found for device %d", outputParams.device);
    }
  }

  // Store device rates for diagnostics
  ctx->input_device_rate = inputInfo->defaultSampleRate;
  ctx->output_device_rate = has_output ? outputInfo->defaultSampleRate : 0;

  log_info("Opening audio:");
  log_info("  Input:  %s (%.0f Hz)", inputInfo->name, inputInfo->defaultSampleRate);
  if (has_output) {
    log_info("  Output: %s (%.0f Hz)", outputInfo->name, outputInfo->defaultSampleRate);
  } else {
    log_info("  Output: None (input-only mode - will send audio to server)");
  }

  // Check if sample rates differ - ALSA full-duplex doesn't handle this well
  // If no output, always use separate streams (input-only)
  bool rates_differ = has_output && (inputInfo->defaultSampleRate != outputInfo->defaultSampleRate);
  bool try_separate = rates_differ || !has_output;
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
    // Fall back to separate streams (needed when sample rates differ or input-only mode)
    if (has_output) {
      log_info("Using separate input/output streams (sample rates differ: %.0f vs %.0f Hz)",
               inputInfo->defaultSampleRate, outputInfo->defaultSampleRate);
      log_info("  Will resample: buffer at %.0f Hz → output at %.0f Hz", (double)AUDIO_SAMPLE_RATE,
               outputInfo->defaultSampleRate);
    } else {
      log_info("Using input-only mode (no output device available)");
    }

    // Store the internal sample rate (buffer rate)
    ctx->sample_rate = AUDIO_SAMPLE_RATE;

    // Create render buffer for AEC3 reference synchronization
    ctx->render_buffer = audio_ring_buffer_create_for_capture();
    if (!ctx->render_buffer) {
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_MEMORY, "Failed to create render buffer");
    }

    // Open output stream only if output device exists
    bool output_ok = false;
    if (has_output) {
      // When using separate streams on the same device, they must use the same sample rate
      // Use output device's sample rate for both streams
      double stream_sample_rate = outputInfo->defaultSampleRate;
      bool same_device = (inputParams.device == outputParams.device);
      if (same_device) {
        log_info("Input and output on same device - using unified sample rate: %.0f Hz", stream_sample_rate);
      }

      // Open output stream at device native rate
      err = Pa_OpenStream(&ctx->output_stream, NULL, &outputParams, stream_sample_rate, AUDIO_FRAMES_PER_BUFFER,
                          paClipOff, output_callback, ctx);
      output_ok = (err == paNoError);
      if (!output_ok) {
        log_warn("Failed to open output stream: %s", Pa_GetErrorText(err));
      }
    }

    // Open input stream - use pipeline sample rate (AUDIO_SAMPLE_RATE)
    // In input-only mode, we don't need to match output device rate
    double input_stream_rate = AUDIO_SAMPLE_RATE;
    err = Pa_OpenStream(&ctx->input_stream, &inputParams, NULL, input_stream_rate, AUDIO_FRAMES_PER_BUFFER, paClipOff,
                        input_callback, ctx);
    bool input_ok = (err == paNoError);

    // If input failed, try device 0 as fallback (HDMI on BeaglePlay)
    if (!input_ok) {
      log_debug("Input failed - trying device 0 as fallback");
      PaStreamParameters fallback_input_params = inputParams;
      fallback_input_params.device = 0;
      const PaDeviceInfo *device_0_info = Pa_GetDeviceInfo(0);
      if (device_0_info && device_0_info->maxInputChannels > 0) {
        err = Pa_OpenStream(&ctx->input_stream, &fallback_input_params, NULL, input_stream_rate,
                            AUDIO_FRAMES_PER_BUFFER, paClipOff, input_callback, ctx);
        if (err == paNoError) {
          log_info("Input stream opened on device 0 (fallback from default)");
          input_ok = true;
        } else {
          log_warn("Fallback also failed on device 0: %s", Pa_GetErrorText(err));
        }
      }
    }

    if (!input_ok) {
      log_warn("Failed to open input stream: %s", Pa_GetErrorText(err));
    }

    // Check if we got at least one stream working
    if (!input_ok && !output_ok) {
      // Neither stream works - fail completely
      audio_ring_buffer_destroy(ctx->render_buffer);
      ctx->render_buffer = NULL;
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_AUDIO, "Failed to open both input and output streams");
    }

    // If output failed but input works, we can still send audio to server
    if (!output_ok && input_ok) {
      log_info("Output stream unavailable - continuing with input-only (can send audio to server)");
      ctx->output_stream = NULL;
    }
    // If input failed but output works, we can still receive audio from server
    if (!input_ok && output_ok) {
      log_info("Input stream unavailable - continuing with output-only (can receive audio from server)");
      ctx->input_stream = NULL;
    }

    // Start output stream if it's open
    if (ctx->output_stream) {
      err = Pa_StartStream(ctx->output_stream);
      if (err != paNoError) {
        if (ctx->input_stream)
          Pa_CloseStream(ctx->input_stream);
        Pa_CloseStream(ctx->output_stream);
        ctx->input_stream = NULL;
        ctx->output_stream = NULL;
        audio_ring_buffer_destroy(ctx->render_buffer);
        ctx->render_buffer = NULL;
        mutex_unlock(&ctx->state_mutex);
        return SET_ERRNO(ERROR_AUDIO, "Failed to start output stream: %s", Pa_GetErrorText(err));
      }
    }

    // Start input stream if it's open
    if (ctx->input_stream) {
      err = Pa_StartStream(ctx->input_stream);
      if (err != paNoError) {
        if (ctx->output_stream)
          Pa_StopStream(ctx->output_stream);
        if (ctx->input_stream)
          Pa_CloseStream(ctx->input_stream);
        if (ctx->output_stream)
          Pa_CloseStream(ctx->output_stream);
        ctx->input_stream = NULL;
        ctx->output_stream = NULL;
        audio_ring_buffer_destroy(ctx->render_buffer);
        ctx->render_buffer = NULL;
        mutex_unlock(&ctx->state_mutex);
        return SET_ERRNO(ERROR_AUDIO, "Failed to start input stream: %s", Pa_GetErrorText(err));
      }
    }

    ctx->separate_streams = true;
    log_info("Separate streams started successfully");
  } else {
    ctx->separate_streams = false;
    log_info("Full-duplex stream started (single callback, perfect AEC3 timing)");
  }

  audio_set_realtime_priority();

  // Start worker thread for heavy audio processing
  if (!ctx->worker_running) {
    atomic_store(&ctx->worker_should_stop, false);
    if (asciichat_thread_create(&ctx->worker_thread, audio_worker_thread, ctx) != 0) {
      // Failed to create worker thread - stop streams and cleanup
      if (ctx->duplex_stream) {
        Pa_StopStream(ctx->duplex_stream);
        Pa_CloseStream(ctx->duplex_stream);
        ctx->duplex_stream = NULL;
      }
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
      audio_ring_buffer_destroy(ctx->render_buffer);
      ctx->render_buffer = NULL;
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_THREAD, "Failed to create worker thread");
    }
    ctx->worker_running = true;
    log_info("Worker thread started successfully");
  }

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

  // Stop worker thread before stopping streams
  if (ctx->worker_running) {
    log_debug("Stopping worker thread");
    atomic_store(&ctx->worker_should_stop, true);
    cond_signal(&ctx->worker_cond); // Wake up worker if waiting
    asciichat_thread_join(&ctx->worker_thread, NULL);
    ctx->worker_running = false;
    log_info("Worker thread stopped successfully");
  }

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
    platform_stderr_redirect_handle_t stderr_handle = platform_stderr_redirect_to_null();

    PaError err = Pa_Initialize();

    // Restore stderr before checking errors
    platform_stderr_restore(stderr_handle);

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
  // Delegate to platform abstraction layer
  asciichat_error_t result = asciichat_thread_set_realtime_priority();
  if (result == ASCIICHAT_OK) {
    log_info("✓ Audio thread real-time priority set successfully");
  }
  return result;
}

/* ============================================================================
 * Audio Batch Packet Parsing
 * ============================================================================
 */

asciichat_error_t audio_parse_batch_header(const void *data, size_t len, audio_batch_info_t *out_batch) {
  if (!data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch header data pointer is NULL");
  }

  if (!out_batch) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch info output pointer is NULL");
  }

  if (len < sizeof(audio_batch_packet_t)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch header too small (len=%zu, expected=%zu)", len,
                     sizeof(audio_batch_packet_t));
  }

  const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)data;

  // Unpack network byte order values to host byte order
  out_batch->batch_count = ntohl(batch_header->batch_count);
  out_batch->total_samples = ntohl(batch_header->total_samples);
  out_batch->sample_rate = ntohl(batch_header->sample_rate);
  out_batch->channels = ntohl(batch_header->channels);

  return ASCIICHAT_OK;
}

asciichat_error_t audio_validate_batch_params(const audio_batch_info_t *batch) {
  if (!batch) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch info pointer is NULL");
  }

  // Validate batch_count
  if (batch->batch_count == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch count cannot be zero");
  }

  // Check for reasonable max (256 frames per batch is very generous)
  if (batch->batch_count > 256) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch count too large (batch_count=%u, max=256)", batch->batch_count);
  }

  // Validate channels (1=mono, 2=stereo, max 8 for multi-channel)
  if (batch->channels == 0 || batch->channels > 8) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid channel count (channels=%u, valid=1-8)", batch->channels);
  }

  // Validate sample rate
  if (!audio_is_supported_sample_rate(batch->sample_rate)) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unsupported sample rate (sample_rate=%u)", batch->sample_rate);
  }

  // Check for reasonable sample counts
  if (batch->total_samples == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch has zero samples");
  }

  // Each batch typically has samples_per_frame worth of samples
  // For 48kHz at 20ms per frame: 48000 * 0.02 = 960 samples per frame
  // With max 256 frames, that's up to ~245k samples per batch
  if (batch->total_samples > 1000000) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Audio batch sample count suspiciously large (total_samples=%u)",
                     batch->total_samples);
  }

  return ASCIICHAT_OK;
}

bool audio_is_supported_sample_rate(uint32_t sample_rate) {
  // List of commonly supported audio sample rates
  static const uint32_t supported_rates[] = {
      8000,   // Telephone quality
      16000,  // Wideband telephony
      24000,  // High quality speech
      32000,  // Good for video
      44100,  // CD quality (less common in VoIP)
      48000,  // Standard professional
      96000,  // High-end professional
      192000, // Ultra-high-end mastering
  };

  const size_t rate_count = sizeof(supported_rates) / sizeof(supported_rates[0]);
  for (size_t i = 0; i < rate_count; i++) {
    if (sample_rate == supported_rates[i]) {
      return true;
    }
  }

  return false;
}
