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

#include "audio.h"
#include "audio_analysis.h"
#include "main.h"
#include "server.h"

#include "../lib/audio.h" // lib/audio.h for PortAudio wrapper
#include "mixer.h"        // Audio processing functions
#include "common.h"
#include "options.h"
#include "platform/system.h" // For platform_memcpy
#include "wav_writer.h"      // WAV file dumping for debugging
#include "opus_codec.h"      // Opus audio compression

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
 * @brief Opus encoder for audio compression
 *
 * Encodes audio samples before network transmission. Uses VOIP application
 * mode optimized for speech with 24 kbps bitrate for excellent quality
 * with ~98% bandwidth reduction (3528 bytes -> ~60 bytes for 20ms).
 *
 * @ingroup client_audio
 */
static opus_codec_t *g_opus_encoder = NULL;

/**
 * @brief Global Opus decoder for receiving server audio
 *
 * Decodes Opus-compressed audio from server before playback.
 * Created when audio is initialized, destroyed on shutdown.
 *
 * @ingroup client_audio
 */
static opus_codec_t *g_opus_decoder = NULL;

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
 * Applies volume boost and soft clipping to incoming audio samples
 * before submitting them to the audio playback system. Prevents
 * distortion while maintaining good listening levels.
 *
 * Processing Pipeline:
 * 1. Input validation and size checking
 * 2. Volume boost application (AUDIO_VOLUME_BOOST multiplier)
 * 3. Soft clipping to prevent values exceeding [-1.0, 1.0]
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

  // DUMP: Received audio from server (before playback processing)
  if (g_wav_playback_received) {
    wav_writer_write(g_wav_playback_received, samples, num_samples);
  }

  // Apply volume boost and clipping protection
  // Buffer must accommodate batched packets (up to AUDIO_BATCH_SAMPLES)
  float audio_buffer[AUDIO_BATCH_SAMPLES];
  for (int i = 0; i < num_samples; i++) {
    audio_buffer[i] = samples[i] * AUDIO_VOLUME_BOOST;
    // Clamp to prevent distortion
    if (audio_buffer[i] > 1.0F) {
      audio_buffer[i] = 1.0F;
    }
    if (audio_buffer[i] < -1.0F) {
      audio_buffer[i] = -1.0F;
    }

    // Track received samples for analysis
    if (opt_audio_analysis_enabled) {
      audio_analysis_track_received_sample(audio_buffer[i]);
    }
  }

  // Submit to audio playback system
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
 * Implements continuous audio capture with real-time processing pipeline.
 * Captures microphone input, applies audio enhancements, and transmits
 * processed samples to server via batching system.
 *
 * Capture Loop Operation:
 * 1. Check global shutdown flags and connection status
 * 2. Read raw samples from microphone device
 * 3. Apply audio processing chain (filters, noise gate, clipping)
 * 4. Accumulate samples into transmission batches
 * 5. Send batches when full or when noise gate closes
 * 6. Handle connection errors and thread termination
 *
 * Audio Processing Chain:
 * - High-pass filter removes low-frequency rumble (80Hz cutoff)
 * - Noise gate eliminates background noise (configurable thresholds)
 * - Soft clipping prevents harsh distortion (95% limit)
 * - Batching reduces network overhead while maintaining quality
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

