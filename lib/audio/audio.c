
/**
 * @file audio.c
 * @ingroup audio
 * @brief 🔊 Audio capture and playback using PortAudio with buffer management
 */

#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/client_audio_pipeline.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>       // For START_TIMER/STOP_TIMER macros
#include <ascii-chat/util/lifecycle.h>  // For lifecycle_t
#include <ascii-chat/asciichat_errno.h> // For asciichat_errno system
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/init.h>        // For static_mutex_t
#include <ascii-chat/platform/abstraction.h> // For platform_sleep_us
#include <ascii-chat/network/packet.h>       // For audio_batch_packet_t
#include <ascii-chat/log/logging.h>          // For log_* macros
#include <ascii-chat/media/source.h>         // For media_source_read_audio()
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

// PortAudio initialization lifecycle (one-time init/shutdown, no refcounting)
// Uses lifecycle_t for thread-safe, lock-free initialization tracking
static lifecycle_t g_pa_lc = LIFECYCLE_INIT;

// Track how many times Pa_Initialize and Pa_Terminate are called (for debugging)
static int g_pa_init_count = 0;
static int g_pa_terminate_count = 0;

/**
 * @brief Ensure PortAudio is initialized
 *
 * This is the single centralized function that initializes PortAudio.
 * All code paths (audio_init, device enumeration) call this to prevent
 * multiple independent Pa_Initialize() calls and duplicate ALSA/PulseAudio probing.
 *
 * Uses lifecycle_t for thread-safe, lock-free initialization tracking.
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
  fflush(stderr);
  fflush(stdout);
  platform_stderr_redirect_handle_t stdio_handle = platform_stdout_stderr_redirect_to_null();

  g_pa_init_count++;
  PaError err = Pa_Initialize();

  // Restore stdout and stderr before checking errors
  platform_stdout_stderr_restore(stdio_handle);

  if (err != paNoError) {
    return SET_ERRNO(ERROR_AUDIO, "Failed to initialize PortAudio: %s", Pa_GetErrorText(err));
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Release PortAudio (no-op with lifecycle_t)
 *
 * With the lifecycle_t pattern (no refcounting), this is a no-op.
 * PortAudio remains initialized for the lifetime of the process.
 * Use audio_terminate_portaudio_final() to actually terminate PortAudio.
 */
static void audio_release_portaudio(void) {
  // No-op with lifecycle_t (no refcounting)
  log_debug_every(1000, "audio_release_portaudio() called (lifecycle managed)");
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

    PaError err = Pa_Terminate();
    g_pa_terminate_count++;

    log_debug("[PORTAUDIO_TERM] Pa_Terminate() returned: %s", Pa_GetErrorText(err));
  }
}

