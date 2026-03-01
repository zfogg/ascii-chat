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
#include "../main.h" // Global exit API
#include "server.h"
#include "display.h"
#include "capture.h"
#include "audio.h"
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/audio/analysis.h>
#include "keepalive.h"
#include <ascii-chat/thread_pool.h>

#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/network/packet/parsing.h>
#include <ascii-chat/network/acip/handlers.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/network/webrtc/peer_manager.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/validation.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/fps.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/handshake/client.h>

// Forward declaration for client crypto functions
bool crypto_client_is_ready(void);
const crypto_context_t *crypto_client_get_context(void);
int crypto_client_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext,
                                 size_t plaintext_size, size_t *plaintext_len);

#include "crypto.h"
#include <ascii-chat/util/time.h>

#include <ascii-chat/atomic.h>
#include <ascii-chat/debug/named.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#ifdef _WIN32
#include <ascii-chat/platform/windows_compat.h>
#endif

#include <ascii-chat/network/compression.h>
#include <ascii-chat/network/packet/parsing.h>

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
static atomic_t g_data_thread_exited = {0};

/* ============================================================================
 * Frame Rendering Statistics
 * ============================================================================ */

/**
 * @brief Counter for total unique frames rendered by the client
 *
 * Incremented each time a frame packet is received and rendered.
 * Used for performance monitoring and verifying that frames are being
 * transmitted and displayed, not just replaying the same frame.
 *
 * @ingroup client_protocol
 */
