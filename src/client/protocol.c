/**
 * @file client/protocol.c
 * @ingroup client_protocol
 * @brief 📡 Client protocol handler: packet reception, parsing, and dispatch with data thread coordination
 *
 * The client protocol handler follows a producer-consumer pattern:
 * - **Producer**: Data reception thread reads packets from socket
 * - **Consumer**: Protocol handlers process packets based on type
 * - **Coordination**: Thread-safe flags manage connection state
 * - **Error Recovery**: Connection loss detection and recovery signaling
 *
 * ## Packet Processing Pipeline
 *
 * 1. **Reception**: Raw packet data read from TCP socket
 * 2. **Validation**: Header validation and CRC verification
 * 3. **Deserialization**: Network byte order conversion
 * 4. **Dispatch**: Route to type-specific handler functions
 * 5. **Processing**: Handler executes packet-specific logic
 * 6. **Cleanup**: Buffer management and resource deallocation
 *
 * ## Thread Management
 *
 * The protocol module manages a dedicated data reception thread:
 * - **Lifecycle**: Thread creation, monitoring, and graceful termination
 * - **Exit Coordination**: Atomic flags coordinate thread shutdown
 * - **Connection Monitoring**: Detect socket closure and network errors
 * - **Resource Management**: Clean buffer pool usage and leak prevention
 *
 * ## Packet Type Handlers
 *
 * Each packet type has a dedicated handler function:
 * - **ASCII_FRAME**: Display ASCII art frames with decompression
 * - **AUDIO**: Process and queue audio samples for playback
 * - **PING/PONG**: Keepalive protocol implementation
 * - **CLEAR_CONSOLE**: Terminal control commands from server
 * - **SERVER_STATE**: Multi-client state synchronization
 * - **Unknown Types**: Graceful handling of protocol extensions
 *
 * ## Compression Support
 *
 * Frame packets support optional zstd compression:
 * - **Detection**: Frame flags indicate compression status
 * - **Decompression**: zstd inflation with size validation
 * - **Fallback**: Graceful handling when compression disabled
 * - **Integrity**: CRC32 verification of decompressed data
 *
 * ## Integration Points
 *
 * - **main.c**: Thread lifecycle management and coordination
 * - **server.c**: Socket operations and connection state
 * - **display.c**: Frame rendering and terminal control
 * - **audio.c**: Audio sample processing and playback
 * - **keepalive.c**: Ping/pong response coordination
 *
 * ## Error Handling
 *
 * Protocol errors are classified and handled appropriately:
 * - **Network Errors**: Socket failures trigger connection loss
 * - **Protocol Errors**: Malformed packets logged but connection continues
 * - **Resource Errors**: Memory allocation failures with graceful degradation
 * - **Compression Errors**: Invalid compressed data with frame skipping
 *
 * ## Buffer Management
 *
 * Uses shared buffer pool for efficient memory management:
 * - **Allocation**: Packets allocated from global buffer pool
 * - **Ownership**: Clear ownership transfer between modules
 * - **Deallocation**: Automatic cleanup with buffer pool integration
 * - **Leak Prevention**: Comprehensive cleanup on all error paths
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0
 */

#include "protocol.h"
#include "main.h"
#include "server.h"
#include "display.h"
#include "capture.h"
#include "audio.h"
#include "audio_analysis.h"
#include "keepalive.h"

#include "network/packet.h"
#include "network/av.h"
#include "buffer_pool.h"
#include "common.h"
#include "options.h"
#include "crc32.h"
#include "crypto/crypto.h"

// Forward declaration for client crypto functions
bool crypto_client_is_ready(void);
const crypto_context_t *crypto_client_get_context(void);
int crypto_client_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext,
                                 size_t plaintext_size, size_t *plaintext_len);

#include "crypto.h"
#include "util/time.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include "platform/windows_compat.h"
#endif

#include "compression.h"
#include "opus_codec.h"
#include "network/av.h"

#include <errno.h>

/* ============================================================================
 * Thread State Management
 * ============================================================================ */

/**
 * @brief Data reception thread handle
 *
 * Thread handle for the background thread that receives and processes packets
 * from the server. Created during connection establishment, joined during shutdown.
 *
 * @ingroup client_protocol
 */
static asciithread_t g_data_thread;

/**
 * @brief Flag indicating if data thread was successfully created
 *
 * Used during shutdown to determine whether the thread handle is valid and
 * should be joined. Prevents attempting to join a thread that was never created.
 *
 * @ingroup client_protocol
 */
static bool g_data_thread_created = false;

/**
 * @brief Atomic flag indicating data thread has exited
 *
 * Set by the data reception thread when it exits. Used by other threads to
 * detect thread termination without blocking on thread join operations.
 *
 * @ingroup client_protocol
 */
static atomic_bool g_data_thread_exited = false;

/* ============================================================================
 * Multi-User Client State
 * ============================================================================ */

