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
#include <ascii-chat/audio/analysis.h>
#include "main.h"
#include "server.h"
#include <ascii-chat/util/fps.h>
#include <ascii-chat/util/thread.h>
#include <ascii-chat/util/time.h> // For timing instrumentation

#include <ascii-chat/audio/audio.h>                 // lib/audio/audio.h for PortAudio wrapper
#include <ascii-chat/audio/client_audio_pipeline.h> // Unified audio processing pipeline
#include <ascii-chat/audio/wav_writer.h>            // WAV file dumping for debugging
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>     // For RCU-based options access
#include <ascii-chat/platform/system.h> // For platform_memcpy

#include <stdatomic.h>
#include <string.h>
#include <math.h>

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/init.h>
#include <ascii-chat/thread_pool.h>

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
static asciichat_thread_t g_audio_capture_thread;

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
 * Async Audio Packet Queue (decouples capture from network I/O)
 * ============================================================================ */

/**
 * @brief Audio packet for async sending
 *
 * Represents a batch of Opus frames ready to be sent to the server.
 * The sender thread pulls these from the queue and handles network I/O.
 */
typedef struct {
  uint8_t data[8 * 4000]; // Max 8 frames * ~500 bytes each (with safety margin)
  size_t size;
  uint16_t frame_sizes[8];
  int frame_count;
} audio_send_packet_t;

/** Ring buffer queue for async audio packets */
#define AUDIO_SEND_QUEUE_SIZE 32
static audio_send_packet_t g_audio_send_queue[AUDIO_SEND_QUEUE_SIZE];
static int g_audio_send_queue_head = 0; // Write position
static int g_audio_send_queue_tail = 0; // Read position
static mutex_t g_audio_send_queue_mutex;
static cond_t g_audio_send_queue_cond;
static bool g_audio_send_queue_initialized = false;
static static_mutex_t g_audio_send_queue_init_mutex = STATIC_MUTEX_INIT;

/** Audio sender thread */
static bool g_audio_sender_thread_created = false;
static atomic_bool g_audio_sender_should_exit = false;

/**
 * @brief Queue an audio packet for async sending (non-blocking)
 *
 * Called by capture thread. Returns immediately without blocking on network.
 *
 * @param opus_data Encoded Opus data
 * @param opus_size Size of Opus data
 * @param frame_sizes Array of frame sizes
 * @param frame_count Number of frames
 * @return 0 on success, -1 if queue is full
 */
static int audio_queue_packet(const uint8_t *opus_data, size_t opus_size, const uint16_t *frame_sizes,
                              int frame_count) {
  if (!g_audio_send_queue_initialized || !opus_data || opus_size == 0) {
    return -1;
  }

  mutex_lock(&g_audio_send_queue_mutex);

  // Check if queue is full
  int next_head = (g_audio_send_queue_head + 1) % AUDIO_SEND_QUEUE_SIZE;
  if (next_head == g_audio_send_queue_tail) {
    mutex_unlock(&g_audio_send_queue_mutex);
    log_warn_every(LOG_RATE_FAST, "Audio send queue full, dropping packet");
    return -1;
  }

  // Copy packet to queue
  audio_send_packet_t *packet = &g_audio_send_queue[g_audio_send_queue_head];
  if (opus_size <= sizeof(packet->data)) {
    memcpy(packet->data, opus_data, opus_size);
    packet->size = opus_size;
    packet->frame_count = frame_count;
    for (int i = 0; i < frame_count && i < 8; i++) {
      packet->frame_sizes[i] = frame_sizes[i];
    }
    g_audio_send_queue_head = next_head;
  }

  // Signal sender thread
  cond_signal(&g_audio_send_queue_cond);
  mutex_unlock(&g_audio_send_queue_mutex);

  return 0;
}

/**
 * @brief Audio sender thread function
 *
 * Pulls packets from the queue and sends them to the server.
 * Network I/O blocking happens here, not in the capture thread.
 */