static atomic_t g_frames_rendered = {0};

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

  char message[BUFFER_SIZE_SMALL];
  safe_vsnprintf(message, sizeof(message), format, args);
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
  fflush(stdout);
  static int frame_count = 0;
  frame_count++;
  if (frame_count == 1) {
  }

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
    int fps = GET_OPTION(fps);
    int expected_fps = fps > 0 ? ((fps > 144) ? 144 : fps) : DEFAULT_MAX_FPS;
    fps_init(&fps_tracker, expected_fps, "ASCII_RX");
    fps_tracker_initialized = true;
  }

  // Track this frame and detect lag
  fps_frame_ns(&fps_tracker, time_get_ns(), "ASCII frame");

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
    static uint64_t first_frame_time_ns = 0;
    static bool first_frame_recorded = false;
    static int snapshot_frame_count = 0;

    snapshot_frame_count++;
    log_debug("Snapshot frame %d received", snapshot_frame_count);

    if (!first_frame_recorded) {
      first_frame_time_ns = time_get_ns();
      first_frame_recorded = true;

      if (GET_OPTION(snapshot_delay) == 0) {
        log_debug("Snapshot captured immediately (delay=0)!");
        take_snapshot = true;
        signal_exit();
      } else {
        log_debug("Snapshot mode: first frame received, waiting %.2f seconds for webcam warmup...",
                  GET_OPTION(snapshot_delay));
      }
    } else {
      uint64_t current_time_ns = time_get_ns();
      double elapsed = time_ns_to_s(time_elapsed_ns(first_frame_time_ns, current_time_ns));

      if (elapsed >= GET_OPTION(snapshot_delay)) {
        char duration_str[32];
        time_pretty((uint64_t)(elapsed * 1e9), -1, duration_str, sizeof(duration_str));
        log_debug("Snapshot captured after %s!", duration_str);
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
    log_debug("First frame - clearing display and disabling terminal logging");
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
  static uint64_t last_render_time_ns = 0;

  // Don't limit frame rate in snapshot mode - always render the final frame
  if (!take_snapshot) {
    // Get the client's desired FPS (what we told the server we can display)
    int fps = GET_OPTION(fps);
    int client_display_fps = fps > 0 ? fps : DEFAULT_MAX_FPS;
    uint64_t render_interval_us = US_PER_SEC_INT / (uint64_t)client_display_fps;

    uint64_t render_time_ns = time_get_ns();
    uint64_t render_elapsed_us = 0;

    if (last_render_time_ns != 0) {
      render_elapsed_us = time_ns_to_us(time_elapsed_ns(last_render_time_ns, render_time_ns));
    }

    // Skip rendering if not enough time has passed (frame rate limiting)
    if (last_render_time_ns != 0) {
      if (render_elapsed_us > 0 && render_elapsed_us < render_interval_us) {
        // Drop this frame to maintain display FPS limit
        SAFE_FREE(frame_data);
        return;
      }
    }

    // Update last render time
    last_render_time_ns = render_time_ns;
  }

  // Increment global frame counter BEFORE rendering (to track unique frames received)
  int total_frames = atomic_fetch_add_u64(&g_frames_rendered, 1) + 1;

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
    log_info("🎬 CLIENT_FRAME: #%d received - %zu bytes, %d newlines, %ux%u", total_frames, frame_len, line_count,
             header.width, header.height);
  }

  // Track frames actually reaching display (after frame rate limiting)
  static int frames_to_display = 0;
  frames_to_display++;
  if (frames_to_display % 10 == 1) {
    log_info("📺 FRAME_TO_DISPLAY: #%d (received: %d)", frames_to_display, total_frames);
  }

  // Render ASCII art frame (display_render_frame will apply effects like --matrix)
  log_debug("🎬 CALLING_DISPLAY_RENDER: frame_data=%p len=%zu, calling display_render_frame()", frame_data,
            strlen(frame_data));
  display_render_frame(frame_data);
  log_debug("🎬 DISPLAY_RENDER_RETURNED: frame was rendered");

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
    log_warn_every(NS_PER_MS_INT, "Received audio packet but audio is disabled");
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
    log_warn_every(NS_PER_MS_INT, "Received opus audio packet but audio is disabled");
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
    char decode_str[32], process_str[32], total_str[32];
    time_pretty((uint64_t)decode_ns, -1, decode_str, sizeof(decode_str));
    time_pretty((uint64_t)process_ns, -1, process_str, sizeof(process_str));
    time_pretty((uint64_t)total_ns, -1, total_str, sizeof(total_str));
    log_debug("Audio packet timing #%d: decode=%s, process=%s, total=%s", timing_count, decode_str, process_str,
              total_str);
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

  // DEBUG: Log first 12 bytes of received Opus batch header
  if (len >= 12) {
    const uint8_t *bytes = (const uint8_t *)data;
    log_info("OPUS_BATCH_RECV_DEBUG: first_bytes=[%02x %02x %02x %02x][%02x %02x %02x %02x][%02x %02x %02x %02x]",
             bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7], bytes[8], bytes[9],
             bytes[10], bytes[11]);

    // Parse header manually to check values
    uint32_t sr, fd, fc;
    memcpy(&sr, bytes, 4);
    memcpy(&fd, bytes + 4, 4);
    memcpy(&fc, bytes + 8, 4);
    log_info("OPUS_BATCH_RECV_PARSED: sample_rate(net)=0x%08x frame_duration(net)=0x%08x frame_count(net)=0x%08x", sr,
             fd, fc);
    log_info("OPUS_BATCH_RECV_PARSED: sample_rate=%u frame_duration=%u frame_count=%u", NET_TO_HOST_U32(sr),
             NET_TO_HOST_U32(fd), NET_TO_HOST_U32(fc));
  }

  if (!GET_OPTION(audio_enabled)) {
    log_warn_every(NS_PER_MS_INT, "Received opus batch packet but audio is disabled");
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
      log_debug("Active client count changed from %u to %u - will clear console before next frame", g_last_active_count,
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
static void acip_on_crypto_key_exchange_init(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_auth_challenge(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_server_auth_resp(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_auth_failed(packet_type_t type, const void *payload, size_t payload_len, void *ctx);
static void acip_on_crypto_handshake_complete(packet_type_t type, const void *payload, size_t payload_len, void *ctx);

/**
 * @brief Global ACIP client callbacks structure
 *
 * Handles all ACIP packet types including crypto rekey protocol.
 * Integrates with existing client-side packet handlers.
 */
static const acip_client_callbacks_t g_acip_client_callbacks = {
    .on_ascii_frame = acip_on_ascii_frame,
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
    .on_crypto_rekey_response = acip_on_crypto_rekey_response,
    .on_webrtc_sdp = acip_on_webrtc_sdp,
    .on_webrtc_ice = acip_on_webrtc_ice,
    .on_session_joined = acip_on_session_joined,
    .on_crypto_key_exchange_init = acip_on_crypto_key_exchange_init,
    .on_crypto_auth_challenge = acip_on_crypto_auth_challenge,
    .on_crypto_server_auth_resp = acip_on_crypto_server_auth_resp,
    .on_crypto_auth_failed = acip_on_crypto_auth_failed,
    .on_crypto_handshake_complete = acip_on_crypto_handshake_complete,
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

  log_warn("[FRAME_RECV_LOOP] 🔄 THREAD_STARTED: Data reception thread active, callbacks initialized");

#ifdef DEBUG_THREADS
  log_debug("[FRAME_RECV_LOOP] Thread lifecycle tracking enabled");
#endif

  int packet_count = 0;
  while (!should_exit() && server_connection_is_active()) {
    // Main loop: receive and process packets while connection is active
    // When connection becomes inactive or shutdown is requested, thread exits cleanly

    // Receive and dispatch packet using ACIP transport API
    // This combines packet reception, decryption, parsing, handler dispatch, and cleanup
    acip_transport_t *transport = server_connection_get_transport();
    if (packet_count < 3) {
    }
    if (!transport) {
      log_error("[FRAME_RECV_LOOP] ❌ NO_TRANSPORT: connection lost, transport not available");
      server_connection_lost();
      break;
    }

    log_debug("[FRAME_RECV_LOOP] 📥 RECV_WAITING: awaiting packet #%d from server (transport ready)", packet_count + 1);

    if (packet_count == 0) {
    }
    // Use short 16ms timeout to allow rendering at 60 FPS
    // Instead of blocking indefinitely waiting for a packet,
    // we timeout quickly so main thread can continue rendering
    asciichat_error_t acip_result = acip_client_receive_and_dispatch(transport, &g_acip_client_callbacks);
    if (packet_count == 0) {
    }

    if (acip_result == ASCIICHAT_OK) {
      packet_count++;
      log_debug("[FRAME_RECV_LOOP] ✅ PACKET_%d_DISPATCHED: callbacks processed successfully", packet_count);
    } else {
      // Handle receive/dispatch errors - ALWAYS exit on network errors
      log_error("[FRAME_RECV_LOOP] ❌ RECV_ERROR: acip_result=%d: %s", acip_result,
                asciichat_error_string(acip_result));

      // Network errors (ERROR_NETWORK, ERROR_NETWORK_PROTOCOL, etc) always disconnect
      if (acip_result == ERROR_NETWORK || acip_result == ERROR_NETWORK_PROTOCOL) {
        log_warn("[FRAME_RECV_LOOP] ⚠️  NETWORK_ERROR: Server disconnected after %d packets, exiting loop",
                 packet_count);
        server_connection_lost();
        break;
      }

      // Check errno context for additional error details
      asciichat_error_context_t err_ctx;
      if (HAS_ERRNO(&err_ctx)) {
        if (err_ctx.code == ERROR_CRYPTO) {
          // Security violation - exit immediately
          log_error("[FRAME_RECV_LOOP] ❌ SECURITY_VIOLATION: Server crypto policy violated - EXITING");
          log_error("SECURITY: This is a critical security violation - exiting immediately");
          exit(1);
        }
      }

      // Other errors - still disconnect to prevent infinite loop
      log_error("[FRAME_RECV_LOOP] ❌ RECV_FAILED: packet #%d failed, disconnecting", packet_count + 1);
      server_connection_lost();
      break;
    }
  }

  log_warn("[FRAME_RECV_LOOP] 🔴 THREAD_EXITING: received %d packets total, connection inactive or shutdown requested",
           packet_count);

#ifdef DEBUG_THREADS
  log_debug("[FRAME_RECV_LOOP] Thread lifecycle tracking - exit");
#endif

  atomic_store_bool(&g_data_thread_exited, true);

  // Clean up thread-local error context before exit
  asciichat_errno_destroy();

  log_warn("[FRAME_RECV_LOOP] ✅ THREAD_CLEANUP: error context destroyed, thread terminating");
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
  log_warn("[FRAME_RECV_INIT] 🟢 PROTOCOL_START: Starting client protocol initialization");

  // Register protocol atomics with named debug registry
  static bool protocol_atomics_registered = false;
  if (!protocol_atomics_registered) {
    NAMED_REGISTER_ATOMIC(&g_data_thread_exited, "protocol_data_reception_thread_exit_confirmation");
    NAMED_REGISTER_ATOMIC(&g_frames_rendered, "protocol_frames_successfully_rendered");
    protocol_atomics_registered = true;
  }

  // Reset protocol state for new connection
  g_server_state_initialized = false;
  g_last_active_count = 0;
  g_should_clear_before_next_frame = false;

  log_info("[FRAME_RECV_INIT] ✅ STATE_RESET: server_initialized=false, active_count=0, clear_flag=false");

  // Reset display state for new connection
  display_reset_for_new_connection();

  // Send CLIENT_CAPABILITIES packet FIRST before starting any threads
  // Server expects this as the first packet after crypto handshake
  log_debug("[FRAME_RECV_INIT] 📤 SENDING_CAPABILITIES: terminal_size negotiation");
  asciichat_error_t cap_result = threaded_send_terminal_size_with_auto_detect((int)terminal_get_effective_width(),
                                                                              (int)terminal_get_effective_height());
  if (cap_result != ASCIICHAT_OK) {
    log_error("[FRAME_RECV_INIT] ❌ CAPABILITIES_FAILED: cannot send terminal size");
    return -1;
  }
  log_debug("[FRAME_RECV_INIT] ✅ CAPABILITIES_SENT: terminal_size sent successfully");

  // Send STREAM_START packet with combined stream types BEFORE starting worker threads
  // This tells the server what streams to expect before any data arrives
  uint32_t stream_types = STREAM_TYPE_VIDEO; // Always have video
  if (GET_OPTION(audio_enabled)) {
    stream_types |= STREAM_TYPE_AUDIO; // Add audio if enabled
  }
  log_info("[FRAME_RECV_INIT] 📤 SENDING_STREAM_START: types=0x%x (video%s)", stream_types,
           (stream_types & STREAM_TYPE_AUDIO) ? "+audio" : "");
  asciichat_error_t stream_result = threaded_send_stream_start_packet(stream_types);
  if (stream_result != ASCIICHAT_OK) {
    log_error("[FRAME_RECV_INIT] ❌ STREAM_START_FAILED: cannot send stream types");
    return -1;
  }
  log_info("[FRAME_RECV_INIT] ✅ STREAM_START_SENT: stream_types=0x%x, server will send frames", stream_types);

  // Start data reception thread
  log_warn("[FRAME_RECV_INIT] 🔄 STARTING_DATA_THREAD: callbacks registered, about to spawn thread");
  atomic_store_bool(&g_data_thread_exited, false);
  if (thread_pool_spawn(g_client_worker_pool, data_reception_thread_func, NULL, 1, "data_reception") != ASCIICHAT_OK) {
    log_error("[FRAME_RECV_INIT] ❌ DATA_THREAD_SPAWN_FAILED: cannot start frame receive thread");
    LOG_ERRNO_IF_SET("Data reception thread creation failed");
    return -1;
  }
  log_warn("[FRAME_RECV_INIT] ✅ DATA_THREAD_SPAWNED: frame receive thread is now running, waiting for frames...");

  // Start webcam capture thread
  log_debug("Starting webcam capture thread...");
  if (capture_start_thread() != 0) {
    log_error("Failed to start webcam capture thread");
    return -1;
  }
  log_debug("Webcam capture thread started successfully");

  // Initialize audio sender thread BEFORE starting audio capture
  // This ensures sender is ready when capture thread starts queueing packets
  // Must happen after connection succeeds to prevent deadlock if connection fails
  audio_sender_init();

  // Start audio capture thread if audio is enabled
  log_debug("Starting audio capture thread...");
  if (audio_start_thread() != 0) {
    log_error("Failed to start audio capture thread");
    return -1;
  }
  log_debug("Audio capture thread started successfully (or skipped if audio disabled)");

  // Start keepalive/ping thread to prevent server timeout
  log_debug("Starting keepalive/ping thread...");
  if (keepalive_start_thread() != 0) {
    log_error("Failed to start keepalive/ping thread");
    return -1;
  }
  log_debug("Keepalive/ping thread started successfully");

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
  // Log final frame count before shutting down
  int final_frame_count = atomic_load_u64(&g_frames_rendered);
  if (final_frame_count > 0) {
    log_info("📊 CLIENT SESSION STATS: %d unique frames rendered during connection", final_frame_count);
  }

  log_debug("[PROTOCOL_STOP] 1. Starting protocol_stop_connection");

  // In snapshot mode, data reception thread was never started, but capture thread may still be running
  // Always stop the capture thread to prevent use-after-free when transport is destroyed
  if (GET_OPTION(snapshot_mode)) {
    log_debug("[PROTOCOL_STOP] Snapshot mode: stopping capture thread before returning");
    capture_stop_thread();
    return;
  }

  // Don't call signal_exit() here - that's for global shutdown only!
  // We just want to stop threads for this connection, not exit the entire client

  // Shutdown the socket FIRST to interrupt any blocking network operations in worker threads
  // This must happen BEFORE audio_stop_thread() so audio sender thread unblocks from network send
  log_debug("[PROTOCOL_STOP] 2. About to call server_connection_shutdown");
  server_connection_shutdown();
  log_debug("[PROTOCOL_STOP] 3. server_connection_shutdown() returned");

  // Signal audio sender thread to exit
  // Must happen after socket shutdown so any blocked network calls fail
  // Audio sender is created in ALL modes except snapshot mode
  log_debug("[PROTOCOL_STOP] 4. About to call audio_stop_thread");
  audio_stop_thread();
  log_debug("[PROTOCOL_STOP] 5. audio_stop_thread() returned");

  // Early return if data thread was never created (e.g., mirror mode)
  // In mirror mode, we only need to stop the audio sender (done above)
  if (!g_data_thread_created) {
    log_debug("[PROTOCOL_STOP] 6. Data thread not created, returning");
    return;
  }

  // Stop keepalive/ping thread - it checks connection status and will exit
  log_debug("[PROTOCOL_STOP] 7. About to call keepalive_stop_thread");
  keepalive_stop_thread();
  log_debug("[PROTOCOL_STOP] 8. keepalive_stop_thread() returned");

  // Stop webcam capture thread
  log_debug("[PROTOCOL_STOP] 9. About to call capture_stop_thread");
  capture_stop_thread();
  log_debug("[PROTOCOL_STOP] 10. capture_stop_thread() returned");

  // Wait for data reception thread to exit gracefully
  log_debug("[PROTOCOL_STOP] 11. Waiting for data thread to exit");
  // Thread checks should_exit() every read cycle (typically <1-5ms), so timeout can be much shorter
  int wait_count = 0;
  while (wait_count < 5 && !atomic_load_bool(&g_data_thread_exited)) {
    platform_sleep_us(10 * US_PER_MS_INT); // 10ms * 5 = 50ms max wait
    wait_count++;
  }

  if (!atomic_load_bool(&g_data_thread_exited)) {
    log_warn("Data thread not responding after 50ms - will be joined by thread pool");
  }
  log_debug("[PROTOCOL_STOP] 12. Data thread wait complete");

  // Join all threads in the client worker pool (in stop_id order)
  // This handles the data reception thread and (eventually) all other worker threads
  log_debug("[PROTOCOL_STOP] 13. About to call thread_pool_stop_all");
  if (g_client_worker_pool) {
    asciichat_error_t result = thread_pool_stop_all(g_client_worker_pool);
    if (result != ASCIICHAT_OK) {
      log_error("Failed to stop client worker threads");
      LOG_ERRNO_IF_SET("Thread pool stop failed");
    }
  }
  log_debug("[PROTOCOL_STOP] 14. thread_pool_stop_all() returned");

  g_data_thread_created = false;

#ifdef DEBUG_THREADS
  log_debug("Data reception thread stopped and joined by thread pool");
#endif
  log_debug("[PROTOCOL_STOP] 15. protocol_stop_connection complete");
}

/**
 * Check if connection has been lost
 *
 * @return true if protocol detected connection loss, false otherwise
 *
 * @ingroup client_protocol
 */
bool protocol_connection_lost() {
  return atomic_load_bool(&g_data_thread_exited) || server_connection_is_lost();
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

  log_info("[FRAME_RECV_CALLBACK] 🎬 FRAME_RECEIVED: width=%u, height=%u, data_len=%zu bytes, flags=0x%x",
           header->width, header->height, data_len, header->flags);

  // Reconstruct full packet for existing handler (header + data)
  // IMPORTANT: header is already in HOST byte order from ACIP layer,
  // but handle_ascii_frame_packet() expects NETWORK byte order and does conversion.
  // So we need to convert back to network order before passing.
  size_t total_len = sizeof(*header) + data_len;
  log_debug("[FRAME_RECV_CALLBACK] 📦 FRAME_SIZE: header=%zu + data=%zu = total=%zu", sizeof(*header), data_len,
            total_len);

  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    log_error("[FRAME_RECV_CALLBACK] ❌ ALLOC_FAILED: cannot allocate %zu bytes for frame", total_len);
    return;
  }

  log_debug("[FRAME_RECV_CALLBACK] ✅ BUFFER_ALLOCATED: packet=%p, capacity=%zu", (void *)packet, total_len);

  // Convert header fields back to network byte order for handle_ascii_frame_packet()
  ascii_frame_packet_t net_header = *header;
  net_header.width = HOST_TO_NET_U32(header->width);
  net_header.height = HOST_TO_NET_U32(header->height);
  net_header.original_size = HOST_TO_NET_U32(header->original_size);
  net_header.compressed_size = HOST_TO_NET_U32(header->compressed_size);
  net_header.checksum = HOST_TO_NET_U32(header->checksum);
  net_header.flags = HOST_TO_NET_U32(header->flags);

  log_debug("[FRAME_RECV_CALLBACK] 🔄 BYTE_ORDER_CONVERSION: converting header to network byte order");

  memcpy(packet, &net_header, sizeof(net_header));
  memcpy(packet + sizeof(net_header), frame_data, data_len);

  log_info("[FRAME_RECV_CALLBACK] 📥 FRAME_DISPATCH: calling handle_ascii_frame_packet() with %zu bytes", total_len);
  handle_ascii_frame_packet(packet, total_len);
  log_info("[FRAME_RECV_CALLBACK] ✅ FRAME_DISPATCH_COMPLETE: frame processing finished");

  SAFE_FREE(packet);
  log_debug("[FRAME_RECV_CALLBACK] 🗑️  BUFFER_FREED: frame callback cleanup complete");
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

  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    log_error("Failed to allocate buffer for error packet callback");
    return;
  }

  memcpy(packet, header, sizeof(*header));
  if (msg_len > 0) {
    memcpy(packet + sizeof(*header), message, msg_len);
  }

  handle_error_message_packet(packet, total_len);
  SAFE_FREE(packet);
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

  uint8_t *packet = SAFE_MALLOC(total_len, uint8_t *);
  if (!packet) {
    log_error("Failed to allocate buffer for remote log callback");
    return;
  }

  memcpy(packet, header, sizeof(*header));
  memcpy(packet + sizeof(*header), message, msg_len);

  handle_remote_log_packet(packet, total_len);
  SAFE_FREE(packet);
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
  log_debug("Console cleared by server");
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
  log_debug("Received WebRTC SDP %s from participant (session_id=%.8s...)", sdp_type_str,
            (const char *)sdp->session_id);

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
  log_debug("ACDS session join succeeded (participant_id=%.8s..., session_type=%s, server=%s:%u)",
            (const char *)joined->participant_id, joined->session_type == 1 ? "WebRTC" : "DirectTCP",
            joined->server_address, joined->server_port);

  // Check if this is a WebRTC session
  if (joined->session_type == SESSION_TYPE_WEBRTC) {
    // TODO: Phase 3 - Initialize WebRTC connection with TURN credentials
    // webrtc_initialize_session(joined->session_id, joined->participant_id,
    //                           joined->turn_username, joined->turn_password);
    log_debug("WebRTC session detected - TODO: initialize WebRTC with TURN credentials");
  } else {
    // Direct TCP - connection is already established or will be established
    log_debug("Direct TCP session - using existing connection");
  }
}

/**
 * @brief Handle CRYPTO_KEY_EXCHANGE_INIT from server (start handshake)
 *
 * Server sent its public key, we respond with ours and derive shared secret.
 * This is step 1 of the crypto handshake protocol.
 *
 * @param type Packet type (should be PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT)
 * @param payload Server's public key packet
 * @param payload_len Payload length
 * @param ctx Application context (unused)
 *
 * @ingroup client_protocol
 */
static void acip_on_crypto_key_exchange_init(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  (void)ctx;

  log_debug("Received CRYPTO_KEY_EXCHANGE_INIT from server");

  acip_transport_t *transport = server_connection_get_transport();
  if (!transport) {
    log_error("Cannot handle key exchange - no transport available");
    return;
  }

  asciichat_error_t result =
      crypto_handshake_client_key_exchange(&g_crypto_ctx, transport, type, (const uint8_t *)payload, payload_len);
  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake key exchange failed");
    server_connection_lost();
  } else {
    log_debug("Sent CRYPTO_KEY_EXCHANGE_RESP to server");
  }
}

/**
 * @brief Handle CRYPTO_AUTH_CHALLENGE from server (authenticate)
 *
 * Server sent authentication challenge, we respond with proof of identity.
 * This is step 2 of the crypto handshake protocol.
 *
 * @param type Packet type (should be PACKET_TYPE_CRYPTO_AUTH_CHALLENGE)
 * @param payload Server's auth challenge packet
 * @param payload_len Payload length
 * @param ctx Application context (unused)
 *
 * @ingroup client_protocol
 */
static void acip_on_crypto_auth_challenge(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  (void)ctx;

  log_debug("Received CRYPTO_AUTH_CHALLENGE from server");

  acip_transport_t *transport = server_connection_get_transport();
  if (!transport) {
    log_error("Cannot handle auth challenge - no transport available");
    return;
  }

  asciichat_error_t result =
      crypto_handshake_client_auth_response(&g_crypto_ctx, transport, type, (const uint8_t *)payload, payload_len);
  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake auth response failed");
    server_connection_lost();
  } else {
    log_debug("Sent CRYPTO_AUTH_RESPONSE to server");
  }
}

/**
 * @brief Handle CRYPTO_SERVER_AUTH_RESP from server (mutual authentication)
 *
 * Server proved its identity, we verify and complete handshake.
 * This is step 3 of the crypto handshake protocol (mutual auth mode).
 *
 * @param type Packet type (should be PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP)
 * @param payload Server's auth response packet
 * @param payload_len Payload length
 * @param ctx Application context (unused)
 *
 * @ingroup client_protocol
 */
static void acip_on_crypto_server_auth_resp(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  (void)ctx;

  log_debug("Received CRYPTO_SERVER_AUTH_RESP from server");

  acip_transport_t *transport = server_connection_get_transport();
  if (!transport) {
    log_error("Cannot handle server auth response - no transport available");
    return;
  }

  asciichat_error_t result =
      crypto_handshake_client_complete(&g_crypto_ctx, transport, type, (const uint8_t *)payload, payload_len);
  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake verification failed");
    server_connection_lost();
  } else {
    log_info("Crypto handshake completed successfully (mutual auth)");

    // Link crypto context to transport for automatic encryption
    transport->crypto_ctx = &g_crypto_ctx.crypto_ctx;
  }
}

