/**
 * @file client/audio.c
 * @ingroup client_audio
 * @brief 🔊 Client audio management: capture thread, sample processing, and playback coordination
 *
 * The audio system implements a dual-thread architecture:
 * - **Capture Thread**: Records microphone input and transmits to server
 * - **Playback System**: Receives server audio and plays through speakers
 * - **Processing Pipeline**: Real-time audio enhancement and filtering
 * - **Coordination**: Thread-safe communication between capture and playback
 *
 * ## Audio Processing Pipeline
 *
 * Both incoming and outgoing audio undergo comprehensive processing:
 * 1. **High-pass Filter**: Removes low-frequency rumble and noise
 * 2. **Noise Gate**: Eliminates background noise during silence
 * 3. **Dynamic Range**: Soft clipping to prevent harsh distortion
 * 4. **Volume Control**: Configurable boost for optimal listening levels
 * 5. **Batching**: Groups samples for efficient network transmission
 *
 * ## Capture Thread Management
 *
 * Audio capture runs in a dedicated thread:
 * - **Continuous Recording**: Real-time microphone sample capture
 * - **Processing Chain**: Applies filters and enhancement algorithms
 * - **Network Transmission**: Sends processed samples to server
 * - **Adaptive Quality**: Noise gate reduces traffic during silence
 * - **Thread Safety**: Coordinated shutdown and resource management
 *
 * ## Batching and Network Efficiency
 *
 * Audio samples are batched for network efficiency:
 * - **Batch Accumulation**: Collect multiple sample packets
 * - **Smart Transmission**: Send batches when full or gate closes
 * - **Reduced Overhead**: Fewer network packets for better performance
 * - **Quality Preservation**: Maintain audio quality while optimizing bandwidth
 *
 * ## Platform Audio Integration
 *
 * Uses PortAudio for cross-platform audio support:
 * - **Device Enumeration**: Automatic microphone and speaker detection
 * - **Format Negotiation**: Optimal sample rate and bit depth selection
 * - **Low Latency**: Optimized for real-time audio processing
 * - **Error Handling**: Graceful handling of device conflicts and changes
 *
 * ## Integration Points
 *
 * - **main.c**: Audio subsystem lifecycle and initialization
 * - **server.c**: Audio packet transmission to server
 * - **protocol.c**: Incoming audio packet processing and dispatch
 * - **lib/audio.c**: Low-level PortAudio device management
 * - **lib/mixer.c**: Audio filtering and processing algorithms
 *
 * ## Error Handling
 *
 * Audio errors handled with graceful degradation:
 * - **Device Unavailable**: Continue without audio, log warnings
 * - **Processing Errors**: Skip problematic samples, maintain stream
 * - **Network Errors**: Continue processing, let connection management handle
 * - **Resource Errors**: Clean shutdown with proper resource release
 *
 * ## Resource Management
 *
 * Careful audio resource lifecycle:
 * - **Context Management**: Proper PortAudio context initialization/cleanup
 * - **Thread Coordination**: Clean thread shutdown and resource release
 * - **Buffer Management**: Efficient sample buffer allocation and reuse
 * - **Device Release**: Proper microphone and speaker device cleanup
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#include "audio.h" // src/client/audio.h
#include "audio/audio_analysis.h"
#include "main.h"
#include "server.h"

#include "audio/audio.h"                 // lib/audio/audio.h for PortAudio wrapper
#include "audio/client_audio_pipeline.h" // Unified audio processing pipeline
#include "audio/wav_writer.h"            // WAV file dumping for debugging
#include "common.h"
#include "options.h"
#include "platform/system.h" // For platform_memcpy

#include <stdatomic.h>
#include <string.h>
#include <math.h>

#include "platform/abstraction.h"

/* ============================================================================
 * Audio System State
 * ============================================================================ */

/**
 * @brief Global audio context for PortAudio operations
 *
 * Maintains the PortAudio stream state, audio format configuration, and
 * device information. Initialized during audio subsystem startup, cleaned
 * up during shutdown.
 *
 * @ingroup client_audio
 */
static audio_context_t g_audio_context = {0};

/**
 * @brief Unified audio processing pipeline
 *
 * Handles all audio processing including:
 * - Acoustic Echo Cancellation (Speex AEC)
 * - Noise suppression, AGC, VAD (Speex preprocessor)
 * - Jitter buffer (Speex jitter buffer)
 * - Opus encoding/decoding
 * - Compressor, noise gate, highpass/lowpass filters
 *
 * @ingroup client_audio
 */
