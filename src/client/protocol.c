/**
 * @file protocol.c
 * @brief ASCII-Chat Client Protocol Handler
 *
 * This module implements the client-side protocol handling for ASCII-Chat,
 * managing packet reception, parsing, and dispatch to appropriate handlers.
 * It coordinates the data reception thread and manages protocol-level
 * connection state.
 *
 * ## Protocol Architecture
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
 * Frame packets support optional zlib compression:
 * - **Detection**: Frame flags indicate compression status
 * - **Decompression**: zlib inflation with size validation
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
 * @date 2025
 * @version 2.0
 */

#include "protocol.h"
#include "main.h"
#include "server.h"
#include "display.h"
#include "audio.h"
#include "os/audio.h"

#include "network.h"
#include "buffer_pool.h"
#include "common.h"
#include "options.h"

#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

/* ============================================================================
 * Thread State Management
 * ============================================================================ */

/** Data reception thread handle */
static asciithread_t g_data_thread;

/** Flag indicating if data thread was created */
static bool g_data_thread_created = false;

/** Atomic flag indicating data thread has exited */
static atomic_bool g_data_thread_exited = false;

/* ============================================================================
 * Multi-User Client State
 * ============================================================================ */

/** Remote client tracking (up to MAX_CLIENTS) */
typedef struct {
  uint32_t client_id;
  char display_name[MAX_DISPLAY_NAME_LEN];
  bool is_active;
  time_t last_seen;
} remote_client_info_t;

/** Server state tracking for console clear logic */
static uint32_t g_last_active_count = 0;
static bool g_server_state_initialized = false;
static bool g_should_clear_before_next_frame = false;

/* ============================================================================
 * Packet Handler Functions
 * ============================================================================ */

/**
 * Handle ASCII frame packet from server
 *
 * Processes unified ASCII frame packets that contain both header information
 * and frame data. Supports optional zlib compression with integrity verification.
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
 */
static void handle_ascii_frame_packet(const void *data, size_t len) {
  if (should_exit()) {
    return;
  }

  if (!data || len < sizeof(ascii_frame_packet_t)) {
    log_warn("Invalid ASCII frame packet size: %zu", len);
    return;
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
#ifndef NO_COMPRESSION
    // Compressed frame - decompress it
    if (frame_data_len != header.compressed_size) {
      log_error("Compressed frame size mismatch: expected %u, got %zu", header.compressed_size, frame_data_len);
      return;
    }

    SAFE_MALLOC(frame_data, header.original_size + 1, char *);

    // Decompress using zlib
    uLongf decompressed_size = header.original_size;
    int result = uncompress((Bytef *)frame_data, &decompressed_size, (const Bytef *)frame_data_ptr, frame_data_len);

    if (result != Z_OK || decompressed_size != header.original_size) {
      log_error("Decompression failed: zlib error %d, size %lu vs expected %u", result, decompressed_size,
                header.original_size);
      free(frame_data);
      return;
    }

    frame_data[header.original_size] = '\0';
#ifdef COMPRESSION_DEBUG
    log_debug("Decompressed frame: %zu -> %u bytes", frame_data_len, header.original_size);
#endif
#else
    // Compression is disabled - cannot handle compressed frames
    log_error("Received compressed frame but compression support is disabled");
    return;
#endif
  } else {
    // Uncompressed frame
    if (frame_data_len != header.original_size) {
      log_error("Uncompressed frame size mismatch: expected %u, got %zu", header.original_size, frame_data_len);
      return;
    }

    SAFE_MALLOC(frame_data, frame_data_len + 1, char *);
    memcpy(frame_data, frame_data_ptr, frame_data_len);
    frame_data[frame_data_len] = '\0';
  }

  // Verify checksum
  uint32_t actual_crc = asciichat_crc32(frame_data, header.original_size);
  if (actual_crc != header.checksum) {
    log_error("Frame checksum mismatch: got 0x%x, expected 0x%x", actual_crc, header.checksum);
    free(frame_data);
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
      log_info("Snapshot mode: first frame received, waiting %.2f seconds for webcam warmup...", opt_snapshot_delay);
    } else {
      time_t current_time = time(NULL);
      double elapsed = difftime(current_time, first_frame_time);
      if (elapsed >= opt_snapshot_delay) {
        log_info("Snapshot captured after %.1f seconds!", elapsed);
        take_snapshot = true;
        signal_exit();
      }
    }
  }

  // Check if we need to clear console before rendering this frame
  if (g_should_clear_before_next_frame) {
    display_full_reset();
    g_should_clear_before_next_frame = false;
  }

  // Render frame through display subsystem
  display_render_frame(frame_data, take_snapshot);

  free(frame_data);
}

