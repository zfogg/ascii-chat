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
#include "audio/audio.h"
#include "audio/analysis.h"
#include "keepalive.h"
#include "thread_pool.h"

#include "network/packet.h"
#include "network/packet_parsing.h"
#include "network/packet_parsing.h"
#include "network/acip/handlers.h"
#include "network/acip/transport.h"
#include "network/acip/client.h"
#include "network/acip/acds.h"
#include "network/webrtc/peer_manager.h"
#include "buffer_pool.h"
#include "common.h"
#include "util/endian.h"
#include "util/validation.h"
#include "util/endian.h"
#include "util/format.h"
#include "options/options.h"
#include "options/rcu.h" // For RCU-based options access
#include "network/crc32.h"
#include "util/fps.h"
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
#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
#include "platform/windows_compat.h"
#endif

#include "network/compression.h"
#include "network/packet_parsing.h"

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
__attribute__((unused)) static asciichat_thread_t g_data_thread;

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
 * Protocol Validation and Error Handling
 * ============================================================================ */

/**
 * @brief Disconnect from server due to bad/invalid packet data
 *
 * Closes the connection when the server sends malformed or invalid packets.
 * This provides client-side protocol enforcement, matching the server's
 * disconnect_client_for_bad_data behavior.
 *
 * @param format Printf-style format string for error details
 * @param ... Arguments for format string
 *
 * @ingroup client_protocol
 */
static void disconnect_server_for_bad_data(const char *format, ...) {
  va_list args;
  va_start(args, format);

  char message[256];
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  log_error("Server sent invalid data - disconnecting: %s", message);

  // Close the server connection
  server_connection_shutdown();
  server_connection_lost();
}

/* ============================================================================
 * Packet Handler Functions
 * ============================================================================ */

/**
 * @brief Decode frame data, handling both compressed and uncompressed formats
 *
 * Wrapper around shared packet_decode_frame_data_malloc() utility function.
 * Consolidates decompression/copy logic with unified size validation and error handling.
 * Allocates buffer and returns decoded frame data, or NULL on error.
 *
 * @param frame_data_ptr Pointer to compressed or uncompressed frame data
 * @param frame_data_len Size of frame_data_ptr in bytes
 * @param is_compressed True if data is zstd-compressed
 * @param original_size Expected decompressed size
 * @param compressed_size Expected compressed size (used for validation when compressed)
 * @return Allocated frame buffer (caller must SAFE_FREE) or NULL on error
 *
 * @see packet_decode_frame_data_malloc() For shared implementation
 * @ingroup client_protocol
 */