/**
 * @brief Remote client information structure for multi-user client tracking
 *
 * Tracks information about other clients connected to the server. Used by
 * the client to maintain awareness of other participants in the chat session.
 *
 * CORE FIELDS:
 * ============
 * - client_id: Unique identifier for this remote client
 * - display_name: User-friendly display name for the client
 * - is_active: Whether this client is currently active (sending video/audio)
 * - last_seen: Timestamp when this client was last seen (for timeout detection)
 *
 * USAGE:
 * ======
 * The client maintains an array of remote_client_info_t structures to track
 * all other clients. This information is used for:
 * - Multi-user display coordination
 * - Client list display
 * - Connection state awareness
 * - Timeout detection
 *
 * @note The client_id matches the server's assigned client identifier.
 * @note display_name is received from server in CLIENT_JOIN packets.
 * @note is_active indicates whether client is sending media (video/audio).
 * @note last_seen is updated when receiving packets from this client.
 *
 * @ingroup client_protocol
 */
typedef struct {
  /** @brief Unique client identifier assigned by server */
  uint32_t client_id;
  /** @brief User-friendly display name (null-terminated) */
  char display_name[MAX_DISPLAY_NAME_LEN];
  /** @brief Whether client is currently active (sending video/audio) */
  bool is_active;
  /** @brief Timestamp when client was last seen (for timeout detection) */
  time_t last_seen;
} remote_client_info_t;

/**
 * @brief Last known active client count from server
 *
 * Tracks the previous active client count to detect changes in the number
 * of active video sources. Used to trigger console clear operations when
 * the active count changes significantly.
 *
 * @ingroup client_protocol
 */
static uint32_t g_last_active_count = 0;

/**
 * @brief Flag indicating if server state has been initialized
 *
 * Set to true after receiving the first SERVER_STATE packet from the server.
 * Used to distinguish between initial state and state updates.
 *
 * @ingroup client_protocol
 */
static bool g_server_state_initialized = false;

/**
 * @brief Flag indicating console should be cleared before next frame
 *
 * Set to true when the active client count changes significantly or when
 * console state needs to be reset. Display thread clears console on next
 * frame render when this flag is set.
 *
 * @ingroup client_protocol
 */
static bool g_should_clear_before_next_frame = false;

/* ============================================================================
 * Packet Handler Functions
 * ============================================================================ */

/**
 * @brief Handle incoming ASCII frame packet from server
 *
 * Processes unified ASCII frame packets that contain both header information
 * and frame data. Supports optional zstd compression with integrity verification.
 * Manages frame display timing for snapshot mode and console clearing logic.
 *
 * Frame Processing Pipeline:
 * 1. Header extraction and network byte order conversion
 * 2. Compression detection and decompression if needed
 * 3. CRC32 integrity verification of frame data
 * 4. Frame dimension change detection and logging
 * 5. Snapshot mode timing and exit logic
 * 6. Console clearing coordination with multi-client state
 * 7. Frame rendering through display subsystem
 *
 * @param data Raw packet data starting with ascii_frame_packet_t header
 * @param len Total packet length including header and frame data
 *
 * @ingroup client_protocol
 */