static void *audio_sender_thread_func(void *arg) {
  (void)arg;
  log_debug("Audio sender thread started");

  // Initialize timing system for performance profiling
  if (!timer_is_initialized()) {
    timer_system_init();
  }

  static int send_count = 0;

  while (!atomic_load(&g_audio_sender_should_exit)) {
    mutex_lock(&g_audio_send_queue_mutex);

    // Wait for packet or exit signal
    while (g_audio_send_queue_head == g_audio_send_queue_tail && !atomic_load(&g_audio_sender_should_exit)) {
      cond_wait(&g_audio_send_queue_cond, &g_audio_send_queue_mutex);
    }

    if (atomic_load(&g_audio_sender_should_exit)) {
      mutex_unlock(&g_audio_send_queue_mutex);
      break;
    }

    // Dequeue packet
    audio_send_packet_t packet = g_audio_send_queue[g_audio_send_queue_tail];
    g_audio_send_queue_tail = (g_audio_send_queue_tail + 1) % AUDIO_SEND_QUEUE_SIZE;

    mutex_unlock(&g_audio_send_queue_mutex);

    // Send packet (may block on network I/O - that's OK, we're not in capture thread)
    START_TIMER("network_send_audio");
    asciichat_error_t send_result =
        threaded_send_audio_opus_batch(packet.data, packet.size, packet.frame_sizes, packet.frame_count);
    double send_time_ns = STOP_TIMER("network_send_audio");

    send_count++;
    if (send_result < 0) {
      log_debug_every(LOG_RATE_VERY_FAST, "Failed to send audio packet");
    } else if (send_count % 50 == 0) {
      char duration_str[32];
      format_duration_ns(send_time_ns, duration_str, sizeof(duration_str));
      log_debug("Audio network send #%d: %zu bytes (%d frames) in %s", send_count, packet.size, packet.frame_count,
                duration_str);
    }
  }

  log_debug("Audio sender thread exiting");

  // Clean up thread-local error context before exit
  asciichat_errno_cleanup();

  return NULL;
}

/**
 * @brief Initialize async audio sender queue and thread
 *
 * Uses mutex protection to prevent TOCTOU race conditions where multiple
 * threads might attempt initialization simultaneously.
 */
static void audio_sender_init(void) {
  static_mutex_lock(&g_audio_send_queue_init_mutex);

  // Check again under lock to prevent race condition
  if (g_audio_send_queue_initialized) {
    static_mutex_unlock(&g_audio_send_queue_init_mutex);
    return;
  }

  // Initialize queue structures under lock
  mutex_init(&g_audio_send_queue_mutex);
  cond_init(&g_audio_send_queue_cond);
  g_audio_send_queue_head = 0;
  g_audio_send_queue_tail = 0;
  g_audio_send_queue_initialized = true;
  atomic_store(&g_audio_sender_should_exit, false);

  static_mutex_unlock(&g_audio_send_queue_init_mutex);

  // Start sender thread (after lock release to avoid blocking other threads)
  if (thread_pool_spawn(g_client_worker_pool, audio_sender_thread_func, NULL, 5, "audio_sender") == ASCIICHAT_OK) {
    g_audio_sender_thread_created = true;
    log_debug("Audio sender thread created");
  } else {
    log_error("Failed to spawn audio sender thread in worker pool");
    LOG_ERRNO_IF_SET("Audio sender thread creation failed");
  }
}

/**
 * @brief Cleanup async audio sender
 */