static client_audio_pipeline_t *g_audio_pipeline = NULL;

/* ============================================================================
 * Audio Debugging - WAV File Dumpers
 * ============================================================================ */

/** WAV writer for raw captured audio (before processing) */
static wav_writer_t *g_wav_capture_raw = NULL;

/** WAV writer for processed audio (after AGC/filters, before network) */
static wav_writer_t *g_wav_capture_processed = NULL;

/** WAV writer for received audio (from server, before playback) */
static wav_writer_t *g_wav_playback_received = NULL;

/* ============================================================================
 * Audio Capture Thread Management
 * ============================================================================ */

/**
 * @brief Audio capture thread handle
 *
 * Thread handle for the background thread that captures audio samples from
 * the audio input device. Created during connection establishment, joined
 * during shutdown.
 *
 * @ingroup client_audio
 */
static asciithread_t g_audio_capture_thread;

/**
 * @brief Flag indicating if audio capture thread was successfully created
 *
 * Used during shutdown to determine whether the thread handle is valid and
 * should be joined. Prevents attempting to join a thread that was never created.
 *
 * @ingroup client_audio
 */
static bool g_audio_capture_thread_created = false;

/**
 * @brief Atomic flag indicating audio capture thread has exited
 *
 * Set by the audio capture thread when it exits. Used by other threads to
 * detect thread termination without blocking on thread join operations.
 *
 * @ingroup client_audio
 */
static atomic_bool g_audio_capture_thread_exited = false;

/* ============================================================================
 * Audio Processing Constants
 * ============================================================================ */

/** Audio volume boost multiplier for received samples */
#define AUDIO_VOLUME_BOOST 1.0f // No boost/attenuation

/* ============================================================================
 * Audio Processing Functions
 * ============================================================================ */

/**
 * Process received audio samples from server
 *
 * Uses the audio pipeline for processing:
 * 1. Input validation and size checking
 * 2. Feed samples to pipeline (applies soft clipping)
 * 3. Feed echo reference for AEC
 * 4. Submit processed samples to PortAudio playback queue
 *
 * @param samples Raw audio sample data from server
 * @param num_samples Number of samples in the buffer
 *
 * @ingroup client_audio
 */
void audio_process_received_samples(const float *samples, int num_samples) {
  if (!opt_audio_enabled || !samples || num_samples <= 0) {
    return;
  }

  // Allow both single packets and batched packets
  if (num_samples > AUDIO_BATCH_SAMPLES) {
    log_warn("Audio packet too large: %d samples (max %d)", num_samples, AUDIO_BATCH_SAMPLES);
    return;
  }

  // Calculate RMS energy of received samples
  float sum_squares = 0.0f;
  for (int i = 0; i < num_samples; i++) {
    sum_squares += samples[i] * samples[i];
  }
  float received_rms = sqrtf(sum_squares / num_samples);

  // DUMP: Received audio from server (before playback processing)
  if (g_wav_playback_received) {
    wav_writer_write(g_wav_playback_received, samples, num_samples);
  }

  // Track samples for analysis
  if (opt_audio_analysis_enabled) {
    for (int i = 0; i < num_samples; i++) {
      audio_analysis_track_received_sample(samples[i]);
    }
  }

  // Copy samples to playback buffer (no processing needed - mixer already handled clipping)
  float audio_buffer[AUDIO_BATCH_SAMPLES];
  memcpy(audio_buffer, samples, (size_t)num_samples * sizeof(float));

  // DEBUG: Log what we're writing to playback buffer
  log_info("AUDIO RECEIVED: %d samples, RMS=%.6f, peak=%.3f -> writing to playback_buffer", num_samples, received_rms,
           samples[0]);

  // Submit to playback system (goes to jitter buffer and speakers)
  // NOTE: AEC3's AnalyzeRender is called in output_callback() when audio actually plays,
  // NOT here. The jitter buffer adds 50-100ms delay, so calling AnalyzeRender here
  // would give AEC3 the wrong timing and break echo cancellation.
  audio_write_samples(&g_audio_context, audio_buffer, num_samples);

#ifdef DEBUG_AUDIO
  log_debug("Processed %d received audio samples", num_samples);
#endif
}

/* ============================================================================
 * Audio Capture Thread Implementation
 * ============================================================================ */