static void handle_ascii_frame_packet(const void *data, size_t len) {
  if (should_exit()) {
    return;
  }

  if (!data || len < sizeof(ascii_frame_packet_t)) {
    log_warn("Invalid ASCII frame packet size: %zu", len);
    return;
  }

  // FPS tracking for received ASCII frames
  static uint64_t frame_count = 0;
  static struct timespec last_fps_report_time = {0};
  static struct timespec last_frame_time = {0};
  static int expected_fps = 0; // Will be set based on client's requested FPS

  struct timespec current_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &current_time);

  // Initialize expected FPS from client's requested FPS (only once)
  if (expected_fps == 0) {
    extern int g_max_fps; // From common.c
    if (g_max_fps > 0) {
      expected_fps = (g_max_fps > 144) ? 144 : g_max_fps;
    } else {
// Use platform default (60 for Unix, 30 for Windows)
#ifdef DEFAULT_MAX_FPS
      expected_fps = DEFAULT_MAX_FPS;
#else
      expected_fps = 60; // Fallback
#endif
    }
    log_debug("CLIENT FPS TRACKING: Expecting %d fps (client's requested rate)", expected_fps);
  }

  // Initialize on first frame
  if (last_fps_report_time.tv_sec == 0) {
    last_fps_report_time = current_time;
    last_frame_time = current_time;
  }

  frame_count++;

  // Calculate time since last frame
  uint64_t frame_interval_us = ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
                               ((uint64_t)last_frame_time.tv_sec * 1000000 + (uint64_t)last_frame_time.tv_nsec / 1000);
  last_frame_time = current_time;

  // Expected frame interval in microseconds (for 60fps = 16666us)
  uint64_t expected_interval_us = 1000000ULL / (uint64_t)expected_fps;
  uint64_t lag_threshold_us = expected_interval_us + (expected_interval_us / 2); // 50% over expected

  // Log error if frame arrived too late
  if (frame_count > 1 && frame_interval_us > lag_threshold_us) {
    log_error("CLIENT FPS LAG: Frame received %.2lfs late (expected %.2lfs, got %.2lfs, actual fps: %.2lf)",
              (double)(frame_interval_us - expected_interval_us) / 1000.0, (double)expected_interval_us / 1000.0,
              (double)frame_interval_us / 1000.0, 1000000.0 / (double)frame_interval_us);
  }

  // Report FPS every 5 seconds
  uint64_t elapsed_us =
      ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
      ((uint64_t)last_fps_report_time.tv_sec * 1000000 + (uint64_t)last_fps_report_time.tv_nsec / 1000);

  if (elapsed_us >= 5000000) { // 5 seconds
    double elapsed_seconds = (double)elapsed_us / 1000000.0;
    double actual_fps = (double)frame_count / elapsed_seconds;

    char duration_str[32];
    format_duration_s(elapsed_seconds, duration_str, sizeof(duration_str));
    log_debug("CLIENT FPS: %.1f fps (%llu frames in %s)", actual_fps, frame_count, duration_str);

    // Reset counters for next interval
    frame_count = 0;
    last_fps_report_time = current_time;
  }

  // Extract header from the packet
  ascii_frame_packet_t header;
  memcpy(&header, data, sizeof(ascii_frame_packet_t));

  // Convert from network byte order
  header.width = ntohl(header.width);
  header.height = ntohl(header.height);
  header.original_size = ntohl(header.original_size);
  header.compressed_size = ntohl(header.compressed_size);
  header.checksum = ntohl(header.checksum);
  header.flags = ntohl(header.flags);

  // Get the frame data (starts after the header)
  const char *frame_data_ptr = (const char *)data + sizeof(ascii_frame_packet_t);
  size_t frame_data_len = len - sizeof(ascii_frame_packet_t);

  char *frame_data = NULL;

  // Handle compression if needed
  if (header.flags & FRAME_FLAG_IS_COMPRESSED && header.compressed_size > 0) {
    // Compressed frame - decompress it
    if (frame_data_len != header.compressed_size) {
      SET_ERRNO(ERROR_NETWORK_SIZE, "Compressed frame size mismatch: expected %u, got %zu", header.compressed_size,
                frame_data_len);
      return;
    }

    frame_data = SAFE_MALLOC(header.original_size + 1, char *);

    // Decompress using compression API
    int result = decompress_data(frame_data_ptr, frame_data_len, frame_data, header.original_size);

    if (result != 0) {
      SET_ERRNO(ERROR_COMPRESSION, "Decompression failed for expected size %u", header.original_size);
      SAFE_FREE(frame_data);
      return;
    }

    frame_data[header.original_size] = '\0';
#ifdef COMPRESSION_DEBUG
    log_debug("Decompressed frame: %zu -> %u bytes", frame_data_len, header.original_size);
#endif
  } else {
    // Uncompressed frame
    if (frame_data_len != header.original_size) {
      log_error("Uncompressed frame size mismatch: expected %u, got %zu", header.original_size, frame_data_len);
      return;
    }

    // Ensure we don't have buffer overflow - use the actual header size for allocation
    size_t alloc_size = header.original_size + 1;
    frame_data = SAFE_MALLOC(alloc_size, char *);

    // Only copy the actual amount of data we received
    size_t copy_size = (frame_data_len > header.original_size) ? header.original_size : frame_data_len;
    memcpy(frame_data, frame_data_ptr, copy_size);
    frame_data[header.original_size] = '\0';
  }

  // Verify checksum
  uint32_t actual_crc = asciichat_crc32(frame_data, header.original_size);
  if (actual_crc != header.checksum) {
    log_error("Frame checksum mismatch: got 0x%x, expected 0x%x (size=%u, first_bytes=%02x%02x%02x%02x)", actual_crc,
              header.checksum, header.original_size, (unsigned char)frame_data[0], (unsigned char)frame_data[1],
              (unsigned char)frame_data[2], (unsigned char)frame_data[3]);

    // DEBUG: Try software CRC32 to compare
    uint32_t sw_crc = asciichat_crc32_sw(frame_data, header.original_size);
    log_error("Software CRC32: 0x%x (matches: %s)", sw_crc, (sw_crc == header.checksum) ? "YES" : "NO");

    SAFE_FREE(frame_data);
    return;
  }

  // Track frame dimension changes
  static uint32_t last_width = 0;
  static uint32_t last_height = 0;

  if (header.width > 0 && header.height > 0) {
    if (header.width != last_width || header.height != last_height) {
      last_width = header.width;
      last_height = header.height;
    }
  }

  // Handle snapshot mode timing
  bool take_snapshot = false;
  if (opt_snapshot_mode) {
    static time_t first_frame_time = 0;
    if (first_frame_time == 0) {
      first_frame_time = time(NULL);
      // If delay is 0, take snapshot immediately on first frame
      if (opt_snapshot_delay == 0) {
        log_info("Snapshot captured immediately (delay=0)!");
        take_snapshot = true;
        signal_exit();
      } else {
        log_info("Snapshot mode: first frame received, waiting %.2f seconds for webcam warmup...", opt_snapshot_delay);
      }
    } else {
      time_t snapshot_time = time(NULL);
      double elapsed = difftime(snapshot_time, first_frame_time);
      if (elapsed >= (double)opt_snapshot_delay) {
        char duration_str[32];
        format_duration_s(elapsed, duration_str, sizeof(duration_str));
        log_info("Snapshot captured after %s!", duration_str);
        take_snapshot = true;
        signal_exit();
      }
    }
  }

  // Check if we need to clear console before rendering this frame
  // IMPORTANT: We track if this is the first frame to ensure proper initialization
  static bool first_frame_rendered = false;

  if (!first_frame_rendered) {
    // Always clear display and disable logging before rendering the first frame
    // This ensures clean ASCII display regardless of packet arrival order
    log_info("First frame - clearing display and disabling terminal logging");
    log_set_terminal_output(false);
    display_full_reset();
    first_frame_rendered = true;
    g_server_state_initialized = true;        // Mark as initialized
    g_should_clear_before_next_frame = false; // Clear any pending clear request
    log_debug("CLIENT_DISPLAY: Display cleared, ready for ASCII frames");
  } else if (g_should_clear_before_next_frame) {
    // Subsequent clear request from server (e.g., after client list changes)
    log_debug("CLIENT_DISPLAY: Clearing display for layout change");
    log_set_terminal_output(false);
    display_full_reset();
    g_should_clear_before_next_frame = false;
  }

  // Safety check before rendering
  if (!frame_data || header.original_size == 0) {
    log_error("Invalid frame data for rendering: frame_data=%p, size=%u", frame_data, header.original_size);
    if (frame_data) {
      SAFE_FREE(frame_data);
    }
    return;
  }

  // Client-side FPS limiting for rendering (display)
  // Server may send at 144fps for high-refresh displays, but this client renders at its requested FPS
  static struct timespec last_render_time = {0, 0};

  // Don't limit frame rate in snapshot mode - always render the final frame
  if (!take_snapshot) {
    // Get the client's desired FPS (what we told the server we can display)
    int client_display_fps = MAX_FPS; // This respects the --fps command line flag
    // Use microseconds for precision - avoid integer division loss
    uint64_t render_interval_us = 1000000ULL / (uint64_t)client_display_fps;

    struct timespec render_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &render_time);

    // Calculate elapsed time since last render in microseconds (high precision)
    uint64_t render_elapsed_us = 0;
    if (last_render_time.tv_sec != 0 || last_render_time.tv_nsec != 0) {
      int64_t sec_diff = (int64_t)render_time.tv_sec - (int64_t)last_render_time.tv_sec;
      int64_t nsec_diff = (int64_t)render_time.tv_nsec - (int64_t)last_render_time.tv_nsec;

      // Handle nanosecond underflow by borrowing from seconds
      if (nsec_diff < 0) {
        sec_diff -= 1;
        nsec_diff += 1000000000LL; // Add 1 second worth of nanoseconds
      }

      // Convert to microseconds (now both values are properly normalized)
      // sec_diff should be >= 0 for forward time progression
      if (sec_diff >= 0) {
        render_elapsed_us = (uint64_t)sec_diff * 1000000ULL + (uint64_t)(nsec_diff / 1000);
      }
      // If sec_diff is negative, time went backwards - treat as 0 elapsed
    }

    // Skip rendering if not enough time has passed (frame rate limiting)
    if (last_render_time.tv_sec != 0 || last_render_time.tv_nsec != 0) {
      if (render_elapsed_us > 0 && render_elapsed_us < render_interval_us) {
        // Drop this frame to maintain display FPS limit
        SAFE_FREE(frame_data);
        return;
      }
    }

    // Update last render time
    last_render_time = current_time;
  }

  display_render_frame(frame_data, take_snapshot);

  SAFE_FREE(frame_data);
}

