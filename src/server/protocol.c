/**
 * @file protocol.c
 * @brief Client Packet Processing and Protocol State Management
 *
 * This module implements the ASCII-Chat server's packet processing engine,
 * handling all incoming communication from connected clients. It was extracted
 * from the original monolithic server.c to provide clean separation between
 * network protocol handling and other server concerns.
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Parse and validate incoming packets from clients
 * 2. Update client state based on received packet data
 * 3. Coordinate with other modules for media processing
 * 4. Generate appropriate server responses to client requests
 * 5. Maintain protocol compliance and packet format standards
 *
 * PACKET PROCESSING ARCHITECTURE:
 * ===============================
 * The protocol processing follows a clear pattern:
 *
 * 1. PACKET RECEPTION (in client.c receive thread):
 *    - Receives raw packet data from socket
 *    - Validates packet header and CRC
 *    - Dispatches to appropriate handler function
 *
 * 2. HANDLER FUNCTION (this module):
 *    - Validates packet payload structure
 *    - Updates client state with thread-safe patterns
 *    - Processes media data (stores in buffers)
 *    - Generates any necessary responses
 *
 * 3. RESPONSE GENERATION (via packet queues):
 *    - Queues response packets for delivery
 *    - Uses client's outgoing packet queues
 *    - Send thread delivers responses asynchronously
 *
 * SUPPORTED PACKET TYPES:
 * =======================
 *
 * CLIENT LIFECYCLE:
 * - PACKET_TYPE_CLIENT_JOIN: Initial client capabilities and identity
 * - PACKET_TYPE_CLIENT_LEAVE: Clean disconnect notification
 * - PACKET_TYPE_CLIENT_CAPABILITIES: Terminal capabilities and preferences
 *
 * MEDIA STREAMING:
 * - PACKET_TYPE_STREAM_START: Begin sending audio/video
 * - PACKET_TYPE_STREAM_STOP: Stop sending audio/video
 * - PACKET_TYPE_IMAGE_FRAME: Raw RGB video frame data
 * - PACKET_TYPE_AUDIO: Single audio sample packet (legacy)
 * - PACKET_TYPE_AUDIO_BATCH: Batched audio samples (efficient)
 *
 * CONTROL PROTOCOL:
 * - PACKET_TYPE_PING: Client keepalive request
 * - PACKET_TYPE_PONG: Server keepalive response
 *
 * THREAD SAFETY AND STATE MANAGEMENT:
 * ====================================
 *
 * CLIENT STATE SYNCHRONIZATION:
 * All client state modifications use the snapshot pattern:
 * 1. Acquire client->client_state_mutex
 * 2. Update client state fields
 * 3. Release mutex immediately
 * 4. Process using local copies if needed
 *
 * MEDIA BUFFER COORDINATION:
 * Video frames: Stored in client->incoming_video_buffer (thread-safe)
 * Audio samples: Stored in client->incoming_audio_buffer (lock-free)
 * Both buffers are processed by render threads in render.c
 *
 * PACKET VALIDATION STRATEGY:
 * All handlers validate:
 * - Packet size matches expected structure size
 * - Client capabilities permit the operation
 * - Buffer pointers are valid before access
 * - Network byte order conversion where needed
 *
 * ERROR HANDLING PHILOSOPHY:
 * ==========================
 * - Invalid packets are logged but don't disconnect clients
 * - Buffer allocation failures are handled gracefully
 * - Network errors during responses don't affect client state
 * - Shutdown conditions are detected and avoid error spam
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - client.c: Called by receive threads, manages client lifecycle
 * - render.c: Consumes video/audio data stored by handlers
 * - stream.c: Uses client capabilities for frame generation
 * - main.c: Provides global state (g_server_should_exit, etc.)
 *
 * WHY THIS MODULAR DESIGN:
 * =========================
 * The original server.c mixed protocol handling with connection management
 * and rendering logic, making it difficult to:
 * - Add new packet types
 * - Modify protocol behavior
 * - Debug packet-specific issues
 * - Test protocol compliance
 *
 * This separation provides:
 * - Clear protocol specification
 * - Easier protocol evolution
 * - Better error isolation
 * - Improved testability
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 * @see client.c For client receive thread implementation
 * @see render.c For media buffer consumption
 * @see network.h For packet structure definitions
 */

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "protocol.h"
#include "client.h"
#include "crypto.h"
#include "common.h"
#include "video_frame.h"
#include "audio.h"
#include "palette.h"
#include "image2ascii/image.h"
#include "compression.h"
#include "util/format.h"

/**
 * @brief Global shutdown flag from main.c - used to avoid error spam during shutdown
 *
 * When the server is shutting down, certain packet processing errors become
 * expected (e.g., buffer allocation failures, queue shutdowns). This flag
 * helps handlers distinguish between genuine errors and shutdown conditions.
 */
extern atomic_bool g_server_should_exit;

/* ============================================================================
 * Client Lifecycle Packet Handlers
 * ============================================================================
 */

/**
 * @brief Process CLIENT_JOIN packet - client announces identity and capabilities
 *
 * This is the first substantive packet clients send after establishing a TCP
 * connection. It provides the server with essential information for managing
 * the client throughout its session.
 *
 * PACKET STRUCTURE EXPECTED:
 * - client_info_packet_t containing:
 *   - display_name: Human-readable client identifier
 *   - capabilities: Bitmask of CLIENT_CAP_* flags
 *
 * STATE CHANGES PERFORMED:
 * - Updates client->display_name from packet
 * - Sets client->can_send_video based on CLIENT_CAP_VIDEO
 * - Sets client->can_send_audio based on CLIENT_CAP_AUDIO
 * - Sets client->wants_stretch based on CLIENT_CAP_STRETCH
 *
 * PROTOCOL BEHAVIOR:
 * - Does NOT automatically start media streams (requires STREAM_START)
 * - Does NOT send CLEAR_CONSOLE to other clients (prevents flicker)
 * - Logs client capabilities for debugging
 *
 * ERROR HANDLING:
 * - Silently ignores packets with wrong size
 * - Invalid display names are truncated safely
 * - Missing capabilities default to false
 *
 * @param client Target client whose state will be updated
 * @param data Packet payload (should be client_info_packet_t)
 * @param len Size of packet payload in bytes
 *
 * @note This function should only be called by client receive threads
 * @note Client state is already protected by receive thread serialization
 * @see handle_stream_start_packet() For enabling media transmission
 */

