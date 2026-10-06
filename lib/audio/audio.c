
/**
 * @file audio.c
 * @ingroup audio
 * @brief 🔊 Audio capture and playback using PortAudio with buffer management
 */

#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/visualization.h>
#include <ascii-chat/audio/recording.h>
#include <ascii-chat/audio/client_pipeline.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/io.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>       // For monotonic timing
#include <ascii-chat/util/lifecycle.h>  // For lifecycle_t
#include <ascii-chat/asciichat_errno.h> // For asciichat_errno system
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/init.h>         // For static_mutex_t
#include <ascii-chat/platform/abstraction.h>  // For platform_sleep_us
#include <ascii-chat/network/packet/packet.h> // For audio_batch_packet_t
#include <ascii-chat/log/log.h>               // For log_* macros
#include <ascii-chat/debug/named.h>
#include <ascii-chat/media/source.h> // For media_source_read_audio()
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ascii-chat/atomic.h>

#ifdef _WIN32
#include <malloc.h> // For _alloca on Windows
#define alloca _alloca
#else
#include <unistd.h> // For dup, dup2, close, STDERR_FILENO
#include <fcntl.h>  // For O_WRONLY
#endif

// PortAudio initialization lifecycle (one-time init/shutdown, no refcounting)
// Uses lifecycle_t for thread-safe, lock-free initialization tracking
static lifecycle_t g_pa_lc = LIFECYCLE_INIT;

/**
 * @brief Ensure PortAudio is initialized
 *
 * This is the single centralized function that initializes PortAudio exactly once.
 * All code paths (audio_init, device enumeration) call this to prevent
 * multiple independent Pa_Initialize() calls and duplicate ALSA/PulseAudio probing.
 *
 * Uses lifecycle_t for thread-safe, lock-free initialization tracking.
 * PortAudio remains initialized for the lifetime of the process.
 * Call audio_terminate_portaudio_final() from asciichat_shared_destroy() to clean up.
 *
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t audio_ensure_portaudio_initialized(void) {
  // Check if already initialized
  if (lifecycle_is_initialized(&g_pa_lc)) {
    return ASCIICHAT_OK;
  }

  // Try to win the initialization race
  if (!lifecycle_init(&g_pa_lc, "portaudio")) {
    return ASCIICHAT_OK; // Already initialized by another thread
  }

  // First initialization - call Pa_Initialize() exactly once
  // Suppress PortAudio backend probe errors (ALSA/JACK/OSS warnings)
  // These are harmless - PortAudio tries multiple backends until one works
  PaError err;
  LOG_IO("portaudio", { err = Pa_Initialize(); });

  if (err != paNoError) {
    return SET_ERRNO(ERROR_AUDIO, "Failed to initialize PortAudio: %s", Pa_GetErrorText(err));
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Terminate PortAudio and free all device resources
 *
 * This must be called to actually free device structures allocated by ALSA/libpulse.
 * It should be called AFTER all audio contexts are destroyed and session cleanup is complete.
 */
void audio_terminate_portaudio_final(void) {
  if (lifecycle_shutdown(&g_pa_lc)) {
    log_debug("[PORTAUDIO_TERM] Calling Pa_Terminate() to release PortAudio");

    PaError err;
    LOG_IO("portaudio", { err = Pa_Terminate(); });

    log_debug("[PORTAUDIO_TERM] Pa_Terminate() returned: %s", Pa_GetErrorText(err));
  }
}

// AEC processes complete 480-sample blocks; the worker buffers must hold at least one block.
#define WORKER_BATCH_FRAMES 480
#define WORKER_BATCH_SAMPLES (WORKER_BATCH_FRAMES * AUDIO_CHANNELS)
#define WORKER_TIMEOUT_MS 1 // Wake up every 1ms to keep up with 48kHz playback (was 3ms)