/**
 * @brief Handle incoming audio packet from server
 *
 * Processes audio sample packets and queues them for playback. Extracts
 * float samples from packet payload and passes them to the audio subsystem
 * for jitter-buffered playback.
 *
 * @param data Packet payload containing float audio samples
 * @param len Total packet length in bytes
 *
 * @ingroup client_protocol
 */
static void handle_audio_packet(const void *data, size_t len) {
  if (!opt_audio_enabled || !data || len == 0) {
    return;
  }

  int num_samples = (int)(len / sizeof(float));
  if (num_samples > AUDIO_SAMPLES_PER_PACKET) {
    log_warn("Audio packet too large: %d samples", num_samples);
    return;
  }

  // Copy data to properly aligned buffer to avoid UndefinedBehaviorSanitizer errors
  float samples[AUDIO_SAMPLES_PER_PACKET];
  SAFE_MEMCPY(samples, sizeof(samples), data, len);

  // Process audio through audio subsystem
  audio_process_received_samples(samples, num_samples);

#ifdef DEBUG_AUDIO
  log_debug("Processed %d audio samples", num_samples);
#endif
}

/**
 * @brief Handle incoming audio batch packet from server
 *
 * Processes batched audio packets more efficiently than individual packets.
 * Parses the audio batch header, converts quantized samples to float, and
 * processes them through the audio subsystem.
 *
 * @param data Packet payload containing audio batch header + quantized samples
 * @param len Total packet length in bytes
 *
 * @ingroup client_protocol
 */