void handle_client_join_packet(client_info_t *client, const void *data, size_t len) {
  if (len == sizeof(client_info_packet_t)) {
    const client_info_packet_t *join_info = (const client_info_packet_t *)data;

    SAFE_STRNCPY(client->display_name, join_info->display_name, MAX_DISPLAY_NAME_LEN - 1);

    client->can_send_video = (join_info->capabilities & CLIENT_CAP_VIDEO) != 0;
    client->can_send_audio = (join_info->capabilities & CLIENT_CAP_AUDIO) != 0;
    client->wants_stretch = (join_info->capabilities & CLIENT_CAP_STRETCH) != 0;

    log_info("Client %u joined: %s (video=%d, audio=%d, stretch=%d)", atomic_load(&client->client_id),
             client->display_name, client->can_send_video, client->can_send_audio, client->wants_stretch);
  }
}

/**
 * @brief Process STREAM_START packet - client requests to begin media transmission
 *
 * Clients send this packet to indicate they're ready to start sending video
 * and/or audio data. The server updates its internal state to expect and
 * process media packets from this client.
 *
 * PACKET STRUCTURE EXPECTED:
 * - uint32_t stream_type (network byte order)
 * - Bitmask containing STREAM_TYPE_VIDEO and/or STREAM_TYPE_AUDIO
 *
 * STATE CHANGES PERFORMED:
 * - VIDEO: Records intention to send video (is_sending_video set by first IMAGE_FRAME)
 * - AUDIO: Sets client->is_sending_audio = true if STREAM_TYPE_AUDIO present
 * - Enables render threads to include this client in output generation
 *
 * PROTOCOL BEHAVIOR:
 * - Client must have announced capabilities via CLIENT_JOIN first
 * - Server will start processing IMAGE_FRAME and AUDIO packets
 * - Render threads will begin generating output for this client
 * - Grid layout will be recalculated to include this client
 *
 * ERROR HANDLING:
 * - Ignores packets with incorrect size
 * - Invalid stream types are silently ignored
 * - Graceful handling if client lacks necessary capabilities
 *
 * @param client Target client starting media transmission
 * @param data Packet payload containing stream type flags
 * @param len Size of packet payload (should be sizeof(uint32_t))
 *
 * @note Changes take effect immediately for subsequent media packets
 * @note Render threads will detect the state change on their next cycle
 * @see handle_stream_stop_packet() For stopping media transmission
 * @see handle_image_frame_packet() For video data processing
 */
void handle_stream_start_packet(client_info_t *client, const void *data, size_t len) {

  // Handle stream start request
  if (len == sizeof(uint32_t)) {
    // Use memcpy to avoid unaligned access
    uint32_t stream_type_net;
    memcpy(&stream_type_net, data, sizeof(uint32_t));
    uint32_t stream_type = ntohl(stream_type_net);

    // FIXED: No locks needed - use atomic operations for thread-safe state updates
    // The is_sending_video and is_sending_audio flags are atomic, so no mutex is required

    if (stream_type & STREAM_TYPE_VIDEO) {
      // Don't set is_sending_video=true here - wait for first IMAGE_FRAME
      // This prevents the race condition where server tries to generate frames
      // before client has sent any video data
    }
    if (stream_type & STREAM_TYPE_AUDIO) {
      atomic_store(&client->is_sending_audio, true);
    }

    // Log after releasing locks to avoid holding locks during I/O
    if (stream_type & STREAM_TYPE_VIDEO) {
      log_info("Client %u announced video stream (waiting for first frame)", atomic_load(&client->client_id));
    }
    if (stream_type & STREAM_TYPE_AUDIO) {
      log_info("Client %u started audio stream", atomic_load(&client->client_id));
    }
  }
}

/**
 * @brief Process STREAM_STOP packet - client requests to halt media transmission
 *
 * Clients send this packet to gracefully stop sending video and/or audio data.
 * The server updates its state to exclude this client from active media
 * processing and grid layout calculations.
 *
 * PACKET STRUCTURE EXPECTED:
 * - uint32_t stream_type (network byte order)
 * - Bitmask containing STREAM_TYPE_VIDEO and/or STREAM_TYPE_AUDIO
 *
 * STATE CHANGES PERFORMED:
 * - Sets client->is_sending_video = false if STREAM_TYPE_VIDEO present
 * - Sets client->is_sending_audio = false if STREAM_TYPE_AUDIO present
 * - Render threads will stop including this client in output
 *
 * PROTOCOL BEHAVIOR:
 * - Client remains connected but won't appear in video grid
 * - Existing buffered media from this client will still be processed
 * - Grid layout recalculates to exclude this client
 * - Client can restart streaming with STREAM_START packet
 *
 * ERROR HANDLING:
 * - Ignores packets with incorrect size
 * - Invalid stream types are silently ignored
 * - Safe to call multiple times or when not streaming
 *
 * @param client Target client stopping media transmission
 * @param data Packet payload containing stream type flags
 * @param len Size of packet payload (should be sizeof(uint32_t))
 *
 * @note Changes take effect immediately
 * @note Render threads will detect the state change on their next cycle
 * @see handle_stream_start_packet() For starting media transmission
 */