static void audio_publish_local_capture(audio_context_t *ctx, float *samples, size_t count, bool use_media) {
  uint64_t now = time_get_ns();
  uint64_t duration = count * 1000000000ULL / 48000;
  uint64_t block_start = now > duration ? now - duration : 0;
  audio_recording_submit(AUDIO_RECORDING_MIC, samples, count, block_start);
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, samples, count);
  if (use_media) {
    float media[WORKER_BATCH_SAMPLES];
    size_t read = media_source_read_audio(ctx->capture_media_source, media, count);
    audio_recording_submit(AUDIO_RECORDING_MEDIA, media, read, block_start);
    audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MEDIA, media, read);
    for (size_t i = 0; i < read; i++)
      samples[i] = fmaxf(-1.0f, fminf(1.0f, samples[i] + media[i]));
    if (ctx->monitor_local_media)
      audio_ring_buffer_write(ctx->playback_buffer, media, (int)read);
  }
  audio_ring_buffer_write(ctx->capture_buffer, samples, (int)count);
}

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
  log_debug("Audio worker thread started (batch size: %d frames = %d samples)", WORKER_BATCH_FRAMES,
            WORKER_BATCH_SAMPLES);

  // Check for AEC3 bypass (static, checked once)
  static int bypass_aec3_worker = -1;
  if (bypass_aec3_worker == -1) {
    const char *env = platform_getenv("BYPASS_AEC3");
    bypass_aec3_worker = (env && (strcmp(env, "1") == 0 || strcmp(env, "true") == 0)) ? 1 : 0;
    if (bypass_aec3_worker) {
      log_warn("Worker thread: AEC3 BYPASSED via BYPASS_AEC3=1 (worker will skip AEC3 processing)");
    }
  }

  // Timing instrumentation for debugging
  static uint64_t loop_count = 0;
  static uint64_t timeout_count = 0;
  static uint64_t signal_count = 0;
  static uint64_t process_count = 0;

  // Detailed timing stats
  static double total_wait_ns = 0;
  static double total_capture_ns = 0;
  static double total_playback_ns = 0;
  static double max_wait_ns = 0;
  static double max_capture_ns = 0;
  static double max_playback_ns = 0;

  uint64_t media_start_ns = time_get_ns();
  uint64_t media_samples = 0;
  while (true) {
    loop_count++;
    uint64_t loop_start_ns = time_get_ns();

    // For output-only mode, don't wait for signal - just write continuously
    // For duplex/input modes, wait for signal from callback
    bool is_output_only = ctx->output_stream && !ctx->input_stream && !ctx->duplex_stream;

    int wait_result = 1; // Default: timeout (will trigger processing for output-only)
    if (!is_output_only) {
      // Wait for signal from callback or timeout
      mutex_lock(&ctx->worker_mutex);
      uint64_t wait_start_ns = time_get_ns();
      wait_result = cond_timedwait(&ctx->worker_cond, &ctx->worker_mutex, WORKER_TIMEOUT_MS * NS_PER_MS_INT);
      double wait_time_ns = (double)time_elapsed_ns(wait_start_ns, time_get_ns());
      mutex_unlock(&ctx->worker_mutex);

      total_wait_ns += wait_time_ns;
      if (wait_time_ns > max_wait_ns)
        max_wait_ns = wait_time_ns;
    } else {
      // Output-only mode: no wait needed, callback handles audio delivery
      // Just sleep briefly to avoid busy-looping
      Pa_Sleep(5);
      wait_result = 1;
    }

    // Check shutdown flag
    if (atomic_load_bool(&ctx->worker_should_stop)) {
      log_debug("Worker thread received shutdown signal");
      break;
    }

    // Count wake-ups
    if (wait_result == 0) {
      signal_count++;
    } else {
      timeout_count++;
    }

    // Skip processing if we timed out and no data available
    size_t capture_available = audio_ring_buffer_available_read(ctx->raw_capture_rb);
    size_t render_available = audio_ring_buffer_available_read(ctx->raw_render_rb);
    size_t playback_available = audio_ring_buffer_available_read(ctx->playback_buffer);

    // Log worker loop state every 100 iterations with detailed timing
    if (loop_count % 100 == 0) {
      char avg_wait_str[32], max_wait_str[32];
      char avg_capture_str[32], max_capture_str[32];
      char avg_playback_str[32], max_playback_str[32];
      time_pretty((uint64_t)(total_wait_ns / loop_count), -1, avg_wait_str, sizeof(avg_wait_str));
      time_pretty(max_wait_ns, -1, max_wait_str, sizeof(max_wait_str));
      time_pretty((uint64_t)(total_capture_ns / (process_count > 0 ? process_count : 1)), -1, avg_capture_str,
                  sizeof(avg_capture_str));
      time_pretty(max_capture_ns, -1, max_capture_str, sizeof(max_capture_str));
      time_pretty((uint64_t)(total_playback_ns / (process_count > 0 ? process_count : 1)), -1, avg_playback_str,
                  sizeof(avg_playback_str));
      time_pretty(max_playback_ns, -1, max_playback_str, sizeof(max_playback_str));

      log_info("Worker stats: loops=%lu, signals=%lu, timeouts=%lu, processed=%lu", loop_count, signal_count,
               timeout_count, process_count);
      log_info("Worker timing: wait avg=%s max=%s, capture avg=%s max=%s, playback avg=%s max=%s", avg_wait_str,
               max_wait_str, avg_capture_str, max_capture_str, avg_playback_str, max_playback_str);
      log_info("Worker buffers: capture=%zu, render=%zu, playback=%zu (need >= %d to process)", capture_available,
               render_available, playback_available, WORKER_BATCH_SAMPLES);
    }

    if (wait_result != 0 && capture_available == 0 && playback_available == 0 && !ctx->capture_media_source) {
      // Timeout with no data - continue waiting
      continue;
    }

    process_count++;

    bool has_media = ctx->capture_media_source && media_source_has_audio(ctx->capture_media_source);
    audio_capture_source_t selection = GET_OPTION(audio_capture_source);
    bool use_media = has_media && (selection == AUDIO_CAPTURE_SOURCE_AUTO ||
                                   selection == AUDIO_CAPTURE_SOURCE_MEDIA ||
                                   selection == AUDIO_CAPTURE_SOURCE_BOTH);
    bool use_mic = audio_should_enable_microphone(selection, has_media);
    if (use_media && !use_mic) {
      uint64_t now = time_get_ns();
      uint64_t due = (now - media_start_ns) / 1000000ULL * 48;
      float media[960];
      while (media_samples < due) {
        size_t count = due - media_samples > 960 ? 960 : (size_t)(due - media_samples);
        size_t read = media_source_read_audio(ctx->capture_media_source, media, count);
        if (read < count)
          memset(media + read, 0, (count - read) * sizeof(float));
        audio_recording_submit(AUDIO_RECORDING_MEDIA, media, count,
                               media_start_ns + media_samples * 1000000000ULL / 48000);
        audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MEDIA, media, count);
        audio_ring_buffer_write(ctx->capture_buffer, media, (int)count);
        if (ctx->monitor_local_media)
          audio_ring_buffer_write(ctx->playback_buffer, media, (int)count);
        media_samples += count;
      }
    }
    if (!use_mic && capture_available > 0) {
      size_t drain = capture_available > WORKER_BATCH_SAMPLES ? WORKER_BATCH_SAMPLES : capture_available;
      audio_ring_buffer_read(ctx->raw_capture_rb, ctx->worker_capture_batch, drain);
      capture_available = 0;
    }

    // STEP 1: Process capture path (mic → AEC3 → encoder)
    // Process capture samples if available (don't wait for full batch - reduces latency)
    // Minimum: 64 samples (1.3ms @ 48kHz) to avoid excessive overhead
    // AEC3 requires 480-sample frames (10ms @ 48kHz). Process only complete
    // frames where both render and capture are available. Remainders stay in
    // the ring buffers and get processed next iteration - no zero-padding,
    // no data corruption, no skipped echo cancellation.
    const size_t AEC3_FRAME_SIZE = 480;
    bool want_aec3 = !bypass_aec3_worker && ctx->audio_pipeline && (ctx->duplex_stream || ctx->output_stream);

    if (want_aec3) {
      // AEC3 path: process matched render+capture in 480-sample-aligned chunks
      size_t matched = (capture_available < render_available) ? capture_available : render_available;
      // Round down to AEC3 frame boundary so no partial frames
      size_t aligned = (matched / AEC3_FRAME_SIZE) * AEC3_FRAME_SIZE;
      // Cap to batch buffer size
      if (aligned > WORKER_BATCH_SAMPLES)
        aligned = (WORKER_BATCH_SAMPLES / AEC3_FRAME_SIZE) * AEC3_FRAME_SIZE;

      if (aligned > 0) {
        uint64_t capture_start_ns = time_get_ns();

        size_t capture_read = audio_ring_buffer_read(ctx->raw_capture_rb, ctx->worker_capture_batch, aligned);
        size_t render_read = audio_ring_buffer_read(ctx->raw_render_rb, ctx->worker_render_batch, aligned);

        if (capture_read > 0 && render_read > 0) {
          uint64_t aec3_start_ns = time_get_ns();

          // Both buffers have identical, frame-aligned sample counts - AEC3 sees
          // the real continuous audio stream with no padding or gaps
          client_audio_pipeline_process_duplex(ctx->audio_pipeline, ctx->worker_render_batch, (int)render_read,
                                               ctx->worker_capture_batch, (int)capture_read, ctx->worker_capture_batch);

          long aec3_ns = (long)time_elapsed_ns(aec3_start_ns, time_get_ns());

          static int aec3_count = 0;
          static long aec3_total_ns = 0;
          static long aec3_max_ns = 0;
          aec3_count++;
          aec3_total_ns += aec3_ns;
          if (aec3_ns > aec3_max_ns)
            aec3_max_ns = aec3_ns;

          if (aec3_count % 100 == 0) {
            long avg_ns = aec3_total_ns / aec3_count;
            char avg_str[32], max_str[32], latest_str[32];
            time_pretty((uint64_t)avg_ns, -1, avg_str, sizeof(avg_str));
            time_pretty((uint64_t)aec3_max_ns, -1, max_str, sizeof(max_str));
            time_pretty((uint64_t)aec3_ns, -1, latest_str, sizeof(latest_str));
            log_info("AEC3 performance: avg=%s, max=%s, latest=%s (samples=%zu, %d calls)", avg_str, max_str,
                     latest_str, capture_read, aec3_count);
          }
        }

        // Apply microphone sensitivity
        float mic_sensitivity = GET_OPTION(microphone_sensitivity);
        if (mic_sensitivity != 1.0f) {
          if (mic_sensitivity < 0.0f)
            mic_sensitivity = 0.0f;
          if (mic_sensitivity > 1.0f)
            mic_sensitivity = 1.0f;
          for (size_t i = 0; i < capture_read; i++) {
            ctx->worker_capture_batch[i] *= mic_sensitivity;
          }
        }

        audio_publish_local_capture(ctx, ctx->worker_capture_batch, capture_read, use_media);

        log_debug_every(NS_PER_MS_INT, "Worker processed %zu samples (AEC3 applied, render=%zu)", capture_read,
                        render_read);

        double capture_time_ns = (double)time_elapsed_ns(capture_start_ns, time_get_ns());
        total_capture_ns += capture_time_ns;
        if (capture_time_ns > max_capture_ns)
          max_capture_ns = capture_time_ns;
      }
    } else if (capture_available >= 64) {
      // No AEC3 (bypassed or no pipeline): process capture directly
      uint64_t capture_start_ns = time_get_ns();

      size_t samples_to_process = (capture_available > WORKER_BATCH_SAMPLES) ? WORKER_BATCH_SAMPLES : capture_available;
      size_t capture_read = audio_ring_buffer_read(ctx->raw_capture_rb, ctx->worker_capture_batch, samples_to_process);

      if (capture_read > 0) {
        // Drain render buffer to keep it from growing unbounded
        if (render_available > 0) {
          size_t drain = (render_available > WORKER_BATCH_SAMPLES) ? WORKER_BATCH_SAMPLES : render_available;
          audio_ring_buffer_read(ctx->raw_render_rb, ctx->worker_render_batch, drain);
        }

        // Capture samples are normalized to AUDIO_SAMPLE_RATE by the input callback.

        // Apply microphone sensitivity (volume control)
        float mic_sensitivity = GET_OPTION(microphone_sensitivity);
        if (mic_sensitivity != 1.0f) {
          // Clamp to valid range [0.0, 1.0]
          if (mic_sensitivity < 0.0f)
            mic_sensitivity = 0.0f;
          if (mic_sensitivity > 1.0f)
            mic_sensitivity = 1.0f;

          for (size_t i = 0; i < capture_read; i++) {
            ctx->worker_capture_batch[i] *= mic_sensitivity;
          }
        }

        // Write processed capture to encoder buffer
        audio_publish_local_capture(ctx, ctx->worker_capture_batch, capture_read, use_media);

        log_debug_every(NS_PER_MS_INT, "Worker processed %zu capture samples (AEC3 %s)", capture_read,
                        bypass_aec3_worker ? "BYPASSED" : "applied");
      }

      double capture_time_ns = (double)time_elapsed_ns(capture_start_ns, time_get_ns());
      total_capture_ns += capture_time_ns;
      if (capture_time_ns > max_capture_ns)
        max_capture_ns = capture_time_ns;
    }

    // STEP 2: Output/Playback handling
    // With output_callback registered, PortAudio handles calling it automatically
    // No need for worker thread to manually write data via Pa_WriteStream
    (void)playback_available; // Suppress unused variable warning if not used in this build

    // Log overall loop iteration time
    double loop_time_ns = (double)time_elapsed_ns(loop_start_ns, time_get_ns());
    static double total_loop_ns = 0;
    static double max_loop_ns = 0;
    total_loop_ns += loop_time_ns;
    if (loop_time_ns > max_loop_ns)
      max_loop_ns = loop_time_ns;

    if (loop_count % 100 == 0) {
      char avg_loop_str[32], max_loop_str[32];
      time_pretty((uint64_t)(total_loop_ns / loop_count), -1, avg_loop_str, sizeof(avg_loop_str));
      time_pretty(max_loop_ns, -1, max_loop_str, sizeof(max_loop_str));
      log_info("Worker loop timing: avg=%s max=%s", avg_loop_str, max_loop_str);
    }
  }

  log_debug("Audio worker thread exiting");
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

  static uint64_t duplex_invoke_count = 0;
  duplex_invoke_count++;
  if (duplex_invoke_count == 1) {
    log_warn("!!! DUPLEX_CALLBACK INVOKED FOR FIRST TIME !!!");
  }

  uint64_t callback_start_ns = time_get_ns();

  static uint64_t total_callbacks = 0;
  total_callbacks++;
  if (total_callbacks == 1) {
    log_warn("FIRST CALLBACK RECEIVED! total=%llu frames=%lu", (unsigned long long)total_callbacks, framesPerBuffer);
  }

  audio_context_t *ctx = (audio_context_t *)userData;
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "duplex_callback: ctx is NULL");
    return paAbort;
  }

  log_info_every(100 * NS_PER_MS_INT, "CB_START: ctx=%p output=%p inputBuffer=%p", (void *)ctx, (void *)outputBuffer,
                 inputBuffer);

  const float *input = (const float *)inputBuffer;
  float *output = (float *)outputBuffer;
  size_t num_samples = framesPerBuffer * AUDIO_CHANNELS;

  // Silence on shutdown
  if (atomic_load_bool(&ctx->shutting_down)) {
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

  // Static counters for playback tracking (used in logging below)
  static uint64_t total_samples_read_local = 0;
  static uint64_t underrun_count_local = 0;

  // The worker supplies local media and the receive path supplies remote audio.
  if (output) {
    size_t samples_read = 0;

    if (ctx->playback_buffer) {
      // Network mode: read from playback buffer with jitter buffering logic
      samples_read = audio_ring_buffer_read(ctx->playback_buffer, output, num_samples);

      static uint64_t playback_count = 0;
      playback_count++;
      if (playback_count <= 5 || playback_count % 500 == 0) {
        log_info("Callback #%lu: playback_buffer path, read %zu samples", playback_count, samples_read);
      }
    } else {
      static uint64_t null_count = 0;
      if (++null_count == 1) {
        log_warn("Callback: playback_buffer is NULL!");
      }
    }

    total_samples_read_local += samples_read;

    if (samples_read < num_samples) {
      // Fill remaining with silence if underrun
      SAFE_MEMSET(output + samples_read, (num_samples - samples_read) * sizeof(float), 0,
                  (num_samples - samples_read) * sizeof(float));
      log_debug_every(NS_PER_MS_INT, "Audio playback underrun: got %zu/%zu samples", samples_read, num_samples);
    }

    // Apply speaker volume control
    if (samples_read > 0) {
      float speaker_volume = GET_OPTION(speakers_volume);
      // Clamp to valid range [0.0, 1.0]
      if (speaker_volume < 0.0f) {
        speaker_volume = 0.0f;
      } else if (speaker_volume > 1.0f) {
        speaker_volume = 1.0f;
      }
      // Apply volume scaling if not at 100%
      if (speaker_volume != 1.0f) {
        log_debug_every(48000, "Applying audio volume %.1f%% to %zu samples", speaker_volume * 100.0, samples_read);
        for (size_t i = 0; i < samples_read; i++) {
          output[i] *= speaker_volume;
        }
      } else {
        log_debug_every(48000, "Audio at 100%% volume, no scaling needed");
      }
    }
  }

  // STEP 2: Copy raw mic samples → worker for AEC3 processing (~0.5ms)
  // Skip microphone capture in playback-only mode (mirror with file/URL audio)
  // When files (--file) or URLs (--url) are being played, microphone input is completely disabled
  // to prevent feedback loops and interference with playback audio
  if (!ctx->playback_only && input && ctx->raw_capture_rb) {
    audio_ring_buffer_write(ctx->raw_capture_rb, input, (int)num_samples);
  } else if (ctx->playback_only && input) {
    // Explicitly discard microphone input when in playback-only mode
    // This ensures complete isolation between microphone and media playback
    (void)input; // Suppress unused parameter warning
  }

  // STEP 3: Copy raw speaker samples → worker for AEC3 reference (~0.5ms)
  // This is CRITICAL for AEC3 - worker needs exact render signal at same time as capture
  // In playback-only mode, we still write the render reference for consistency
  if (output && ctx->raw_render_rb) {
    audio_ring_buffer_write(ctx->raw_render_rb, output, (int)num_samples);
  }

  // STEP 4: Signal worker thread (non-blocking, ~0.1ms)
  // Worker wakes up, processes batch, writes back to processed buffers
  cond_signal(&ctx->worker_cond);

  // Log callback timing and playback stats periodically
  double callback_time_ns = (double)time_elapsed_ns(callback_start_ns, time_get_ns());
  static double total_callback_ns = 0;
  static double max_callback_ns = 0;
  static uint64_t callback_count = 0;

  callback_count++;
  total_callback_ns += callback_time_ns;
  if (callback_time_ns > max_callback_ns)
    max_callback_ns = callback_time_ns;

  if (callback_count % 500 == 0) { // Log every ~10 seconds @ 48 FPS
    char avg_str[32], max_str[32];
    time_pretty((uint64_t)(total_callback_ns / callback_count), -1, avg_str, sizeof(avg_str));
    time_pretty(max_callback_ns, -1, max_str, sizeof(max_str));
    log_info("Duplex callback timing: count=%lu, avg=%s, max=%s (budget: 2ms)", callback_count, avg_str, max_str);
    log_info("Playback stats: total_samples_read=%lu, underruns=%lu, read_success_rate=%.1f%%",
             total_samples_read_local, underrun_count_local,
             100.0 * (double)(callback_count - underrun_count_local) / (double)callback_count);

    // DEBUG: Log first few output samples to verify they're not zero
    if (output && num_samples >= 4) {
      log_info("Output sample check: first4=[%.4f, %.4f, %.4f, %.4f] (verifying audio is not silent)", output[0],
               output[1], output[2], output[3]);
    }
  }

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
 * Native-rate playback is converted to the device rate here with fixed-size,
 * stateful interpolation; no allocation or locking is performed.
 */
static int output_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                           const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags,
                           void *userData) {
  (void)inputBuffer;
  (void)timeInfo;

  static uint64_t output_cb_invoke_count = 0;
  output_cb_invoke_count++;
  if (output_cb_invoke_count == 1) {
    log_warn("!!! OUTPUT_CALLBACK INVOKED FOR FIRST TIME !!!");
  }

  audio_context_t *ctx = (audio_context_t *)userData;
  float *output = (float *)outputBuffer;
  size_t num_samples = framesPerBuffer * AUDIO_CHANNELS;

  static uint64_t output_cb_count = 0;
  output_cb_count++;
  if (output_cb_count == 1) {
    log_debug("First output callback: frames=%lu", framesPerBuffer);
  }

  // Silence on shutdown
  if (atomic_load_bool(&ctx->shutting_down)) {
    if (output) {
      SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
    }
    return paContinue;
  }

  if (statusFlags & paOutputUnderflow) {
    log_warn_every(LOG_RATE_FAST, "PortAudio output underflow (separate stream)");
  }

  // STEP 1: Read audio source
  size_t samples_read = 0;
  if (output) {
    const bool resample_output = ctx->output_device_rate > 0.0 && ctx->output_device_rate != AUDIO_SAMPLE_RATE;
    float source[AUDIO_FRAMES_PER_BUFFER * 8];
    float *source_output = resample_output ? source : output;
    size_t source_capacity = resample_output ? sizeof(source) / sizeof(source[0]) : num_samples;

    if (resample_output && framesPerBuffer > AUDIO_FRAMES_PER_BUFFER) {
      log_warn_every(LOG_RATE_FAST, "Output callback block too large to resample safely (%lu frames)", framesPerBuffer);
      SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
      cond_signal(&ctx->worker_cond);
      return paContinue;
    }

    size_t source_request = num_samples;
    if (resample_output) {
      double step = (double)AUDIO_SAMPLE_RATE / ctx->output_device_rate;
      double last_position = ctx->output_resample_phase + (double)(num_samples - 1) * step;
      source_request = (last_position < 0.0) ? 1 : (size_t)floor(last_position) + 2;
      if (source_request > source_capacity) {
        log_warn_every(LOG_RATE_FAST, "Output resample request exceeds callback buffer (%zu samples)", source_request);
        SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
        cond_signal(&ctx->worker_cond);
        return paContinue;
      }
    }

    if (ctx->playback_buffer) {
      samples_read = audio_ring_buffer_read(ctx->playback_buffer, source_output, source_request);
      if (output_cb_count <= 3) {
        log_warn("OUTPUT_CB: playback_buffer path, read %zu samples", samples_read);
      }
    } else {
      if (output_cb_count <= 3) {
        log_warn("Output callback has no playback buffer");
      }
    }

    if (resample_output) {
      float render_volume = GET_OPTION(speakers_volume);
      if (render_volume < 0.0f) {
        render_volume = 0.0f;
      } else if (render_volume > 1.0f) {
        render_volume = 1.0f;
      }
      if (render_volume != 1.0f) {
        for (size_t i = 0; i < samples_read; i++) {
          source[i] *= render_volume;
        }
      }

      // Keep the AEC render reference in the pipeline's 48 kHz domain.
      if (ctx->render_buffer && samples_read > 0) {
        audio_ring_buffer_write(ctx->render_buffer, source, (int)samples_read);
      }

      double step = (double)AUDIO_SAMPLE_RATE / ctx->output_device_rate;
      size_t generated = 0;
      for (; generated < num_samples; generated++) {
        double position = ctx->output_resample_phase + (double)generated * step;
        float first;
        float second;
        double fraction;
        if (position < 0.0) {
          if (!ctx->output_resample_has_previous || samples_read == 0) {
            break;
          }
          first = ctx->output_resample_previous;
          second = source[0];
          fraction = position + 1.0;
        } else {
          size_t index = (size_t)position;
          if (index + 1 >= samples_read) {
            break;
          }
          first = source[index];
          second = source[index + 1];
          fraction = position - (double)index;
        }
        output[generated] = (float)((1.0 - fraction) * first + fraction * second);
      }

      if (samples_read > 0) {
        ctx->output_resample_phase += (double)generated * step - (double)samples_read;
        ctx->output_resample_previous = source[samples_read - 1];
        ctx->output_resample_has_previous = true;
      }
      samples_read = generated;
    }

    // Apply speaker volume control
    if (samples_read > 0 && !resample_output) {
      float speaker_volume = GET_OPTION(speakers_volume);
      // Clamp to valid range [0.0, 1.0]
      if (speaker_volume < 0.0f) {
        speaker_volume = 0.0f;
      } else if (speaker_volume > 1.0f) {
        speaker_volume = 1.0f;
      }
      // Apply volume scaling if not at 100%
      if (speaker_volume != 1.0f) {
        log_debug_every(48000, "OUTPUT_CALLBACK: Applying volume %.0f%% to %zu samples", speaker_volume * 100.0,
                        samples_read);
        for (size_t i = 0; i < samples_read; i++) {
          output[i] *= speaker_volume;
        }
      }
    }

    // Fill remaining with silence if underrun
    if (samples_read < num_samples) {
      SAFE_MEMSET(output + samples_read, (num_samples - samples_read) * sizeof(float), 0,
                  (num_samples - samples_read) * sizeof(float));
    }

    // STEP 2: Copy to render buffer for input callback (AEC3 reference)
    if (!resample_output && ctx->render_buffer && samples_read > 0) {
      audio_ring_buffer_write(ctx->render_buffer, output, (int)samples_read);
    }
  }

  // STEP 3: Signal worker
  cond_signal(&ctx->worker_cond);

  return paContinue;
}