static void handle_audio_batch_packet(const void *data, size_t len) {
  if (!opt_audio_enabled || !data) {
    return;
  }

  if (len < sizeof(audio_batch_packet_t)) {
    log_warn("Audio batch packet too small: %zu bytes", len);
    return;
  }

  // Parse batch header
  const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)data;
  uint32_t batch_count = ntohl(batch_header->batch_count);
  uint32_t total_samples = ntohl(batch_header->total_samples);
  uint32_t sample_rate = ntohl(batch_header->sample_rate);
  uint32_t channels = ntohl(batch_header->channels);

  (void)batch_count;
  (void)sample_rate;
  (void)channels;

  if (batch_count == 0 || total_samples == 0) {
    log_warn("Empty audio batch: batch_count=%u, total_samples=%u", batch_count, total_samples);
    return;
  }

  // Validate packet size
  size_t expected_size = sizeof(audio_batch_packet_t) + (total_samples * sizeof(uint32_t));
  if (len != expected_size) {
    log_warn("Audio batch size mismatch: got %zu expected %zu", len, expected_size);
    return;
  }

  if (total_samples > AUDIO_BATCH_SAMPLES * 2) {
    log_warn("Audio batch too large: %u samples", total_samples);
    return;
  }

  // Extract quantized samples (uint32_t network byte order)
  const uint8_t *samples_ptr = (const uint8_t *)data + sizeof(audio_batch_packet_t);

  // Convert quantized samples to float
  float *samples = SAFE_MALLOC(total_samples * sizeof(float), float *);
  if (!samples) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for audio batch conversion");
    return;
  }

  // Dequantize: int32_t -> float (scale from [-2147483647, 2147483647] to [-1.0, 1.0])
  for (uint32_t i = 0; i < total_samples; i++) {
    uint32_t network_sample;
    SAFE_MEMCPY(&network_sample, sizeof(network_sample), samples_ptr + (i * sizeof(uint32_t)), sizeof(uint32_t));

    // Convert from network byte order and treat as signed int32_t
    int32_t scaled = (int32_t)ntohl(network_sample);

    // Scale signed int32_t to float range [-1.0, 1.0]
    samples[i] = (float)scaled / 2147483647.0f;
  }

  // Track received packet for analysis
  if (opt_audio_analysis_enabled) {
    audio_analysis_track_received_packet(len);
  }

  // Process through audio subsystem
  audio_process_received_samples(samples, (int)total_samples);

  // Clean up
  SAFE_FREE(samples);

  log_debug_every(5000000, "Processed audio batch: %u samples from server", total_samples);
}

/**
 * @brief Handle incoming Opus-encoded audio packet from server
 *
 * Decodes single Opus-encoded audio frame and processes for playback.
 * Opus provides ~98% bandwidth reduction compared to raw PCM.
 *
 * @param data Packet payload containing Opus-encoded audio
 * @param len Total packet length in bytes
 *
 * @ingroup client_protocol
 */
static void handle_audio_opus_packet(const void *data, size_t len) {
  if (!opt_audio_enabled || !data || len == 0) {
    return;
  }

  // Get Opus decoder
  opus_codec_t *decoder = audio_get_opus_decoder();
  if (!decoder) {
    log_warn("Opus decoder not initialized, cannot decode audio");
    return;
  }

  // Parse Opus packet header using av_receive_audio_opus for consistency
  // Packet format: [sample_rate:4][frame_duration:4][reserved:8][opus_data:...]
  const uint8_t *opus_data = NULL;
  size_t opus_size = 0;
  int sample_rate = 0;
  int frame_duration = 0;

  int result = av_receive_audio_opus(data, len, &opus_data, &opus_size, &sample_rate, &frame_duration);
  if (result < 0) {
    log_warn("Failed to parse Opus audio packet header");
    return;
  }

  if (opus_size == 0 || !opus_data) {
    log_warn("Empty Opus audio data after header parse");
    return;
  }

  // Opus max frame size is 2880 samples (120ms @ 48kHz)
  float samples[2880];
  int decoded_samples = opus_codec_decode(decoder, opus_data, opus_size, samples, 2880);

  if (decoded_samples <= 0) {
    log_warn("Failed to decode Opus audio packet, decoded=%d", decoded_samples);
    return;
  }

  // Track received packet for analysis
  if (opt_audio_analysis_enabled) {
    audio_analysis_track_received_packet(len);
  }

  // Process decoded audio through audio subsystem
  audio_process_received_samples(samples, decoded_samples);

  log_debug_every(5000000, "Processed Opus audio: %d decoded samples (sample_rate=%d, duration=%dms)",
                  decoded_samples, sample_rate, frame_duration);
}

/**
 * @brief Handle incoming Opus batch packet from server
 *
 * Processes batched Opus-encoded audio packets for efficiency.
 * Each batch contains multiple Opus frames.
 *
 * Batch packet format (from av_send_audio_opus_batch):
 * - Offset 0: sample_rate (uint32_t, network byte order)
 * - Offset 4: frame_duration (uint32_t, network byte order)
 * - Offset 8: frame_count (uint32_t, network byte order)
 * - Offset 12: reserved (4 bytes)
 * - Offset 16: frame_sizes array (uint16_t * frame_count, network byte order)
 * - After frame_sizes: Opus encoded data
 *
 * @param data Packet payload containing Opus batch header + Opus frames
 * @param len Total packet length in bytes
 *
 * @ingroup client_protocol
 */