// Worker thread batch size (in frames, not samples)
// Reduced from 480 (10ms) to 128 (2.7ms) for lower latency and less jitter
// Smaller batches mean more frequent processing, reducing audio gaps
#define WORKER_BATCH_FRAMES 128
#define WORKER_BATCH_SAMPLES (WORKER_BATCH_FRAMES * AUDIO_CHANNELS)
#define WORKER_TIMEOUT_MS 1 // Wake up every 1ms to keep up with 48kHz playback (was 3ms)

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

  while (true) {
    loop_count++;
    START_TIMER("worker_loop_iteration");

    // For output-only mode, don't wait for signal - just write continuously
    // For duplex/input modes, wait for signal from callback
    bool is_output_only = ctx->output_stream && !ctx->input_stream && !ctx->duplex_stream;

    int wait_result = 1; // Default: timeout (will trigger processing for output-only)
    if (!is_output_only) {
      // Wait for signal from callback or timeout
      mutex_lock(&ctx->worker_mutex);
      START_TIMER("worker_cond_wait");
      wait_result = cond_timedwait(&ctx->worker_cond, &ctx->worker_mutex, WORKER_TIMEOUT_MS * NS_PER_MS_INT);
      double wait_time_ns = STOP_TIMER("worker_cond_wait");
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
    if (atomic_load(&ctx->worker_should_stop)) {
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

    if (wait_result != 0 && capture_available == 0 && playback_available == 0) {
      // Timeout with no data - continue waiting
      STOP_TIMER("worker_loop_iteration"); // Must stop before loop repeats
      continue;
    }

    process_count++;

    // STEP 1: Process capture path (mic → AEC3 → encoder)
    // Process capture samples if available (don't wait for full batch - reduces latency)
    // Minimum: 64 samples (1.3ms @ 48kHz) to avoid excessive overhead
    const size_t MIN_PROCESS_SAMPLES = 64;
    if (capture_available >= MIN_PROCESS_SAMPLES) {
      START_TIMER("worker_capture_processing");

      // Read up to WORKER_BATCH_SAMPLES, but process whatever is available
      size_t samples_to_process = (capture_available > WORKER_BATCH_SAMPLES) ? WORKER_BATCH_SAMPLES : capture_available;
      // Read raw capture samples from callbacks (variable size batch)
      size_t capture_read = audio_ring_buffer_read(ctx->raw_capture_rb, ctx->worker_capture_batch, samples_to_process);

      if (capture_read > 0) {
        // For AEC3, we need render samples too. If not available, skip AEC3 but still process capture.
        if (!bypass_aec3_worker && ctx->audio_pipeline && render_available >= capture_read) {
          // Read render samples for AEC3 (match capture size)
          size_t render_read = audio_ring_buffer_read(ctx->raw_render_rb, ctx->worker_render_batch, capture_read);

          if (render_read > 0) {
            // Measure AEC3 processing time
            uint64_t aec3_start_ns = time_get_ns();

            // AEC3 processing - should be fast enough for real-time on Pi 5
            // Output is written back to worker_capture_batch (in-place processing)
            client_audio_pipeline_process_duplex(ctx->audio_pipeline, ctx->worker_render_batch, (int)render_read,
                                                 ctx->worker_capture_batch, (int)capture_read,
                                                 ctx->worker_capture_batch); // Process in-place

            long aec3_ns = (long)time_elapsed_ns(aec3_start_ns, time_get_ns());

            // Log AEC3 timing periodically
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
              log_info("AEC3 performance: avg=%s, max=%s, latest=%s (samples=%zu, %d calls)",
                       avg_str, max_str, latest_str, capture_read, aec3_count);
            }
          }
        }

        // TODO: Add optional resampling here if input device rate != 48kHz
        // For now, assume 48kHz (most common for professional audio)

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
        audio_ring_buffer_write(ctx->capture_buffer, ctx->worker_capture_batch, (int)capture_read);

        log_debug_every(NS_PER_MS_INT, "Worker processed %zu capture samples (AEC3 %s)", capture_read,
                        bypass_aec3_worker
                            ? "BYPASSED"
                            : (render_available >= WORKER_BATCH_SAMPLES ? "applied" : "skipped-no-render"));
      }

      double capture_time_ns = STOP_TIMER("worker_capture_processing");
      total_capture_ns += capture_time_ns;
      if (capture_time_ns > max_capture_ns)
        max_capture_ns = capture_time_ns;
    }

    // STEP 2: Output/Playback handling
    // With output_callback registered, PortAudio handles calling it automatically
    // No need for worker thread to manually write data via Pa_WriteStream
    (void)playback_available; // Suppress unused variable warning if not used in this build

    // Log overall loop iteration time
    double loop_time_ns = STOP_TIMER("worker_loop_iteration");
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

  START_TIMER("duplex_callback");

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

  static uint64_t audio_callback_debug_count = 0;
  audio_callback_debug_count++;
  if (audio_callback_debug_count <= 10 || audio_callback_debug_count % 100 == 0) {
    log_info("AUDIO_CALLBACK #%lu: frames=%lu samples=%zu media_source=%p", audio_callback_debug_count, framesPerBuffer,
             num_samples, (void *)ctx->media_source);
  }

  // Silence on shutdown
  if (atomic_load(&ctx->shutting_down)) {
    if (output) {
      SAFE_MEMSET(output, num_samples * sizeof(float), 0, num_samples * sizeof(float));
    }
    STOP_TIMER("duplex_callback");
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

  // STEP 1: Read playback from media source (mirror mode) or network buffer
  if (output) {
    size_t samples_read = 0;

    // For mirror mode with media file: read audio directly from media source
    // This bypasses buffering and provides audio at the exact sample rate PortAudio needs
    if (ctx->media_source) {
      samples_read = media_source_read_audio((void *)ctx->media_source, output, num_samples);

      static uint64_t cb_count = 0;
      cb_count++;
      if (cb_count <= 5 || cb_count % 500 == 0) {
        log_info("Callback #%lu: media_source path, read %zu samples", cb_count, samples_read);
      }
    } else if (ctx->playback_buffer) {
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
        log_warn("Callback: BOTH media_source AND playback_buffer are NULL!");
      }
    }

    total_samples_read_local += samples_read;

    if (samples_read < num_samples) {
      // Fill remaining with silence if underrun
      SAFE_MEMSET(output + samples_read, (num_samples - samples_read) * sizeof(float), 0,
                  (num_samples - samples_read) * sizeof(float));
      if (ctx->media_source) {
        log_debug_every(5 * NS_PER_MS_INT, "Media playback: got %zu/%zu samples", samples_read, num_samples);
      } else {
        log_debug_every(NS_PER_MS_INT, "Network playback underrun: got %zu/%zu samples", samples_read, num_samples);
        underrun_count_local++;
      }
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
  double callback_time_ns = STOP_TIMER("duplex_callback");
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
 * Resampling (if needed) is handled by worker thread, not here.
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
    log_warn("FIRST OUTPUT_CALLBACK! frames=%lu ctx->media_source=%p", framesPerBuffer, (void *)ctx->media_source);
  }

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

  // STEP 1: Read audio source
  size_t samples_read = 0;
  if (output) {
    if (ctx->media_source) {
      // Mirror mode: read audio directly from media source
      samples_read = media_source_read_audio((void *)ctx->media_source, output, num_samples);
      if (output_cb_count <= 3) {
        log_warn("OUTPUT_CB: media_source path, read %zu samples", samples_read);
      }
    } else if (ctx->processed_playback_rb) {
      // Network mode: read from processed playback buffer (worker output)
      samples_read = audio_ring_buffer_read(ctx->processed_playback_rb, output, num_samples);
      if (output_cb_count <= 3) {
        log_warn("OUTPUT_CB: processed_playback_rb path, read %zu samples", samples_read);
      }
    } else if (ctx->playback_buffer) {
      // Fallback: read from playback buffer if available
      samples_read = audio_ring_buffer_read(ctx->playback_buffer, output, num_samples);
      if (output_cb_count <= 3) {
        log_warn("OUTPUT_CB: playback_buffer path, read %zu samples", samples_read);
      }
    } else {
      if (output_cb_count <= 3) {
        log_warn("OUTPUT_CB: NO BUFFERS! media_source=%p processed_rb=%p playback_buf=%p", (void *)ctx->media_source,
                 (void *)ctx->processed_playback_rb, (void *)ctx->playback_buffer);
      }
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
    if (ctx->render_buffer && samples_read > 0) {
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
  if (atomic_load(&ctx->shutting_down)) {
    return paContinue;
  }

  if (statusFlags & paInputOverflow) {
    log_warn_every(LOG_RATE_FAST, "PortAudio input overflow (separate stream)");
  }

  // STEP 1: Copy raw mic samples → worker for AEC3 processing
  // Skip microphone capture in playback-only mode (mirror)
  if (!ctx->playback_only && input && ctx->raw_capture_rb) {
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
      log_debug_every(NS_PER_MS_INT, "Jitter buffer filling: %zu/%d samples (%.1f%%)", available,
                      AUDIO_JITTER_BUFFER_THRESHOLD, (100.0f * available) / AUDIO_JITTER_BUFFER_THRESHOLD);
      return 0; // Return 0 samples - caller will pad with silence
    }
  }

  // Periodic buffer health logging (every 5 seconds when healthy)
  unsigned int underruns = atomic_load_explicit(&rb->underrun_count, memory_order_relaxed);
  log_dev_every(5 * NS_PER_MS_INT, "Buffer health: %zu/%d samples (%.1f%%), underruns=%u", available,
                AUDIO_RING_BUFFER_SIZE, (100.0f * available) / AUDIO_RING_BUFFER_SIZE, underruns);

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
    audio_release_portaudio();
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
    audio_release_portaudio();
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
    audio_release_portaudio();
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
    audio_release_portaudio();
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
    audio_release_portaudio();
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

    ctx->initialized = false;

    mutex_unlock(&ctx->state_mutex);
    mutex_destroy(&ctx->state_mutex);

    log_debug("Audio system cleanup complete (all resources released)");
  } else {
  }

  // MUST happen for both initialized and non-initialized contexts
  // If audio_init() called Pa_Initialize() but failed partway, refcount must be decremented
  audio_release_portaudio();
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

  // Initialize PortAudio here, when we actually need to open streams
  // This defers Pa_Initialize() until necessary, avoiding premature ALSA allocation
  asciichat_error_t pa_result = audio_ensure_portaudio_initialized();
  if (pa_result != ASCIICHAT_OK) {
    return pa_result;
  }

  mutex_lock(&ctx->state_mutex);

  // Already running?
  if (ctx->duplex_stream || ctx->input_stream || ctx->output_stream) {
    mutex_unlock(&ctx->state_mutex);
    return ASCIICHAT_OK;
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
      mutex_unlock(&ctx->state_mutex);
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

  // Check if sample rates differ - ALSA full-duplex doesn't handle this well
  // If no input or no output, always use separate streams
  bool rates_differ = has_input && has_output && (inputInfo->defaultSampleRate != outputInfo->defaultSampleRate);
  bool try_separate = rates_differ || !has_input || !has_output;
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
    // Fall back to separate streams (needed when sample rates differ or input-only/playback-only mode)
    if (has_output && has_input) {
      log_info("Using separate input/output streams (sample rates differ: %.0f vs %.0f Hz)",
               inputInfo->defaultSampleRate, outputInfo->defaultSampleRate);
      log_info("  Will resample: buffer at %.0f Hz → output at %.0f Hz", (double)AUDIO_SAMPLE_RATE,
               outputInfo->defaultSampleRate);
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
      mutex_unlock(&ctx->state_mutex);
      return SET_ERRNO(ERROR_MEMORY, "Failed to create render buffer");
    }

    // Open output stream only if output device exists
    bool output_ok = false;
    double actual_output_rate = 0;
    if (has_output) {
      // Try to use AUDIO_SAMPLE_RATE (48kHz) first for best quality and duplex compatibility
      // Fall back to native rate if 48kHz not supported
      double preferred_rate = AUDIO_SAMPLE_RATE;
      double native_rate = outputInfo->defaultSampleRate;

      log_debug("Attempting output at %.0f Hz (preferred) vs %.0f Hz (native)", preferred_rate, native_rate);

      // Always use output_callback for output streams (both output-only and duplex modes)
      // PortAudio will invoke the callback whenever it needs audio data
      // The callback reads from media_source (mirror mode) or playback buffers (network mode)
      PaStreamCallback *callback = output_callback;

      // Try preferred rate first
      err = Pa_OpenStream(&ctx->output_stream, NULL, &outputParams, preferred_rate, AUDIO_FRAMES_PER_BUFFER, paClipOff,
                          callback, ctx);

      if (err == paNoError) {
        actual_output_rate = preferred_rate;
        output_ok = true;
        log_info("✓ Output opened at preferred rate: %.0f Hz (matches input - optimal!)", preferred_rate);
      } else {
        log_warn("Failed to open output at %.0f Hz: %s, trying native rate %.0f Hz", preferred_rate,
                 Pa_GetErrorText(err), native_rate);

        // If first Pa_OpenStream call left a partial stream, clean it up before retrying
        if (ctx->output_stream) {
          log_debug("Closing partially-opened output stream from failed preferred rate");
          Pa_CloseStream(ctx->output_stream);
          ctx->output_stream = NULL;
        }

        // Fall back to native rate (still using blocking mode for output-only)
        err = Pa_OpenStream(&ctx->output_stream, NULL, &outputParams, native_rate, AUDIO_FRAMES_PER_BUFFER, paClipOff,
                            callback, ctx);

        if (err == paNoError) {
          actual_output_rate = native_rate;
          output_ok = true;
          log_info("✓ Output opened at native rate: %.0f Hz (will need resampling)", native_rate);
        } else {
          log_warn("Failed to open output stream at native rate: %s", Pa_GetErrorText(err));
          // Clean up if fallback also failed
          if (ctx->output_stream) {
            log_debug("Closing partially-opened output stream from failed native rate");
            Pa_CloseStream(ctx->output_stream);
            ctx->output_stream = NULL;
          }
        }
      }

      // Store actual output rate for resampling
      if (output_ok) {
        ctx->output_device_rate = actual_output_rate;
        if (actual_output_rate != AUDIO_SAMPLE_RATE) {
          log_warn("⚠️  Output rate mismatch: %.0f Hz output vs %.0f Hz input - resampling will be used",
                   actual_output_rate, (double)AUDIO_SAMPLE_RATE);
        }
      }
    }

    // Open input stream only if we have input (skip for playback-only mode)
    bool input_ok = !has_input; // If no input, mark as OK (skip)
    if (has_input) {
      // Use pipeline sample rate (AUDIO_SAMPLE_RATE)
      // In input-only mode, we don't need to match output device rate
      double input_stream_rate = AUDIO_SAMPLE_RATE;
      err = Pa_OpenStream(&ctx->input_stream, &inputParams, NULL, input_stream_rate, AUDIO_FRAMES_PER_BUFFER, paClipOff,
                          input_callback, ctx);
      input_ok = (err == paNoError);

      // If input failed, try device 0 as fallback (HDMI on BeaglePlay)
      if (!input_ok) {
        log_debug("Input failed - trying device 0 as fallback");

        // Clean up partial stream from first attempt before retrying
        if (ctx->input_stream) {
          log_debug("Closing partially-opened input stream from failed primary device");
          Pa_CloseStream(ctx->input_stream);
          ctx->input_stream = NULL;
        }

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
            // Clean up if fallback also failed
            if (ctx->input_stream) {
              log_debug("Closing partially-opened input stream from failed fallback device");
              Pa_CloseStream(ctx->input_stream);
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
    log_debug("Separate streams started successfully");
  } else {
    ctx->separate_streams = false;
    log_info("Full-duplex stream started (single callback, perfect AEC3 timing)");
  }

  audio_set_realtime_priority();

  // Start worker thread for heavy audio processing
  if (!ctx->worker_running) {
    atomic_store(&ctx->worker_should_stop, false);
    if (asciichat_thread_create(&ctx->worker_thread, "audio_worker", audio_worker_thread, ctx) != 0) {
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
    log_debug("Worker thread started successfully");
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
    log_debug("Worker thread stopped successfully");
  }

  if (ctx->playback_buffer) {
    audio_ring_buffer_clear(ctx->playback_buffer);
  }

  Pa_Sleep(50); // Let callbacks drain

  mutex_lock(&ctx->state_mutex);

  if (ctx->duplex_stream) {
    log_debug("Stopping duplex stream");
    PaError err = Pa_StopStream(ctx->duplex_stream);
    if (err != paNoError) {
      log_warn("Pa_StopStream failed: %s", Pa_GetErrorText(err));
    }
    log_debug("Closing duplex stream");
    err = Pa_CloseStream(ctx->duplex_stream);
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
    PaError err = Pa_StopStream(ctx->input_stream);
    if (err != paNoError) {
      log_warn("Pa_StopStream input failed: %s", Pa_GetErrorText(err));
    }
    log_debug("Closing input stream");
    err = Pa_CloseStream(ctx->input_stream);
    if (err != paNoError) {
      log_warn("Pa_CloseStream input failed: %s", Pa_GetErrorText(err));
    } else {
      log_debug("Input stream closed successfully");
    }
    ctx->input_stream = NULL;
  }

  if (ctx->output_stream) {
    log_debug("Stopping output stream");
    PaError err = Pa_StopStream(ctx->output_stream);
    if (err != paNoError) {
      log_warn("Pa_StopStream output failed: %s", Pa_GetErrorText(err));
    }
    log_debug("Closing output stream");
    err = Pa_CloseStream(ctx->output_stream);
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

  // Ensure PortAudio is initialized (centralized initialization)
  asciichat_error_t pa_result = audio_ensure_portaudio_initialized();
  if (pa_result != ASCIICHAT_OK) {
    return pa_result;
  }

  int num_devices = Pa_GetDeviceCount();
  if (num_devices < 0) {
    audio_release_portaudio();
    return SET_ERRNO(ERROR_AUDIO, "Failed to get device count: %s", Pa_GetErrorText(num_devices));
  }

  if (num_devices == 0) {
    audio_release_portaudio();
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
    audio_release_portaudio();
    return ASCIICHAT_OK; // No matching devices
  }

  // Allocate device array
  audio_device_info_t *devices = SAFE_CALLOC(device_count, sizeof(audio_device_info_t), audio_device_info_t *);
  if (!devices) {
    audio_release_portaudio();
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

  // Release PortAudio (centralized refcount management)
  audio_release_portaudio();

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

bool audio_should_enable_microphone(audio_source_t source, bool has_media_audio) {
  switch (source) {
  case AUDIO_SOURCE_AUTO:
    return !has_media_audio;

  case AUDIO_SOURCE_MIC:
    return true;

  case AUDIO_SOURCE_MEDIA:
    return false;

  case AUDIO_SOURCE_BOTH:
    return true;

  default:
    return !has_media_audio;
  }
}