/**
 * Separate input callback - handles capture only (separate streams mode).
 *
 * NEW ARCHITECTURE (Real-Time Safe):
 * - Convert native-rate mic to 48 kHz and copy to worker (~0.5ms)
 * - Read render reference from render_buffer (~0.5ms)
 * - Copy render → worker (~0.5ms)
 * - Signal worker (~0.1ms)
 * TOTAL: ~1.6ms ✓
 *
 * AEC3 processing (with render reference) is handled by worker thread.
 * Resampling uses bounded stack buffers and persistent per-context phase state.
 */
static size_t resample_capture_block(audio_context_t *ctx, const float *input, size_t input_samples, float *output,
                                     size_t output_capacity) {
  if (!ctx || !input || !output || input_samples == 0 || ctx->input_device_rate <= 0.0) {
    return 0;
  }

  const double step = ctx->input_device_rate / AUDIO_SAMPLE_RATE;
  double position = ctx->input_resample_phase;
  size_t produced = 0;

  // The previous block's last sample is the sample immediately before input[0].
  // Retaining position and that sample avoids restarting interpolation at each
  // PortAudio callback boundary and preserves the long-term sample-rate ratio.
  while (position < (double)input_samples) {
    float first;
    float second;
    double fraction;
    if (position < 0.0) {
      if (!ctx->input_resample_has_previous) {
        position += step;
        continue;
      }
      first = ctx->input_resample_previous;
      second = input[0];
      fraction = position + 1.0;
    } else {
      size_t index = (size_t)position;
      if (index + 1 >= input_samples) {
        break; // Keep the endpoint for interpolation with the next callback.
      }
      first = input[index];
      second = input[index + 1];
      fraction = position - (double)index;
    }

    if (produced == output_capacity) {
      return 0;
    }
    output[produced++] = (float)((1.0 - fraction) * first + fraction * second);
    position += step;
  }

  ctx->input_resample_phase = position - (double)input_samples;
  ctx->input_resample_previous = input[input_samples - 1];
  ctx->input_resample_has_previous = true;
  return produced;
}