static void handle_audio_opus_batch_packet(const void *data, size_t len) {
  if (!opt_audio_enabled || !data || len == 0) {
    return;
  }

  // Get Opus decoder
  opus_codec_t *decoder = audio_get_opus_decoder();
  if (!decoder) {
    log_warn("Opus decoder not initialized, cannot decode audio batch");
    return;
  }

  // Parse batch header using av_receive_audio_opus_batch() for consistency
  const uint8_t *opus_data = NULL;
  size_t opus_size = 0;
  const uint16_t *frame_sizes = NULL;
  int sample_rate = 0;
  int frame_duration = 0;
  int frame_count = 0;

  int result = av_receive_audio_opus_batch(data, len, &opus_data, &opus_size, &frame_sizes, &sample_rate,
                                           &frame_duration, &frame_count);

  if (result < 0) {
    log_warn("Failed to parse Opus batch packet");
    return;
  }

  if (frame_count <= 0 || frame_count > 256 || opus_size == 0) {
    log_warn("Invalid Opus batch: frame_count=%d, opus_size=%zu", frame_count, opus_size);
    return;
  }

  // Calculate samples per frame
  int samples_per_frame = (sample_rate * frame_duration) / 1000;
  if (samples_per_frame <= 0 || samples_per_frame > 2880) {
    log_warn("Invalid Opus frame parameters: samples_per_frame=%d", samples_per_frame);
    return;
  }

  // Allocate buffer for all decoded samples
  size_t max_decoded_samples = (size_t)samples_per_frame * (size_t)frame_count;
  float *all_samples = SAFE_MALLOC(max_decoded_samples * sizeof(float), float *);
  if (!all_samples) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for Opus batch decoding");
    return;
  }

  // Decode each Opus frame using frame_sizes array
  int total_decoded_samples = 0;
  size_t opus_offset = 0;

  for (int i = 0; i < frame_count; i++) {
    // Get frame size (convert from network byte order)
    size_t frame_size = (size_t)ntohs(frame_sizes[i]);

    if (opus_offset + frame_size > opus_size) {
      log_warn("Opus batch truncated at frame %d (offset=%zu, frame_size=%zu, total=%zu)", i, opus_offset, frame_size,
               opus_size);
      break;
    }

    // Decode frame - use remaining buffer space (not 2880-total which would fail after 3 frames)
    float *frame_buffer = all_samples + total_decoded_samples;
    int remaining_space = (int)(max_decoded_samples - (size_t)total_decoded_samples);
    int decoded = opus_codec_decode(decoder, opus_data + opus_offset, frame_size, frame_buffer, remaining_space);

    if (decoded <= 0) {
      log_warn("Failed to decode Opus frame %d in batch, decoded=%d", i, decoded);
      break;
    }

    total_decoded_samples += decoded;
    opus_offset += frame_size;
  }

  if (total_decoded_samples > 0) {
    // Track received packet for analysis
    if (opt_audio_analysis_enabled) {
      audio_analysis_track_received_packet(len);
    }

    // Process decoded audio through audio subsystem
    audio_process_received_samples(all_samples, total_decoded_samples);

    log_debug_every(5000000, "Processed Opus batch: %d decoded samples from %d frames", total_decoded_samples,
                    frame_count);
  }

  // Clean up
  SAFE_FREE(all_samples);
}

static bool handle_error_message_packet(const void *data, size_t len) {
  asciichat_error_t remote_error = ASCIICHAT_OK;
  char message[MAX_ERROR_MESSAGE_LENGTH + 1] = {0};

  asciichat_error_t parse_result = packet_parse_error_message(data, len, &remote_error, message, sizeof(message), NULL);
  if (parse_result != ASCIICHAT_OK) {
    log_error("Failed to parse error packet from server: %s", asciichat_error_string(parse_result));
    return false;
  }

  log_error("Server reported error %d (%s): %s", remote_error, asciichat_error_string(remote_error), message);
  log_warn("Server signaled protocol error; closing connection");
  server_connection_shutdown();
  server_connection_lost();
  return true;
}

static void handle_remote_log_packet(const void *data, size_t len) {
  log_level_t remote_level = LOG_INFO;
  remote_log_direction_t direction = REMOTE_LOG_DIRECTION_UNKNOWN;
  uint16_t flags = 0;
  char message[MAX_REMOTE_LOG_MESSAGE_LENGTH + 1] = {0};

  asciichat_error_t parse_result =
      packet_parse_remote_log(data, len, &remote_level, &direction, &flags, message, sizeof(message), NULL);
  if (parse_result != ASCIICHAT_OK) {
    log_error("Failed to parse remote log packet from server: %s", asciichat_error_string(parse_result));
    return;
  }

  if (direction != REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT) {
    log_error("Remote log packet direction mismatch (direction=%u)", direction);
    return;
  }

  bool truncated = (flags & REMOTE_LOG_FLAG_TRUNCATED) != 0;

  if (truncated) {
    log_msg(remote_level, __FILE__, __LINE__, __func__, "[REMOTE SERVER] %s [message truncated]", message);
  } else {
    log_msg(remote_level, __FILE__, __LINE__, __func__, "[REMOTE SERVER] %s", message);
  }
}

/**
 * @brief Handle incoming server state packet
 *
 * Processes server state updates including active client count and console
 * clear coordination. Manages multi-client state tracking and terminal
 * synchronization.
 *
 * @param data Packet payload (must be server_state_packet_t)
 * @param len Total packet length in bytes
 *
 * @ingroup client_protocol
 */