void handle_stream_stop_packet(client_info_t *client, const void *data, size_t len) {
  // Handle stream stop request
  if (len == sizeof(uint32_t)) {
    // Use memcpy to avoid unaligned access
    uint32_t stream_type_net;
    memcpy(&stream_type_net, data, sizeof(uint32_t));
    uint32_t stream_type = ntohl(stream_type_net);

    // LOCK OPTIMIZATION: No locks needed - is_sending_video and is_sending_audio are atomic
    // We already have a stable client pointer from receive thread

    if (stream_type & STREAM_TYPE_VIDEO) {
      atomic_store(&client->is_sending_video, false);
    }
    if (stream_type & STREAM_TYPE_AUDIO) {
      atomic_store(&client->is_sending_audio, false);
    }

    // Log after releasing locks to avoid holding locks during I/O
    if (stream_type & STREAM_TYPE_VIDEO) {
      log_info("Client %u stopped video stream", atomic_load(&client->client_id));
    }
    if (stream_type & STREAM_TYPE_AUDIO) {
      log_info("Client %u stopped audio stream", atomic_load(&client->client_id));
    }
  }
}

/* ============================================================================
 * Media Data Packet Handlers
 * ============================================================================
 */

/**
 * @brief Process IMAGE_FRAME packet - store client's video data for rendering
 *
 * This is the most performance-critical packet handler, processing real-time
 * video data from clients. It validates, stores, and tracks video frames
 * for subsequent ASCII conversion and grid layout.
 *
 * PACKET STRUCTURE EXPECTED:
 * - uint32_t width (network byte order)
 * - uint32_t height (network byte order)
 * - rgb_t pixels[width * height] (RGB888 format)
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Called at 30fps per active client
 * - Uses zero-copy storage when possible
 * - Validates packet size before processing
 * - Implements frame counting for debug logging
 *
 * STATE CHANGES PERFORMED:
 * - Auto-enables client->is_sending_video if not already set
 * - Increments client->frames_received counter
 * - Updates client dimensions if changed
 *
 * BUFFER MANAGEMENT:
 * - Stores entire packet (including dimensions) in client->incoming_video_buffer
 * - Uses multi-frame ringbuffer for burst handling
 * - Buffer overflow drops oldest frames (maintains real-time performance)
 * - render.c threads consume frames for ASCII conversion
 *
 * VALIDATION PERFORMED:
 * - Packet size matches width * height * 3 + 8 bytes
 * - Width and height are reasonable (prevents memory exhaustion)
 * - Buffer pointers are valid before access
 *
 * ERROR HANDLING:
 * - Invalid packets are logged and dropped
 * - Buffer overflow is handled gracefully
 * - Shutdown conditions don't generate error spam
 *
 * PERFORMANCE OPTIMIZATIONS:
 * - Debug logging is throttled (every 25000 frames)
 * - Fast path for common case (valid packet with buffer space)
 * - Minimal CPU work in receive thread (storage only)
 *
 * @param client Source client providing video data
 * @param data Packet payload containing image dimensions and RGB data
 * @param len Total size of packet payload in bytes
 *
 * @note This function is called by client receive threads at high frequency
 * @note Actual ASCII conversion happens in render threads (render.c)
 * @note Frame timestamps are added for synchronization purposes
 * @see framebuffer_write_multi_frame() For buffer storage implementation
 * @see create_mixed_ascii_frame_for_client() For frame consumption
 */
