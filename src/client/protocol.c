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
#include "capture.h"
#include "audio.h"
#include "audio.h"

#include "network.h"
#include "buffer_pool.h"
#include "common.h"
#include "options.h"
#include "crc32.h"
#include "crypto/handshake.h"
#include "crypto/crypto.h"

// Forward declaration for client crypto functions
bool crypto_client_is_ready(void);
const crypto_context_t* crypto_client_get_context(void);
int crypto_client_decrypt_packet(const uint8_t* ciphertext, size_t ciphertext_len, uint8_t* plaintext, size_t plaintext_size, size_t* plaintext_len);

#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

#ifdef _WIN32
#include "platform/windows_compat.h"
#endif

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
    log_info("CLIENT FPS TRACKING: Expecting %d fps (client's requested rate)", expected_fps);
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
  uint64_t expected_interval_us = 1000000 / expected_fps;
  uint64_t lag_threshold_us = expected_interval_us + (expected_interval_us / 2); // 50% over expected

  // Log error if frame arrived too late
  if (frame_count > 1 && frame_interval_us > lag_threshold_us) {
    log_error("CLIENT FPS LAG: Frame received %.1fms late (expected %.1fms, got %.1fms, actual fps: %.1f)",
              (float)(frame_interval_us - expected_interval_us) / 1000.0f, (float)expected_interval_us / 1000.0f,
              (float)frame_interval_us / 1000.0f, 1000000.0f / frame_interval_us);
  }

  // Report FPS every 5 seconds
  uint64_t elapsed_us =
      ((uint64_t)current_time.tv_sec * 1000000 + (uint64_t)current_time.tv_nsec / 1000) -
      ((uint64_t)last_fps_report_time.tv_sec * 1000000 + (uint64_t)last_fps_report_time.tv_nsec / 1000);

  if (elapsed_us >= 5000000) { // 5 seconds
    float actual_fps = (float)frame_count / ((float)elapsed_us / 1000000.0f);
    log_info("CLIENT FPS: %.1f fps (%llu frames in %.1f seconds)", actual_fps, frame_count,
             (float)elapsed_us / 1000000.0f);

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
  } else {
    // Uncompressed frame
    if (frame_data_len != header.original_size) {
      log_error("Uncompressed frame size mismatch: expected %u, got %zu", header.original_size, frame_data_len);
      return;
    }

    // Ensure we don't have buffer overflow - use the actual header size for allocation
    size_t alloc_size = header.original_size + 1;
    SAFE_MALLOC(frame_data, alloc_size, char *);

    // Only copy the actual amount of data we received
    size_t copy_size = (frame_data_len > header.original_size) ? header.original_size : frame_data_len;
    memcpy(frame_data, frame_data_ptr, copy_size);
    frame_data[header.original_size] = '\0';
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
  // IMPORTANT: We track if this is the first frame to ensure proper initialization
  static bool first_frame_rendered = false;

  if (!first_frame_rendered) {
    // Always clear display and disable logging before rendering the first frame
    // This ensures clean ASCII display regardless of packet arrival order
    log_info("CLIENT_DISPLAY: First frame - clearing display and disabling terminal logging");
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
      free(frame_data);
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
    long render_interval_ms = 1000 / client_display_fps;

    struct timespec current_time;
    (void)clock_gettime(CLOCK_MONOTONIC, &current_time);

    // Calculate elapsed time since last render
    long elapsed_ms = 0;
    if (last_render_time.tv_sec != 0 || last_render_time.tv_nsec != 0) {
      elapsed_ms = (current_time.tv_sec - last_render_time.tv_sec) * 1000 +
                   (current_time.tv_nsec - last_render_time.tv_nsec) / 1000000;
    }

    // Skip rendering if not enough time has passed (frame rate limiting)
    if (elapsed_ms > 0 && elapsed_ms < render_interval_ms) {
      // Drop this frame to maintain display FPS limit
      free(frame_data);
      return;
    }

    // Update last render time
    last_render_time = current_time;
  }

  log_debug("CLIENT_RENDER: Calling display_render_frame with %u bytes", header.original_size);
  display_render_frame(frame_data, take_snapshot);
  log_debug("CLIENT_RENDER: Frame rendered successfully");

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
    // Clear terminal before the very first frame
    g_should_clear_before_next_frame = true;
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
      platform_sleep_usec(10 * 1000);
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
    }
    if (result == 0) {
      log_info("CLIENT: Server closed connection");
      server_connection_lost();
      break;
    }

    // DEBUG: Log all packet types received
    log_debug("CLIENT_RECV: Received packet type=%d, len=%zu", type, len);

    switch (type) {
    case PACKET_TYPE_ENCRYPTED:
      // Decrypt the encrypted packet from server
      if (crypto_client_is_ready()) {
        log_debug("CLIENT_RECV: Processing encrypted packet from server, len=%zu", len);

        // Allocate buffer for decrypted data
        void* decrypted_data = buffer_pool_alloc(len);
        if (!decrypted_data) {
          log_error("Failed to allocate buffer for decrypted packet from server");
          break;
        }

        size_t decrypted_len;
        int decrypt_result = crypto_client_decrypt_packet((const uint8_t*)data, len,
                                                         (uint8_t*)decrypted_data, len, &decrypted_len);

        if (decrypt_result != 0) {
          log_error("Failed to decrypt packet from server (result=%d)", decrypt_result);
          buffer_pool_free(decrypted_data, len);
          break;
        }

        // Parse the decrypted packet header to determine the actual packet type
        if (decrypted_len >= sizeof(packet_header_t)) {
          packet_header_t* decrypted_header = (packet_header_t*)decrypted_data;
          packet_type_t decrypted_type = (packet_type_t)ntohs(decrypted_header->type);

          // Extract the actual payload (skip the header)
          void* payload = (uint8_t*)decrypted_data + sizeof(packet_header_t);
          size_t payload_len = decrypted_len - sizeof(packet_header_t);

          log_debug("CLIENT_RECV: Decrypted packet type=%d, payload_len=%zu", decrypted_type, payload_len);

          // Process the decrypted packet based on its type
          switch (decrypted_type) {
          case PACKET_TYPE_ASCII_FRAME:
            handle_ascii_frame_packet(payload, payload_len);
            break;
          case PACKET_TYPE_AUDIO:
            handle_audio_packet(payload, payload_len);
            break;
          case PACKET_TYPE_CLEAR_CONSOLE:
            display_full_reset();
            log_info("Console cleared by server");
            break;
          case PACKET_TYPE_SERVER_STATE:
            handle_server_state_packet(payload, payload_len);
            break;
          default:
            log_warn("Unknown decrypted packet type: %d", decrypted_type);
            break;
          }
        } else {
          log_error("Decrypted packet too small for header from server");
        }

        buffer_pool_free(decrypted_data, len);
      } else {
        log_error("Received encrypted packet but crypto not ready");
      }
      break;

    case PACKET_TYPE_ASCII_FRAME:
      log_debug("CLIENT_RECV: Processing ASCII_FRAME packet, len=%zu", len);
      handle_ascii_frame_packet(data, len);
      break;

    case PACKET_TYPE_AUDIO:
      handle_audio_packet(data, len);
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

  // Reset display state for new connection
  display_reset_for_new_connection();

#ifdef DEBUG_THREADS
  log_info("DEBUG: Starting protocol connection - creating threads");
#endif

  // Start data reception thread
  atomic_store(&g_data_thread_exited, false);
  if (ascii_thread_create(&g_data_thread, data_reception_thread_func, NULL) != 0) {
    log_error("Failed to create data reception thread");
    return -1;
  }
#ifdef DEBUG_THREADS
  log_info("DEBUG: Data reception thread created successfully");
#endif

  // Start webcam capture thread
  if (capture_start_thread() != 0) {
    log_error("Failed to start webcam capture thread");
    return -1;
  }
#ifdef DEBUG_THREADS
  log_info("DEBUG: Webcam capture thread started successfully");
#endif

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

#ifdef DEBUG_THREADS
  log_info("DEBUG: Stopping protocol connection - stopping threads");
#endif

  // Don't call signal_exit() here - that's for global shutdown only!
  // We just want to stop threads for this connection, not exit the entire client

  // Shutdown the socket to interrupt any blocking recv() in data thread
  server_connection_shutdown();

  // Stop webcam capture thread
  capture_stop_thread();
#ifdef DEBUG_THREADS
  log_info("DEBUG: Webcam capture thread stopped");
#endif

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
 */
bool protocol_connection_lost() {
  return atomic_load(&g_data_thread_exited) || server_connection_is_lost();
}