// Opus frame size: 960 samples = 20ms @ 48kHz
#define OPUS_FRAME_SAMPLES 960
#define OPUS_MAX_PACKET_SIZE 250 // Opus can encode to ~60 bytes @ 24kbps, 250 is safe max
#define OPUS_BATCH_FRAMES 20     // Batch 20 frames = 400ms of audio

  // Audio batching buffers for Opus encoding
  float opus_frame_buffer[OPUS_FRAME_SAMPLES];                         // Accumulate samples for one Opus frame
  int opus_frame_samples_collected = 0;                                // Samples in current Opus frame
  uint8_t opus_batch_buffer[OPUS_MAX_PACKET_SIZE * OPUS_BATCH_FRAMES]; // Encoded Opus data batch
  uint16_t opus_frame_sizes[OPUS_BATCH_FRAMES];                        // Size of each frame in batch
  size_t opus_batch_size = 0;                                          // Total bytes in opus_batch_buffer
  int opus_batch_frame_count = 0;                                      // Number of Opus frames in batch

  // Audio processing components
  static highpass_filter_t hp_filter;
  static bool processors_initialized = false;

  // Initialize audio processors on first run
  if (!processors_initialized) {
    // Initialize high-pass filter to remove DC offset and subsonic frequencies
    highpass_filter_init(&hp_filter, 80.0F, AUDIO_SAMPLE_RATE);

    // Initialize WAV dumpers if debugging enabled
    if (wav_dump_enabled()) {
      g_wav_capture_raw = wav_writer_open("/tmp/audio_capture_raw.wav", AUDIO_SAMPLE_RATE, 1);
      g_wav_capture_processed = wav_writer_open("/tmp/audio_capture_processed.wav", AUDIO_SAMPLE_RATE, 1);
      log_info("Audio debugging enabled: dumping to /tmp/audio_capture_*.wav");
    }

    processors_initialized = true;
  }

  while (!should_exit() && !server_connection_is_lost()) {
    if (!server_connection_is_active()) {
      platform_sleep_usec(100 * 1000); // Wait for connection
      continue;
    }

    // Check how many samples are available in the ring buffer
    int available = audio_ring_buffer_available_read(g_audio_context.capture_buffer);
    if (available <= 0) {
      // No samples available, sleep briefly and continue
      platform_sleep_usec(5 * 1000); // 5ms
      continue;
    }

    // Read samples from the ring buffer (up to AUDIO_SAMPLES_PER_PACKET)
    int to_read = (available < AUDIO_SAMPLES_PER_PACKET) ? available : AUDIO_SAMPLES_PER_PACKET;
    asciichat_error_t read_result = audio_read_samples(&g_audio_context, audio_buffer, to_read);

    if (read_result != ASCIICHAT_OK) {
      log_error("Failed to read audio samples from ring buffer");
      platform_sleep_usec(5 * 1000); // 5ms
      continue;
    }

    int samples_read = to_read; // We successfully read this many samples

    // Log every 10 reads to see if we're getting samples
    static int total_reads = 0;
    total_reads++;
    if (total_reads % 10 == 0) {
      log_info("Audio capture loop iteration #%d: available=%d, samples_read=%d", total_reads, available, samples_read);
    }

    if (samples_read > 0) {
      // DUMP: Raw captured audio (before any processing)
      if (g_wav_capture_raw) {
        wav_writer_write(g_wav_capture_raw, audio_buffer, samples_read);
      }
      // Log sample levels every 100 reads for debugging
      static int read_count = 0;
      read_count++;
      if (read_count % 100 == 0) {
        // Calculate RMS of captured samples
        float sum_squares = 0.0f;
        for (int i = 0; i < samples_read; i++) {
          sum_squares += audio_buffer[i] * audio_buffer[i];
        }
        float rms = sqrtf(sum_squares / samples_read);
        log_info("Audio capture read #%d: samples=%d, first=[%.4f, %.4f, %.4f], RMS=%.6f", read_count, samples_read,
                 audio_buffer[0], audio_buffer[1], audio_buffer[2], rms);
      }

      // Apply audio processing chain
      // 1. High-pass filter to remove low-frequency rumble (DC offset and very low frequencies)
      highpass_filter_process_buffer(&hp_filter, audio_buffer, samples_read);

      // 2. Apply conservative automatic gain control - gently boost quiet audio
      // Calculate RMS to measure audio level
      float sum_squares = 0.0f;
      for (int i = 0; i < samples_read; i++) {
        sum_squares += audio_buffer[i] * audio_buffer[i];
      }
      float rms = sqrtf(sum_squares / samples_read);

      // Conservative AGC settings to avoid amplifying noise
      const float target_rms = 0.05f;         // Target RMS - 5% of full scale (prevent over-amplification)
      const float max_gain = 20.0f;           // Maximum 20x amplification
      const float min_rms_for_gain = 0.0001f; // Noise floor threshold (lower for quiet environments)

      if (rms > min_rms_for_gain) { // Only apply gain if there's actual audio above noise floor
        float gain = target_rms / rms;
        // Clamp gain to reasonable range [0.5x, max_gain]
        if (gain > max_gain)
          gain = max_gain;
        if (gain < 0.5f)
          gain = 0.5f;     // Also limit attenuation
        if (gain > 1.0f) { // Only apply if we need to boost
          for (int i = 0; i < samples_read; i++) {
            audio_buffer[i] *= gain;
          }
        }
        log_debug_every(2000000, "AGC: rms=%.6f, gain=%.2fx (target=%.2f)", rms, gain, target_rms);
      }

      // 3. Gentle soft clipping to prevent harsh distortion
      // Use higher threshold (0.98 vs 0.95) to reduce clipping artifacts
      soft_clip_buffer(audio_buffer, samples_read, 0.98F);

      // DUMP: Processed audio (after all filtering, before network send)
      if (g_wav_capture_processed) {
        wav_writer_write(g_wav_capture_processed, audio_buffer, samples_read);
      }

      // Accumulate samples into Opus frame buffer
      int samples_to_process = samples_read;
      int sample_offset = 0;

      while (samples_to_process > 0) {
        // How many samples can we add to current Opus frame?
        int space_in_frame = OPUS_FRAME_SAMPLES - opus_frame_samples_collected;
        int samples_to_copy = (samples_to_process < space_in_frame) ? samples_to_process : space_in_frame;

        // Copy samples to Opus frame buffer
        size_t copy_size = (size_t)samples_to_copy * sizeof(float);
        size_t dest_space = (size_t)(OPUS_FRAME_SAMPLES - opus_frame_samples_collected) * sizeof(float);
        if (platform_memcpy(&opus_frame_buffer[opus_frame_samples_collected], dest_space, &audio_buffer[sample_offset],
                            copy_size) != 0) {
          log_error("Failed to copy audio samples to Opus frame buffer");
          break;
        }

        // Track sent samples for analysis
        if (opt_audio_analysis_enabled) {
          for (int i = 0; i < samples_to_copy; i++) {
            audio_analysis_track_sent_sample(audio_buffer[sample_offset + i]);
          }
        }

        opus_frame_samples_collected += samples_to_copy;
        sample_offset += samples_to_copy;
        samples_to_process -= samples_to_copy;

        // Do we have a complete Opus frame (882 samples)?
        if (opus_frame_samples_collected >= OPUS_FRAME_SAMPLES) {
          // Encode frame with Opus
          uint8_t opus_packet[OPUS_MAX_PACKET_SIZE];
          size_t encoded_bytes = opus_codec_encode(g_opus_encoder, opus_frame_buffer, OPUS_FRAME_SAMPLES, opus_packet,
                                                   OPUS_MAX_PACKET_SIZE);

          if (encoded_bytes == 0) {
            log_error("Opus encoding failed");
            opus_frame_samples_collected = 0; // Reset frame
            break;
          }

          log_debug_every(100000, "Opus encoded: %d samples -> %zu bytes (compression: %.1fx)", OPUS_FRAME_SAMPLES,
                          encoded_bytes, (float)(OPUS_FRAME_SAMPLES * sizeof(float)) / (float)encoded_bytes);

          // Append encoded data to batch buffer and track frame size
          if (opus_batch_size + encoded_bytes <= sizeof(opus_batch_buffer) &&
              opus_batch_frame_count < OPUS_BATCH_FRAMES) {
            memcpy(&opus_batch_buffer[opus_batch_size], opus_packet, encoded_bytes);
            opus_frame_sizes[opus_batch_frame_count] = (uint16_t)encoded_bytes;
            opus_batch_size += encoded_bytes;
            opus_batch_frame_count++;
          } else {
            log_error("Opus batch buffer overflow, discarding frame");
          }

          // Reset frame buffer for next frame
          opus_frame_samples_collected = 0;

          // Send batch when we have collected enough Opus frames
          if (opus_batch_frame_count >= OPUS_BATCH_FRAMES) {
            log_info("Sending Opus batch: %d frames, %zu bytes", opus_batch_frame_count, opus_batch_size);

            if (threaded_send_audio_opus_batch(opus_batch_buffer, opus_batch_size, opus_frame_sizes,
                                               opus_batch_frame_count) < 0) {
              log_error("Failed to send Opus audio batch to server");
              // Don't set connection lost here as receive thread will detect it
            } else {
              log_debug("Opus audio batch sent successfully: %d frames, %zu bytes", opus_batch_frame_count,
                        opus_batch_size);

              // Track packet for analysis
              if (opt_audio_analysis_enabled) {
                audio_analysis_track_sent_packet(opus_batch_size);
              }
            }

            // Reset batch counters
            opus_batch_size = 0;
            opus_batch_frame_count = 0;
          }
        }
      }
    } else {
      // Small delay if no audio available
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
 * Sets up PortAudio context and starts audio playback and capture
 * if audio is enabled. Must be called once during client initialization.
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

  // Create Opus encoder for audio compression (24 kbps VOIP mode)
  g_opus_encoder = opus_codec_create_encoder(OPUS_APPLICATION_VOIP, AUDIO_SAMPLE_RATE, 24000);
  if (!g_opus_encoder) {
    log_error("Failed to create Opus encoder");
    audio_destroy(&g_audio_context);
    return -1;
  }

  log_info("Opus encoder created: 24 kbps VOIP mode, %d Hz sample rate", AUDIO_SAMPLE_RATE);

  // Create Opus decoder for receiving server audio
  g_opus_decoder = opus_codec_create_decoder(AUDIO_SAMPLE_RATE);
  if (!g_opus_decoder) {
    log_error("Failed to create Opus decoder");
    opus_codec_destroy(g_opus_encoder);
    g_opus_encoder = NULL;
    audio_destroy(&g_audio_context);
    return -1;
  }

  log_info("Opus decoder created: %d Hz sample rate", AUDIO_SAMPLE_RATE);

  // Start audio playback
  if (audio_start_playback(&g_audio_context) != 0) {
    log_error("Failed to start audio playback");
    opus_codec_destroy(g_opus_encoder);
    g_opus_encoder = NULL;
    opus_codec_destroy(g_opus_decoder);
    g_opus_decoder = NULL;
    audio_destroy(&g_audio_context);
    return -1;
  }

  // Start audio capture
  if (audio_start_capture(&g_audio_context) != ASCIICHAT_OK) {
    log_error("Failed to start audio capture");
    opus_codec_destroy(g_opus_encoder);
    g_opus_encoder = NULL;
    opus_codec_destroy(g_opus_decoder);
    g_opus_decoder = NULL;
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
    ascii_thread_join(&g_audio_capture_thread, NULL);
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

  // Destroy Opus encoder and decoder
  if (g_opus_encoder) {
    opus_codec_destroy(g_opus_encoder);
    g_opus_encoder = NULL;
    log_info("Opus encoder destroyed");
  }
  if (g_opus_decoder) {
    opus_codec_destroy(g_opus_decoder);
    g_opus_decoder = NULL;
    log_info("Opus decoder destroyed");
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

  // Stop audio playback and capture
  if (g_audio_context.initialized) {
    audio_stop_playback(&g_audio_context);
    audio_stop_capture(&g_audio_context);
    audio_destroy(&g_audio_context);
  }
}

/**
 * @brief Get Opus decoder for receiving server audio
 * @return Pointer to Opus decoder, or NULL if not initialized
 *
 * @ingroup client_audio
 */
opus_codec_t *audio_get_opus_decoder(void) {
  return g_opus_decoder;
}