static void handle_server_state_packet(const void *data, size_t len) {
  if (!data || len != sizeof(server_state_packet_t)) {
    log_error("Invalid server state packet size: %zu", len);
    return;
  }

  const server_state_packet_t *state = (const server_state_packet_t *)data;

  // Convert from network byte order
  uint32_t active_count = ntohl(state->active_client_count);

  // Check if connected count changed - if so, set flag to clear console before next frame
  if (g_server_state_initialized) {
    if (g_last_active_count != active_count) {
      log_info("Active client count changed from %u to %u - will clear console before next frame", g_last_active_count,
               active_count);
      g_should_clear_before_next_frame = true;
    }
  } else {
    // First state packet received
    g_server_state_initialized = true;
    // Clear terminal before the very first frame
    g_should_clear_before_next_frame = true;
  }

  g_last_active_count = active_count;
}

/* ============================================================================
 * Data Reception Thread
 * ============================================================================ */

/**
 * @brief Data reception thread function
 *
 * Implements the core packet reception loop that continuously reads packets
 * from the server connection and dispatches them to appropriate handlers.
 * Manages connection health monitoring and graceful thread termination.
 *
 * Reception Loop:
 * 1. Check global shutdown flags and connection status
 * 2. Call receive_packet() to read next packet from socket
 * 3. Handle connection errors and socket closure events
 * 4. Dispatch packet to type-specific handler based on packet type
 * 5. Manage buffer cleanup and memory leak prevention
 * 6. Continue until shutdown or connection failure
 *
 * Error Handling:
 * - Network errors trigger connection loss signaling
 * - Protocol errors are logged but processing continues
 * - Resource errors handled with graceful degradation
 * - Buffer cleanup performed on all exit paths
 *
 * @param arg Thread argument (unused)
 * @return NULL on thread exit
 *
 * @ingroup client_protocol
 */
static void *data_reception_thread_func(void *arg) {
  (void)arg;

#ifdef DEBUG_THREADS
  log_debug("Data reception thread started");
#endif

  while (!should_exit()) {
    socket_t sockfd = server_connection_get_socket();

    if (sockfd == INVALID_SOCKET_VALUE || !server_connection_is_active()) {
      log_debug("Waiting for socket connection");
      platform_sleep_usec(10 * 1000);
      continue;
    }

    // Use unified secure packet reception with auto-decryption
    // FIX: Use per-client crypto ready state instead of global opt_no_encrypt
    // Encryption is enforced only AFTER this client completes the handshake
    bool crypto_ready = crypto_client_is_ready();
    const crypto_context_t *crypto_ctx = crypto_ready ? crypto_client_get_context() : NULL;
    packet_envelope_t envelope;
    packet_recv_result_t result = receive_packet_secure(sockfd, (void *)crypto_ctx, crypto_ready, &envelope);

    // Handle different result codes
    if (result == PACKET_RECV_EOF) {
      log_debug("Server closed connection");
      server_connection_lost();
      break;
    }

    if (result == PACKET_RECV_ERROR) {
      log_error("Failed to receive packet, errno=%d (%s)", errno, SAFE_STRERROR(errno));
      server_connection_lost();
      break;
    }

    if (result == PACKET_RECV_SECURITY_VIOLATION) {
      log_error("SECURITY: Server violated encryption policy");
      log_error("SECURITY: This is a critical security violation - exiting immediately");
      exit(1); // Exit immediately on security violation
    }

    // Extract packet details from envelope
    packet_type_t type = envelope.type;
    void *data = envelope.data;
    size_t len = envelope.len;

    bool should_disconnect = false;

    switch (type) {
    case PACKET_TYPE_ASCII_FRAME:
      handle_ascii_frame_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO:
      handle_audio_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO_BATCH:
      handle_audio_batch_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO_OPUS:
      handle_audio_opus_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO_OPUS_BATCH:
      handle_audio_opus_batch_packet(data, len);
      break;

    case PACKET_TYPE_PING:
      // Respond with PONG
      if (threaded_send_pong_packet() < 0) {
        log_error("Failed to send PONG response");
      }
      break;

    case PACKET_TYPE_PONG:
      // Server acknowledged our PING - no action needed
      break;

    case PACKET_TYPE_CLEAR_CONSOLE:
      // Server requested console clear
      display_full_reset();
      log_info("Console cleared by server");
      break;

    case PACKET_TYPE_SERVER_STATE:
      handle_server_state_packet(data, len);
      break;

    case PACKET_TYPE_ERROR_MESSAGE:
      should_disconnect = handle_error_message_packet(data, len);
      break;

    case PACKET_TYPE_REMOTE_LOG:
      handle_remote_log_packet(data, len);
      break;

    // Session rekeying packets
    case PACKET_TYPE_CRYPTO_REKEY_REQUEST: {
      log_debug("Received REKEY_REQUEST from server");

      // Process the server's rekey request
      asciichat_error_t crypto_result = crypto_client_process_rekey_request(data, len);
      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to process REKEY_REQUEST: %d", crypto_result);
        break;
      }

      // Send REKEY_RESPONSE
      crypto_result = crypto_client_send_rekey_response();
      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to send REKEY_RESPONSE: %d", crypto_result);
      } else {
        log_debug("Sent REKEY_RESPONSE to server");
      }
      break;
    }

    case PACKET_TYPE_CRYPTO_REKEY_RESPONSE: {
      log_debug("Received REKEY_RESPONSE from server");

      // Process server's response
      asciichat_error_t crypto_result = crypto_client_process_rekey_response(data, len);
      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to process REKEY_RESPONSE: %d", crypto_result);
        break;
      }

      // Send REKEY_COMPLETE
      crypto_result = crypto_client_send_rekey_complete();
      if (crypto_result != ASCIICHAT_OK) {
        log_error("Failed to send REKEY_COMPLETE: %d", crypto_result);
      } else {
        log_debug("Session rekeying completed successfully");
      }
      break;
    }

    default:
      log_warn("Unknown packet type: %d", type);
      break;
    }

    // Clean up packet buffer using the allocated_buffer pointer, not the data pointer
    // The data pointer is offset into the buffer, but we need to free the actual allocated buffer
    if (envelope.allocated_buffer && envelope.allocated_size > 0) {
      buffer_pool_free(envelope.allocated_buffer, envelope.allocated_size);
    }

    if (should_disconnect) {
      log_info("Terminating data reception thread due to server error packet");
      break;
    }
  }