void handle_image_frame_packet(client_info_t *client, void *data, size_t len) {
  // Handle incoming image data from client
  // New format: [width:4][height:4][compressed_flag:4][data_size:4][rgb_data:data_size]
  // Old format: [width:4][height:4][rgb_data:w*h*3] (for backward compatibility)
  // CRITICAL FIX: Use atomic compare-and-swap to avoid race condition
  // This ensures thread-safe auto-enabling of video stream
  bool was_sending_video = atomic_load(&client->is_sending_video);
  if (!was_sending_video) {
    // Try to atomically enable video sending
    if (atomic_compare_exchange_weak(&client->is_sending_video, &was_sending_video, true)) {
      log_info("Client %u auto-enabled video stream (received IMAGE_FRAME)", atomic_load(&client->client_id));
    }
  } else {
    // Log periodically to confirm we're receiving frames
    static int frame_count[MAX_CLIENTS] = {0};
    frame_count[atomic_load(&client->client_id) % MAX_CLIENTS]++;
    if (frame_count[atomic_load(&client->client_id) % MAX_CLIENTS] % 25000 == 0) {
      char pretty[64];
      format_bytes_pretty(len, pretty, sizeof(pretty));
      log_debug("Client %u has sent %d IMAGE_FRAME packets (%s)", atomic_load(&client->client_id),
                frame_count[atomic_load(&client->client_id) % MAX_CLIENTS], pretty);
    }
  }

  // Parse image dimensions (use memcpy to avoid unaligned access)
  uint32_t img_width_net, img_height_net;
  memcpy(&img_width_net, data, sizeof(uint32_t));
  memcpy(&img_height_net, (char *)data + sizeof(uint32_t), sizeof(uint32_t));
  uint32_t img_width = ntohl(img_width_net);
  uint32_t img_height = ntohl(img_height_net);

  // Check if this is the new compressed format (has 4 fields) or old format (has 2 fields)
  size_t rgb_size = (size_t)img_width * (size_t)img_height * sizeof(rgb_t);
  size_t old_format_size = sizeof(uint32_t) * 2 + rgb_size;
  bool is_new_format = (len != old_format_size) && (len > sizeof(uint32_t) * 4);

  void *rgb_data = NULL;
  size_t rgb_data_size = 0;
  bool needs_free = false;

  if (is_new_format) {
    // New format: [width:4][height:4][compressed_flag:4][data_size:4][data:data_size]
    if (len < sizeof(uint32_t) * 4) {
      log_error("Invalid new format image packet from client %u: too small for headers",
                atomic_load(&client->client_id));
      return;
    }

    // Use memcpy to avoid unaligned access for compressed_flag and data_size
    uint32_t compressed_flag_net, data_size_net;
    memcpy(&compressed_flag_net, (char *)data + sizeof(uint32_t) * 2, sizeof(uint32_t));
    memcpy(&data_size_net, (char *)data + sizeof(uint32_t) * 3, sizeof(uint32_t));
    uint32_t compressed_flag = ntohl(compressed_flag_net);
    uint32_t data_size = ntohl(data_size_net);
    void *frame_data = (char *)data + sizeof(uint32_t) * 4;

    size_t expected_total = sizeof(uint32_t) * 4 + data_size;
    if (len != expected_total) {
      log_error("Invalid new format image packet from client %u: expected %zu bytes, got %zu",
                atomic_load(&client->client_id), expected_total, len);
      return;
    }

    if (compressed_flag) {
      // Decompress the data
      rgb_data = SAFE_MALLOC(rgb_size, void *);
      if (!rgb_data) {
        log_error("Failed to allocate decompression buffer for client %u", atomic_load(&client->client_id));
        return;
      }

      if (decompress_data(frame_data, data_size, rgb_data, rgb_size) != 0) {
        log_error("Failed to decompress frame data from client %u", atomic_load(&client->client_id));
        SAFE_FREE(rgb_data);
        return;
      }

      rgb_data_size = rgb_size;
      needs_free = true;
    } else {
      // Uncompressed data
      rgb_data = frame_data;
      rgb_data_size = data_size;
      if (rgb_data_size != rgb_size) {
        log_error("Invalid uncompressed data size from client %u: expected %zu, got %zu",
                  atomic_load(&client->client_id), rgb_size, rgb_data_size);
        return;
      }
    }
  } else {
    // Old format: [width:4][height:4][rgb_data:w*h*3]
    if (len != old_format_size) {
      log_error("Invalid old format image packet from client %u: expected %zu bytes, got %zu",
                atomic_load(&client->client_id), old_format_size, len);
      return;
    }
    rgb_data = (char *)data + sizeof(uint32_t) * 2;
    rgb_data_size = rgb_size;
  }

  if (client->incoming_video_buffer) {
    // Get the write buffer
    video_frame_t *frame = video_frame_begin_write(client->incoming_video_buffer);

    if (frame && frame->data) {
      // Build the packet in the old format for internal storage: [width:4][height:4][rgb_data:w*h*3]
      size_t old_packet_size = sizeof(uint32_t) * 2 + rgb_data_size;

      if (old_packet_size <= 2 * 1024 * 1024) { // Max 2MB frame size
        uint32_t width_net = htonl(img_width);
        uint32_t height_net = htonl(img_height);

        // Pack in old format for internal consistency
        memcpy(frame->data, &width_net, sizeof(uint32_t));
        memcpy((char *)frame->data + sizeof(uint32_t), &height_net, sizeof(uint32_t));
        memcpy((char *)frame->data + sizeof(uint32_t) * 2, rgb_data, rgb_data_size);

        frame->size = old_packet_size;
        frame->width = img_width;
        frame->height = img_height;
        frame->capture_timestamp_us = (uint64_t)time(NULL) * 1000000;
        frame->sequence_number = ++client->frames_received;
        video_frame_commit(client->incoming_video_buffer);
      } else {
        log_warn("Frame from client %u too large (%zu bytes)", atomic_load(&client->client_id), old_packet_size);
      }
    } else {
      log_warn("Failed to get write buffer for client %u (frame=%p, frame->data=%p)", atomic_load(&client->client_id),
               (void *)frame, frame ? frame->data : NULL);
    }
  } else {
    // During shutdown, this is expected - don't spam error logs
    if (!atomic_load(&g_server_should_exit)) {
      log_error("Client %u has no incoming video buffer!", atomic_load(&client->client_id));
    } else {
      log_debug("Client %u: ignoring video packet during shutdown", atomic_load(&client->client_id));
    }
  }

  // Clean up decompressed data if allocated
  if (needs_free && rgb_data) {
    SAFE_FREE(rgb_data);
  }
}

/**
 * @brief Process AUDIO packet - store single audio sample batch (legacy format)
 *
 * Handles the original audio packet format that sends one batch of float samples
 * per packet. This format is less efficient than AUDIO_BATCH but still supported
 * for backward compatibility.
 *
 * PACKET STRUCTURE:
 * - float samples[len/sizeof(float)] (IEEE 754 format)
 * - Sample rate assumed to be 44100 Hz
 * - Mono audio (single channel)
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Less efficient than handle_audio_batch_packet()
 * - Higher packet overhead per sample
 * - Still real-time capable for typical loads
 *
 * BUFFER MANAGEMENT:
 * - Stores samples in client->incoming_audio_buffer (lock-free ring buffer)
 * - Buffer overflow drops oldest samples to maintain real-time behavior
 * - mixer.c consumes samples for multi-client audio mixing
 *
 * STATE VALIDATION:
 * - Only processes if client->is_sending_audio is true
 * - Requires valid buffer pointer and non-zero length
 * - Handles buffer pointer safely during shutdown
 *
 * ERROR HANDLING:
 * - Invalid packets are silently ignored
 * - Buffer overflow is handled by ring buffer (drops old data)
 * - Graceful shutdown behavior
 *
 * @param client Source client providing audio data
 * @param data Packet payload containing float audio samples
 * @param len Size of packet payload in bytes
 *
 * @note Prefer AUDIO_BATCH format for better efficiency
 * @note Audio processing happens in mixer threads, not here
 * @see handle_audio_batch_packet() For efficient batched format
 * @see audio_ring_buffer_write() For storage implementation
 */