/**
 * Main audio capture thread function
 *
 * Uses ClientAudioPipeline for unified audio processing:
 * 1. Check global shutdown flags and connection status
 * 2. Read raw samples from microphone device
 * 3. Process through pipeline (AEC, filters, AGC, noise gate, Opus encode)
 * 4. Send encoded Opus packets to server
 *
 * The pipeline handles all audio processing in a single call:
 * - Acoustic Echo Cancellation (Speex AEC)
 * - Noise suppression and AGC (Speex preprocessor)
 * - Highpass/lowpass filters
 * - Noise gate and compressor
 * - Opus encoding
 *
 * @param arg Unused thread argument
 * @return NULL on thread exit
 *
 * @ingroup client_audio
 */
static void *audio_capture_thread_func(void *arg) {
  (void)arg;

  log_info("Audio capture thread started");

  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];
  static bool wav_dumpers_initialized = false;

  // Initialize WAV dumpers only once (file handles persist)
  if (!wav_dumpers_initialized && wav_dump_enabled()) {
    g_wav_capture_raw = wav_writer_open("/tmp/audio_capture_raw.wav", AUDIO_SAMPLE_RATE, 1);
    g_wav_capture_processed = wav_writer_open("/tmp/audio_capture_processed.wav", AUDIO_SAMPLE_RATE, 1);
    log_info("Audio debugging enabled: dumping to /tmp/audio_capture_*.wav");
    wav_dumpers_initialized = true;
  }