static char *decode_frame_data(const char *frame_data_ptr, size_t frame_data_len, bool is_compressed,
                               uint32_t original_size, uint32_t compressed_size) {
  return packet_decode_frame_data_malloc(frame_data_ptr, frame_data_len, is_compressed, original_size, compressed_size);
}

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
    disconnect_server_for_bad_data("ASCII_FRAME payload too small: %zu (min %zu)", len, sizeof(ascii_frame_packet_t));
    return;
  }

  // FPS tracking for received ASCII frames using reusable tracker utility
  static fps_t fps_tracker = {0};
  static bool fps_tracker_initialized = false;

  // Initialize FPS tracker on first frame
  if (!fps_tracker_initialized) {
    extern int g_max_fps; // From common.c
    int expected_fps = g_max_fps > 0 ? ((g_max_fps > 144) ? 144 : g_max_fps) : DEFAULT_MAX_FPS;
    fps_init(&fps_tracker, expected_fps, "ASCII_RX");
    fps_tracker_initialized = true;
  }

  struct timespec current_time;
  (void)clock_gettime(CLOCK_MONOTONIC, &current_time);

  // Track this frame and detect lag
  fps_frame(&fps_tracker, &current_time, "ASCII frame");

  // Extract header from the packet
  ascii_frame_packet_t header;
  memcpy(&header, data, sizeof(ascii_frame_packet_t));

  // Convert from network byte order
  header.width = NET_TO_HOST_U32(header.width);
  header.height = NET_TO_HOST_U32(header.height);
  header.original_size = NET_TO_HOST_U32(header.original_size);
  header.compressed_size = NET_TO_HOST_U32(header.compressed_size);
  header.checksum = NET_TO_HOST_U32(header.checksum);
  header.flags = NET_TO_HOST_U32(header.flags);

  // Get the frame data (starts after the header)
  const char *frame_data_ptr = (const char *)data + sizeof(ascii_frame_packet_t);
  size_t frame_data_len = len - sizeof(ascii_frame_packet_t);

  // Decode frame data (handles both compressed and uncompressed)
  bool is_compressed = (header.flags & FRAME_FLAG_IS_COMPRESSED) && header.compressed_size > 0;
  char *frame_data =
      decode_frame_data(frame_data_ptr, frame_data_len, is_compressed, header.original_size, header.compressed_size);
  if (!frame_data) {
    return; // Error already logged by decode_frame_data
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
  if (GET_OPTION(snapshot_mode)) {
    // Use high-resolution monotonic clock instead of time(NULL) to avoid 1-second precision issues
    static struct timespec first_frame_time = {0};
    static bool first_frame_recorded = false;
    static int snapshot_frame_count = 0;

    snapshot_frame_count++;
    // DEBUG: Log every frame received (even when terminal output is disabled)
    log_debug("Snapshot frame %d received", snapshot_frame_count);

    if (!first_frame_recorded) {
      (void)clock_gettime(CLOCK_MONOTONIC, &first_frame_time);
      first_frame_recorded = true;

      // If delay is 0, take snapshot immediately on first frame
      if (GET_OPTION(snapshot_delay) == 0) {
        log_info("Snapshot captured immediately (delay=0)!");
        take_snapshot = true;
        signal_exit();
      } else {
        log_info("Snapshot mode: first frame received, waiting %.2f seconds for webcam warmup...",
                 GET_OPTION(snapshot_delay));
      }
    } else {
      struct timespec current_time;
      (void)clock_gettime(CLOCK_MONOTONIC, &current_time);

      // Calculate elapsed time in seconds with microsecond precision
      double elapsed = (double)(current_time.tv_sec - first_frame_time.tv_sec) +
                       (double)(current_time.tv_nsec - first_frame_time.tv_nsec) / 1000000000.0;

      if (elapsed >= GET_OPTION(snapshot_delay)) {
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

  // DEBUG: Periodically log frame stats on client side
  static int client_frame_counter = 0;
  client_frame_counter++;
  if (client_frame_counter % 60 == 1) {
    // Count lines and check for issues
    int line_count = 0;
    size_t frame_len = strlen(frame_data);
    for (size_t i = 0; i < frame_len; i++) {
      if (frame_data[i] == '\n')
        line_count++;
    }
    log_info("CLIENT_FRAME: received %zu bytes, %d newlines, header: %ux%u", frame_len, line_count, header.width,
             header.height);
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
  if (!data || len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio packet data");
    return;
  }

  if (!GET_OPTION(audio_enabled)) {
    log_warn_every(1000000, "Received audio packet but audio is disabled");
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
__attribute__((unused)) static void handle_audio_batch_packet(const void *data, size_t len) {
  if (!data) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio batch packet data");
    return;
  }

  if (!GET_OPTION(audio_enabled)) {
    log_warn_every(1000000, "Received audio batch packet but audio is disabled");
    return;
  }

  if (len < sizeof(audio_batch_packet_t)) {
    log_warn("Audio batch packet too small: %zu bytes", len);
    return;
  }

  // Parse batch header
  const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)data;
  uint32_t batch_count = NET_TO_HOST_U32(batch_header->batch_count);
  uint32_t total_samples = NET_TO_HOST_U32(batch_header->total_samples);
  uint32_t sample_rate = NET_TO_HOST_U32(batch_header->sample_rate);
  uint32_t channels = NET_TO_HOST_U32(batch_header->channels);

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

  // Use helper function to dequantize samples
  asciichat_error_t dq_result = audio_dequantize_samples(samples_ptr, total_samples, samples);
  if (dq_result != ASCIICHAT_OK) {
    SAFE_FREE(samples);
    return;
  }

  // Track received packet for analysis
  if (GET_OPTION(audio_analysis_enabled)) {
    audio_analysis_track_received_packet(len);
  }

  // Process through audio subsystem
  audio_process_received_samples(samples, (int)total_samples);

  // Clean up
  SAFE_FREE(samples);

  log_debug_every(LOG_RATE_DEFAULT, "Processed audio batch: %u samples from server", total_samples);
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
  START_TIMER("audio_packet_total");

  // Validate parameters
  if (!data || len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid audio opus packet: data=%p, len=%zu", data, len);
    return;
  }

  if (!GET_OPTION(audio_enabled)) {
    log_warn_every(1000000, "Received opus audio packet but audio is disabled");
    return;
  }

  // Data is raw Opus-encoded frame (no header parsing needed)
  const uint8_t *opus_data = (const uint8_t *)data;

  // Opus max frame size is 2880 samples (120ms @ 48kHz)
  float samples[2880];

  START_TIMER("opus_decode");
  int decoded_samples = audio_decode_opus(opus_data, len, samples, 2880);
  double decode_ns = STOP_TIMER("opus_decode");

  if (decoded_samples <= 0) {
    log_warn("Failed to decode Opus audio packet, decoded=%d", decoded_samples);
    return;
  }

  // Track received packet for analysis
  if (GET_OPTION(audio_analysis_enabled)) {
    audio_analysis_track_received_packet(len);
  }

  // Process decoded audio through audio subsystem
  START_TIMER("process_samples");
  audio_process_received_samples(samples, decoded_samples);
  double process_ns = STOP_TIMER("process_samples");

  double total_ns = STOP_TIMER("audio_packet_total");

  static int timing_count = 0;
  if (++timing_count % 100 == 0) {
    log_info("Audio packet timing #%d: decode=%.2fµs, process=%.2fµs, total=%.2fµs", timing_count, decode_ns / 1000.0,
             process_ns / 1000.0, total_ns / 1000.0);
  }

  log_debug_every(LOG_RATE_DEFAULT, "Processed Opus audio: %d decoded samples from %zu byte packet", decoded_samples,
                  len);
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
  // Validate parameters
  if (!data || len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid opus batch packet: data=%p, len=%zu", data, len);
    return;
  }

  if (!GET_OPTION(audio_enabled)) {
    log_warn_every(1000000, "Received opus batch packet but audio is disabled");
    return;
  }

  // Parse batch header using av_receive_audio_opus_batch() for consistency
  const uint8_t *opus_data = NULL;
  size_t opus_size = 0;
  const uint16_t *frame_sizes = NULL;
  int sample_rate = 0;
  int frame_duration = 0;
  int frame_count = 0;

  asciichat_error_t result = packet_parse_opus_batch(data, len, &opus_data, &opus_size, &frame_sizes, &sample_rate,
                                                     &frame_duration, &frame_count);

  if (result != ASCIICHAT_OK) {
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
    size_t frame_size = (size_t)NET_TO_HOST_U16(frame_sizes[i]);

    if (opus_offset + frame_size > opus_size) {
      log_warn("Opus batch truncated at frame %d (offset=%zu, frame_size=%zu, total=%zu)", i, opus_offset, frame_size,
               opus_size);
      break;
    }

    // Decode frame through audio pipeline
    float *frame_buffer = all_samples + total_decoded_samples;
    int remaining_space = (int)(max_decoded_samples - (size_t)total_decoded_samples);
    int decoded = audio_decode_opus(opus_data + opus_offset, frame_size, frame_buffer, remaining_space);

    if (decoded <= 0) {
      log_warn("Failed to decode Opus frame %d in batch, decoded=%d", i, decoded);
      break;
    }

    total_decoded_samples += decoded;
    opus_offset += frame_size;
  }

  if (total_decoded_samples > 0) {
    // Track received packet for analysis
    if (GET_OPTION(audio_analysis_enabled)) {
      audio_analysis_track_received_packet(len);
    }

    // Process decoded audio through audio subsystem
    audio_process_received_samples(all_samples, total_decoded_samples);

    log_debug_every(LOG_RATE_DEFAULT, "Processed Opus batch: %d decoded samples from %d frames", total_decoded_samples,
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
  uint32_t active_count = NET_TO_HOST_U32(state->active_client_count);

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
 * ACIP Callback Forward Declarations and Structure
 * ============================================================================ */

// Forward declarations for ACIP callbacks (implemented after this section)
static void acip_on_ascii_frame(const ascii_frame_packet_t *header, const void *frame_data, size_t data_len, void *ctx);
static void acip_on_audio(const void *audio_data, size_t audio_len, void *ctx);
static void acip_on_audio_batch(const audio_batch_packet_t *header, const float *samples, size_t num_samples,
                                void *ctx);
static void acip_on_audio_opus(const void *opus_data, size_t opus_len, void *ctx);
static void acip_on_audio_opus_batch(const void *batch_data, size_t batch_len, void *ctx);
static void acip_on_server_state(const server_state_packet_t *state, void *ctx);
static void acip_on_error(const error_packet_t *header, const char *message, void *ctx);
static void acip_on_remote_log(const remote_log_packet_t *header, const char *message, void *ctx);
static void acip_on_ping(void *ctx);
static void acip_on_pong(void *ctx);
static void acip_on_clear_console(void *ctx);
static void acip_on_crypto_rekey_request(const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_rekey_response(const void *payload, size_t payload_len, void *ctx);
static void acip_on_webrtc_sdp(const acip_webrtc_sdp_t *sdp, size_t total_len, void *ctx);
static void acip_on_webrtc_ice(const acip_webrtc_ice_t *ice, size_t total_len, void *ctx);
static void acip_on_session_joined(const acip_session_joined_t *joined, void *ctx);

/**
 * @brief Global ACIP client callbacks structure
 *
 * Handles all ACIP packet types including crypto rekey protocol.
 * Integrates with existing client-side packet handlers.
 */
static const acip_client_callbacks_t g_acip_client_callbacks = {.on_ascii_frame = acip_on_ascii_frame,
                                                                .on_audio = acip_on_audio,
                                                                .on_audio_batch = acip_on_audio_batch,
                                                                .on_audio_opus = acip_on_audio_opus,
                                                                .on_audio_opus_batch = acip_on_audio_opus_batch,
                                                                .on_server_state = acip_on_server_state,
                                                                .on_error = acip_on_error,
                                                                .on_remote_log = acip_on_remote_log,
                                                                .on_ping = acip_on_ping,
                                                                .on_pong = acip_on_pong,
                                                                .on_clear_console = acip_on_clear_console,
                                                                .on_crypto_rekey_request = acip_on_crypto_rekey_request,
                                                                .on_crypto_rekey_response =
                                                                    acip_on_crypto_rekey_response,
                                                                .on_webrtc_sdp = acip_on_webrtc_sdp,
                                                                .on_webrtc_ice = acip_on_webrtc_ice,
                                                                .on_session_joined = acip_on_session_joined,
                                                                .app_ctx = NULL};

/**
 * @brief Get ACIP client callbacks for packet dispatch
 *
 * Returns pointer to the global callback structure for use by
 * WebRTC sessions that need to receive ACDS signaling packets.
 *
 * @return Pointer to client callbacks (never NULL)
 */
const acip_client_callbacks_t *protocol_get_acip_callbacks() {
  return &g_acip_client_callbacks;
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
      // Use rate-limited logging instead of logging every 10ms
      log_debug_every(1000000, "Waiting for socket connection"); // Max once per second
      platform_sleep_usec(10 * 1000);
      continue;
    }

    // Receive and dispatch packet using ACIP transport API
    // This combines packet reception, decryption, parsing, handler dispatch, and cleanup
    acip_transport_t *transport = server_connection_get_transport();
    if (!transport) {
      log_error("Transport not available, connection lost");
      server_connection_lost();
      break;
    }

    asciichat_error_t acip_result = acip_client_receive_and_dispatch(transport, &g_acip_client_callbacks);

    // Handle receive/dispatch errors
    if (acip_result != ASCIICHAT_OK) {
      // Check error type to determine action
      asciichat_error_context_t err_ctx;
      if (HAS_ERRNO(&err_ctx)) {
        if (err_ctx.code == ERROR_NETWORK) {
          // Network error or EOF - server disconnected
          log_debug("Server disconnected (network error): %s", err_ctx.context_message);
          server_connection_lost();
          break;
        } else if (err_ctx.code == ERROR_CRYPTO) {
          // Security violation - exit immediately
          log_error("SECURITY: Server violated encryption policy");
          log_error("SECURITY: This is a critical security violation - exiting immediately");
          exit(1);
        }
      }

      // Other errors - log warning but continue
      log_warn("ACIP receive/dispatch failed: %s", asciichat_error_string(acip_result));
    }
  }

#ifdef DEBUG_THREADS
  log_debug("Data reception thread stopped");
#endif

  atomic_store(&g_data_thread_exited, true);

  // Clean up thread-local error context before exit
  asciichat_errno_cleanup();

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

  // Send CLIENT_CAPABILITIES packet FIRST before starting any threads
  // Server expects this as the first packet after crypto handshake
  log_info("Sending client capabilities to server...");
  if (threaded_send_terminal_size_with_auto_detect(GET_OPTION(width), GET_OPTION(height)) < 0) {
    log_error("Failed to send client capabilities to server");
    return -1;
  }
  log_info("Client capabilities sent successfully");

  // Send STREAM_START packet with combined stream types BEFORE starting worker threads
  // This tells the server what streams to expect before any data arrives
  uint32_t stream_types = STREAM_TYPE_VIDEO; // Always have video
  if (GET_OPTION(audio_enabled)) {
    stream_types |= STREAM_TYPE_AUDIO; // Add audio if enabled
  }
  log_info("Sending STREAM_START packet (types=0x%x: %s%s)...", stream_types, "video",
           (stream_types & STREAM_TYPE_AUDIO) ? "+audio" : "");
  if (threaded_send_stream_start_packet(stream_types) < 0) {
    log_error("Failed to send STREAM_START packet");
    return -1;
  }
  log_info("STREAM_START packet sent successfully");

  // Start data reception thread
  atomic_store(&g_data_thread_exited, false);
  if (thread_pool_spawn(g_client_worker_pool, data_reception_thread_func, NULL, 1, "data_reception") != ASCIICHAT_OK) {
    log_error("Failed to spawn data reception thread in worker pool");
    LOG_ERRNO_IF_SET("Data reception thread creation failed");
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

  // Stop audio threads if running
  audio_stop_thread();

  // Wait for data reception thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_data_thread_exited)) {
    platform_sleep_usec(100000); // 100ms
    wait_count++;
  }

  if (!atomic_load(&g_data_thread_exited)) {
    log_warn("Data thread not responding after 2 seconds - will be joined by thread pool");
  }

  // Join all threads in the client worker pool (in stop_id order)
  // This handles the data reception thread and (eventually) all other worker threads
  if (g_client_worker_pool) {
    asciichat_error_t result = thread_pool_stop_all(g_client_worker_pool);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to stop client worker threads");
      LOG_ERRNO_IF_SET("Thread pool stop failed");
    }
  }

  g_data_thread_created = false;

#ifdef DEBUG_THREADS
  log_info("Data reception thread stopped and joined by thread pool");
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

/* ============================================================================
 * ACIP Callback Implementations
 * ============================================================================ */

/**
 * @brief ACIP callback for ASCII frame packets
 *
 * Wraps handle_ascii_frame_packet() to integrate with ACIP handler system.
 */
static void acip_on_ascii_frame(const ascii_frame_packet_t *header, const void *frame_data, size_t data_len,
                                void *ctx) {
  (void)ctx;

  // Reconstruct full packet for existing handler (header + data)
  // IMPORTANT: header is already in HOST byte order from ACIP layer,
  // but handle_ascii_frame_packet() expects NETWORK byte order and does conversion.
  // So we need to convert back to network order before passing.
  size_t total_len = sizeof(*header) + data_len;
  uint8_t *packet = buffer_pool_alloc(NULL, total_len);
  if (!packet) {
    log_error("Failed to allocate buffer for ASCII frame callback");
    return;
  }

  // Convert header fields back to network byte order for handle_ascii_frame_packet()
  ascii_frame_packet_t net_header = *header;
  net_header.width = HOST_TO_NET_U32(header->width);
  net_header.height = HOST_TO_NET_U32(header->height);
  net_header.original_size = HOST_TO_NET_U32(header->original_size);
  net_header.compressed_size = HOST_TO_NET_U32(header->compressed_size);
  net_header.checksum = HOST_TO_NET_U32(header->checksum);
  net_header.flags = HOST_TO_NET_U32(header->flags);

  memcpy(packet, &net_header, sizeof(net_header));
  memcpy(packet + sizeof(net_header), frame_data, data_len);

  handle_ascii_frame_packet(packet, total_len);
  buffer_pool_free(NULL, packet, total_len);
}

/**
 * @brief ACIP callback for audio batch packets
 */
static void acip_on_audio_batch(const audio_batch_packet_t *header, const float *samples, size_t num_samples,
                                void *ctx) {
  (void)ctx;
  (void)header;

  if (!GET_OPTION(audio_enabled)) {
    return;
  }

  // Process samples directly (already dequantized by ACIP handler)
  audio_process_received_samples((float *)samples, (int)num_samples);

  if (GET_OPTION(audio_analysis_enabled)) {
    // Approximate packet size for analysis
    size_t approx_size = sizeof(*header) + (num_samples * sizeof(uint32_t));
    audio_analysis_track_received_packet(approx_size);
  }

  log_debug_every(LOG_RATE_DEFAULT, "Processed audio batch: %zu samples from server", num_samples);
}

/**
 * @brief ACIP callback for Opus audio packets
 */
static void acip_on_audio_opus(const void *opus_data, size_t opus_len, void *ctx) {
  (void)ctx;

  // Call existing handler directly
  handle_audio_opus_packet(opus_data, opus_len);
}

/**
 * @brief ACIP callback for server state packets
 */
static void acip_on_server_state(const server_state_packet_t *state, void *ctx) {
  (void)ctx;

  // Call existing handler directly
  handle_server_state_packet(state, sizeof(*state));
}

/**
 * @brief ACIP callback for error packets
 */
static void acip_on_error(const error_packet_t *header, const char *message, void *ctx) {
  (void)ctx;

  // Reconstruct packet for existing handler
  size_t msg_len = message ? strlen(message) : 0;
  size_t total_len = sizeof(*header) + msg_len;

  uint8_t *packet = buffer_pool_alloc(NULL, total_len);
  if (!packet) {
    log_error("Failed to allocate buffer for error packet callback");
    return;
  }

  memcpy(packet, header, sizeof(*header));
  if (msg_len > 0) {
    memcpy(packet + sizeof(*header), message, msg_len);
  }

  handle_error_message_packet(packet, total_len);
  buffer_pool_free(NULL, packet, total_len);
}

/**
 * @brief ACIP callback for ping packets
 */
static void acip_on_ping(void *ctx) {
  (void)ctx;

  // Respond with PONG
  if (threaded_send_pong_packet() < 0) {
    log_error("Failed to send PONG response");
  }
}

/**
 * @brief ACIP callback for raw audio packets
 */
static void acip_on_audio(const void *audio_data, size_t audio_len, void *ctx) {
  (void)ctx;

  // Call existing handler directly
  handle_audio_packet(audio_data, audio_len);
}

/**
 * @brief ACIP callback for Opus batch packets
 */
static void acip_on_audio_opus_batch(const void *batch_data, size_t batch_len, void *ctx) {
  (void)ctx;

  // Call existing handler directly
  handle_audio_opus_batch_packet(batch_data, batch_len);
}

/**
 * @brief ACIP callback for remote log packets
 */
static void acip_on_remote_log(const remote_log_packet_t *header, const char *message, void *ctx) {
  (void)ctx;

  // Reconstruct packet for existing handler
  size_t msg_len = strlen(message);
  size_t total_len = sizeof(*header) + msg_len;

  uint8_t *packet = buffer_pool_alloc(NULL, total_len);
  if (!packet) {
    log_error("Failed to allocate buffer for remote log callback");
    return;
  }

  memcpy(packet, header, sizeof(*header));
  memcpy(packet + sizeof(*header), message, msg_len);

  handle_remote_log_packet(packet, total_len);
  buffer_pool_free(NULL, packet, total_len);
}

/**
 * @brief ACIP callback for pong packets
 */
static void acip_on_pong(void *ctx) {
  (void)ctx;
  // Pong received - no action needed (server acknowledged our ping)
}

/**
 * @brief ACIP callback for console clear packets
 */
static void acip_on_clear_console(void *ctx) {
  (void)ctx;

  // Server requested console clear
  display_full_reset();
  log_info("Console cleared by server");
}

/**
 * @brief ACIP callback for crypto rekey request packets
 */
static void acip_on_crypto_rekey_request(const void *payload, size_t payload_len, void *ctx) {
  (void)ctx;

  // Process the server's rekey request
  asciichat_error_t crypto_result = crypto_client_process_rekey_request(payload, payload_len);
  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_REQUEST: %d", crypto_result);
    return;
  }

  // Send REKEY_RESPONSE
  crypto_result = crypto_client_send_rekey_response();
  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_RESPONSE: %d", crypto_result);
  }
}

/**
 * @brief ACIP callback for crypto rekey response packets
 */
static void acip_on_crypto_rekey_response(const void *payload, size_t payload_len, void *ctx) {
  (void)ctx;

  // Process server's response
  asciichat_error_t crypto_result = crypto_client_process_rekey_response(payload, payload_len);
  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to process REKEY_RESPONSE: %d", crypto_result);
    return;
  }

  // Send REKEY_COMPLETE
  crypto_result = crypto_client_send_rekey_complete();
  if (crypto_result != ASCIICHAT_OK) {
    log_error("Failed to send REKEY_COMPLETE: %d", crypto_result);
  }
}

/**
 * @brief ACIP callback for WebRTC SDP offer/answer packets
 *
 * Routes incoming SDP signaling messages to the peer manager for processing.
 * Called when ACDS relays SDP from another session participant.
 *
 * @param sdp SDP packet (header + variable SDP data)
 * @param total_len Total length of the SDP packet
 * @param ctx Application context (unused)
 */
static void acip_on_webrtc_sdp(const acip_webrtc_sdp_t *sdp, size_t total_len, void *ctx) {
  (void)ctx;
  (void)total_len; // Peer manager reads variable data via pointer arithmetic

  // Check if WebRTC is initialized
  if (!g_peer_manager) {
    log_warn("Received WebRTC SDP but peer manager not initialized - ignoring");
    return;
  }

  // Log SDP type for debugging
  const char *sdp_type_str = (sdp->sdp_type == 0) ? "offer" : "answer";
  log_info("Received WebRTC SDP %s from participant (session_id=%.8s...)", sdp_type_str, (const char *)sdp->session_id);

  // Handle SDP through peer manager (extracts variable data internally)
  asciichat_error_t result = webrtc_peer_manager_handle_sdp(g_peer_manager, sdp);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to handle WebRTC SDP: %s", asciichat_error_string(result));
  }
}

/**
 * @brief ACIP callback for WebRTC ICE candidate packets
 *
 * Routes incoming ICE candidates to the peer manager for processing.
 * Called when ACDS relays ICE candidates from another session participant.
 *
 * @param ice ICE packet (header + variable candidate/mid data)
 * @param total_len Total length of the ICE packet
 * @param ctx Application context (unused)
 */
static void acip_on_webrtc_ice(const acip_webrtc_ice_t *ice, size_t total_len, void *ctx) {
  (void)ctx;
  (void)total_len; // Peer manager reads variable data via pointer arithmetic

  // Check if WebRTC is initialized
  if (!g_peer_manager) {
    log_warn("Received WebRTC ICE but peer manager not initialized - ignoring");
    return;
  }

  log_debug("Received WebRTC ICE candidate from participant (session_id=%.8s...)", (const char *)ice->session_id);

  // Handle ICE through peer manager (extracts variable data internally)
  asciichat_error_t result = webrtc_peer_manager_handle_ice(g_peer_manager, ice);

  if (result != ASCIICHAT_OK) {
    log_error("Failed to handle WebRTC ICE: %s", asciichat_error_string(result));
  }
}

/**
 * @brief Handle ACDS SESSION_JOINED response (Phase 3 WebRTC integration)
 *
 * Called when server responds to ACDS session join request.
 * Validates join success and stores session context for WebRTC handshake.
 *
 * Flow:
 * 1. Check if join succeeded
 * 2. If failed, log error and return (connection will timeout and fallback)
 * 3. If successful:
 *    - Store session context (session_id, participant_id)
 *    - Check session_type (DIRECT_TCP or WEBRTC)
 *    - For WEBRTC: signal WebRTC initialization with TURN credentials
 *    - For DIRECT_TCP: continue with existing TCP flow
 *
 * @param joined SESSION_JOINED response from ACDS
 * @param ctx Application context (unused for now)
 *
 * @ingroup client_protocol
 */
static void acip_on_session_joined(const acip_session_joined_t *joined, void *ctx) {
  (void)ctx; // Unused for now, may be used in future for context passing

  if (!joined) {
    log_error("SESSION_JOINED callback received NULL response");
    return;
  }

  // Check if join was successful
  if (!joined->success) {
    log_error("ACDS session join failed: error %d: %s", joined->error_code, joined->error_message);
    // Connection will timeout waiting for SDP/WebRTC completion and fallback to next stage
    return;
  }

  // Join succeeded - we have session context now
  log_info("ACDS session join succeeded (participant_id=%.8s..., session_type=%s, server=%s:%u)",
           (const char *)joined->participant_id, joined->session_type == 1 ? "WebRTC" : "DirectTCP",
           joined->server_address, joined->server_port);

  // Check if this is a WebRTC session
  if (joined->session_type == SESSION_TYPE_WEBRTC) {
    // TODO: Phase 3 - Initialize WebRTC connection with TURN credentials
    // webrtc_initialize_session(joined->session_id, joined->participant_id,
    //                           joined->turn_username, joined->turn_password);
    log_info("WebRTC session detected - TODO: initialize WebRTC with TURN credentials");
  } else {
    // Direct TCP - connection is already established or will be established
    log_info("Direct TCP session - using existing connection");
  }
}