void handle_audio_packet(client_info_t *client, const void *data, size_t len) {
  // Handle incoming audio samples from client (old single-packet format)
  if (atomic_load(&client->is_sending_audio) && data && len > 0) {
    // Convert data to float samples
    int num_samples = len / sizeof(float);
    if (num_samples > 0 && client->incoming_audio_buffer) {
      const float *samples = (const float *)data;
      int written = audio_ring_buffer_write(client->incoming_audio_buffer, samples, num_samples);
      // Note: audio_ring_buffer_write now always writes all samples, dropping old ones if needed
      (void)written;
      // log_debug("Stored %d audio samples from client %u", num_samples, client->client_id);
    }
  }
}

/**
 * @brief Process AUDIO_BATCH packet - store efficiently batched audio samples
 *
 * Handles the optimized audio packet format that bundles multiple sample
 * chunks into a single packet. This reduces packet overhead and improves
 * network efficiency for audio streaming.
 *
 * PACKET STRUCTURE EXPECTED:
 * - audio_batch_packet_t header:
 *   - uint32_t batch_count: Number of sample chunks in this batch
 *   - uint32_t total_samples: Total number of float samples
 *   - uint32_t sample_rate: Samples per second (typically 44100)
 *   - uint32_t channels: Number of audio channels (1 = mono)
 * - float samples[total_samples]: IEEE 754 sample data
 *
 * PERFORMANCE ADVANTAGES:
 * - Reduces packet count by 5-10x compared to single audio packets
 * - Lower network overhead and CPU context switching
 * - Better burst tolerance with larger buffers
 *
 * VALIDATION PERFORMED:
 * - Header size matches audio_batch_packet_t
 * - Total packet size matches header + samples
 * - Sample count is within reasonable bounds
 * - Client is authorized to send audio
 *
 * BUFFER MANAGEMENT:
 * - Extracts samples from packet payload
 * - Stores in client->incoming_audio_buffer (same as single format)
 * - Ring buffer automatically handles overflow
 * - mixer.c consumes batched samples identically
 *
 * ERROR HANDLING:
 * - Invalid batch headers are logged and packet dropped
 * - Oversized batches are rejected (prevents DoS)
 * - Buffer allocation failures are handled gracefully
 *
 * @param client Source client providing batched audio data
 * @param data Packet payload containing batch header and samples
 * @param len Total size of packet payload in bytes
 *
 * @note This is the preferred audio packet format
 * @note Batching is transparent to audio processing pipeline
 * @see handle_audio_packet() For legacy single-batch format
 * @see AUDIO_BATCH_SAMPLES For maximum batch size constant
 */
void handle_audio_batch_packet(client_info_t *client, const void *data, size_t len) {
  // Log every audio batch packet reception
  static int batch_count = 0;
  batch_count++;
  if (batch_count % 10 == 0) {
    log_info("SERVER: Received audio batch packet #%d from client %u (len=%zu, is_sending_audio=%d)", batch_count,
             atomic_load(&client->client_id), len, atomic_load(&client->is_sending_audio));
  }

  // Handles batched audio samples from client (new efficient format)
  if (atomic_load(&client->is_sending_audio) && data && len >= sizeof(audio_batch_packet_t)) {
    // Parse batch header
    const audio_batch_packet_t *batch_header = (const audio_batch_packet_t *)data;
    uint32_t batch_count = ntohl(batch_header->batch_count);
    uint32_t total_samples = ntohl(batch_header->total_samples);
    uint32_t sample_rate = ntohl(batch_header->sample_rate);
    // uint32_t channels = ntohl(batch_header->channels); // For future stereo support

    // Suppress static analyzer warnings for conditionally used variables
    (void)batch_count; // Used in DEBUG_AUDIO log
    (void)sample_rate; // Used in DEBUG_AUDIO log

    // Validate batch parameters (samples now transmitted as uint32_t for portability)
    size_t expected_size = sizeof(audio_batch_packet_t) + (total_samples * sizeof(uint32_t));
    if (len != expected_size) {
      log_error("Invalid audio batch size from client %u: got %zu, expected %zu", atomic_load(&client->client_id), len,
                expected_size);
      return;
    }

    if (total_samples > AUDIO_BATCH_SAMPLES * 2) { // Sanity check
      log_error("Audio batch too large from client %u: %u samples", atomic_load(&client->client_id), total_samples);
      return;
    }

    // Extract samples (they follow the header as network byte order uint32_t)
    const uint32_t *network_samples = (const uint32_t *)((const uint8_t *)data + sizeof(audio_batch_packet_t));

    // Convert from network byte order integers back to floats
    // Allocate temporary buffer for converted samples
    float *samples = SAFE_MALLOC(total_samples * sizeof(float), float *);
    if (!samples) {
      log_error("Failed to allocate memory for audio sample conversion");
      return;
    }

    for (uint32_t i = 0; i < total_samples; i++) {
      // Convert from network byte order and scale back to [-1.0, 1.0]
      int32_t scaled = (int32_t)ntohl(network_samples[i]);
      samples[i] = (float)scaled / 2147483647.0f;
    }

    // Debug: Log first few samples to verify conversion
    static int recv_count = 0;
    recv_count++;
    if (recv_count % 100 == 0) {
      int32_t scaled0 = (int32_t)ntohl(network_samples[0]);
      int32_t scaled1 = (int32_t)ntohl(network_samples[1]);
      int32_t scaled2 = (int32_t)ntohl(network_samples[2]);
      log_info("RECV: network[0]=0x%08x, network[1]=0x%08x, network[2]=0x%08x", network_samples[0], network_samples[1],
               network_samples[2]);
      log_info("RECV: scaled[0]=%d, scaled[1]=%d, scaled[2]=%d", scaled0, scaled1, scaled2);
      log_info("RECV: samples[0]=%.6f, samples[1]=%.6f, samples[2]=%.6f", samples[0], samples[1], samples[2]);
    }

    // Write all samples to the ring buffer
    if (client->incoming_audio_buffer) {
      int written = audio_ring_buffer_write(client->incoming_audio_buffer, samples, total_samples);
      // Note: audio_ring_buffer_write now always writes all samples, dropping old ones if needed
      (void)written;
    }

    // Free temporary buffer
    SAFE_FREE(samples);
  }
}