static void audio_sender_cleanup(void) {
  if (!g_audio_send_queue_initialized) {
    return;
  }

  // Signal thread to exit
  atomic_store(&g_audio_sender_should_exit, true);
  mutex_lock(&g_audio_send_queue_mutex);
  cond_signal(&g_audio_send_queue_cond);
  mutex_unlock(&g_audio_send_queue_mutex);

  // Thread will be joined by thread_pool_stop_all() in protocol_stop_connection()
  if (THREAD_IS_CREATED(g_audio_sender_thread_created)) {
    g_audio_sender_thread_created = false;
    log_debug("Audio sender thread will be joined by thread pool");
  }

  mutex_destroy(&g_audio_send_queue_mutex);
  cond_destroy(&g_audio_send_queue_cond);
  g_audio_send_queue_initialized = false;
}

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
  // Validate parameters
  if (!samples || num_samples <= 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio samples: samples=%p, num_samples=%d", (void *)samples, num_samples);
    return;
  }

  if (!GET_OPTION(audio_enabled)) {
    log_warn_every(1000000, "Received audio samples but audio is disabled");
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
  if (GET_OPTION(audio_analysis_enabled)) {
    for (int i = 0; i < num_samples; i++) {
      audio_analysis_track_received_sample(samples[i]);
    }
  }

  // Copy samples to playback buffer (no processing needed - mixer already handled clipping)
  float audio_buffer[AUDIO_BATCH_SAMPLES];
  memcpy(audio_buffer, samples, (size_t)num_samples * sizeof(float));

  // DEBUG: Log what we're writing to playback buffer (with first 4 samples to verify audio integrity)
  static int recv_count = 0;
  recv_count++;
  if (recv_count <= 10 || recv_count % 50 == 0) {
    float peak = 0.0f;
    for (int i = 0; i < num_samples; i++) {
      float abs_val = fabsf(samples[i]);
      if (abs_val > peak)
        peak = abs_val;
    }
    log_debug("CLIENT AUDIO RECV #%d: %d samples, RMS=%.6f, Peak=%.6f, first4=[%.4f,%.4f,%.4f,%.4f]", recv_count,
              num_samples, received_rms, peak, num_samples > 0 ? samples[0] : 0.0f, num_samples > 1 ? samples[1] : 0.0f,
              num_samples > 2 ? samples[2] : 0.0f, num_samples > 3 ? samples[3] : 0.0f);
  }

  // Submit to playback system (goes to jitter buffer and speakers)
  // NOTE: AEC3's AnalyzeRender is called in output_callback() when audio actually plays,
  // NOT here. The jitter buffer adds 50-100ms delay, so calling AnalyzeRender here
  // would give AEC3 the wrong timing and break echo cancellation.
  audio_write_samples(&g_audio_context, audio_buffer, num_samples);

  // Log latency after writing to playback buffer
  if (g_audio_context.playback_buffer) {
    size_t buffer_samples = audio_ring_buffer_available_read(g_audio_context.playback_buffer);
    float buffer_latency_ms = (float)buffer_samples / 48.0f;
    log_debug_every(500000, "LATENCY: Client playback buffer after recv: %.1fms (%zu samples)", buffer_latency_ms,
                    buffer_samples);
  }

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

  log_debug("Audio capture thread started");

  // Initialize timing system for performance profiling
  if (!timer_is_initialized()) {
    timer_system_init();
  }

  // FPS tracking for audio capture thread (tracking Opus frames, ~50 FPS at 20ms per frame)
  static fps_t fps_tracker = {0};
  static bool fps_tracker_initialized = false;
  if (!fps_tracker_initialized) {
    fps_init(&fps_tracker, 50, "AUDIO_TX");
    fps_tracker_initialized = true;
  }

  // Detailed timing stats
  static double total_loop_ns = 0;
  static double total_read_ns = 0;
  static double total_encode_ns = 0;
  static double total_queue_ns = 0;
  static double max_loop_ns = 0;
  static double max_read_ns = 0;
  static double max_encode_ns = 0;
  static double max_queue_ns = 0;
  static uint64_t timing_loop_count = 0;

// Opus frame size: 960 samples = 20ms @ 48kHz (must match pipeline config)
#define OPUS_FRAME_SAMPLES 960
#define OPUS_MAX_PACKET_SIZE 500 // Max Opus packet size

  // Read enough samples per iteration to drain faster than we fill
  // Buffer holds multiple Opus frames worth to prevent overflow
  // 4 frames = 3840 samples = 80ms, but we'll read what's available up to this
#define CAPTURE_READ_SIZE (OPUS_FRAME_SAMPLES * 4)

  float audio_buffer[CAPTURE_READ_SIZE];
  static bool wav_dumpers_initialized = false;

  // Initialize WAV dumpers only once (file handles persist)
  if (!wav_dumpers_initialized && wav_dump_enabled()) {
    g_wav_capture_raw = wav_writer_open("/tmp/audio_capture_raw.wav", AUDIO_SAMPLE_RATE, 1);
    g_wav_capture_processed = wav_writer_open("/tmp/audio_capture_processed.wav", AUDIO_SAMPLE_RATE, 1);
    log_debug("Audio debugging enabled: dumping to /tmp/audio_capture_*.wav");
    wav_dumpers_initialized = true;
  }

  // Accumulator for building complete Opus frames
  float opus_frame_buffer[OPUS_FRAME_SAMPLES];
  int opus_frame_samples_collected = 0;

  // Batch buffer for multiple Opus frames - send all at once to reduce blocking
#define MAX_BATCH_FRAMES 8
#define BATCH_TIMEOUT_MS 40 // Flush batch after 40ms even if not full (2 Opus frames @ 20ms each)
  static uint8_t batch_buffer[MAX_BATCH_FRAMES * OPUS_MAX_PACKET_SIZE];
  static uint16_t batch_frame_sizes[MAX_BATCH_FRAMES];
  static int batch_frame_count = 0;
  static size_t batch_total_size = 0;
  static uint64_t batch_start_time_ns = 0;
  static bool batch_has_data = false;

  while (!should_exit() && !server_connection_is_lost()) {
    START_TIMER("audio_capture_loop_iteration");
    timing_loop_count++;

    if (!server_connection_is_active()) {
      STOP_TIMER("audio_capture_loop_iteration"); // Don't count sleep time
      platform_sleep_usec(100 * 1000);            // Wait for connection
      continue;
    }

    // Check if pipeline is ready
    if (!g_audio_pipeline) {
      STOP_TIMER("audio_capture_loop_iteration"); // Don't count sleep time
      platform_sleep_usec(100 * 1000);
      continue;
    }

    // Check how many samples are available in the ring buffer
    int available = audio_ring_buffer_available_read(g_audio_context.capture_buffer);
    if (available <= 0) {
      // Flush partial batch before sleeping (prevent starvation during idle periods)
      if (batch_has_data && batch_frame_count > 0) {
        uint64_t now_ns = time_get_ns();
        uint64_t elapsed_ns = time_elapsed_ns(batch_start_time_ns, now_ns);
        long elapsed_ms = (long)time_ns_to_ms(elapsed_ns);

        if (elapsed_ms >= BATCH_TIMEOUT_MS) {
          log_debug_every(LOG_RATE_FAST, "Idle timeout flush: %d frames (%zu bytes) after %ld ms", batch_frame_count,
                          batch_total_size, elapsed_ms);
          (void)audio_queue_packet(batch_buffer, batch_total_size, batch_frame_sizes, batch_frame_count);
          batch_frame_count = 0;
          batch_total_size = 0;
          batch_has_data = false;
        }
      }

      // Sleep briefly to reduce CPU usage when idle
      // 5ms polling = 200 times/sec, fast enough to catch audio promptly
      // CRITICAL: 50ms was causing 872ms gaps in audio transmission!
      STOP_TIMER("audio_capture_loop_iteration"); // Must stop before loop repeats
      platform_sleep_usec(5 * 1000);              // 5ms (was 50ms - caused huge gaps!)
      continue;
    }

    // Read as many samples as possible (up to CAPTURE_READ_SIZE) to drain faster
    // This prevents buffer overflow when processing is slower than capture
    int to_read = (available < CAPTURE_READ_SIZE) ? available : CAPTURE_READ_SIZE;

    START_TIMER("audio_read_samples");
    asciichat_error_t read_result = audio_read_samples(&g_audio_context, audio_buffer, to_read);
    double read_time_ns = STOP_TIMER("audio_read_samples");

    total_read_ns += read_time_ns;
    if (read_time_ns > max_read_ns)
      max_read_ns = read_time_ns;

    if (read_result != ASCIICHAT_OK) {
      log_error("Failed to read audio samples from ring buffer");
      STOP_TIMER("audio_capture_loop_iteration"); // Don't count sleep time
      platform_sleep_usec(5 * 1000);              // 5ms (error path - was 50ms, caused gaps!)
      continue;
    }

    int samples_read = to_read;

    // Log every 10 reads to see if we're getting samples
    static int total_reads = 0;
    total_reads++;
    if (total_reads % 10 == 0) {
      log_debug("Audio capture loop iteration #%d: available=%d, samples_read=%d", total_reads, available,
                samples_read);
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
          log_debug("Input normalization #%d: peak=%.4f, gain=%.4f", norm_count, peak, gain);
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
        log_debug("Audio capture read #%d: available=%d, samples_read=%d, first=[%.6f,%.6f,%.6f], RMS=%.6f", read_count,
                  available, samples_read, samples_read > 0 ? audio_buffer[0] : 0.0f,
                  samples_read > 1 ? audio_buffer[1] : 0.0f, samples_read > 2 ? audio_buffer[2] : 0.0f, rms);
      }

      // Track sent samples for analysis
      if (GET_OPTION(audio_analysis_enabled)) {
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

          START_TIMER("opus_encode");
          int opus_len = client_audio_pipeline_capture(g_audio_pipeline, opus_frame_buffer, OPUS_FRAME_SAMPLES,
                                                       opus_packet, OPUS_MAX_PACKET_SIZE);
          double encode_time_ns = STOP_TIMER("opus_encode");

          total_encode_ns += encode_time_ns;
          if (encode_time_ns > max_encode_ns)
            max_encode_ns = encode_time_ns;

          if (opus_len > 0) {
            static int encode_count = 0;
            encode_count++;
            if (encode_count % 50 == 0) {
              char duration_str[32];
              format_duration_ns(encode_time_ns, duration_str, sizeof(duration_str));
              log_debug("Opus encode #%d: %d samples -> %d bytes in %s", encode_count, OPUS_FRAME_SAMPLES, opus_len,
                        duration_str);
            }

            log_debug_every(LOG_RATE_VERY_FAST, "Pipeline encoded: %d samples -> %d bytes (compression: %.1fx)",
                            OPUS_FRAME_SAMPLES, opus_len,
                            (float)(OPUS_FRAME_SAMPLES * sizeof(float)) / (float)opus_len);

            // Add to batch buffer
            if (batch_frame_count < MAX_BATCH_FRAMES && batch_total_size + (size_t)opus_len <= sizeof(batch_buffer)) {
              // Mark batch start time on first frame
              if (batch_frame_count == 0) {
                batch_start_time_ns = time_get_ns();
                batch_has_data = true;
              }

              memcpy(batch_buffer + batch_total_size, opus_packet, (size_t)opus_len);
              batch_frame_sizes[batch_frame_count] = (uint16_t)opus_len;
              batch_total_size += (size_t)opus_len;
              batch_frame_count++;

              if (GET_OPTION(audio_analysis_enabled)) {
                audio_analysis_track_sent_packet((size_t)opus_len);
              }
            }
          } else if (opus_len == 0) {
            // DTX frame (silence) - no data to send
            log_debug_every(LOG_RATE_VERY_FAST, "Pipeline DTX frame (silence detected)");
          }

          // Reset frame buffer
          opus_frame_samples_collected = 0;
        }
      }

      // Queue batch for async sending (non-blocking - sender thread handles network I/O)
      if (batch_frame_count > 0) {
        static int batch_send_count = 0;
        batch_send_count++;

        START_TIMER("audio_queue_packet");
        int queue_result = audio_queue_packet(batch_buffer, batch_total_size, batch_frame_sizes, batch_frame_count);
        double queue_time_ns = STOP_TIMER("audio_queue_packet");

        total_queue_ns += queue_time_ns;
        if (queue_time_ns > max_queue_ns)
          max_queue_ns = queue_time_ns;

        if (queue_result < 0) {
          log_debug_every(LOG_RATE_VERY_FAST, "Failed to queue audio batch (queue full)");
        } else {
          if (batch_send_count <= 10 || batch_send_count % 50 == 0) {
            char queue_duration_str[32];
            format_duration_ns(queue_time_ns, queue_duration_str, sizeof(queue_duration_str));
            log_debug("CLIENT: Queued Opus batch #%d (%d frames, %zu bytes) in %s", batch_send_count, batch_frame_count,
                      batch_total_size, queue_duration_str);
          }
          // Track audio frame for FPS reporting
          fps_frame_ns(&fps_tracker, time_get_ns(), "audio batch queued");
        }

        // Reset batch
        batch_frame_count = 0;
        batch_total_size = 0;
        batch_has_data = false;
      }

      // Log overall loop iteration time periodically
      double loop_time_ns = STOP_TIMER("audio_capture_loop_iteration");
      total_loop_ns += loop_time_ns;
      if (loop_time_ns > max_loop_ns)
        max_loop_ns = loop_time_ns;

      // Comprehensive timing report every 100 iterations (~2 seconds)
      if (timing_loop_count % 100 == 0) {
        char avg_loop_str[32], max_loop_str[32];
        char avg_read_str[32], max_read_str[32];
        char avg_encode_str[32], max_encode_str[32];
        char avg_queue_str[32], max_queue_str[32];

        format_duration_ns(total_loop_ns / timing_loop_count, avg_loop_str, sizeof(avg_loop_str));
        format_duration_ns(max_loop_ns, max_loop_str, sizeof(max_loop_str));
        format_duration_ns(total_read_ns / timing_loop_count, avg_read_str, sizeof(avg_read_str));
        format_duration_ns(max_read_ns, max_read_str, sizeof(max_read_str));
        format_duration_ns(total_encode_ns / timing_loop_count, avg_encode_str, sizeof(avg_encode_str));
        format_duration_ns(max_encode_ns, max_encode_str, sizeof(max_encode_str));
        format_duration_ns(total_queue_ns / timing_loop_count, avg_queue_str, sizeof(avg_queue_str));
        format_duration_ns(max_queue_ns, max_queue_str, sizeof(max_queue_str));

        log_debug("CAPTURE TIMING #%lu: loop avg=%s max=%s, read avg=%s max=%s", timing_loop_count, avg_loop_str,
                  max_loop_str, avg_read_str, max_read_str);
        log_info("  encode avg=%s max=%s, queue avg=%s max=%s", avg_encode_str, max_encode_str, avg_queue_str,
                 max_queue_str);
      }

      // Check if we have a partial batch that's been waiting too long (time-based flush)
      // This prevents batches from sitting indefinitely when audio capture is irregular
      if (batch_has_data && batch_frame_count > 0) {
        uint64_t now_ns = time_get_ns();
        uint64_t elapsed_ns = time_elapsed_ns(batch_start_time_ns, now_ns);
        long elapsed_ms = (long)time_ns_to_ms(elapsed_ns);

        if (elapsed_ms >= BATCH_TIMEOUT_MS) {
          static int timeout_flush_count = 0;
          timeout_flush_count++;

          log_debug_every(LOG_RATE_FAST, "Timeout flush #%d: %d frames (%zu bytes) after %ld ms", timeout_flush_count,
                          batch_frame_count, batch_total_size, elapsed_ms);

          // Queue partial batch
          int queue_result = audio_queue_packet(batch_buffer, batch_total_size, batch_frame_sizes, batch_frame_count);
          if (queue_result == 0) {
            // Track audio frame for FPS reporting
            fps_frame_ns(&fps_tracker, time_get_ns(), "audio batch timeout flush");
          }

          // Reset batch
          batch_frame_count = 0;
          batch_total_size = 0;
          batch_has_data = false;
        }
      }

      // Yield to reduce CPU usage - audio arrives at ~20ms per Opus frame (960 samples @ 48kHz)
      // Without sleep, thread spins at 90-100% CPU constantly checking for new samples
      // Even 1ms sleep reduces CPU usage from 90% to <10% with minimal latency impact
      platform_sleep_usec(1000); // 1ms
    } else {
      // Track loop time even when no samples processed
      double loop_time_ns = STOP_TIMER("audio_capture_loop_iteration");
      total_loop_ns += loop_time_ns;
      if (loop_time_ns > max_loop_ns)
        max_loop_ns = loop_time_ns;

      // Flush partial batch before sleeping on error path (prevent starvation)
      if (batch_has_data && batch_frame_count > 0) {
        uint64_t now_ns = time_get_ns();
        uint64_t elapsed_ns = time_elapsed_ns(batch_start_time_ns, now_ns);
        long elapsed_ms = (long)time_ns_to_ms(elapsed_ns);

        if (elapsed_ms >= BATCH_TIMEOUT_MS) {
          log_debug_every(LOG_RATE_FAST, "Error path timeout flush: %d frames (%zu bytes) after %ld ms",
                          batch_frame_count, batch_total_size, elapsed_ms);
          (void)audio_queue_packet(batch_buffer, batch_total_size, batch_frame_sizes, batch_frame_count);
          batch_frame_count = 0;
          batch_total_size = 0;
          batch_has_data = false;
        }
      }

      platform_sleep_usec(5 * 1000); // 5ms (error path - was 50ms, caused gaps!)
    }
  }

  log_debug("Audio capture thread stopped");
  atomic_store(&g_audio_capture_thread_exited, true);

  // Clean up thread-local error context before exit
  asciichat_errno_cleanup();

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
  if (!GET_OPTION(audio_enabled)) {
    return 0; // Audio disabled - not an error
  }

  // Initialize WAV dumper for received audio if debugging enabled
  if (wav_dump_enabled()) {
    g_wav_playback_received = wav_writer_open("/tmp/audio_playback_received.wav", AUDIO_SAMPLE_RATE, 1);
    if (g_wav_playback_received) {
      log_debug("Audio debugging enabled: dumping received audio to /tmp/audio_playback_received.wav");
    }
  }

  // Initialize PortAudio context using library function
  log_debug("DEBUG: About to call audio_init()...");
  if (audio_init(&g_audio_context) != ASCIICHAT_OK) {
    log_error("Failed to initialize audio system");
    // Clean up WAV writer if it was opened
    if (g_wav_playback_received) {
      wav_writer_close(g_wav_playback_received);
      g_wav_playback_received = NULL;
    }
    return -1;
  }
  log_debug("DEBUG: audio_init() completed successfully");

  // Create unified audio pipeline (handles AEC, AGC, noise suppression, Opus)
  client_audio_pipeline_config_t pipeline_config = client_audio_pipeline_default_config();
  pipeline_config.opus_bitrate = 128000; // 128 kbps AUDIO mode for music quality

  // Enable echo cancellation, AGC, and essential processing for clear audio
  // Noise suppression and VAD can destroy music quality, so keep them disabled
  pipeline_config.flags.echo_cancel = true;     // ENABLE: removes echo
  pipeline_config.flags.jitter_buffer = true;   // ENABLE: needed for AEC sync
  pipeline_config.flags.noise_suppress = false; // DISABLED: destroys music quality
  pipeline_config.flags.agc = true;             // ENABLE: boost quiet microphones (35 dB gain)
  pipeline_config.flags.vad = false;            // DISABLED: destroys music quality
  pipeline_config.flags.compressor = true;      // ENABLE: prevent clipping from AGC boost
  pipeline_config.flags.noise_gate = false;     // DISABLED: would cut quiet music passages
  pipeline_config.flags.highpass = true;        // ENABLE: remove rumble and low-frequency feedback
  pipeline_config.flags.lowpass = false;        // DISABLED: preserve high-frequency content

  // Set jitter buffer margin for smooth playback without excessive delay
  // 100ms is conservative - AEC3 will adapt to actual network delay automatically
  // We don't tune this; let the system adapt to its actual conditions
  pipeline_config.jitter_margin_ms = 100;

  log_debug("DEBUG: About to create audio pipeline...");
  g_audio_pipeline = client_audio_pipeline_create(&pipeline_config);
  log_debug("DEBUG: client_audio_pipeline_create() returned");
  if (!g_audio_pipeline) {
    log_error("Failed to create audio pipeline");
    audio_destroy(&g_audio_context);
    // Clean up WAV writer if it was opened
    if (g_wav_playback_received) {
      wav_writer_close(g_wav_playback_received);
      g_wav_playback_received = NULL;
    }
    return -1;
  }

  log_debug("Audio pipeline created: %d Hz sample rate, %d bps bitrate", pipeline_config.sample_rate,
            pipeline_config.opus_bitrate);

  // Associate pipeline with audio context for echo cancellation
  // The audio output callback will feed playback samples directly to AEC3 from the speaker output,
  // ensuring proper timing synchronization (not from the decode path 50-100ms earlier)
  audio_set_pipeline(&g_audio_context, (void *)g_audio_pipeline);

  // Start full-duplex audio (simultaneous capture + playback for perfect AEC3 timing)
  if (audio_start_duplex(&g_audio_context) != ASCIICHAT_OK) {
    log_error("Failed to start full-duplex audio");
    client_audio_pipeline_destroy(g_audio_pipeline);
    g_audio_pipeline = NULL;
    audio_destroy(&g_audio_context);
    // Clean up WAV writer if it was opened
    if (g_wav_playback_received) {
      wav_writer_close(g_wav_playback_received);
      g_wav_playback_received = NULL;
    }
    return -1;
  }

  // Initialize async audio sender (decouples capture from network I/O)
  audio_sender_init();

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
  log_debug("audio_start_thread called: audio_enabled=%d", GET_OPTION(audio_enabled));

  if (!GET_OPTION(audio_enabled)) {
    log_debug("Audio is disabled, skipping audio capture thread creation");
    return 0; // Audio disabled - not an error
  }

  // Check if thread is already running (not just created flag)
  if (g_audio_capture_thread_created && !atomic_load(&g_audio_capture_thread_exited)) {
    log_warn("Audio capture thread already running");
    return 0;
  }

  // If thread exited, allow recreation
  if (g_audio_capture_thread_created && atomic_load(&g_audio_capture_thread_exited)) {
    log_debug("Previous audio capture thread exited, recreating");
    // Use timeout to prevent indefinite blocking
    int join_result = asciichat_thread_join_timeout(&g_audio_capture_thread, NULL, 5000);
    if (join_result != 0) {
      log_warn("Audio capture thread join timed out after 5s - thread may be deadlocked, "
               "forcing thread handle reset (stuck thread resources will not be cleaned up)");
      // Thread is stuck - we can't safely reuse the handle, but we can reset our tracking
      // This is a resource leak of the stuck thread but continuing is safer than hanging
    }
    g_audio_capture_thread_created = false;
  }

  // Notify server we're starting to send audio BEFORE spawning thread
  // IMPORTANT: Must send STREAM_START before thread starts sending packets to avoid protocol violation
  if (threaded_send_stream_start_packet(STREAM_TYPE_AUDIO) < 0) {
    log_error("Failed to send audio stream start packet");
    return -1; // Don't start thread if we can't notify server
  }

  // Start audio capture thread
  atomic_store(&g_audio_capture_thread_exited, false);
  if (thread_pool_spawn(g_client_worker_pool, audio_capture_thread_func, NULL, 4, "audio_capture") != ASCIICHAT_OK) {
    log_error("Failed to spawn audio capture thread in worker pool");
    LOG_ERRNO_IF_SET("Audio capture thread creation failed");
    return -1;
  }

  g_audio_capture_thread_created = true;

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
  // CRITICAL: Signal audio sender thread to exit FIRST
  // This must happen BEFORE thread_pool_stop_all() is called, otherwise the sender
  // thread will be stuck in cond_wait() and thread_pool_stop_all() will hang forever.
  // The sender thread uses a condition variable to wait for packets - we must wake it up.
  if (g_audio_send_queue_initialized) {
    log_debug("Signaling audio sender thread to exit");
    atomic_store(&g_audio_sender_should_exit, true);
    mutex_lock(&g_audio_send_queue_mutex);
    cond_signal(&g_audio_send_queue_cond);
    mutex_unlock(&g_audio_send_queue_mutex);
  }

  if (!THREAD_IS_CREATED(g_audio_capture_thread_created)) {
    return;
  }

  // Note: We don't call signal_exit() here because that's for global shutdown only
  // The audio capture thread checks server_connection_is_active() to detect connection loss

  // Wait for thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_audio_capture_thread_exited)) {
    platform_sleep_usec(100000); // 100ms
    wait_count++;
  }

  if (!atomic_load(&g_audio_capture_thread_exited)) {
    log_warn("Audio capture thread not responding - will be joined by thread pool");
  }

  // Thread will be joined by thread_pool_stop_all() in protocol_stop_connection()
  g_audio_capture_thread_created = false;

  log_debug("Audio capture thread stopped");
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
  if (!GET_OPTION(audio_enabled)) {
    return;
  }

  // Stop capture thread first (stops producing packets)
  audio_stop_thread();

  // Stop async sender thread (drains queue and exits)
  audio_sender_cleanup();

  // Terminate PortAudio FIRST to properly free device resources before cleanup
  // This must happen before audio_stop_duplex() and audio_destroy()
  audio_terminate_portaudio_final();

  // CRITICAL: Stop audio stream BEFORE destroying pipeline to prevent race condition
  // PortAudio may invoke the callback one more time after we request stop.
  // We need to clear the pipeline pointer first so the callback can't access freed memory.
  if (g_audio_context.initialized) {
    audio_stop_duplex(&g_audio_context);
  }

  // Clear the pipeline pointer from audio context BEFORE destroying pipeline
  // This prevents any lingering PortAudio callbacks from trying to access freed memory
  audio_set_pipeline(&g_audio_context, NULL);

  // CRITICAL: Sleep to allow CoreAudio threads to finish executing callbacks
  // On macOS, CoreAudio's internal threads may continue running after Pa_StopStream() returns.
  // The duplex_callback may still be in-flight on other threads. Even after we set the pipeline
  // pointer to NULL, a CoreAudio thread may have already cached the pointer before the assignment.
  // This sleep ensures all in-flight callbacks have fully completed before we destroy the pipeline.
  // 500ms is sufficient on macOS for CoreAudio's internal thread pool to completely wind down.
  platform_sleep_usec(500000); // 500ms - macOS CoreAudio needs time to shut down all threads

  // Destroy audio pipeline (handles Opus, AEC, etc.)
  if (g_audio_pipeline) {
    client_audio_pipeline_destroy(g_audio_pipeline);
    g_audio_pipeline = NULL;
    log_debug("Audio pipeline destroyed");
  }

  // Close WAV dumpers
  if (g_wav_capture_raw) {
    wav_writer_close(g_wav_capture_raw);
    g_wav_capture_raw = NULL;
    log_debug("Closed audio capture raw dump");
  }
  if (g_wav_capture_processed) {
    wav_writer_close(g_wav_capture_processed);
    g_wav_capture_processed = NULL;
    log_debug("Closed audio capture processed dump");
  }
  if (g_wav_playback_received) {
    wav_writer_close(g_wav_playback_received);
    g_wav_playback_received = NULL;
    log_debug("Closed audio playback received dump");
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

/**
 * @brief Get the global audio context for use by other subsystems
 * @return Pointer to the audio context, or NULL if not initialized
 *
 * @ingroup client_audio
 */
audio_context_t *audio_get_context(void) {
  return &g_audio_context;
}