/**
 * @brief Handle CRYPTO_AUTH_FAILED from server (authentication failed)
 *
 * Server rejected our authentication credentials.
 * Connection cannot proceed - disconnect.
 *
 * @param type Packet type (should be PACKET_TYPE_CRYPTO_AUTH_FAILED)
 * @param payload Error message (optional)
 * @param payload_len Payload length
 * @param ctx Application context (unused)
 *
 * @ingroup client_protocol
 */
static void acip_on_crypto_auth_failed(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  (void)ctx;
  (void)type;

  // Extract error message if present
  char error_msg[256] = "Unknown error";
  if (payload && payload_len > 0) {
    size_t msg_len = payload_len < sizeof(error_msg) - 1 ? payload_len : sizeof(error_msg) - 1;
    memcpy(error_msg, payload, msg_len);
    error_msg[msg_len] = '\0';
  }

  log_error("Server rejected authentication: %s", error_msg);
  log_error("Disconnecting - crypto handshake failed");

  server_connection_lost();
}

/**
 * @brief Handle CRYPTO_HANDSHAKE_COMPLETE from server (handshake success)
 *
 * Server confirmed handshake completion, encryption is now active.
 * This is the final step in simple (non-mutual-auth) handshake mode.
 *
 * @param type Packet type (should be PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE)
 * @param payload Confirmation data (optional)
 * @param payload_len Payload length
 * @param ctx Application context (unused)
 *
 * @ingroup client_protocol
 */
static void acip_on_crypto_handshake_complete(packet_type_t type, const void *payload, size_t payload_len, void *ctx) {
  (void)ctx;

  log_debug("Received CRYPTO_HANDSHAKE_COMPLETE from server");

  acip_transport_t *transport = server_connection_get_transport();
  if (!transport) {
    log_error("Cannot complete handshake - no transport available");
    return;
  }

  asciichat_error_t result =
      crypto_handshake_client_complete(&g_crypto_ctx, transport, type, (const uint8_t *)payload, payload_len);
  if (result != ASCIICHAT_OK) {
    log_error("Crypto handshake completion failed");
    server_connection_lost();
  } else {
    log_info("Crypto handshake completed successfully");

    // Link crypto context to transport for automatic encryption
    transport->crypto_ctx = &g_crypto_ctx.crypto_ctx;
  }
}
