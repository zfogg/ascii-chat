/**
 * @file audio.c
 * @brief ASCII-Chat Client Audio Processing Management
 *
 * This module manages all audio-related functionality for the ASCII-Chat client,
 * including audio system initialization, capture thread management, sample
 * processing pipeline, and audio playback coordination.
 *
 * ## Audio Architecture
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
#include "main.h"
#include "server.h"

#include "../lib/audio.h" // lib/audio.h for PortAudio wrapper
#include "mixer.h"    // Audio processing functions
#include "common.h"
#include "options.h"

#include <stdatomic.h>
#include <string.h>

#include "platform/abstraction.h"

/* ============================================================================
 * Audio System State
 * ============================================================================ */

/** Global audio context for PortAudio operations */
static audio_context_t g_audio_context = {0};

/* ============================================================================
 * Audio Capture Thread Management
 * ============================================================================ */

/** Audio capture thread handle */
static asciithread_t g_audio_capture_thread;

/** Flag indicating if audio capture thread was created */
static bool g_audio_capture_thread_created = false;

/** Atomic flag indicating audio capture thread has exited */
static atomic_bool g_audio_capture_thread_exited = false;

/* ============================================================================
 * Audio Processing Constants
 * ============================================================================ */

/** Audio volume boost multiplier for received samples */
#define AUDIO_VOLUME_BOOST 2.0f

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
 */
void audio_process_received_samples(const float *samples, int num_samples) {
  if (!opt_audio_enabled || !samples || num_samples <= 0) {
    return;
  }

  if (num_samples > AUDIO_SAMPLES_PER_PACKET) {
    log_warn("Audio packet too large: %d samples", num_samples);
    return;
  }

  // Apply volume boost and clipping protection
  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];
  for (int i = 0; i < num_samples; i++) {
    audio_buffer[i] = samples[i] * AUDIO_VOLUME_BOOST;
    // Clamp to prevent distortion
    if (audio_buffer[i] > 1.0f)
      audio_buffer[i] = 1.0f;
    if (audio_buffer[i] < -1.0f)
      audio_buffer[i] = -1.0f;
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
 */
static void *audio_capture_thread_func(void *arg) {
  (void)arg;

  log_info("Audio capture thread started");

  float audio_buffer[AUDIO_SAMPLES_PER_PACKET];

  // Audio batching buffers
  float batch_buffer[AUDIO_BATCH_SAMPLES]; // Buffer for accumulating samples
  int batch_samples_collected = 0;         // How many samples we've collected
  int batch_chunks_collected = 0;          // How many chunks we've collected

  // Audio processing components
  static noise_gate_t noise_gate;
  static highpass_filter_t hp_filter;
  static bool processors_initialized = false;

  // Initialize audio processors on first run
  if (!processors_initialized) {
    noise_gate_init(&noise_gate, AUDIO_SAMPLE_RATE); // Use correct sample rate
    noise_gate_set_params(&noise_gate, 0.01f, 2.0f, 50.0f, 0.9f);

    highpass_filter_init(&hp_filter, 80.0f, AUDIO_SAMPLE_RATE); // Use correct sample rate

    processors_initialized = true;
  }

  while (!should_exit() && !server_connection_is_lost()) {
    if (!server_connection_is_active()) {
      platform_sleep_usec(100 * 1000); // Wait for connection
      continue;
    }

    // Read audio samples from microphone
    int samples_read = audio_read_samples(&g_audio_context, audio_buffer, AUDIO_SAMPLES_PER_PACKET);

    if (samples_read > 0) {
      // Apply audio processing chain
      // 1. High-pass filter to remove low-frequency rumble
      highpass_filter_process_buffer(&hp_filter, audio_buffer, samples_read);

      // 2. Noise gate to eliminate background noise
      noise_gate_process_buffer(&noise_gate, audio_buffer, samples_read);

      // 3. Soft clipping to prevent harsh distortion
      soft_clip_buffer(audio_buffer, samples_read, 0.95f);

      // Only batch if gate is open (reduces network traffic for silence)
      if (noise_gate_is_open(&noise_gate)) {
        // Copy processed samples to batch buffer
        memcpy(&batch_buffer[batch_samples_collected], audio_buffer, samples_read * sizeof(float));
        batch_samples_collected += samples_read;
        batch_chunks_collected++;

        // Send batch when we have enough chunks or buffer is full
        if (batch_chunks_collected >= AUDIO_BATCH_COUNT ||
            batch_samples_collected + AUDIO_SAMPLES_PER_PACKET > AUDIO_BATCH_SAMPLES) {

          if (threaded_send_audio_batch_packet(batch_buffer, batch_samples_collected, batch_chunks_collected) < 0) {
            log_debug("Failed to send audio batch to server");
            // Don't set connection lost here as receive thread will detect it
          }
#ifdef DEBUG_AUDIO
          else {
            log_debug("Sent audio batch: %d chunks, %d total samples (gate: %s)", batch_chunks_collected,
                      batch_samples_collected, noise_gate_is_open(&noise_gate) ? "open" : "closed");
          }
#endif
          // Reset batch counters
          batch_samples_collected = 0;
          batch_chunks_collected = 0;
        }
      } else if (batch_samples_collected > 0) {
        // Gate closed but we have pending samples - send what we have
        if (threaded_send_audio_batch_packet(batch_buffer, batch_samples_collected, batch_chunks_collected) < 0) {
          log_debug("Failed to send final audio batch to server");
        }
#ifdef DEBUG_AUDIO
        else {
          log_debug("Sent final audio batch before silence: %d chunks, %d samples", batch_chunks_collected,
                    batch_samples_collected);
        }
#endif
        batch_samples_collected = 0;
        batch_chunks_collected = 0;
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
 */
int audio_client_init() {
  if (!opt_audio_enabled) {
    return 0; // Audio disabled - not an error
  }

  // Initialize PortAudio context using library function
  if (audio_init(&g_audio_context) != 0) {
    log_error("Failed to initialize audio system");
    return -1;
  }

  // Start audio playback
  if (audio_start_playback(&g_audio_context) != 0) {
    log_error("Failed to start audio playback");
    return -1;
  }

  // Start audio capture
  if (audio_start_capture(&g_audio_context) != 0) {
    log_error("Failed to start audio capture");
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
 */
int audio_start_thread() {
  if (!opt_audio_enabled) {
    return 0; // Audio disabled - not an error
  }

  if (g_audio_capture_thread_created) {
    log_warn("Audio capture thread already created");
    return 0;
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
 */
bool audio_thread_exited() {
  return atomic_load(&g_audio_capture_thread_exited);
}

/**
 * Cleanup audio subsystem
 *
 * Stops audio threads and cleans up PortAudio resources.
 * Called during client shutdown.
 */
void audio_cleanup() {
  if (!opt_audio_enabled) {
    return;
  }

  // Stop capture thread
  audio_stop_thread();

  // Stop audio playback and capture
  if (g_audio_context.initialized) {
    audio_stop_playback(&g_audio_context);
    audio_stop_capture(&g_audio_context);
    audio_destroy(&g_audio_context);
  }
}