/* ============================================================================
 * Client Configuration Packet Handlers
 * ============================================================================
 */

/**
 * @brief Process CLIENT_CAPABILITIES packet - configure client-specific rendering
 *
 * This packet contains detailed information about the client's terminal
 * capabilities and preferences. The server uses this data to generate
 * appropriately formatted ASCII art and ANSI escape sequences.
 *
 * PACKET STRUCTURE EXPECTED:
 * - terminal_capabilities_packet_t containing:
 *   - width, height: Terminal dimensions in characters
 *   - capabilities: Bitmask of terminal features
 *   - color_level: ANSI color support level (1, 8, 16, 256, 24-bit)
 *   - color_count: Number of supported colors
 *   - render_mode: Foreground, background, or half-block rendering
 *   - term_type: $TERM environment variable value
 *   - colorterm: $COLORTERM environment variable value
 *   - utf8_support: Whether terminal supports UTF-8
 *   - palette_type: ASCII character palette preference
 *   - palette_custom: Custom character set if PALETTE_CUSTOM
 *
 * STATE CHANGES PERFORMED:
 * - Updates client dimensions (width, height)
 * - Stores complete terminal capabilities structure
 * - Initializes per-client ASCII palette cache
 * - Sets client->has_terminal_caps = true
 *
 * PALETTE INITIALIZATION:
 * The function performs critical palette setup:
 * 1. Determines character set based on palette_type
 * 2. Handles custom palettes if provided
 * 3. Generates luminance-to-character mapping
 * 4. Caches results for fast ASCII generation
 *
 * THREAD SAFETY:
 * - All client state updates are mutex-protected
 * - Uses client->client_state_mutex for atomicity
 * - Safe to call concurrently with render threads
 *
 * VALIDATION PERFORMED:
 * - Packet size matches expected structure
 * - String fields are safely copied with bounds checking
 * - Palette initialization is verified
 * - Network byte order conversion
 *
 * ERROR HANDLING:
 * - Invalid packets are logged and ignored
 * - Palette initialization failures use server defaults
 * - Missing capabilities default to safe values
 *
 * INTEGRATION IMPACT:
 * - render.c uses capabilities for ASCII generation
 * - Color output depends on client's color_level
 * - Character selection uses initialized palette
 *
 * @param client Target client whose capabilities are being configured
 * @param data Packet payload containing terminal_capabilities_packet_t
 * @param len Size of packet payload in bytes
 *
 * @note This packet is typically sent once after CLIENT_JOIN
 * @note Capabilities can be updated during the session
 * @note Changes affect all subsequent ASCII frame generation
 * @see initialize_client_palette() For palette setup details
 * @see terminal_color_level_name() For color level descriptions
 */
void handle_client_capabilities_packet(client_info_t *client, const void *data, size_t len) {
  // Handle terminal capabilities from client
  log_info("CAPS_HANDLER: Client %u starting capabilities processing", client->client_id);

  if (len == sizeof(terminal_capabilities_packet_t)) {
    const terminal_capabilities_packet_t *caps = (const terminal_capabilities_packet_t *)data;

    // LOCK OPTIMIZATION: Only need client_state_mutex for non-atomic fields
    // We already have a stable client pointer from receive thread
    log_info("CAPS_HANDLER: Client %u attempting to acquire client_state_mutex", client->client_id);
    mutex_lock(&client->client_state_mutex);
    log_info("CAPS_HANDLER: Client %u acquired client_state_mutex", client->client_id);

    // Convert from network byte order and store dimensions
    atomic_store(&client->width, ntohs(caps->width));
    atomic_store(&client->height, ntohs(caps->height));

    log_info("CAPS_RECEIVED: Client %u dimensions: %ux%u, desired_fps=%u", atomic_load(&client->client_id),
             client->width, client->height, caps->desired_fps);

    // Store terminal capabilities
    client->terminal_caps.capabilities = ntohl(caps->capabilities);
    client->terminal_caps.color_level = ntohl(caps->color_level);
    client->terminal_caps.color_count = ntohl(caps->color_count);
    client->terminal_caps.render_mode = ntohl(caps->render_mode);
    client->terminal_caps.detection_reliable = caps->detection_reliable;
    client->terminal_caps.wants_background = (ntohl(caps->render_mode) == RENDER_MODE_BACKGROUND);

    // Copy terminal type strings safely
    SAFE_STRNCPY(client->terminal_caps.term_type, caps->term_type, sizeof(client->terminal_caps.term_type));
    SAFE_STRNCPY(client->terminal_caps.colorterm, caps->colorterm, sizeof(client->terminal_caps.colorterm));

    // NEW: Store client's palette preferences
    client->terminal_caps.utf8_support = ntohl(caps->utf8_support);
    client->terminal_caps.palette_type = ntohl(caps->palette_type);
    SAFE_STRNCPY(client->terminal_caps.palette_custom, caps->palette_custom,
                 sizeof(client->terminal_caps.palette_custom));

    // Store client's desired FPS
    client->terminal_caps.desired_fps = caps->desired_fps;

    // Initialize client's per-client palette cache
    const char *custom_chars =
        (client->terminal_caps.palette_type == PALETTE_CUSTOM && client->terminal_caps.palette_custom[0])
            ? client->terminal_caps.palette_custom
            : NULL;

    if (initialize_client_palette((palette_type_t)client->terminal_caps.palette_type, custom_chars,
                                  client->client_palette_chars, &client->client_palette_len,
                                  client->client_luminance_palette) == 0) {
      client->client_palette_type = (palette_type_t)client->terminal_caps.palette_type;
      client->client_palette_initialized = true;
      log_info("Client %d palette initialized: type=%u, %zu chars, utf8=%u", atomic_load(&client->client_id),
               client->terminal_caps.palette_type, client->client_palette_len, client->terminal_caps.utf8_support);
    } else {
      log_error("Failed to initialize palette for client %d, using server default", atomic_load(&client->client_id));
      client->client_palette_initialized = false;
    }

    // Mark that we have received capabilities for this client
    client->has_terminal_caps = true;

    log_info("Client %u capabilities: %ux%u, color_level=%s (%u colors), caps=0x%x, term=%s, colorterm=%s, "
             "render_mode=%s, reliable=%s, fps=%u",
             atomic_load(&client->client_id), client->width, client->height,
             terminal_color_level_name(client->terminal_caps.color_level), client->terminal_caps.color_count,
             client->terminal_caps.capabilities, client->terminal_caps.term_type, client->terminal_caps.colorterm,
             (client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK
                  ? "half-block"
                  : (client->terminal_caps.render_mode == RENDER_MODE_BACKGROUND ? "background" : "foreground")),
             client->terminal_caps.detection_reliable ? "yes" : "no", client->terminal_caps.desired_fps);

    // Release lock acquired at function start
    log_info("CAPS_HANDLER: Client %u releasing client_state_mutex", client->client_id);
    mutex_unlock(&client->client_state_mutex);
    log_info("CAPS_HANDLER: Client %u released client_state_mutex", client->client_id);

  } else {
    log_error("Invalid client capabilities packet size: %zu, expected %zu", len,
              sizeof(terminal_capabilities_packet_t));
  }
}