// Opus frame size: 960 samples = 20ms @ 48kHz (must match pipeline config)
#define OPUS_FRAME_SAMPLES 960
#define OPUS_MAX_PACKET_SIZE 500 // Max Opus packet size

  // Accumulator for building complete Opus frames
  float opus_frame_buffer[OPUS_FRAME_SAMPLES];
  int opus_frame_samples_collected = 0;

  while (!should_exit() && !server_connection_is_lost()) {
    if (!server_connection_is_active()) {
      platform_sleep_usec(100 * 1000); // Wait for connection
      continue;
    }

    // Check if pipeline is ready
    if (!g_audio_pipeline) {
      platform_sleep_usec(100 * 1000);
      continue;
    }

    // Check how many samples are available in the ring buffer
    int available = audio_ring_buffer_available_read(g_audio_context.capture_buffer);
    if (available <= 0) {
      platform_sleep_usec(5 * 1000); // 5ms
      continue;
    }

    // Read samples from the ring buffer (up to AUDIO_SAMPLES_PER_PACKET)
    int to_read = (available < AUDIO_SAMPLES_PER_PACKET) ? available : AUDIO_SAMPLES_PER_PACKET;
    asciichat_error_t read_result = audio_read_samples(&g_audio_context, audio_buffer, to_read);

    if (read_result != ASCIICHAT_OK) {
      log_error("Failed to read audio samples from ring buffer");
      platform_sleep_usec(5 * 1000);
      continue;
    }

    int samples_read = to_read;

    // Log every 10 reads to see if we're getting samples
    static int total_reads = 0;
    total_reads++;
    if (total_reads % 10 == 0) {
      log_info("Audio capture loop iteration #%d: available=%d, samples_read=%d", total_reads, available, samples_read);
    }

    if (samples_read > 0) {
      // Normalize input to prevent clipping: bring peak to ±0.99
      // Calculate peak level first
      float peak = 0.0f;
      for (int i = 0; i < samples_read; i++) {
        float abs_val = fabsf(audio_buffer[i]);
        if (abs_val > peak)
          peak = abs_val;
      }

      // Apply normalization if peak exceeds 1.0
      // Use 0.99 to leave headroom for processing
      if (peak > 1.0f) {
        float gain = 0.99f / peak;
        for (int i = 0; i < samples_read; i++) {
          audio_buffer[i] *= gain;
        }
        static int norm_count = 0;
        norm_count++;
        if (norm_count <= 5 || norm_count % 100 == 0) {
          log_info("Input normalization #%d: peak=%.4f, gain=%.4f", norm_count, peak, gain);
        }
      }

      // DUMP: Raw captured audio (before any processing)
      if (g_wav_capture_raw) {
        wav_writer_write(g_wav_capture_raw, audio_buffer, samples_read);
      }

      // DEBUG: Log EVERY read to see what we're getting from the ring buffer
      static int read_count = 0;
      read_count++;
      float sum_squares = 0.0f;
      for (int i = 0; i < samples_read && i < 10; i++) {
        sum_squares += audio_buffer[i] * audio_buffer[i];
      }
      float rms = sqrtf(sum_squares / (samples_read > 10 ? 10 : samples_read));
      if (read_count <= 5 || read_count % 20 == 0) {
        log_info("Audio capture read #%d: available=%d, samples_read=%d, first=[%.6f,%.6f,%.6f], RMS=%.6f", read_count,
                 available, samples_read, samples_read > 0 ? audio_buffer[0] : 0.0f,
                 samples_read > 1 ? audio_buffer[1] : 0.0f, samples_read > 2 ? audio_buffer[2] : 0.0f, rms);
      }

      // Track sent samples for analysis
      if (opt_audio_analysis_enabled) {
        for (int i = 0; i < samples_read; i++) {
          audio_analysis_track_sent_sample(audio_buffer[i]);
        }
      }

      // Accumulate samples into Opus frame buffer
      int samples_to_process = samples_read;
      int sample_offset = 0;

      while (samples_to_process > 0) {
        // How many samples can we add to current frame?
        int space_in_frame = OPUS_FRAME_SAMPLES - opus_frame_samples_collected;
        int samples_to_copy = (samples_to_process < space_in_frame) ? samples_to_process : space_in_frame;

        // Copy samples to frame buffer
        memcpy(&opus_frame_buffer[opus_frame_samples_collected], &audio_buffer[sample_offset],
               (size_t)samples_to_copy * sizeof(float));

        opus_frame_samples_collected += samples_to_copy;
        sample_offset += samples_to_copy;
        samples_to_process -= samples_to_copy;

        // Do we have a complete frame?
        if (opus_frame_samples_collected >= OPUS_FRAME_SAMPLES) {
          // Process through pipeline: AEC, filters, AGC, noise gate, Opus encode
          uint8_t opus_packet[OPUS_MAX_PACKET_SIZE];

          int opus_len = client_audio_pipeline_capture(g_audio_pipeline, opus_frame_buffer, OPUS_FRAME_SAMPLES,
                                                       opus_packet, OPUS_MAX_PACKET_SIZE);

          // DUMP: Processed audio (would need to decode to dump, skip for now)
          // The pipeline processes and encodes in one step

          if (opus_len > 0) {
            log_debug_every(100000, "Pipeline encoded: %d samples -> %d bytes (compression: %.1fx)", OPUS_FRAME_SAMPLES,
                            opus_len, (float)(OPUS_FRAME_SAMPLES * sizeof(float)) / (float)opus_len);

            // Send Opus frame to server IMMEDIATELY - don't batch
            if (threaded_send_audio_opus(opus_packet, (size_t)opus_len, 48000, 20) < 0) {
              log_error("Failed to send Opus audio frame to server");
            } else {
              // Log every send to verify packets are flowing
              static int send_count = 0;
              send_count++;
              if (send_count <= 10 || send_count % 50 == 0) {
                log_info("CLIENT: Sent Opus packet #%d (%d bytes) immediately", send_count, opus_len);
              }
              if (opt_audio_analysis_enabled) {
                audio_analysis_track_sent_packet((size_t)opus_len);
              }
            }
          } else if (opus_len == 0) {
            // DTX frame (silence) - no data to send
            log_debug_every(100000, "Pipeline DTX frame (silence detected)");
          }

          // Reset frame buffer
          opus_frame_samples_collected = 0;
        }
      }
    } else {
      platform_sleep_usec(5 * 1000); // 5ms
    }
  }

  log_info("Audio capture thread stopped");
  atomic_store(&g_audio_capture_thread_exited, true);
  return NULL;
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * Initialize audio subsystem
 *
 * Sets up PortAudio context, creates the audio pipeline, and starts
 * audio playback/capture if audio is enabled.
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_audio
 */