/**
 * Handle audio packet from server
 *
 * Processes audio sample data with volume boosting and clipping protection.
 * Integrates with audio subsystem for playback queue management.
 *
 * Audio Processing Pipeline:
 * 1. Input validation and size checking
 * 2. Volume boost application (configurable multiplier)
 * 3. Soft clipping to prevent distortion
 * 4. Queue submission to audio playback system
 *
 * @param data Raw audio sample data (float array)
 * @param len Length of data in bytes
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

  // Process audio through audio subsystem
  audio_process_received_samples((const float *)data, num_samples);

#ifdef DEBUG_AUDIO
  log_debug("Processed %d audio samples", num_samples);
#endif
}

/**
 * Handle server state packet for multi-client coordination
 *
 * Processes server state updates that coordinate console clearing logic
 * across multiple client connections. When the active client count changes,
 * triggers console clearing before the next frame to prevent display artifacts.
 *
 * @param data Server state packet data
 * @param len Packet data length
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
  }

  g_last_active_count = active_count;
}

/* ============================================================================
 * Data Reception Thread
 * ============================================================================ */

/**
 * Main data reception thread function
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
 * @param arg Unused thread argument
 * @return NULL on thread exit
 */
static void *data_reception_thread_func(void *arg) {
  (void)arg;

#ifdef DEBUG_THREADS
  log_debug("Data reception thread started");
#endif

  while (!should_exit()) {
    socket_t sockfd = server_connection_get_socket();

    if (sockfd == INVALID_SOCKET_VALUE || !server_connection_is_active()) {
      log_debug("CLIENT: Waiting for socket connection");
      usleep(10 * 1000);
      continue;
    }

    packet_type_t type;
    void *data;
    size_t len;

    int result = receive_packet(sockfd, &type, &data, &len);
    if (result < 0) {
      log_error("CLIENT: Failed to receive packet, errno=%d (%s)", errno, strerror(errno));
      server_connection_lost();
      break;
    } else if (result == 0) {
      log_info("CLIENT: Server closed connection");
      server_connection_lost();
      break;
    }

    // Dispatch packet to appropriate handler
    switch (type) {
    case PACKET_TYPE_ASCII_FRAME:
      handle_ascii_frame_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO:
      handle_audio_packet(data, len);
      break;

    case PACKET_TYPE_PING:
      // Respond with PONG
      if (server_send_pong() < 0) {
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

    default:
      log_warn("Unknown packet type: %d", type);
      break;
    }

    // Clean up packet buffer (only if we actually allocated one)
    if (data && len > 0) {
      buffer_pool_free(data, len);
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
 */
int protocol_start_connection() {
  // Reset protocol state for new connection
  g_server_state_initialized = false;
  g_last_active_count = 0;
  g_should_clear_before_next_frame = false;

  // Start data reception thread
  atomic_store(&g_data_thread_exited, false);
  if (ascii_thread_create(&g_data_thread, data_reception_thread_func, NULL) != 0) {
    log_error("Failed to create data reception thread");
    return -1;
  }

  g_data_thread_created = true;
  return 0;
}

/**
 * Stop protocol connection handling
 *
 * Gracefully shuts down the data reception thread and cleans up
 * protocol state. Safe to call multiple times.
 */
void protocol_stop_connection() {
  if (!g_data_thread_created) {
    return;
  }

  // Signal thread to stop
  signal_exit();

  // Wait for thread to exit gracefully
  int wait_count = 0;
  while (wait_count < 20 && !atomic_load(&g_data_thread_exited)) {
    usleep(100000); // 100ms
    wait_count++;
  }

  if (!atomic_load(&g_data_thread_exited)) {
    log_error("Data thread not responding - forcing join");
  }

  // Join the thread
  ascii_thread_join(&g_data_thread, NULL);
  g_data_thread_created = false;

  log_info("Data reception thread stopped and joined");
}

/**
 * Check if connection has been lost
 *
 * @return true if protocol detected connection loss, false otherwise
 */
bool protocol_connection_lost() {
  return atomic_load(&g_data_thread_exited) || server_connection_is_lost();
}