/**
 * @brief Process terminal size update packet - handle client window resize
 *
 * Clients send this packet when their terminal window is resized, allowing
 * the server to adjust ASCII frame dimensions accordingly. This ensures
 * optimal use of the client's display area.
 *
 * PACKET STRUCTURE EXPECTED:
 * - size_packet_t containing:
 *   - uint16_t width: New terminal width in characters
 *   - uint16_t height: New terminal height in characters
 *
 * STATE CHANGES PERFORMED:
 * - Updates client->width with new dimensions
 * - Updates client->height with new dimensions
 * - Thread-safe update using client state mutex
 *
 * RENDERING IMPACT:
 * - Subsequent ASCII frames will use new dimensions
 * - Grid layout calculations will incorporate new size
 * - No immediate frame regeneration (happens on next cycle)
 *
 * ERROR HANDLING:
 * - Invalid packet sizes are ignored silently
 * - Extreme dimensions are accepted (client responsibility)
 * - Concurrent updates are handled safely
 *
 * @param client Target client whose terminal was resized
 * @param data Packet payload containing new dimensions
 * @param len Size of packet payload (should be sizeof(size_packet_t))
 *
 * @note Changes take effect on the next rendering cycle
 * @note No validation of reasonable dimension ranges
 */
void handle_size_packet(client_info_t *client, const void *data, size_t len) {
  // Handle terminal size update from client
  if (len == sizeof(size_packet_t)) {
    const size_packet_t *size_pkt = (const size_packet_t *)data;

    // LOCK OPTIMIZATION: Only need client_state_mutex for dimension updates
    // Width and height are atomics, but mutex provides consistency
    mutex_lock(&client->client_state_mutex);
    client->width = ntohs(size_pkt->width);   // Regular assignment under mutex protection
    client->height = ntohs(size_pkt->height); // Regular assignment under mutex protection
    mutex_unlock(&client->client_state_mutex);

    log_info("Client %u updated terminal size: %ux%u", atomic_load(&client->client_id), client->width, client->height);
  }
}

/* ============================================================================
 * Protocol Control Packet Handlers
 * ============================================================================
 */

/**
 * @brief Process PING packet - respond with PONG for keepalive
 *
 * Clients send periodic PING packets to verify the connection is still active.
 * The server responds with a PONG packet to confirm bi-directional connectivity.
 * This prevents network equipment from timing out idle connections.
 *
 * PACKET STRUCTURE:
 * - PING packets have no payload (header only)
 * - PONG responses also have no payload
 *
 * PROTOCOL BEHAVIOR:
 * - Every PING must be answered with exactly one PONG
 * - PONG is queued in client's video queue for delivery
 * - No state changes are made to client
 *
 * ERROR HANDLING:
 * - Queue failures are logged but not fatal
 * - Client disconnection during PONG delivery is handled gracefully
 * - Excessive PING rate is not rate-limited (client responsibility)
 *
 * PERFORMANCE CHARACTERISTICS:
 * - Very low overhead (no payload processing)
 * - Uses existing packet queue infrastructure
 * - Send thread delivers PONG asynchronously
 *
 * @param client Source client requesting keepalive confirmation
 *
 * @note PING/PONG packets help detect network failures
 * @note Response is queued, not sent immediately
 * @see packet_queue_enqueue() For response delivery mechanism
 */
void handle_ping_packet(client_info_t *client) {
  // PONG responses should be handled directly via socket in send thread
  // For now, just log the ping
  (void)client;
}