int audio_client_init() {
  if (!opt_audio_enabled) {
    return 0; // Audio disabled - not an error
  }

  // Initialize WAV dumper for received audio if debugging enabled
  if (wav_dump_enabled()) {
    g_wav_playback_received = wav_writer_open("/tmp/audio_playback_received.wav", AUDIO_SAMPLE_RATE, 1);
    if (g_wav_playback_received) {
      log_info("Audio debugging enabled: dumping received audio to /tmp/audio_playback_received.wav");
    }
  }

  // Initialize PortAudio context using library function
  if (audio_init(&g_audio_context) != ASCIICHAT_OK) {
    log_error("Failed to initialize audio system");
    return -1;
  }

  // Create unified audio pipeline (handles AEC, AGC, noise suppression, Opus)
  client_audio_pipeline_config_t pipeline_config = client_audio_pipeline_default_config();
  pipeline_config.opus_bitrate = 128000; // 128 kbps AUDIO mode for music quality

  // Use FLAGS_MINIMAL but enable echo cancellation and jitter buffer
  // Noise suppression, AGC, VAD destroy music/non-voice audio, so keep them disabled
  // But AEC removes echo without destroying audio quality
  // Jitter buffer helps synchronize the AEC echo reference
  pipeline_config.flags.echo_cancel = true;     // ENABLE: removes echo
  pipeline_config.flags.jitter_buffer = true;   // ENABLE: needed for AEC sync
  pipeline_config.flags.noise_suppress = false; // DISABLED: destroys audio
  pipeline_config.flags.agc = false;            // DISABLED: destroys audio
  pipeline_config.flags.vad = false;            // DISABLED: destroys audio
  pipeline_config.flags.compressor = false;     // DISABLED: minimal processing
  pipeline_config.flags.noise_gate = false;     // DISABLED: minimal processing
  pipeline_config.flags.highpass = false;       // DISABLED: minimal processing
  pipeline_config.flags.lowpass = false;        // DISABLED: minimal processing

  // Set jitter buffer margin for smooth playback without excessive delay
  // 100ms is conservative - AEC3 will adapt to actual network delay automatically
  // We don't tune this; let the system adapt to its actual conditions
  pipeline_config.jitter_margin_ms = 100;

  g_audio_pipeline = client_audio_pipeline_create(&pipeline_config);
  if (!g_audio_pipeline) {
    log_error("Failed to create audio pipeline");
    audio_destroy(&g_audio_context);
    return -1;
  }

  log_info("Audio pipeline created: %d Hz sample rate, %d bps bitrate", pipeline_config.sample_rate,
           pipeline_config.opus_bitrate);

  // Associate pipeline with audio context for echo cancellation
  // The audio output callback will feed playback samples directly to AEC3 from the speaker output,
  // ensuring proper timing synchronization (not from the decode path 50-100ms earlier)
  audio_set_pipeline(&g_audio_context, (void *)g_audio_pipeline);

  // Start audio playback
  if (audio_start_playback(&g_audio_context) != 0) {
    log_error("Failed to start audio playback");
    client_audio_pipeline_destroy(g_audio_pipeline);
    g_audio_pipeline = NULL;
    audio_destroy(&g_audio_context);
    return -1;
  }

  // Start audio capture
  if (audio_start_capture(&g_audio_context) != ASCIICHAT_OK) {
    log_error("Failed to start audio capture");
    client_audio_pipeline_destroy(g_audio_pipeline);
    g_audio_pipeline = NULL;
    audio_stop_playback(&g_audio_context);
    audio_destroy(&g_audio_context);
    return -1;
  }

  return 0;
}

/**
 * Start audio capture thread
 *
 * Creates and starts the audio capture thread. Also sends stream
 * start notification to server.
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_audio
 */
int audio_start_thread() {
  log_info("audio_start_thread called: opt_audio_enabled=%d", opt_audio_enabled);

  if (!opt_audio_enabled) {
    log_info("Audio is disabled, skipping audio capture thread creation");
    return 0; // Audio disabled - not an error
  }

  // Check if thread is already running (not just created flag)
  if (g_audio_capture_thread_created && !atomic_load(&g_audio_capture_thread_exited)) {
    log_warn("Audio capture thread already running");
    return 0;
  }

  // If thread exited, allow recreation
  if (g_audio_capture_thread_created && atomic_load(&g_audio_capture_thread_exited)) {
    log_info("Previous audio capture thread exited, recreating");
    // THREAD SAFETY FIX: Use timeout to prevent indefinite blocking
    int join_result = ascii_thread_join_timeout(&g_audio_capture_thread, NULL, 5000);
    if (join_result != 0) {
      log_warn("Audio capture thread join timed out after 5s, forcing thread handle reset");
      // Thread is stuck - we can't safely reuse the handle, but we can reset our tracking
    }
    g_audio_capture_thread_created = false;
  }

  // Start audio capture thread
  atomic_store(&g_audio_capture_thread_exited, false);
  if (ascii_thread_create(&g_audio_capture_thread, audio_capture_thread_func, NULL) != 0) {
    log_error("Failed to create audio capture thread");
    return -1;
  }

  g_audio_capture_thread_created = true;

  // Notify server we're starting to send audio
  if (threaded_send_stream_start_packet(STREAM_TYPE_AUDIO) < 0) {
    log_error("Failed to send audio stream start packet");
  }

  return 0;
}