static int input_callback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
  (void)outputBuffer;
  (void)timeInfo;

  static uint64_t input_invoke_count = 0;
  input_invoke_count++;
  if (input_invoke_count == 1) {
    log_warn("!!! INPUT_CALLBACK INVOKED FOR FIRST TIME !!!");
  }

  audio_context_t *ctx = (audio_context_t *)userData;
  const float *input = (const float *)inputBuffer;
  size_t num_samples = framesPerBuffer * AUDIO_CHANNELS;

  // Track callback frequency
  static uint64_t callback_count = 0;
  static uint64_t last_log_time_ns = 0;
  callback_count++;

  uint64_t now_ns = time_get_ns();

  if (last_log_time_ns == 0) {
    last_log_time_ns = now_ns;
  } else {
    long elapsed_ms = (long)time_ns_to_ms(time_elapsed_ns(last_log_time_ns, now_ns));
    if (elapsed_ms >= 1000) {
      log_info("Input callback: %lu calls/sec, %lu frames/call, %zu samples/call", callback_count, framesPerBuffer,
               num_samples);
      callback_count = 0;
      last_log_time_ns = now_ns;
    }
  }

  // Silence on shutdown
  if (atomic_load_bool(&ctx->shutting_down)) {
    return paContinue;
  }

  if (statusFlags & paInputOverflow) {
    log_warn_every(LOG_RATE_FAST, "PortAudio input overflow (separate stream)");
  }

  // STEP 1: Copy raw mic samples → worker for AEC3 processing
  // Skip microphone capture in playback-only mode (mirror)
  if (!ctx->playback_only && input && ctx->raw_capture_rb) {
    const float *capture = input;
    size_t capture_samples = num_samples;
    float normalized[AUDIO_FRAMES_PER_BUFFER * 8];
    if (ctx->input_device_rate > 0.0 && ctx->input_device_rate != AUDIO_SAMPLE_RATE) {
      // PortAudio delivers frames at the native stream rate. Convert them to
      // the pipeline's fixed 48 kHz clock before AEC3/Opus.
      size_t output_samples = resample_capture_block(ctx, input, num_samples, normalized,
                                                     sizeof(normalized) / sizeof(normalized[0]));
      if (output_samples == 0) {
        log_warn_every(LOG_RATE_FAST, "Input callback block too large to resample safely (%zu samples)", num_samples);
        cond_signal(&ctx->worker_cond);
        return paContinue;
      }
      capture = normalized;
      capture_samples = output_samples;
    }
    audio_ring_buffer_write(ctx->raw_capture_rb, capture, (int)capture_samples);

    // Keep the render reference in the same 48 kHz sample domain as capture.
    num_samples = capture_samples;
  }

  // STEP 2: Read render reference from render_buffer and copy to worker
  // (render_buffer is written by output_callback, read here for synchronization)
  if (ctx->render_buffer && ctx->raw_render_rb) {
    // Worker will handle the AEC3 processing using this render reference
    size_t render_available = audio_ring_buffer_available_read(ctx->render_buffer);
    if (render_available >= num_samples) {
      // Read exactly what we need
      float render_temp[AUDIO_FRAMES_PER_BUFFER * 8]; // Allows the converted 48 kHz callback block.
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
  atomic_store_u64(&rb->write_index, 0);
  atomic_store_u64(&rb->read_index, 0);
  atomic_store_bool(&rb->discard_pending, false);
  // For capture buffers (jitter_buffer_enabled=false), mark as already filled to bypass jitter logic
  // For playback buffers (jitter_buffer_enabled=true), start unfilled to wait for threshold
  atomic_store_bool(&rb->jitter_buffer_filled, !jitter_buffer_enabled);
  atomic_store_u64(&rb->crossfade_samples_remaining, 0);
  atomic_store_bool(&rb->crossfade_fade_in, false);
  rb->last_sample = 0.0f;
  atomic_store_u64(&rb->underrun_count, 0);
  rb->jitter_buffer_enabled = jitter_buffer_enabled;

  if (mutex_init(&rb->mutex, "audio_ring_buffer") != 0) {
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

  NAMED_UNREGISTER(rb);
  audio_ring_buffer_unregister_atomics(rb);

  mutex_destroy(&rb->mutex);
  buffer_pool_free(NULL, rb, sizeof(audio_ring_buffer_t));
}

void audio_ring_buffer_register_atomics(audio_ring_buffer_t *rb, const char *context_name) {
  if (!rb || !context_name || !context_name[0]) {
    return;
  }

  // Register the ring buffer itself with a unique name
  NAMED_REGISTER_AUDIO_RINGBUF(rb, context_name, NULL);

  // Ring buffer indices (lock-free producer-consumer)
  NAMED_REGISTER_ATOMIC(&rb->write_index, "write_index_producer_position", (uintptr_t)(const void *)(rb));
  NAMED_REGISTER_ATOMIC(&rb->read_index, "read_index_consumer_position", (uintptr_t)(const void *)(rb));

  // Jitter buffer state management
  NAMED_REGISTER_ATOMIC(&rb->jitter_buffer_filled, "jitter_buffer_has_filled_threshold_flag",
                        (uintptr_t)(const void *)(rb));
  NAMED_REGISTER_ATOMIC(&rb->crossfade_samples_remaining, "crossfade_samples_remaining_count",
                        (uintptr_t)(const void *)(rb));
  NAMED_REGISTER_ATOMIC(&rb->crossfade_fade_in, "crossfade_fade_in_direction_flag", (uintptr_t)(const void *)(rb));

  // Audio quality monitoring
  NAMED_REGISTER_ATOMIC(&rb->underrun_count, "audio_underrun_event_counter", (uintptr_t)(const void *)(rb));
}

void audio_ring_buffer_unregister_atomics(audio_ring_buffer_t *rb) {
  if (!rb) {
    return;
  }

  NAMED_UNREGISTER(&rb->write_index);
  NAMED_UNREGISTER(&rb->read_index);
  NAMED_UNREGISTER(&rb->jitter_buffer_filled);
  NAMED_UNREGISTER(&rb->crossfade_samples_remaining);
  NAMED_UNREGISTER(&rb->crossfade_fade_in);
  NAMED_UNREGISTER(&rb->underrun_count);
}

void audio_ring_buffer_clear(audio_ring_buffer_t *rb) {
  if (!rb)
    return;

  mutex_lock(&rb->mutex);
  // Reset buffer to empty state (no audio to play = silence at shutdown)
  atomic_store_u64(&rb->write_index, 0);
  atomic_store_u64(&rb->read_index, 0);
  rb->last_sample = 0.0f;
  // Clear the actual data to zeros to prevent any stale audio
  SAFE_MEMSET(rb->data, sizeof(rb->data), 0, sizeof(rb->data));
  mutex_unlock(&rb->mutex);
}

void audio_ring_buffer_discard_pending(audio_ring_buffer_t *rb) {
  if (rb) {
    atomic_store_bool(&rb->discard_pending, true);
  }
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
  unsigned int write_idx = atomic_load_u64(&rb->write_index);
  unsigned int read_idx = atomic_load_u64(&rb->read_index);

  // Calculate current buffer level (how many samples are buffered)
  int buffer_level;
  if (write_idx >= read_idx) {
    buffer_level = (int)(write_idx - read_idx);
  } else {
    buffer_level = AUDIO_RING_BUFFER_SIZE - (int)(read_idx - write_idx);
  }
  // Reserve 1 slot to distinguish between full and empty states
  // (when buffer is full, write_idx will be just before read_idx, not equal to it)
  int available = AUDIO_RING_BUFFER_SIZE - 1 - buffer_level;

  // HIGH WATER MARK: Drop INCOMING samples to prevent latency accumulation
  // Writer must not modify read_index (race condition with reader).
  // Instead, we drop incoming samples to keep buffer bounded.
  // This sacrifices newest data to prevent unbounded latency growth.
  if (buffer_level > AUDIO_JITTER_HIGH_WATER_MARK) {
    // Buffer is already too full - drop incoming samples to maintain target level
    int target_writes = AUDIO_JITTER_TARGET_LEVEL - buffer_level;
    if (target_writes < 0) {
      target_writes = 0; // Buffer is way over - drop everything
    }

    if (samples > target_writes) {
      int dropped = samples - target_writes;
      log_warn_every(LOG_RATE_FAST,
                     "Audio buffer high water mark exceeded (%d > %d): dropping %d INCOMING samples "
                     "(keeping newest %d to maintain target %d)",
                     buffer_level, AUDIO_JITTER_HIGH_WATER_MARK, dropped, target_writes, AUDIO_JITTER_TARGET_LEVEL);
      samples = target_writes; // Only write what fits within target level
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
    atomic_store_u64(&rb->write_index, new_write_idx);
  }

  // Note: jitter buffer fill check is now done in read function for better control

  return ASCIICHAT_OK; // Success
}

size_t audio_ring_buffer_read(audio_ring_buffer_t *rb, float *data, size_t samples) {
  if (!rb || !data || samples <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: rb=%p, data=%p, samples=%d", rb, data, samples);
    return 0; // Return 0 samples read on error
  }

  // Keep read_index consumer-owned while letting a producer request a flush.
  if (atomic_exchange_bool(&rb->discard_pending, false)) {
    unsigned int write_idx = atomic_load_u64(&rb->write_index);
    atomic_store_u64(&rb->read_index, write_idx);
    rb->last_sample = 0.0f;
    atomic_store_u64(&rb->crossfade_samples_remaining, 0);
    return 0;
  }

  // LOCK-FREE: Load indices with proper memory ordering
  // - Load writer's write_index with acquire (see writer's data updates)
  // - Load our own read_index with relaxed (no sync needed with ourselves)
  unsigned int write_idx = atomic_load_u64(&rb->write_index);
  unsigned int read_idx = atomic_load_u64(&rb->read_index);

  // Calculate available samples
  size_t available;
  if (write_idx >= read_idx) {
    available = write_idx - read_idx;
  } else {
    available = AUDIO_RING_BUFFER_SIZE - read_idx + write_idx;
  }

  // LOCK-FREE: Load jitter buffer state with acquire ordering
  bool jitter_filled = atomic_load_u64(&rb->jitter_buffer_filled);
  int crossfade_remaining = atomic_load_u64(&rb->crossfade_samples_remaining);
  bool fade_in = atomic_load_u64(&rb->crossfade_fade_in);

  // Jitter buffer: don't read until initial fill threshold is reached
  // (only for playback buffers - capture buffers have jitter_buffer_enabled = false)
  if (!jitter_filled && rb->jitter_buffer_enabled) {
    // Check if we've accumulated enough samples to start playback
    if (available >= AUDIO_JITTER_BUFFER_THRESHOLD) {
      atomic_store_u64(&rb->jitter_buffer_filled, true);
      atomic_store_u64(&rb->crossfade_samples_remaining, AUDIO_CROSSFADE_SAMPLES);
      atomic_store_u64(&rb->crossfade_fade_in, true);
      log_info("Jitter buffer filled (%zu samples), starting playback with fade-in", available);
      // Reload state for processing below
      jitter_filled = true;
      crossfade_remaining = AUDIO_CROSSFADE_SAMPLES;
      fade_in = true;
    } else {
      // Log buffer fill progress every second
      log_debug_every(NS_PER_MS_INT, "Jitter buffer filling: %zu/%d samples (%.1f%%)", available,
                      AUDIO_JITTER_BUFFER_THRESHOLD, (100.0f * available) / AUDIO_JITTER_BUFFER_THRESHOLD);
      return 0; // Return 0 samples - caller will pad with silence
    }
  }

  // Periodic buffer health logging (every 5 seconds when healthy)
  unsigned int underruns = atomic_load_u64(&rb->underrun_count);
  log_dev_every(5 * NS_PER_MS_INT, "Buffer health: %zu/%d samples (%.1f%%), underruns=%u", available,
                AUDIO_RING_BUFFER_SIZE, (100.0f * available) / AUDIO_RING_BUFFER_SIZE, underruns);

  // Low buffer handling: DON'T pause playback - continue reading what's available
  // and fill the rest with silence. Pausing causes a feedback loop where:
  // 1. Underrun -> pause reading -> buffer overflows from incoming samples
  // 2. Threshold reached -> resume reading -> drains too fast -> underrun again
  //
  // Instead: always consume samples to prevent overflow, use silence for missing data
  if (rb->jitter_buffer_enabled && available < AUDIO_JITTER_LOW_WATER_MARK) {
    unsigned int underrun_count = atomic_fetch_add_u64(&rb->underrun_count, 1) + 1;
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
  atomic_store_u64(&rb->read_index, new_read_idx);

  // Apply fade-in if recovering from underrun
  if (fade_in && crossfade_remaining > 0) {
    int fade_start = AUDIO_CROSSFADE_SAMPLES - crossfade_remaining;
    size_t fade_samples = (to_read < (size_t)crossfade_remaining) ? to_read : (size_t)crossfade_remaining;

    for (size_t i = 0; i < fade_samples; i++) {
      float fade_factor = (float)(fade_start + (int)i + 1) / (float)AUDIO_CROSSFADE_SAMPLES;
      data[i] *= fade_factor;
    }

    int new_crossfade_remaining = crossfade_remaining - (int)fade_samples;
    atomic_store_u64(&rb->crossfade_samples_remaining, new_crossfade_remaining);
    if (new_crossfade_remaining <= 0) {
      atomic_store_u64(&rb->crossfade_fade_in, false);
      log_debug("Audio fade-in complete");
    }
  }

  // Save last sample for potential fade-out
  // Note: only update if we actually read some data
  // This is NOT atomic - only the reader thread writes this
  if (to_read > 0) {
    rb->last_sample = data[to_read - 1];
  }

  // Return ACTUAL number of samples read, not padded count
  // The caller (mixer) expects truthful return values to detect underruns
  // and handle silence padding externally. Internal padding creates double-padding bugs.
  //
  // This function was incorrectly returning `samples` even when
  // it only read `to_read` samples. This broke the mixer's underrun detection.
  return to_read;
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
  unsigned int write_idx = atomic_load_u64(&rb->write_index);
  unsigned int read_idx = atomic_load_u64(&rb->read_index);

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
  unsigned int write_idx = atomic_load_u64(&rb->write_index);
  unsigned int read_idx = atomic_load_u64(&rb->read_index);

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
  log_debug("audio_init: starting, ctx=%p", (void *)ctx);
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx is NULL");
  }

  SAFE_MEMSET(ctx, sizeof(audio_context_t), 0, sizeof(audio_context_t));

  if (mutex_init(&ctx->state_mutex, "audio_context") != 0) {
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize audio context mutex");
  }

  // NOTE: PortAudio initialization deferred to audio_start_duplex() where streams are actually opened
  // This avoids Pa_Initialize() overhead for contexts that might not start duplex
  // and prevents premature ALSA device allocation and memory leaks

  // Create capture buffer WITHOUT jitter buffering (PortAudio writes directly from microphone)
  ctx->capture_buffer = audio_ring_buffer_create_for_capture();
  if (!ctx->capture_buffer) {
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create capture buffer");
  }

  ctx->playback_buffer = audio_ring_buffer_create();
  if (!ctx->playback_buffer) {
    audio_ring_buffer_destroy(ctx->capture_buffer);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create playback buffer");
  }

  // Create new ring buffers for worker thread architecture
  ctx->raw_capture_rb = audio_ring_buffer_create_for_capture();
  if (!ctx->raw_capture_rb) {
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create raw capture buffer");
  }

  ctx->raw_render_rb = audio_ring_buffer_create_for_capture();
  if (!ctx->raw_render_rb) {
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create raw render buffer");
  }

  ctx->processed_playback_rb = audio_ring_buffer_create();
  if (!ctx->processed_playback_rb) {
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to create processed playback buffer");
  }

  // Initialize worker thread infrastructure
  if (mutex_init(&ctx->worker_mutex, "audio_worker_mutex") != 0) {
    audio_ring_buffer_destroy(ctx->processed_playback_rb);
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_THREAD, "Failed to initialize worker mutex");
  }

  if (cond_init(&ctx->worker_cond, "audio_worker_cond") != 0) {
    mutex_destroy(&ctx->worker_mutex);
    audio_ring_buffer_destroy(ctx->processed_playback_rb);
    audio_ring_buffer_destroy(ctx->raw_render_rb);
    audio_ring_buffer_destroy(ctx->raw_capture_rb);
    audio_ring_buffer_destroy(ctx->playback_buffer);
    audio_ring_buffer_destroy(ctx->capture_buffer);
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
    mutex_destroy(&ctx->state_mutex);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate worker playback batch buffer");
  }

  ctx->capture_media_source = NULL;
  ctx->monitor_local_media = false;

  // Initialize worker thread state (thread will be started in audio_start_duplex)
  ctx->worker_running = false;
  atomic_store_bool(&ctx->worker_should_stop, false);

  ctx->initialized = true;
  atomic_store_bool(&ctx->shutting_down, false);

  /* Register audio context with named registry */
  NAMED_REGISTER(ctx, "audio_context", "audio_context_t", "0x%tx", NULL);

  /* Register audio context's sync primitives with hierarchical naming */
  NAMED_REGISTER_MUTEX(&ctx->state_mutex, "state_mutex", (uintptr_t)(const void *)(ctx));
  NAMED_REGISTER_MUTEX(&ctx->worker_mutex, "worker_mutex", (uintptr_t)(const void *)(ctx));
  NAMED_REGISTER_COND(&ctx->worker_cond, "worker_cond", (uintptr_t)(const void *)(ctx));
  NAMED_REGISTER_ATOMIC(&ctx->worker_should_stop, "worker_should_stop", (uintptr_t)(const void *)(ctx));
  NAMED_REGISTER_ATOMIC(&ctx->shutting_down, "shutting_down", (uintptr_t)(const void *)(ctx));

  /* Register ring buffers as child structures with audio context as parent */
  NAMED_REGISTER_AUDIO_RINGBUF(ctx->capture_buffer, "capture_buffer", (uintptr_t)(const void *)(ctx));
  NAMED_REGISTER_AUDIO_RINGBUF(ctx->playback_buffer, "playback_buffer", (uintptr_t)(const void *)(ctx));
  NAMED_REGISTER_AUDIO_RINGBUF(ctx->raw_capture_rb, "raw_capture_rb", (uintptr_t)(const void *)(ctx));
  NAMED_REGISTER_AUDIO_RINGBUF(ctx->raw_render_rb, "raw_render_rb", (uintptr_t)(const void *)(ctx));
  NAMED_REGISTER_AUDIO_RINGBUF(ctx->processed_playback_rb, "processed_playback_rb", (uintptr_t)(const void *)(ctx));

  log_info("Audio system initialized successfully (worker thread architecture enabled)");
  return ASCIICHAT_OK;
}

void audio_destroy(audio_context_t *ctx) {
  if (!ctx) {
    return;
  }

  // Always release PortAudio refcount if it was incremented
  // audio_init() calls Pa_Initialize() very early, and if it fails partway through,
  // ctx->initialized will be false. But we MUST still call audio_release_portaudio()
  // to properly decrement the refcount and allow Pa_Terminate() to be called.
  if (ctx->initialized) {

    // Stop duplex stream if running (this also stops the worker thread)
    if (ctx->running) {
      audio_stop_duplex(ctx);
    }

    // Ensure worker thread is stopped even if streams weren't running
    if (ctx->worker_running) {
      log_debug("Stopping worker thread during audio_destroy");
      atomic_store_bool(&ctx->worker_should_stop, true);
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

    ctx->initialized = false;

    mutex_unlock(&ctx->state_mutex);
    mutex_destroy(&ctx->state_mutex);

    log_debug("Audio system cleanup complete (all resources released)");
  } else {
  }
}

void audio_set_pipeline(audio_context_t *ctx, void *pipeline) {
  if (!ctx)
    return;
  ctx->audio_pipeline = pipeline;
}

void audio_flush_playback_buffers(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  if (ctx->playback_buffer) {
    audio_ring_buffer_clear(ctx->playback_buffer);
  }
  if (ctx->processed_playback_rb) {
    audio_ring_buffer_clear(ctx->processed_playback_rb);
  }
  if (ctx->render_buffer) {
    audio_ring_buffer_clear(ctx->render_buffer);
  }
  if (ctx->raw_render_rb) {
    audio_ring_buffer_clear(ctx->raw_render_rb);
  }
}

asciichat_error_t audio_start_duplex(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Audio context not initialized");
  }

  // Check if already running (without holding lock during blocking operations)
  // Do this check first before acquiring any locks
  if (ctx->duplex_stream || ctx->input_stream || ctx->output_stream) {
    return ASCIICHAT_OK;
  }

  bool media_audio = ctx->capture_media_source && media_source_has_audio(ctx->capture_media_source);
  if (!audio_should_enable_microphone(GET_OPTION(audio_capture_source), media_audio))
    ctx->playback_only = true;
  bool media_only = media_audio && ctx->playback_only;

  // Media-only recording can run through the software worker without an audio
  // device. Initialize PortAudio opportunistically so monitoring still works
  // when a speaker is available.
  asciichat_error_t pa_result = audio_ensure_portaudio_initialized();
  if (pa_result != ASCIICHAT_OK && !media_only)
    return pa_result;

  if (pa_result != ASCIICHAT_OK) {
    ctx->sample_rate = AUDIO_SAMPLE_RATE;
    log_warn("PortAudio unavailable; continuing media-only audio without an audio device");
    goto start_worker;
  }

  // Setup input parameters (skip if playback-only mode)
  PaStreamParameters inputParams = {0};
  const PaDeviceInfo *inputInfo = NULL;
  bool has_input = false;

  if (!ctx->playback_only) {
    if (GET_OPTION(microphone_index) >= 0) {
      inputParams.device = GET_OPTION(microphone_index);
    } else {
      inputParams.device = Pa_GetDefaultInputDevice();
    }

    if (inputParams.device == paNoDevice) {
      return SET_ERRNO(ERROR_AUDIO, "No input device available");
    }

    inputInfo = Pa_GetDeviceInfo(inputParams.device);
    if (!inputInfo) {
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_AUDIO, "Input device info not found");
    }

    has_input = true;
    inputParams.channelCount = AUDIO_CHANNELS;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = inputInfo->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = NULL;
  }

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

  // Store device rates for diagnostics (only access if device info was retrieved)
  ctx->input_device_rate = (has_input && inputInfo) ? inputInfo->defaultSampleRate : 0;
  ctx->output_device_rate = (has_output && outputInfo) ? outputInfo->defaultSampleRate : 0;

  log_debug("Opening audio:");
  if (has_input) {
    log_info("  Input:  %s (%.0f Hz)", inputInfo->name, inputInfo->defaultSampleRate);
  } else if (ctx->playback_only) {
    log_debug("  Input:  (playback-only mode - no microphone)");
  } else {
    log_debug("  Input:  (none)");
  }
  if (has_output) {
    log_info("  Output: %s (%.0f Hz)", outputInfo->name, outputInfo->defaultSampleRate);
  } else {
    log_debug("  Output: None (input-only mode - will send audio to server)");
  }

  // Keep both device streams native and convert in the callbacks whenever a
  // device does not run at the pipeline rate. A single 48 kHz duplex stream is
  // used only when both devices natively support that rate.
  bool rates_differ = has_input && has_output &&
                      (inputInfo->defaultSampleRate != outputInfo->defaultSampleRate ||
                       inputInfo->defaultSampleRate != AUDIO_SAMPLE_RATE ||
                       outputInfo->defaultSampleRate != AUDIO_SAMPLE_RATE);
  bool try_separate = rates_differ || !has_input || !has_output;
  PaError err = paNoError;

  if (!try_separate) {
    // Try full-duplex first (preferred - perfect AEC3 timing)
    LOG_IO("portaudio", {
      err = Pa_OpenStream(&ctx->duplex_stream, &inputParams, &outputParams, AUDIO_SAMPLE_RATE, AUDIO_FRAMES_PER_BUFFER,
                          paClipOff, duplex_callback, ctx);
    });

    if (err == paNoError) {
      LOG_IO("portaudio", { err = Pa_StartStream(ctx->duplex_stream); });
      if (err != paNoError) {
        LOG_IO("portaudio", { Pa_CloseStream(ctx->duplex_stream); });
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
    // Use separate streams for non-48 kHz devices and single-direction modes.
    if (has_output && has_input) {
      log_info("Using separate native-rate input/output streams (%.0f Hz in, %.0f Hz out)",
               inputInfo->defaultSampleRate, outputInfo->defaultSampleRate);
      log_info("  Converting between device rates and the %.0f Hz audio pipeline", (double)AUDIO_SAMPLE_RATE);
    } else if (has_output) {
      log_debug("Using output-only mode (playback-only for mirror/media)");
    } else if (has_input) {
      log_info("Using input-only mode (no output device available)");
    }

    // Store the internal sample rate (buffer rate)
    ctx->sample_rate = AUDIO_SAMPLE_RATE;

    // Create render buffer for AEC3 reference synchronization
    ctx->render_buffer = audio_ring_buffer_create_for_capture();
    if (!ctx->render_buffer) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to create render buffer");
    }

    // Open output stream only if output device exists
    bool output_ok = false;
    double actual_output_rate = 0;
    if (has_output) {
      // Use the device's native rate first so conversion stays explicit and stateful.
      double native_rate = outputInfo->defaultSampleRate;
      if (native_rate <= 0.0) {
        native_rate = AUDIO_SAMPLE_RATE;
      }
      double preferred_rate = native_rate;
      double fallback_rate = (preferred_rate == AUDIO_SAMPLE_RATE) ? native_rate : AUDIO_SAMPLE_RATE;

      log_debug("Attempting output at native rate %.0f Hz (fallback %.0f Hz)", preferred_rate, fallback_rate);

      // Always use output_callback for output streams (both output-only and duplex modes)
      // PortAudio will invoke the callback whenever it needs audio data
      // The callback reads PCM supplied to the shared playback buffer.
      PaStreamCallback *callback = output_callback;

      // Try preferred rate first
      LOG_IO("portaudio", {
        err = Pa_OpenStream(&ctx->output_stream, NULL, &outputParams, preferred_rate, AUDIO_FRAMES_PER_BUFFER,
                            paClipOff, callback, ctx);
      });

      if (err == paNoError) {
        actual_output_rate = preferred_rate;
        output_ok = true;
        log_info("Output stream opened at %.0f Hz", preferred_rate);
      } else {
        log_warn("Failed to open output at %.0f Hz: %s, trying %.0f Hz", preferred_rate, Pa_GetErrorText(err),
                 fallback_rate);

        // If first Pa_OpenStream call left a partial stream, clean it up before retrying
        if (ctx->output_stream) {
          log_debug("Closing partially-opened output stream from failed preferred rate");
          LOG_IO("portaudio", { Pa_CloseStream(ctx->output_stream); });
          ctx->output_stream = NULL;
        }

        // Try the other endpoint rate if the native rate is unavailable.
        LOG_IO("portaudio", {
          err = Pa_OpenStream(&ctx->output_stream, NULL, &outputParams, fallback_rate, AUDIO_FRAMES_PER_BUFFER,
                              paClipOff, callback, ctx);
        });

        if (err == paNoError) {
          actual_output_rate = fallback_rate;
          output_ok = true;
          log_info("Output stream opened at fallback rate %.0f Hz", fallback_rate);
        } else {
          log_warn("Failed to open output stream at fallback rate %.0f Hz: %s", fallback_rate, Pa_GetErrorText(err));
          // Clean up if fallback also failed
          if (ctx->output_stream) {
            log_debug("Closing partially-opened output stream from failed native rate");
            LOG_IO("portaudio", { Pa_CloseStream(ctx->output_stream); });
            ctx->output_stream = NULL;
          }
        }
      }

      // Store actual output rate for resampling
      if (output_ok) {
        ctx->output_device_rate = actual_output_rate;
        ctx->output_resample_phase = 0.0;
        ctx->output_resample_previous = 0.0f;
        ctx->output_resample_has_previous = false;
        if (actual_output_rate != AUDIO_SAMPLE_RATE) {
          log_warn("⚠️  Output rate mismatch: %.0f Hz output vs %.0f Hz input - resampling will be used",
                   actual_output_rate, (double)AUDIO_SAMPLE_RATE);
        }
      }
    }

    // Open input stream only if we have input (skip for playback-only mode)
    bool input_ok = !has_input; // If no input, mark as OK (skip)
    if (has_input) {
      // Open at the device's native rate. The separate input callback converts
      // to AUDIO_SAMPLE_RATE before samples enter the shared processing path.
      double input_stream_rate = inputInfo->defaultSampleRate;
      if (input_stream_rate <= 0.0) {
        input_stream_rate = AUDIO_SAMPLE_RATE;
      }
      LOG_IO("portaudio", {
        err = Pa_OpenStream(&ctx->input_stream, &inputParams, NULL, input_stream_rate, AUDIO_FRAMES_PER_BUFFER,
                            paClipOff, input_callback, ctx);
      });
      input_ok = (err == paNoError);
      if (input_ok) {
        ctx->input_device_rate = input_stream_rate;
        ctx->input_resample_phase = 0.0;
        ctx->input_resample_previous = 0.0f;
        ctx->input_resample_has_previous = false;
        log_info("Input stream opened at native rate %.0f Hz", input_stream_rate);
      }

      // If input failed, try device 0 as fallback (HDMI on BeaglePlay)
      if (!input_ok) {
        log_debug("Input failed - trying device 0 as fallback");

        // Clean up partial stream from first attempt before retrying
        if (ctx->input_stream) {
          log_debug("Closing partially-opened input stream from failed primary device");
          LOG_IO("portaudio", { Pa_CloseStream(ctx->input_stream); });
          ctx->input_stream = NULL;
        }

        PaStreamParameters fallback_input_params = inputParams;
        fallback_input_params.device = 0;
        const PaDeviceInfo *device_0_info = Pa_GetDeviceInfo(0);
        if (device_0_info && device_0_info->maxInputChannels > 0) {
          LOG_IO("portaudio", {
            err = Pa_OpenStream(&ctx->input_stream, &fallback_input_params, NULL, input_stream_rate,
                                AUDIO_FRAMES_PER_BUFFER, paClipOff, input_callback, ctx);
          });
          if (err == paNoError) {
            log_info("Input stream opened on device 0 (fallback from default)");
            input_ok = true;
          } else {
            log_warn("Fallback also failed on device 0: %s", Pa_GetErrorText(err));
            // Clean up if fallback also failed
            if (ctx->input_stream) {
              log_debug("Closing partially-opened input stream from failed fallback device");
              LOG_IO("portaudio", { Pa_CloseStream(ctx->input_stream); });
              ctx->input_stream = NULL;
            }
          }
        }
      }

      if (!input_ok) {
        log_warn("Failed to open input stream: %s", Pa_GetErrorText(err));
      }
    }

    // Check if we got at least one stream working
    if (!input_ok && !output_ok && !media_only) {
      // Neither stream works - fail completely
      audio_ring_buffer_destroy(ctx->render_buffer);
      ctx->render_buffer = NULL;
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
      LOG_IO("portaudio", { err = Pa_StartStream(ctx->output_stream); });
      if (err != paNoError) {
        if (ctx->input_stream) {
          LOG_IO("portaudio", { Pa_CloseStream(ctx->input_stream); });
        }
        LOG_IO("portaudio", { Pa_CloseStream(ctx->output_stream); });
        ctx->input_stream = NULL;
        ctx->output_stream = NULL;
        audio_ring_buffer_destroy(ctx->render_buffer);
        ctx->render_buffer = NULL;
        return SET_ERRNO(ERROR_AUDIO, "Failed to start output stream: %s", Pa_GetErrorText(err));
      }
    }

    // Start input stream if it's open
    if (ctx->input_stream) {
      LOG_IO("portaudio", { err = Pa_StartStream(ctx->input_stream); });
      if (err != paNoError) {
        if (ctx->output_stream) {
          LOG_IO("portaudio", { Pa_StopStream(ctx->output_stream); });
        }
        if (ctx->input_stream) {
          LOG_IO("portaudio", { Pa_CloseStream(ctx->input_stream); });
        }
        if (ctx->output_stream) {
          LOG_IO("portaudio", { Pa_CloseStream(ctx->output_stream); });
        }
        ctx->input_stream = NULL;
        ctx->output_stream = NULL;
        audio_ring_buffer_destroy(ctx->render_buffer);
        ctx->render_buffer = NULL;
        return SET_ERRNO(ERROR_AUDIO, "Failed to start input stream: %s", Pa_GetErrorText(err));
      }
    }

    ctx->separate_streams = true;
    log_debug("Separate streams started successfully");
  } else {
    ctx->separate_streams = false;
    log_info("Full-duplex stream started (single callback, perfect AEC3 timing)");
  }
start_worker:

  audio_set_realtime_priority();

  // Start worker thread for heavy audio processing
  if (!ctx->worker_running) {
    atomic_store_bool(&ctx->worker_should_stop, false);
    if (asciichat_thread_create(&ctx->worker_thread, "audio_worker", audio_worker_thread, ctx) != 0) {
      // Failed to create worker thread - stop streams and cleanup
      if (ctx->duplex_stream) {
        LOG_IO("portaudio", {
          Pa_StopStream(ctx->duplex_stream);
          Pa_CloseStream(ctx->duplex_stream);
        });
        ctx->duplex_stream = NULL;
      }
      if (ctx->input_stream) {
        LOG_IO("portaudio", {
          Pa_StopStream(ctx->input_stream);
          Pa_CloseStream(ctx->input_stream);
        });
        ctx->input_stream = NULL;
      }
      if (ctx->output_stream) {
        LOG_IO("portaudio", {
          Pa_StopStream(ctx->output_stream);
          Pa_CloseStream(ctx->output_stream);
        });
        ctx->output_stream = NULL;
      }
      audio_ring_buffer_destroy(ctx->render_buffer);
      ctx->render_buffer = NULL;
      return SET_ERRNO(ERROR_THREAD, "Failed to create worker thread");
    }
    ctx->worker_running = true;
    log_debug("Worker thread started successfully");
  }

  // Lock to update final state flags atomically
  mutex_lock(&ctx->state_mutex);
  ctx->running = true;
  ctx->sample_rate = AUDIO_SAMPLE_RATE;
  mutex_unlock(&ctx->state_mutex);
  return ASCIICHAT_OK;
}

asciichat_error_t audio_stop_duplex(audio_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Audio context not initialized");
  }

  atomic_store_bool(&ctx->shutting_down, true);

  // Stop worker thread before stopping streams
  if (ctx->worker_running) {
    log_debug("Stopping worker thread");
    atomic_store_bool(&ctx->worker_should_stop, true);
    cond_signal(&ctx->worker_cond); // Wake up worker if waiting
    asciichat_thread_join(&ctx->worker_thread, NULL);
    ctx->worker_running = false;
    log_debug("Worker thread stopped successfully");
  }

  if (ctx->playback_buffer) {
    audio_ring_buffer_clear(ctx->playback_buffer);
  }

  mutex_lock(&ctx->state_mutex);

  if (ctx->duplex_stream) {
    log_debug("Stopping duplex stream");
    PaError err;
    LOG_IO("portaudio", { err = Pa_StopStream(ctx->duplex_stream); });
    if (err != paNoError) {
      log_warn("Pa_StopStream failed: %s", Pa_GetErrorText(err));
    }
    log_debug("Closing duplex stream");
    LOG_IO("portaudio", { err = Pa_CloseStream(ctx->duplex_stream); });
    if (err != paNoError) {
      log_warn("Pa_CloseStream failed: %s", Pa_GetErrorText(err));
    } else {
      log_debug("Duplex stream closed successfully");
    }
    ctx->duplex_stream = NULL;
  }

  // Stop separate streams if used
  if (ctx->input_stream) {
    log_debug("Stopping input stream");
    PaError err;
    LOG_IO("portaudio", { err = Pa_StopStream(ctx->input_stream); });
    if (err != paNoError) {
      log_warn("Pa_StopStream input failed: %s", Pa_GetErrorText(err));
    }
    log_debug("Closing input stream");
    LOG_IO("portaudio", { err = Pa_CloseStream(ctx->input_stream); });
    if (err != paNoError) {
      log_warn("Pa_CloseStream input failed: %s", Pa_GetErrorText(err));
    } else {
      log_debug("Input stream closed successfully");
    }
    ctx->input_stream = NULL;
  }

  if (ctx->output_stream) {
    log_debug("Stopping output stream");
    PaError err;
    LOG_IO("portaudio", { err = Pa_StopStream(ctx->output_stream); });
    if (err != paNoError) {
      log_warn("Pa_StopStream output failed: %s", Pa_GetErrorText(err));
    }
    log_debug("Closing output stream");
    LOG_IO("portaudio", { err = Pa_CloseStream(ctx->output_stream); });
    if (err != paNoError) {
      log_warn("Pa_CloseStream output failed: %s", Pa_GetErrorText(err));
    } else {
      log_debug("Output stream closed successfully");
    }
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

  log_debug("Audio stopped");
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
  if (atomic_load_bool(&ctx->shutting_down)) {
    return ASCIICHAT_OK; // Silently discard
  }

  audio_recording_submit(AUDIO_RECORDING_REMOTE, buffer, num_samples, time_get_ns());
  audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_REMOTE, buffer, (size_t)num_samples);
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

  // Ensure PortAudio is initialized (centralized initialization)
  asciichat_error_t pa_result = audio_ensure_portaudio_initialized();
  if (pa_result != ASCIICHAT_OK) {
    return pa_result;
  }

  int num_devices = Pa_GetDeviceCount();
  if (num_devices < 0) {
    return SET_ERRNO(ERROR_AUDIO, "Failed to get device count: %s", Pa_GetErrorText(num_devices));
  }

  if (num_devices == 0) {
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
    return ASCIICHAT_OK; // No matching devices
  }

  // Allocate device array
  audio_device_info_t *devices = SAFE_CALLOC(device_count, sizeof(audio_device_info_t), audio_device_info_t *);
  if (!devices) {
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
    log_debug("✓ Audio thread real-time priority set successfully");
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

bool audio_should_enable_microphone(audio_capture_source_t source, bool has_media_audio) {
  switch (source) {
  case AUDIO_CAPTURE_SOURCE_AUTO:
    return !has_media_audio;

  case AUDIO_CAPTURE_SOURCE_MIC:
    return true;

  case AUDIO_CAPTURE_SOURCE_MEDIA:
    return false;

  case AUDIO_CAPTURE_SOURCE_BOTH:
    return true;

  case AUDIO_CAPTURE_SOURCE_REMOTE:
    return false;

  default:
    return !has_media_audio;
  }
}