#ifdef DEBUG_THREADS
  log_debug("Data reception thread stopped");
#endif

  atomic_store(&g_data_thread_exited, true);
  return NULL;
}

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * Start protocol connection handling
 *
 * Initializes protocol state and starts the data reception thread.
 * Must be called after successful server connection establishment.
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_protocol
 */
int protocol_start_connection() {
  // Reset protocol state for new connection
  g_server_state_initialized = false;
  g_last_active_count = 0;
  g_should_clear_before_next_frame = false;

  // Reset display state for new connection
  display_reset_for_new_connection();

  // Start data reception thread
  atomic_store(&g_data_thread_exited, false);
  if (ascii_thread_create(&g_data_thread, data_reception_thread_func, NULL) != 0) {
    log_error("Failed to create data reception thread");
    return -1;
  }

  // Start webcam capture thread
  log_info("Starting webcam capture thread...");
  if (capture_start_thread() != 0) {
    log_error("Failed to start webcam capture thread");
    return -1;
  }
  log_info("Webcam capture thread started successfully");

  // Start audio capture thread if audio is enabled
  log_info("Starting audio capture thread...");
  if (audio_start_thread() != 0) {
    log_error("Failed to start audio capture thread");
    return -1;
  }
  log_info("Audio capture thread started successfully (or skipped if audio disabled)");

  // Start keepalive/ping thread to prevent server timeout
  log_info("Starting keepalive/ping thread...");
  if (keepalive_start_thread() != 0) {
    log_error("Failed to start keepalive/ping thread");
    return -1;
  }
  log_info("Keepalive/ping thread started successfully");

  g_data_thread_created = true;
  return 0;
}

/**
 * Stop protocol connection handling
 *
 * Gracefully shuts down the data reception thread and cleans up
 * protocol state. Safe to call multiple times.
 *
 * @ingroup client_protocol
 */
void protocol_stop_connection() {
  if (!g_data_thread_created) {
    return;
  }

  // Don't call signal_exit() here - that's for global shutdown only!
  // We just want to stop threads for this connection, not exit the entire client

  // Shutdown the socket to interrupt any blocking recv() in data thread
  server_connection_shutdown();

  // Stop keepalive/ping thread - it checks connection status and will exit
  keepalive_stop_thread();

  // Stop webcam capture thread
  capture_stop_thread();

  // Wait for thread to exit gracefully with timeout
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_data_thread_exited)) {
    platform_sleep_usec(100000); // 100ms
    wait_count++;
  }

  if (!atomic_load(&g_data_thread_exited)) {
    log_warn("Data thread not responding after 2 seconds - forcing join with timeout");
  }

  // Join the thread with timeout to prevent hanging
  void *thread_retval = NULL;
  int join_result = ascii_thread_join_timeout(&g_data_thread, &thread_retval, 5000); // 5 second timeout

  if (join_result == -2) {
    log_error("Data thread join timed out - thread may be stuck, forcing termination");
    // Force close the thread handle to prevent resource leak
#ifdef _WIN32
    if (g_data_thread) {
      CloseHandle(g_data_thread);
      g_data_thread = NULL;
    }
#else
    // On POSIX, threads clean up automatically after join
    g_data_thread = 0;
#endif
  } else if (join_result != 0) {
    log_error("Failed to join data thread, result=%d", join_result);
    // Still force close the handle to prevent leak
#ifdef _WIN32
    if (g_data_thread) {
      CloseHandle(g_data_thread);
      g_data_thread = NULL;
    }
#else
    // On POSIX, threads clean up automatically after join
    g_data_thread = 0;
#endif
  }

  g_data_thread_created = false;

#ifdef DEBUG_THREADS
  log_info("Data reception thread stopped and joined");
#endif
}

/**
 * Check if connection has been lost
 *
 * @return true if protocol detected connection loss, false otherwise
 *
 * @ingroup client_protocol
 */
bool protocol_connection_lost() {
  return atomic_load(&g_data_thread_exited) || server_connection_is_lost();
}