/**
 * Stop audio capture thread
 *
 * Gracefully stops the audio capture thread and cleans up resources.
 * Safe to call multiple times.
 *
 * @ingroup client_audio
 */
void audio_stop_thread() {
  if (!g_audio_capture_thread_created) {
    return;
  }

  // Signal thread to stop
  signal_exit();

  // Wait for thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_audio_capture_thread_exited)) {
    platform_sleep_usec(100000); // 100ms
    wait_count++;
  }

  if (!atomic_load(&g_audio_capture_thread_exited)) {
    log_error("Audio capture thread not responding - forcing join");
  }

  // Join the thread
  ascii_thread_join(&g_audio_capture_thread, NULL);
  g_audio_capture_thread_created = false;

  log_info("Audio capture thread stopped and joined");
}

/**
 * Check if audio capture thread has exited
 *
 * @return true if thread has exited, false otherwise
 *
 * @ingroup client_audio
 */
bool audio_thread_exited() {
  return atomic_load(&g_audio_capture_thread_exited);
}

/**
 * Cleanup audio subsystem
 *
 * Stops audio threads and cleans up PortAudio resources.
 * Called during client shutdown.
 *
 * @ingroup client_audio
 */
void audio_cleanup() {
  if (!opt_audio_enabled) {
    return;
  }

  // Stop capture thread
  audio_stop_thread();

  // CRITICAL: Stop audio streams BEFORE destroying pipeline to prevent race condition
  // The PortAudio output_callback is still executing after audio_stop_playback() returns
  // because PortAudio may invoke the callback one more time. We need to clear the
  // pipeline pointer first so the callback can't access freed memory.
  if (g_audio_context.initialized) {
    audio_stop_playback(&g_audio_context);
    audio_stop_capture(&g_audio_context);
  }

  // Clear the pipeline pointer from audio context BEFORE destroying pipeline
  // This prevents any lingering PortAudio callbacks from trying to access freed memory
  audio_set_pipeline(&g_audio_context, NULL);

  // CRITICAL: Sleep to allow CoreAudio threads to finish executing callbacks
  // On macOS, CoreAudio's internal threads may continue running after Pa_StopStream() returns.
  // The output_callback may still be in-flight on other threads. Even after we set the pipeline
  // pointer to NULL, a CoreAudio thread may have already cached the pointer before the assignment.
  // This sleep ensures all in-flight callbacks have fully completed before we destroy the pipeline.
  // 500ms is sufficient on macOS for CoreAudio's internal thread pool to completely wind down.
  platform_sleep_usec(500000); // 500ms - macOS CoreAudio needs time to shut down all threads

  // Destroy audio pipeline (handles Opus, AEC, etc.)
  if (g_audio_pipeline) {
    client_audio_pipeline_destroy(g_audio_pipeline);
    g_audio_pipeline = NULL;
    log_info("Audio pipeline destroyed");
  }

  // Close WAV dumpers
  if (g_wav_capture_raw) {
    wav_writer_close(g_wav_capture_raw);
    g_wav_capture_raw = NULL;
    log_info("Closed audio capture raw dump");
  }
  if (g_wav_capture_processed) {
    wav_writer_close(g_wav_capture_processed);
    g_wav_capture_processed = NULL;
    log_info("Closed audio capture processed dump");
  }
  if (g_wav_playback_received) {
    wav_writer_close(g_wav_playback_received);
    g_wav_playback_received = NULL;
    log_info("Closed audio playback received dump");
  }

  // Finally destroy the audio context
  if (g_audio_context.initialized) {
    audio_destroy(&g_audio_context);
  }
}

/**
 * @brief Get the audio pipeline (for advanced usage)
 * @return Pointer to the audio pipeline, or NULL if not initialized
 *
 * @ingroup client_audio
 */
client_audio_pipeline_t *audio_get_pipeline(void) {
  return g_audio_pipeline;
}

/**
 * @brief Decode Opus packet using the audio pipeline
 * @param opus_data Opus packet data
 * @param opus_len Opus packet length
 * @param output Output buffer for decoded samples
 * @param max_samples Maximum samples output buffer can hold
 * @return Number of decoded samples, or negative on error
 *
 * @ingroup client_audio
 */
int audio_decode_opus(const uint8_t *opus_data, size_t opus_len, float *output, int max_samples) {
  if (!g_audio_pipeline || !output || max_samples <= 0) {
    return -1;
  }

  return client_audio_pipeline_playback(g_audio_pipeline, opus_data, (int)opus_len, output, max_samples);
}