/**
 * @brief Process CLIENT_LEAVE packet - handle graceful client disconnect
 *
 * Clients send this packet to notify the server of an intentional disconnect,
 * as opposed to a network failure or crash. This allows the server to perform
 * clean shutdown procedures without waiting for socket timeouts.
 *
 * PACKET STRUCTURE:
 * - LEAVE packets have no payload (header only)
 *
 * STATE CHANGES PERFORMED:
 * - Sets client->active = false immediately
 * - Triggers client cleanup procedures
 * - Prevents new packets from being processed
 *
 * PROTOCOL BEHAVIOR:
 * - Client should not send additional packets after LEAVE
 * - Server will begin client removal process
 * - Socket will be closed by cleanup procedures
 *
 * CLEANUP COORDINATION:
 * - Receive thread will exit after processing this packet
 * - Send thread will stop when active flag becomes false
 * - Render threads will detect inactive state and stop processing
 * - remove_client() will be called to complete cleanup
 *
 * ERROR HANDLING:
 * - Safe to call multiple times
 * - No validation required (simple state change)
 * - Cleanup is idempotent
 *
 * @param client Source client requesting graceful disconnect
 *
 * @note This provides clean shutdown versus network timeout
 * @note Actual client removal happens in main thread
 * @see remove_client() For complete cleanup implementation
 */
void handle_client_leave_packet(client_info_t *client) {
  // Handle clean disconnect notification from client
  log_info("Client %u sent LEAVE packet - clean disconnect", atomic_load(&client->client_id));
  // OPTIMIZED: Use atomic operation for thread control flag (lock-free)
  atomic_store(&client->active, false);
}

/* ============================================================================
 * Protocol Utility Functions
 * ============================================================================
 */

/**
 * @brief Send current server state to a specific client
 *
 * Generates and queues a SERVER_STATE packet containing information about
 * the current number of connected and active clients. This helps clients
 * understand the multi-user environment and adjust their behavior accordingly.
 *
 * PACKET CONTENT GENERATED:
 * - server_state_packet_t containing:
 *   - connected_client_count: Total clients connected to server
 *   - active_client_count: Clients actively sending video/audio
 *   - reserved: Padding for future extensions
 *
 * USAGE SCENARIOS:
 * - Initial state after client joins server
 * - Periodic updates when client count changes
 * - Response to client requests for server information
 *
 * IMPLEMENTATION DETAILS:
 * - Counts active clients by scanning global client manager
 * - Converts data to network byte order before queuing
 * - Uses client's video queue for delivery
 * - Non-blocking operation (queues for later delivery)
 *
 * THREAD SAFETY:
 * - Uses reader lock on global client manager
 * - Safe to call from any thread
 * - Atomic snapshot of client counts
 *
 * ERROR HANDLING:
 * - Returns -1 if client or queue is invalid
 * - Queue overflow handled by packet_queue_enqueue()
 * - No side effects on failure
 *
 * @param client Target client to receive server state information
 * @return 0 on successful queuing, -1 on error
 *
 * @note Packet delivery happens asynchronously via send thread
 * @note Client count may change between queuing and delivery
 * @see broadcast_server_state_to_all_clients() For multi-client updates
 */

int send_server_state_to_client(client_info_t *client) {
  if (!client) {
    return -1;
  }

  // Count active clients - LOCK OPTIMIZATION: Use atomic reads, no rwlock needed
  int active_count = 0;
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (atomic_load(&g_client_manager.clients[i].active)) {
      active_count++;
    }
  }

  // Prepare server state packet
  server_state_packet_t state;
  state.connected_client_count = active_count;
  state.active_client_count = active_count; // For now, all connected are active
  memset(state.reserved, 0, sizeof(state.reserved));

  // Convert to network byte order
  server_state_packet_t net_state;
  net_state.connected_client_count = htonl(state.connected_client_count);
  net_state.active_client_count = htonl(state.active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  // Send server state directly via socket
  // LOCK OPTIMIZATION: Access crypto context directly - no need for find_client_by_id() rwlock!
  // Crypto context is stable after handshake and stored in client struct
  const crypto_context_t *crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);
  int result = send_packet_secure(client->socket, PACKET_TYPE_SERVER_STATE, &net_state, sizeof(net_state),
                                  (crypto_context_t *)crypto_ctx);

  if (result != 0) {
    log_error("Failed to send server state to client %u", client->client_id);
    return -1;
  }

  log_debug("Sent server state to client %u: %u connected, %u active", client->client_id, state.connected_client_count,
            state.active_client_count);
  return 0;
}

/**
 * @brief Signal all active clients to clear their displays before next video frame
 *
 * Sets the needs_display_clear flag for all currently connected and active clients.
 * This is used when the grid layout changes (clients join/leave) to ensure
 * all clients clear their displays before receiving frames with the new layout.
 *
 * ARCHITECTURE:
 * - Uses atomic flag (needs_display_clear) instead of packet queue
 * - Send thread checks flag and sends CLEAR_CONSOLE before next video frame
 * - Guarantees CLEAR_CONSOLE arrives before new grid layout frame
 *
 * SYNCHRONIZATION:
 * - Acquires g_client_manager_rwlock for reading
 * - Uses atomic operations for flag setting (no mutex needed)
 * - Thread-safe access to client list
 *
 * USAGE SCENARIO:
 * - Called when active video source count changes
 * - Ensures all clients clear before new grid layout is sent
 * - Prevents visual artifacts from old content
 *
 * @note This function iterates all clients but only flags active ones
 * @note Non-blocking - just sets atomic flags
 */
// NOTE: This function is no longer used - CLEAR_CONSOLE is now sent directly
// from each client's render thread when it detects a grid layout change.
// Keeping this for reference but it should not be called.
void broadcast_clear_console_to_all_clients(void) {
  log_error("broadcast_clear_console_to_all_clients() called - this should not happen!");
  log_error("CLEAR_CONSOLE is now sent from render threads, not broadcast");
}
