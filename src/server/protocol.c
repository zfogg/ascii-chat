/**
 * @file server/protocol.c
 * @ingroup server_protocol
 * @brief 📡 Server packet processor: client communication handling, protocol state management, and packet dispatch
 *
 * CORE RESPONSIBILITIES
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

#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "main.h"
#include "protocol.h"
#include "client.h"
#include <ascii-chat/common.h>
#include <ascii-chat/common/buffer_sizes.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/validation.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/bytes.h>
#include <ascii-chat/util/image.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/video/video_frame.h>
#include <ascii-chat/audio/audio.h>
#include <ascii-chat/video/palette.h>
#include <ascii-chat/video/image.h>
#include <ascii-chat/network/compression.h>
#include <ascii-chat/network/packet_parsing.h>
#include <ascii-chat/network/frame_validator.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/server.h>
#include <ascii-chat/util/format.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/audio/opus_codec.h>
#include <ascii-chat/network/packet_parsing.h>
#include <ascii-chat/network/logging.h>
#include <ascii-chat/crypto/handshake/common.h>

static void protocol_cleanup_thread_locals(void) {
  // Placeholder for thread-local resources owned by the receive thread.
  // Add cleanup logic here if future protocol changes introduce
  // thread-local allocations that must be released before disconnecting.
}

void disconnect_client_for_bad_data(client_info_t *client, const char *format, ...) {
  if (!client) {
    return;
  }

  protocol_cleanup_thread_locals();

  bool already_requested = atomic_exchange(&client->protocol_disconnect_requested, true);
  if (already_requested) {
    return;
  }

  char reason[BUFFER_SIZE_SMALL] = {0};
  if (format) {
    va_list args;
    va_start(args, format);
    safe_vsnprintf(reason, sizeof(reason), format, args);
    va_end(args);
  } else {
    SAFE_STRNCPY(reason, "Protocol violation", sizeof(reason));
  }

  const char *reason_str = reason[0] != '\0' ? reason : "Protocol violation";
  uint32_t client_id = atomic_load(&client->client_id);

  socket_t socket_snapshot = INVALID_SOCKET_VALUE;
  const crypto_context_t *crypto_ctx = NULL;
  acip_transport_t *transport_snapshot = NULL;

  mutex_lock(&client->client_state_mutex);
  if (client->socket != INVALID_SOCKET_VALUE) {
    socket_snapshot = client->socket;
    if (client->crypto_initialized) {
      crypto_ctx = crypto_handshake_get_context(&client->crypto_handshake_ctx);
    }
  }
  mutex_unlock(&client->client_state_mutex);

  // Get transport reference for WebSocket clients
  mutex_lock(&client->send_mutex);
  transport_snapshot = client->transport;
  mutex_unlock(&client->send_mutex);

  // NOTE: Disconnecting a client due to the client's own bad behavior isn't an
  // error for us, it's desired behavior for us, so we simply warn and do not
  // have a need for asciichat_errno here.
  log_warn("Disconnecting client %u due to protocol violation: %s", client_id, reason_str);

  if (socket_snapshot != INVALID_SOCKET_VALUE) {
    // Protect socket writes with send_mutex to prevent race with send_thread.
    // This receive_thread and send_thread both write to same socket.
    mutex_lock(&client->send_mutex);

    asciichat_error_t log_result =
        log_network_message(socket_snapshot, (const struct crypto_context_t *)crypto_ctx, LOG_ERROR,
                            REMOTE_LOG_DIRECTION_SERVER_TO_CLIENT, "Protocol violation: %s", reason_str);
    if (log_result != ASCIICHAT_OK) {
      log_warn("Failed to send remote log to client %u: %s", client_id, asciichat_error_string(log_result));
    }

    asciichat_error_t send_result = packet_send_error(socket_snapshot, crypto_ctx, ERROR_NETWORK_PROTOCOL, reason_str);
    if (send_result != ASCIICHAT_OK) {
      log_warn("Failed to send error packet to client %u: %s", client_id, asciichat_error_string(send_result));
    }

    mutex_unlock(&client->send_mutex);
  } else if (transport_snapshot) {
    // For WebSocket clients, try to send error via transport
    log_debug("Sending error to WebSocket client %u via transport", client_id);
    acip_send_error(transport_snapshot, ERROR_NETWORK_PROTOCOL, reason_str);
  }

  platform_sleep_ms(500);

  log_debug("Setting active=false in disconnect_client_for_bad_data (client_id=%u, reason=%s)", client_id, reason_str);
  atomic_store(&client->active, false);
  atomic_store(&client->shutting_down, true);
  atomic_store(&client->send_thread_running, false);
  atomic_store(&client->video_render_thread_running, false);
  atomic_store(&client->audio_render_thread_running, false);

  if (client->audio_queue) {
    packet_queue_stop(client->audio_queue);
  }

  mutex_lock(&client->client_state_mutex);
  if (client->socket != INVALID_SOCKET_VALUE) {
    socket_shutdown(client->socket, 2);
    socket_close(client->socket);
    client->socket = INVALID_SOCKET_VALUE;
  }
  mutex_unlock(&client->client_state_mutex);
}

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
  VALIDATE_PACKET_SIZE(client, data, len, sizeof(client_info_packet_t), "CLIENT_JOIN");

  const client_info_packet_t *join_info = (const client_info_packet_t *)data;

  // Validate display name is present and not just whitespace
  if (join_info->display_name[0] == '\0') {
    disconnect_client_for_bad_data(client, "CLIENT_JOIN display_name cannot be empty");
    return;
  }

  uint32_t capabilities = NET_TO_HOST_U32(join_info->capabilities);

  // Validate at least one capability flag is set
  const uint32_t VALID_CAP_MASK = CLIENT_CAP_VIDEO | CLIENT_CAP_AUDIO | CLIENT_CAP_COLOR | CLIENT_CAP_STRETCH;
  VALIDATE_CAPABILITY_FLAGS(client, capabilities, VALID_CAP_MASK, "CLIENT_JOIN");

  // Validate no unknown capability bits are set
  VALIDATE_FLAGS_MASK(client, capabilities, VALID_CAP_MASK, "CLIENT_JOIN");

  SAFE_STRNCPY(client->display_name, join_info->display_name, MAX_DISPLAY_NAME_LEN - 1);

  client->can_send_video = (capabilities & CLIENT_CAP_VIDEO) != 0;
  client->can_send_audio = (capabilities & CLIENT_CAP_AUDIO) != 0;
  client->wants_stretch = (capabilities & CLIENT_CAP_STRETCH) != 0;

  log_info("Client %u joined: %s (video=%d, audio=%d, stretch=%d)", atomic_load(&client->client_id),
           client->display_name, client->can_send_video, client->can_send_audio, client->wants_stretch);

  // Notify client of successful join (encrypted channel)
  if (client->socket != INVALID_SOCKET_VALUE) {
    log_info_client(client, "Joined as '%s' (video=%s, audio=%s)", client->display_name,
                    client->can_send_video ? "yes" : "no", client->can_send_audio ? "yes" : "no");
  }
}

/**
 * @brief Process PROTOCOL_VERSION packet - validate protocol compatibility
 *
 * Clients send this packet to announce their protocol version and capabilities.
 * The server validates that the major version matches and logs any version
 * mismatches for debugging purposes.
 *
 * PACKET STRUCTURE EXPECTED:
 * - protocol_version_packet_t containing:
 *   - protocol_version: Major version number (must match PROTOCOL_VERSION_MAJOR)
 *   - protocol_revision: Minor version number
 *   - supports_encryption: Encryption capability flag
 *   - compression_algorithms: Supported compression bitmask
 *   - feature_flags: Optional feature flags
 *
 * VALIDATION PERFORMED:
 * - Packet size matches sizeof(protocol_version_packet_t)
 * - Major protocol version matches (PROTOCOL_VERSION_MAJOR)
 * - Reserved bytes are zero (future-proofing)
 *
 * ERROR HANDLING:
 * - Version mismatch is logged but NOT fatal (backward compatibility)
 * - Invalid packet size triggers disconnect
 *
 * @param client Client that sent the packet
 * @param data Packet payload (protocol_version_packet_t)
 * @param len Size of packet payload in bytes
 *
 * @note This is typically the first packet in the handshake
 * @note Protocol version validation ensures compatibility
 */
void handle_protocol_version_packet(client_info_t *client, const void *data, size_t len) {
  if (!data) {
    disconnect_client_for_bad_data(client, "PROTOCOL_VERSION payload missing");
    return;
  }

  if (len != sizeof(protocol_version_packet_t)) {
    disconnect_client_for_bad_data(client, "PROTOCOL_VERSION invalid size: %zu (expected %zu)", len,
                                   sizeof(protocol_version_packet_t));
    return;
  }

  const protocol_version_packet_t *version = (const protocol_version_packet_t *)data;
  uint16_t client_major = NET_TO_HOST_U16(version->protocol_version);
  uint16_t client_minor = NET_TO_HOST_U16(version->protocol_revision);

  // Validate major version match (minor version can differ for backward compat)
  if (client_major != PROTOCOL_VERSION_MAJOR) {
    log_warn("Client %u protocol version mismatch: client=%u.%u, server=%u.%u", atomic_load(&client->client_id),
             client_major, client_minor, PROTOCOL_VERSION_MAJOR, PROTOCOL_VERSION_MINOR);
    // Note: We don't disconnect on version mismatch for backward compatibility
    // Clients may be older or newer than server
  } else if (client_minor != PROTOCOL_VERSION_MINOR) {
    log_info("Client %u has different protocol revision: client=%u.%u, server=%u.%u", atomic_load(&client->client_id),
             client_major, client_minor, PROTOCOL_VERSION_MAJOR, PROTOCOL_VERSION_MINOR);
  }

  // Validate reserved bytes are zero
  for (size_t i = 0; i < sizeof(version->reserved); i++) {
    if (version->reserved[i] != 0) {
      log_warn("Client %u sent non-zero reserved bytes in PROTOCOL_VERSION packet", atomic_load(&client->client_id));
      // Don't disconnect - reserved bytes may be used in future versions
      break;
    }
  }

  // Log supported features
  if (ACIP_CRYPTO_HAS_ENCRYPT(version->supports_encryption)) {
    log_debug("Client %u supports encryption", atomic_load(&client->client_id));
  }
  if (version->compression_algorithms != 0) {
    log_debug("Client %u supports compression: 0x%02x", atomic_load(&client->client_id),
              version->compression_algorithms);
  }
  if (version->feature_flags != 0) {
    uint16_t feature_flags = NET_TO_HOST_U16(version->feature_flags);
    log_debug("Client %u supports features: 0x%04x", atomic_load(&client->client_id), feature_flags);
  }
}

/**
 * @brief Process CLIENT_LEAVE packet - handle clean client disconnect
 *
 * Clients may send this packet before disconnecting to allow the server to
 * log the disconnect reason and perform clean state management. This is
 * optional but preferred over abrupt disconnects.
 *
 * PACKET STRUCTURE EXPECTED:
 * - Optional string containing disconnect reason (0-256 bytes)
 *
 * PROTOCOL BEHAVIOR:
 * - Client logs the disconnect reason if provided
 * - Server continues normal disconnect sequence after receiving packet
 * - Client remains responsible for closing socket
 *
 * ERROR HANDLING:
 * - Empty payload is handled gracefully
 * - Oversized payloads are rejected
 * - Invalid UTF-8 in reason is handled gracefully (logged as-is)
 *
 * @param client Client that sent the leave packet
 * @param data Packet payload (optional reason string)
 * @param len Size of packet payload in bytes (0-256)
 *
 * @note This handler doesn't trigger immediate disconnect
 * @note Actual disconnect occurs when socket closes
 */
void handle_client_leave_packet(client_info_t *client, const void *data, size_t len) {
  if (!client) {
    return;
  }

  uint32_t client_id = atomic_load(&client->client_id);

  if (len == 0) {
    // Empty reason - client disconnecting without explanation
    log_info("Client %u sent leave notification (no reason)", client_id);
  } else if (len <= 256) {
    // Reason provided - extract and log it
    if (!data) {
      SET_ERRNO(ERROR_INVALID_STATE, "Client %u sent leave notification with non-zero length but NULL data", client_id);
      return;
    }

    char reason[257] = {0};
    memcpy(reason, data, len);
    reason[len] = '\0';

    // Validate reason is printable (handle potential non-UTF8 gracefully)
    bool all_printable = true;
    for (size_t i = 0; i < len; i++) {
      uint8_t c = (uint8_t)reason[i];
      if (c < 32 && c != '\t' && c != '\n') {
        all_printable = false;
        break;
      }
    }

    if (all_printable) {
      log_info("Client %u sent leave notification: %s", client_id, reason);
    } else {
      log_info("Client %u sent leave notification (reason contains non-printable characters)", client_id);
    }
  } else {
    // Oversized reason - shouldn't happen with validation.h checks
    log_warn("Client %u sent oversized leave reason (%zu bytes, max 256)", client_id, len);
  }

  // Deactivate client to stop processing packets
  // Sets client->active = false immediately - triggers client cleanup procedures
  log_debug("Setting active=false in handle_client_leave_packet (client_id=%u)", client_id);
  atomic_store(&client->active, false);

  // Note: We don't disconnect the client here - that happens when socket closes
  // This is just a clean notification before disconnect
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
  VALIDATE_PACKET_SIZE(client, data, len, sizeof(uint32_t), "STREAM_START");

  uint32_t stream_type_net;
  memcpy(&stream_type_net, data, sizeof(uint32_t));
  uint32_t stream_type = NET_TO_HOST_U32(stream_type_net);

  // Validate at least one stream type flag is set
  const uint32_t VALID_STREAM_MASK = STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO;
  VALIDATE_CAPABILITY_FLAGS(client, stream_type, VALID_STREAM_MASK, "STREAM_START");

  // Validate no unknown stream type bits are set
  VALIDATE_FLAGS_MASK(client, stream_type, VALID_STREAM_MASK, "STREAM_START");

  if (stream_type & STREAM_TYPE_VIDEO) {
    atomic_store(&client->is_sending_video, true);
  }
  if (stream_type & STREAM_TYPE_AUDIO) {
    atomic_store(&client->is_sending_audio, true);

    // Create Opus decoder for this client if not already created
    if (!client->opus_decoder) {
      client->opus_decoder = opus_codec_create_decoder(48000);
      if (client->opus_decoder) {
        log_info("Client %u: Opus decoder created (48kHz)", atomic_load(&client->client_id));
      } else {
        log_error("Client %u: Failed to create Opus decoder", atomic_load(&client->client_id));
      }
    }
  }

  if (stream_type & STREAM_TYPE_VIDEO) {
    log_info("Client %u announced video stream (waiting for first frame)", atomic_load(&client->client_id));
  }
  if (stream_type & STREAM_TYPE_AUDIO) {
    log_info("Client %u started audio stream", atomic_load(&client->client_id));
  }

  // Notify client of stream start acknowledgment
  const char *streams = (stream_type & STREAM_TYPE_VIDEO) && (stream_type & STREAM_TYPE_AUDIO)
                            ? "video+audio"
                            : ((stream_type & STREAM_TYPE_VIDEO) ? "video" : "audio");
  // Only send remote log to TCP clients (WebSocket clients have invalid socket)
  if (client->socket != INVALID_SOCKET_VALUE) {
    log_info_client(client, "Stream started: %s", streams);
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
  VALIDATE_PACKET_SIZE(client, data, len, sizeof(uint32_t), "STREAM_STOP");

  uint32_t stream_type_net;
  memcpy(&stream_type_net, data, sizeof(uint32_t));
  uint32_t stream_type = NET_TO_HOST_U32(stream_type_net);

  // Validate at least one stream type flag is set
  const uint32_t VALID_STREAM_MASK = STREAM_TYPE_VIDEO | STREAM_TYPE_AUDIO;
  VALIDATE_CAPABILITY_FLAGS(client, stream_type, VALID_STREAM_MASK, "STREAM_STOP");

  // Validate no unknown stream type bits are set
  VALIDATE_FLAGS_MASK(client, stream_type, VALID_STREAM_MASK, "STREAM_STOP");

  if (stream_type & STREAM_TYPE_VIDEO) {
    atomic_store(&client->is_sending_video, false);
  }
  if (stream_type & STREAM_TYPE_AUDIO) {
    atomic_store(&client->is_sending_audio, false);
  }

  if (stream_type & STREAM_TYPE_VIDEO) {
    log_info("Client %u stopped video stream", atomic_load(&client->client_id));
  }
  if (stream_type & STREAM_TYPE_AUDIO) {
    log_info("Client %u stopped audio stream", atomic_load(&client->client_id));
  }

  // Notify client of stream stop acknowledgment
  const char *streams = (stream_type & STREAM_TYPE_VIDEO) && (stream_type & STREAM_TYPE_AUDIO)
                            ? "video+audio"
                            : ((stream_type & STREAM_TYPE_VIDEO) ? "video" : "audio");
  // Only send remote log to TCP clients (WebSocket clients have invalid socket)
  if (client->socket != INVALID_SOCKET_VALUE) {
    log_info_client(client, "Stream stopped: %s", streams);
  }
}

/**
 * @brief Handle PING packet - respond with PONG
 */
void handle_ping_packet(client_info_t *client, const void *data, size_t len) {
  (void)data;
  (void)len;

  // Get transport reference briefly to avoid deadlock on TCP buffer full
  mutex_lock(&client->send_mutex);
  if (atomic_load(&client->shutting_down) || !client->transport) {
    mutex_unlock(&client->send_mutex);
    return;
  }
  acip_transport_t *pong_transport = client->transport;
  mutex_unlock(&client->send_mutex);

  // Network I/O happens OUTSIDE the mutex
  asciichat_error_t pong_result = acip_send_pong(pong_transport);
  if (pong_result != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_NETWORK, "Failed to send PONG response to client %u: %s", atomic_load(&client->client_id),
              asciichat_error_string(pong_result));
  }
}

/**
 * @brief Handle PONG packet - client acknowledged our PING
 */
void handle_pong_packet(client_info_t *client, const void *data, size_t len) {
  (void)client;
  (void)data;
  (void)len;
  // No action needed - client acknowledged our PING
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
 * - rgb_pixel_t pixels[width * height] (RGB888 format)
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
  // Use atomic compare-and-swap to avoid race condition - ensures thread-safe auto-enabling of video stream

  log_info("RECV_IMAGE_FRAME: client_id=%u, len=%zu", atomic_load(&client->client_id), len);

  if (!data || len < sizeof(uint32_t) * 2) {
    disconnect_client_for_bad_data(client, "IMAGE_FRAME payload too small: %zu bytes", len);
    return;
  }
  bool was_sending_video = atomic_load(&client->is_sending_video);
  if (!was_sending_video) {
    // Try to atomically enable video sending
    // Use atomic_compare_exchange_strong to avoid spurious failures
    if (atomic_compare_exchange_strong(&client->is_sending_video, &was_sending_video, true)) {
      log_info("Client %u auto-enabled video stream (received IMAGE_FRAME)", atomic_load(&client->client_id));
      // Notify client that their first video frame was received
      if (client->socket != INVALID_SOCKET_VALUE) {
        log_info_client(client, "First video frame received - streaming active");
      }
    }
  } else {
    // Log periodically to confirm we're receiving frames
    // Use per-client counter protected by client_state_mutex to avoid race conditions
    mutex_lock(&client->client_state_mutex);
    client->frames_received_logged++;
    if (client->frames_received_logged % 25000 == 0) {
      char pretty[64];
      format_bytes_pretty(len, pretty, sizeof(pretty));
      log_debug("Client %u has sent %u IMAGE_FRAME packets (%s)", atomic_load(&client->client_id),
                client->frames_received_logged, pretty);
    }
    mutex_unlock(&client->client_state_mutex);
  }

  // Parse image dimensions (use memcpy to avoid unaligned access)
  uint32_t img_width_net, img_height_net;
  memcpy(&img_width_net, data, sizeof(uint32_t));
  memcpy(&img_height_net, (char *)data + sizeof(uint32_t), sizeof(uint32_t));
  uint32_t img_width = NET_TO_HOST_U32(img_width_net);
  uint32_t img_height = NET_TO_HOST_U32(img_height_net);

  log_debug("IMAGE_FRAME packet: width=%u, height=%u, payload_len=%zu", img_width, img_height, len);

  // Validate dimensions using image utility functions
  if (image_validate_dimensions((size_t)img_width, (size_t)img_height) != ASCIICHAT_OK) {
    log_error("IMAGE_FRAME validation failed for dimensions: %u x %u", img_width, img_height);
    disconnect_client_for_bad_data(client, "IMAGE_FRAME invalid dimensions");
    return;
  }

  // Calculate RGB buffer size with overflow checking
  size_t rgb_size = 0;
  if (image_calc_rgb_size((size_t)img_width, (size_t)img_height, &rgb_size) != ASCIICHAT_OK) {
    disconnect_client_for_bad_data(client, "IMAGE_FRAME buffer size calculation failed");
    return;
  }

  // Validate final buffer size against maximum
  if (image_validate_buffer_size(rgb_size) != ASCIICHAT_OK) {
    disconnect_client_for_bad_data(client, "IMAGE_FRAME buffer size exceeds maximum");
    return;
  }

  // Only support legacy format: [width:4][height:4][rgb_data:w*h*3]
  if (rgb_size > SIZE_MAX - FRAME_HEADER_SIZE_LEGACY) {
    char size_str[32];
    format_bytes_pretty(rgb_size, size_str, sizeof(size_str));
    disconnect_client_for_bad_data(client, "IMAGE_FRAME legacy packet size overflow: %s", size_str);
    return;
  }
  size_t expected_size = FRAME_HEADER_SIZE_LEGACY + rgb_size;

  if (len != expected_size) {
    disconnect_client_for_bad_data(client, "IMAGE_FRAME size mismatch: expected %zu bytes got %zu", expected_size, len);
    return;
  }

  // Validate legacy format
  asciichat_error_t validate_result = frame_validate_legacy(len, rgb_size);
  if (validate_result != ASCIICHAT_OK) {
    disconnect_client_for_bad_data(client, "IMAGE_FRAME legacy validation failed");
    return;
  }

  void *rgb_data = (char *)data + FRAME_HEADER_SIZE_LEGACY;
  size_t rgb_data_size = rgb_size;
  bool needs_free = false;

  if (client->incoming_video_buffer) {
    // Get the write buffer
    video_frame_t *frame = video_frame_begin_write(client->incoming_video_buffer);

    if (frame && frame->data) {
      // Build the packet in the old format for internal storage: [width:4][height:4][rgb_data:w*h*3]
      // Use frame_check_size_overflow to validate overflow before repacking
      asciichat_error_t overflow_check = frame_check_size_overflow(FRAME_HEADER_SIZE_LEGACY, rgb_data_size);
      if (overflow_check != ASCIICHAT_OK) {
        if (needs_free && rgb_data) {
          SAFE_FREE(rgb_data);
        }
        disconnect_client_for_bad_data(client, "IMAGE_FRAME size overflow while repacking");
        return;
      }
      size_t old_packet_size = FRAME_HEADER_SIZE_LEGACY + rgb_data_size;

      if (old_packet_size <= MAX_FRAME_BUFFER_SIZE) { // Max frame buffer size
        uint32_t width_net = HOST_TO_NET_U32(img_width);
        uint32_t height_net = HOST_TO_NET_U32(img_height);

        // Pack in old format for internal consistency
        memcpy(frame->data, &width_net, sizeof(uint32_t));
        memcpy((char *)frame->data + sizeof(uint32_t), &height_net, sizeof(uint32_t));
        memcpy((char *)frame->data + sizeof(uint32_t) * 2, rgb_data, rgb_data_size);

        frame->size = old_packet_size;
        frame->width = img_width;
        frame->height = img_height;
        frame->capture_timestamp_ns = (uint64_t)time(NULL) * NS_PER_SEC_INT;
        frame->sequence_number = ++client->frames_received;

        // DEBUG: Compute hash of incoming RGB data to detect duplicates
        uint32_t incoming_rgb_hash = 0;
        for (size_t i = 0; i < rgb_data_size && i < 1000; i++) {
          incoming_rgb_hash = (uint32_t)((uint64_t)incoming_rgb_hash * 31 + ((unsigned char *)rgb_data)[i]);
        }

        // Per-client hash tracking (not static!) to avoid cross-client interference
        uint32_t client_id = atomic_load(&client->client_id);
        bool is_new_frame = (incoming_rgb_hash != client->last_received_frame_hash);

        if (is_new_frame) {
          log_info("RECV_FRAME #%u NEW: Client %u size=%zu dims=%ux%u hash=0x%08x (prev=0x%08x)",
                   client->frames_received, client_id, rgb_data_size, img_width, img_height, incoming_rgb_hash,
                   client->last_received_frame_hash);
          client->last_received_frame_hash = incoming_rgb_hash;
        } else {
          log_info("RECV_FRAME #%u DUP: Client %u size=%zu dims=%ux%u hash=0x%08x", client->frames_received, client_id,
                   rgb_data_size, img_width, img_height, incoming_rgb_hash);
        }

        video_frame_commit(client->incoming_video_buffer);
      } else {
        if (needs_free && rgb_data) {
          SAFE_FREE(rgb_data);
        }
        disconnect_client_for_bad_data(client, "IMAGE_FRAME repacked frame too large (%zu bytes)", old_packet_size);
        return;
      }
    } else {
      log_warn("Failed to get write buffer for client %u (frame=%p, frame->data=%p)", atomic_load(&client->client_id),
               (void *)frame, frame ? frame->data : NULL);
    }
  } else {
    // During shutdown, this is expected - don't spam error logs
    if (!atomic_load(&g_server_should_exit)) {
      SET_ERRNO(ERROR_INVALID_STATE, "Client %u has no incoming video buffer!", atomic_load(&client->client_id));
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
  VALIDATE_NOTNULL_DATA(client, data, "AUDIO");
  VALIDATE_AUDIO_ALIGNMENT(client, len, sizeof(float), "AUDIO");
  VALIDATE_AUDIO_STREAM_ENABLED(client, "AUDIO");

  int num_samples = (int)(len / sizeof(float));
  VALIDATE_AUDIO_SAMPLE_COUNT(client, num_samples, AUDIO_SAMPLES_PER_PACKET, "AUDIO");
  VALIDATE_RESOURCE_INITIALIZED(client, client->incoming_audio_buffer, "audio buffer");

  const float *samples = (const float *)data;
  asciichat_error_t result = audio_ring_buffer_write(client->incoming_audio_buffer, samples, num_samples);
  if (result != ASCIICHAT_OK) {
    log_error("Failed to write audio samples to buffer: %s", asciichat_error_string(result));
  }
}

void handle_remote_log_packet_from_client(client_info_t *client, const void *data, size_t len) {
  if (!client) {
    return;
  }

  log_level_t remote_level = LOG_INFO;
  remote_log_direction_t direction = REMOTE_LOG_DIRECTION_UNKNOWN;
  uint16_t flags = 0;
  char message[MAX_REMOTE_LOG_MESSAGE_LENGTH + 1] = {0};

  asciichat_error_t parse_result =
      packet_parse_remote_log(data, len, &remote_level, &direction, &flags, message, sizeof(message), NULL);
  if (parse_result != ASCIICHAT_OK) {
    disconnect_client_for_bad_data(client, "Invalid REMOTE_LOG packet: %s", asciichat_error_string(parse_result));
    return;
  }

  if (direction != REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER) {
    disconnect_client_for_bad_data(client, "REMOTE_LOG direction mismatch: %u", direction);
    return;
  }

  const bool truncated = (flags & REMOTE_LOG_FLAG_TRUNCATED) != 0;
  const char *display_name = client->display_name[0] ? client->display_name : "(unnamed)";
  uint32_t client_id = atomic_load(&client->client_id);

  if (truncated) {
    log_msg(remote_level, __FILE__, __LINE__, __func__, "[REMOTE CLIENT %u \"%s\"] %s [message truncated]", client_id,
            display_name, message);
  } else {
    log_msg(remote_level, __FILE__, __LINE__, __func__, "[REMOTE CLIENT %u \"%s\"] %s", client_id, display_name,
            message);
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
  log_debug_every(LOG_RATE_DEFAULT, "Received audio batch packet from client %u (len=%zu, is_sending_audio=%d)",
                  atomic_load(&client->client_id), len, atomic_load(&client->is_sending_audio));

  VALIDATE_NOTNULL_DATA(client, data, "AUDIO_BATCH");
  VALIDATE_MIN_SIZE(client, len, sizeof(audio_batch_packet_t), "AUDIO_BATCH");
  VALIDATE_AUDIO_STREAM_ENABLED(client, "AUDIO_BATCH");

  // Parse batch header using utility function
  audio_batch_info_t batch_info;
  asciichat_error_t parse_result = audio_parse_batch_header(data, len, &batch_info);
  if (parse_result != ASCIICHAT_OK) {
    disconnect_client_for_bad_data(client, "Failed to parse audio batch header");
    return;
  }

  uint32_t packet_batch_count = batch_info.batch_count;
  uint32_t total_samples = batch_info.total_samples;
  uint32_t sample_rate = batch_info.sample_rate;

  (void)packet_batch_count;
  (void)sample_rate;

  VALIDATE_NONZERO(client, packet_batch_count, "batch_count", "AUDIO_BATCH");
  VALIDATE_NONZERO(client, total_samples, "total_samples", "AUDIO_BATCH");

  size_t samples_bytes = 0;
  if (safe_size_mul(total_samples, sizeof(uint32_t), &samples_bytes)) {
    disconnect_client_for_bad_data(client, "AUDIO_BATCH sample size overflow (samples=%u)", total_samples);
    return;
  }

  size_t expected_size = sizeof(audio_batch_packet_t) + samples_bytes;
  if (len != expected_size) {
    disconnect_client_for_bad_data(client, "AUDIO_BATCH length mismatch: got %zu expected %zu", len, expected_size);
    return;
  }

  // Bounds check to prevent integer overflow on allocation
  // Maximum allowed samples: AUDIO_BATCH_SAMPLES * 2 (2048 samples)
  // This prevents total_samples * sizeof(float) from exceeding 8KB
  const uint32_t MAX_AUDIO_SAMPLES = AUDIO_BATCH_SAMPLES * 2;
  if (total_samples > MAX_AUDIO_SAMPLES) {
    disconnect_client_for_bad_data(client, "AUDIO_BATCH too many samples: %u (max: %u)", total_samples,
                                   MAX_AUDIO_SAMPLES);
    return;
  }

  const uint8_t *samples_ptr = (const uint8_t *)data + sizeof(audio_batch_packet_t);

  // Safe allocation: total_samples is bounded above, so multiplication won't overflow
  size_t alloc_size = (size_t)total_samples * sizeof(float);
  float *samples = SAFE_MALLOC(alloc_size, float *);
  if (!samples) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for audio sample conversion");
    return;
  }

  // Use helper function to dequantize samples
  asciichat_error_t dq_result = audio_dequantize_samples(samples_ptr, total_samples, samples);
  if (dq_result != ASCIICHAT_OK) {
    SAFE_FREE(samples);
    return;
  }

#ifndef NDEBUG
  static int recv_count = 0;
  recv_count++;
  if (recv_count % 100 == 0) {
    uint32_t raw0 = bytes_read_u32_unaligned(samples_ptr + 0 * sizeof(uint32_t));
    uint32_t raw1 = bytes_read_u32_unaligned(samples_ptr + 1 * sizeof(uint32_t));
    uint32_t raw2 = bytes_read_u32_unaligned(samples_ptr + 2 * sizeof(uint32_t));
    int32_t scaled0 = (int32_t)NET_TO_HOST_U32(raw0);
    int32_t scaled1 = (int32_t)NET_TO_HOST_U32(raw1);
    int32_t scaled2 = (int32_t)NET_TO_HOST_U32(raw2);
    log_info("RECV: network[0]=0x%08x, network[1]=0x%08x, network[2]=0x%08x", raw0, raw1, raw2);
    log_info("RECV: scaled[0]=%d, scaled[1]=%d, scaled[2]=%d", scaled0, scaled1, scaled2);
    log_info("RECV: samples[0]=%.6f, samples[1]=%.6f, samples[2]=%.6f", samples[0], samples[1], samples[2]);
  }
#endif

  if (client->incoming_audio_buffer) {
    asciichat_error_t write_result = audio_ring_buffer_write(client->incoming_audio_buffer, samples, total_samples);
    if (write_result != ASCIICHAT_OK) {
      log_error("Failed to write decoded audio batch to buffer: %s", asciichat_error_string(write_result));
    }
  }

  SAFE_FREE(samples);
}

/**
 * @brief Process AUDIO_OPUS_BATCH packet - efficient Opus-encoded audio batch from client
 *
 * Handles batched Opus-encoded audio frames sent by the client. This provides
 * ~98% bandwidth reduction compared to raw PCM audio while maintaining excellent
 * audio quality.
 *
 * PACKET STRUCTURE EXPECTED:
 * - opus_batch_header_t (16 bytes):
 *   - sample_rate (4 bytes)
 *   - frame_duration (4 bytes)
 *   - frame_count (4 bytes)
 *   - reserved (4 bytes)
 * - Opus encoded data (variable size, typically ~60 bytes/frame @ 24kbps)
 *
 * PROCESSING FLOW:
 * 1. Parse batch header
 * 2. Decode each Opus frame back to PCM samples (960 samples/frame @ 48kHz)
 * 3. Write decoded samples to client's incoming audio buffer
 *
 * PERFORMANCE:
 * - Bandwidth: ~60 bytes/frame vs ~3840 bytes/frame for raw PCM (64:1 compression)
 * - Quality: Excellent at 24 kbps VOIP mode
 * - Latency: 20ms frames for real-time audio
 *
 * @param client Client that sent the audio packet
 * @param data Packet payload data
 * @param len Packet payload length
 *
 * @see av_receive_audio_opus_batch() For packet parsing
 * @see opus_codec_decode() For Opus decoding
 * @see handle_audio_batch_packet() For raw PCM audio batch handling
 *
 * @ingroup server_protocol
 */
void handle_audio_opus_batch_packet(client_info_t *client, const void *data, size_t len) {
  log_debug_every(LOG_RATE_SLOW, "Received Opus audio batch from client %u (len=%zu)", atomic_load(&client->client_id),
                  len);

  VALIDATE_NOTNULL_DATA(client, data, "AUDIO_OPUS_BATCH");
  VALIDATE_AUDIO_STREAM_ENABLED(client, "AUDIO_OPUS_BATCH");
  VALIDATE_RESOURCE_INITIALIZED(client, client->opus_decoder, "Opus decoder");

  // Parse Opus batch packet
  const uint8_t *opus_data = NULL;
  size_t opus_size = 0;
  const uint16_t *frame_sizes = NULL;
  int sample_rate = 0;
  int frame_duration = 0;
  int frame_count = 0;

  asciichat_error_t result = packet_parse_opus_batch(data, len, &opus_data, &opus_size, &frame_sizes, &sample_rate,
                                                     &frame_duration, &frame_count);

  if (result != ASCIICHAT_OK) {
    disconnect_client_for_bad_data(client, "Failed to parse AUDIO_OPUS_BATCH packet");
    return;
  }

  VALIDATE_NONZERO(client, frame_count, "frame_count", "AUDIO_OPUS_BATCH");
  VALIDATE_NONZERO(client, opus_size, "opus_size", "AUDIO_OPUS_BATCH");

  // Calculate samples per frame (20ms @ 48kHz = 960 samples)
  int samples_per_frame = (sample_rate * frame_duration) / 1000;
  VALIDATE_RANGE(client, samples_per_frame, 1, 4096, "samples_per_frame", "AUDIO_OPUS_BATCH");

  // Use static buffer for common case to avoid malloc in hot path
  // Typical batches: 1-32 frames of 960 samples = up to 30,720 samples
  // Static buffer holds 32 frames @ 48kHz 20ms = 30,720 samples (120KB)
#define OPUS_DECODE_STATIC_MAX_SAMPLES (32 * 960)
  static float static_decode_buffer[OPUS_DECODE_STATIC_MAX_SAMPLES];

  size_t total_samples = (size_t)samples_per_frame * (size_t)frame_count;
  float *decoded_samples;
  bool used_malloc = false;

  if (total_samples <= OPUS_DECODE_STATIC_MAX_SAMPLES) {
    decoded_samples = static_decode_buffer;
  } else {
    // Unusual large batch - fall back to malloc
    log_warn("Client %u: Large audio batch requires malloc (%zu samples)", atomic_load(&client->client_id),
             total_samples);
    decoded_samples = SAFE_MALLOC(total_samples * sizeof(float), float *);
    if (!decoded_samples) {
      SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for Opus decoded samples");
      return;
    }
    used_malloc = true;
  }

  // Decode each Opus frame using frame_sizes array
  int total_decoded = 0;
  size_t opus_offset = 0;

  for (int i = 0; i < frame_count; i++) {
    // Get exact frame size from frame_sizes array (convert from network byte order)
    size_t frame_size = (size_t)NET_TO_HOST_U16(frame_sizes[i]);

    // DEBUG: Log the actual bytes of each Opus frame
    if (frame_size > 0) {
      log_debug_every(LOG_RATE_DEFAULT, "Client %u: Opus frame %d: size=%zu, first_bytes=[0x%02x,0x%02x,0x%02x,0x%02x]",
                      atomic_load(&client->client_id), i, frame_size, opus_data[opus_offset] & 0xFF,
                      frame_size > 1 ? (opus_data[opus_offset + 1] & 0xFF) : 0,
                      frame_size > 2 ? (opus_data[opus_offset + 2] & 0xFF) : 0,
                      frame_size > 3 ? (opus_data[opus_offset + 3] & 0xFF) : 0);
    }

    if (opus_offset + frame_size > opus_size) {
      log_error("Client %u: Frame %d size overflow (offset=%zu, frame_size=%zu, total=%zu)",
                atomic_load(&client->client_id), i + 1, opus_offset, frame_size, opus_size);
      if (used_malloc) {
        SAFE_FREE(decoded_samples);
      }
      return;
    }

    // SECURITY: Bounds check before writing decoded samples to prevent buffer overflow
    // An attacker could send malicious Opus frames that decode to more samples than expected
    if ((size_t)total_decoded + (size_t)samples_per_frame > total_samples) {
      log_error("Client %u: Opus decode would overflow buffer (decoded=%d, frame_samples=%d, max=%zu)",
                atomic_load(&client->client_id), total_decoded, samples_per_frame, total_samples);
      if (used_malloc) {
        SAFE_FREE(decoded_samples);
      }
      return;
    }

    int decoded_count = opus_codec_decode((opus_codec_t *)client->opus_decoder, &opus_data[opus_offset], frame_size,
                                          &decoded_samples[total_decoded], samples_per_frame);

    if (decoded_count < 0) {
      log_error("Client %u: Opus decoding failed for frame %d/%d (size=%zu)", atomic_load(&client->client_id), i + 1,
                frame_count, frame_size);
      if (used_malloc) {
        SAFE_FREE(decoded_samples);
      }
      return;
    }

    total_decoded += decoded_count;
    opus_offset += frame_size;
  }

  log_debug_every(LOG_RATE_DEFAULT, "Client %u: Decoded %d Opus frames -> %d samples", atomic_load(&client->client_id),
                  frame_count, total_decoded);

  // DEBUG: Log sample values to detect all-zero issue
  static int server_decode_count = 0;
  server_decode_count++;
  if (total_decoded > 0 && (server_decode_count <= 10 || server_decode_count % 100 == 0)) {
    float peak = 0.0f, rms = 0.0f;
    for (int i = 0; i < total_decoded && i < 100; i++) {
      float abs_val = fabsf(decoded_samples[i]);
      if (abs_val > peak)
        peak = abs_val;
      rms += decoded_samples[i] * decoded_samples[i];
    }
    rms = sqrtf(rms / (total_decoded > 100 ? 100 : total_decoded));
    // Log first 4 bytes of Opus data to compare with client encode
    log_info("SERVER OPUS DECODE #%d from client %u: decoded_rms=%.6f, opus_first4=[0x%02x,0x%02x,0x%02x,0x%02x]",
             server_decode_count, atomic_load(&client->client_id), rms, opus_size > 0 ? opus_data[0] : 0,
             opus_size > 1 ? opus_data[1] : 0, opus_size > 2 ? opus_data[2] : 0, opus_size > 3 ? opus_data[3] : 0);
  }

  // Write decoded samples to client's incoming audio buffer
  // Note: audio_ring_buffer_write returns error code, not sample count
  // Buffer overflow warnings are logged inside audio_ring_buffer_write if buffer is full
  if (client->incoming_audio_buffer && total_decoded > 0) {
    asciichat_error_t result = audio_ring_buffer_write(client->incoming_audio_buffer, decoded_samples, total_decoded);
    if (result != ASCIICHAT_OK) {
      log_error("Client %u: Failed to write decoded audio to buffer: %d", atomic_load(&client->client_id), result);
    }
  }

  if (used_malloc) {
    SAFE_FREE(decoded_samples);
  }
}

/**
 * @brief Process AUDIO_OPUS packet - decode single Opus frame from client
 *
 * Handles single Opus-encoded audio frames sent by clients. The packet format
 * includes a 16-byte header followed by the Opus-encoded data.
 *
 * PACKET STRUCTURE:
 * - Offset 0: sample_rate (uint32_t, native byte order - bug in client)
 * - Offset 4: frame_duration (uint32_t, native byte order - bug in client)
 * - Offset 8: reserved (8 bytes)
 * - Offset 16: Opus-encoded audio data
 *
 * @param client Client info structure
 * @param data Packet payload
 * @param len Packet length
 *
 * @ingroup server_protocol
 */
void handle_audio_opus_packet(client_info_t *client, const void *data, size_t len) {
  log_debug_every(LOG_RATE_DEFAULT, "Received Opus audio from client %u (len=%zu)", atomic_load(&client->client_id),
                  len);

  if (VALIDATE_PACKET_NOT_NULL(client, data, "AUDIO_OPUS")) {
    return;
  }

  // Minimum size: 16-byte header + at least 1 byte of Opus data
  if (len < 17) {
    disconnect_client_for_bad_data(client, "AUDIO_OPUS packet too small: %zu bytes", len);
    return;
  }

  if (!atomic_load(&client->is_sending_audio)) {
    disconnect_client_for_bad_data(client, "AUDIO_OPUS received before audio stream enabled");
    return;
  }

  if (!client->opus_decoder) {
    disconnect_client_for_bad_data(client, "Opus decoder not initialized");
    return;
  }

  // Parse header (16 bytes) - convert from network byte order
  const uint8_t *buf = (const uint8_t *)data;
  uint32_t sample_rate_net, frame_duration_net;
  memcpy(&sample_rate_net, buf, 4);
  memcpy(&frame_duration_net, buf + 4, 4);
  uint32_t sample_rate = NET_TO_HOST_U32(sample_rate_net);
  uint32_t frame_duration = NET_TO_HOST_U32(frame_duration_net);

  // Extract Opus data (after 16-byte header)
  const uint8_t *opus_data = buf + 16;
  size_t opus_size = len - 16;

  // Validate parameters
  if (sample_rate == 0 || sample_rate > 192000) {
    disconnect_client_for_bad_data(client, "AUDIO_OPUS invalid sample_rate: %u", sample_rate);
    return;
  }

  if (frame_duration == 0 || frame_duration > 120) {
    disconnect_client_for_bad_data(client, "AUDIO_OPUS invalid frame_duration: %u ms", frame_duration);
    return;
  }

  // Calculate expected samples per frame
  int samples_per_frame = (int)((sample_rate * frame_duration) / 1000);
  if (samples_per_frame <= 0 || samples_per_frame > 5760) { // Max 120ms @ 48kHz
    disconnect_client_for_bad_data(client, "AUDIO_OPUS invalid samples_per_frame: %d", samples_per_frame);
    return;
  }

  // Decode Opus frame
  float decoded_samples[5760]; // Max Opus frame size (120ms @ 48kHz)
  int decoded_count =
      opus_codec_decode((opus_codec_t *)client->opus_decoder, opus_data, opus_size, decoded_samples, samples_per_frame);

  if (decoded_count < 0) {
    log_error("Client %u: Opus decoding failed (size=%zu)", atomic_load(&client->client_id), opus_size);
    return;
  }

  log_debug_every(LOG_RATE_VERY_FAST, "Client %u: Decoded Opus frame -> %d samples", atomic_load(&client->client_id),
                  decoded_count);

  // Write decoded samples to client's incoming audio buffer
  if (client->incoming_audio_buffer && decoded_count > 0) {
    asciichat_error_t write_result =
        audio_ring_buffer_write(client->incoming_audio_buffer, decoded_samples, decoded_count);
    if (write_result != ASCIICHAT_OK) {
      log_error("Failed to write decoded Opus samples to buffer: %s", asciichat_error_string(write_result));
    }
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
  uint32_t client_id = atomic_load(&client->client_id);
  log_warn("[CAPS_HANDLER] 🟢 CAPS_RECEIVED: client_id=%u, data_ptr=%p, len=%zu bytes", client_id, data, len);

  log_debug("[CAPS_HANDLER] Step 1: Validating packet size (expected=%zu, actual=%zu)",
            sizeof(terminal_capabilities_packet_t), len);
  VALIDATE_PACKET_SIZE(client, data, len, sizeof(terminal_capabilities_packet_t), "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ Size validation passed");

  const terminal_capabilities_packet_t *caps = (const terminal_capabilities_packet_t *)data;

  // Extract and validate dimensions
  uint16_t width = NET_TO_HOST_U16(caps->width);
  uint16_t height = NET_TO_HOST_U16(caps->height);
  log_warn("[CAPS_HANDLER] 📐 DIMENSIONS: width=%u, height=%u", width, height);

  log_debug("[CAPS_HANDLER] Step 2: Validating width (value=%u, must be nonzero)", width);
  VALIDATE_NONZERO(client, width, "width", "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ Width nonzero check passed");

  log_debug("[CAPS_HANDLER] Step 3: Validating height (value=%u, must be nonzero)", height);
  VALIDATE_NONZERO(client, height, "height", "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ Height nonzero check passed");

  log_debug("[CAPS_HANDLER] Step 4: Validating width range (value=%u, range=1-4096)", width);
  VALIDATE_RANGE(client, width, 1, 4096, "width", "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ Width range check passed");

  log_debug("[CAPS_HANDLER] Step 5: Validating height range (value=%u, range=1-4096)", height);
  VALIDATE_RANGE(client, height, 1, 4096, "height", "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ Height range check passed");

  // Extract and validate color level (0=none, 1=16, 2=256, 3=truecolor)
  uint32_t color_level = NET_TO_HOST_U32(caps->color_level);
  log_debug("[CAPS_HANDLER] Step 6: Validating color_level (value=%u, range=0-3)", color_level);
  VALIDATE_RANGE(client, color_level, 0, 3, "color_level", "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ Color level check passed");

  // Extract and validate render mode (0=foreground, 1=background, 2=half-block)
  uint32_t render_mode = NET_TO_HOST_U32(caps->render_mode);
  log_debug("[CAPS_HANDLER] Step 7: Validating render_mode (value=%u, range=0-2)", render_mode);
  VALIDATE_RANGE(client, render_mode, 0, 2, "render_mode", "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ Render mode check passed");

  // Extract and validate palette type (0-5 are valid, 5=PALETTE_CUSTOM)
  uint32_t palette_type = NET_TO_HOST_U32(caps->palette_type);
  log_debug("[CAPS_HANDLER] Step 8: Validating palette_type (value=%u, range=0-5)", palette_type);
  VALIDATE_RANGE(client, palette_type, 0, 5, "palette_type", "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ Palette type check passed");

  // Validate desired FPS (1-144)
  log_debug("[CAPS_HANDLER] Step 9: Validating desired_fps (value=%u, range=1-144)", caps->desired_fps);
  VALIDATE_RANGE(client, caps->desired_fps, 1, 144, "desired_fps", "CLIENT_CAPABILITIES");
  log_debug("[CAPS_HANDLER] ✅ FPS check passed");

  mutex_lock(&client->client_state_mutex);

  atomic_store(&client->width, width);
  atomic_store(&client->height, height);

  log_debug("Client %u dimensions: %ux%u, desired_fps=%u", atomic_load(&client->client_id), client->width,
            client->height, caps->desired_fps);

  client->terminal_caps.capabilities = NET_TO_HOST_U32(caps->capabilities);
  client->terminal_caps.color_level = color_level;
  client->terminal_caps.color_count = NET_TO_HOST_U32(caps->color_count);
  client->terminal_caps.render_mode = render_mode;
  client->terminal_caps.detection_reliable = caps->detection_reliable;
  client->terminal_caps.wants_background = (render_mode == RENDER_MODE_BACKGROUND);

  SAFE_STRNCPY(client->terminal_caps.term_type, caps->term_type, sizeof(client->terminal_caps.term_type));
  SAFE_STRNCPY(client->terminal_caps.colorterm, caps->colorterm, sizeof(client->terminal_caps.colorterm));

  client->terminal_caps.utf8_support = NET_TO_HOST_U32(caps->utf8_support);
  client->terminal_caps.palette_type = palette_type;
  SAFE_STRNCPY(client->terminal_caps.palette_custom, caps->palette_custom,
               sizeof(client->terminal_caps.palette_custom));

  client->terminal_caps.desired_fps = caps->desired_fps;

  // Extract wants_padding flag (1=padding enabled, 0=no padding for snapshot/piped modes)
  client->terminal_caps.wants_padding = (caps->wants_padding != 0);

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
    SET_ERRNO(ERROR_INVALID_STATE, "Failed to initialize palette for client %d", atomic_load(&client->client_id));
    client->client_palette_initialized = false;
  }

  client->has_terminal_caps = true;

  log_info("Client %u capabilities: %ux%u, color_level=%s (%u colors), caps=0x%x, term=%s, colorterm=%s, "
           "render_mode=%s, reliable=%s, fps=%u, wants_padding=%d",
           atomic_load(&client->client_id), client->width, client->height,
           terminal_color_level_name(client->terminal_caps.color_level), client->terminal_caps.color_count,
           client->terminal_caps.capabilities, client->terminal_caps.term_type, client->terminal_caps.colorterm,
           (client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK
                ? "half-block"
                : (client->terminal_caps.render_mode == RENDER_MODE_BACKGROUND ? "background" : "foreground")),
           client->terminal_caps.detection_reliable ? "yes" : "no", client->terminal_caps.desired_fps,
           client->terminal_caps.wants_padding);

  // Send capabilities acknowledgment to client
  if (client->socket != INVALID_SOCKET_VALUE) {
    log_info_client(client, "Terminal configured: %ux%u, %s, %s mode, %u fps", client->width, client->height,
                    terminal_color_level_name(client->terminal_caps.color_level),
                    (client->terminal_caps.render_mode == RENDER_MODE_HALF_BLOCK
                         ? "half-block"
                         : (client->terminal_caps.render_mode == RENDER_MODE_BACKGROUND ? "background" : "foreground")),
                    client->terminal_caps.desired_fps);
  }

  mutex_unlock(&client->client_state_mutex);

  log_warn("[CAPS_HANDLER] ✅ CAPS_COMPLETE: client_id=%u - all validations passed, capabilities stored",
           atomic_load(&client->client_id));
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
  VALIDATE_PACKET_SIZE(client, data, len, sizeof(size_packet_t), "SIZE");

  const size_packet_t *size_pkt = (const size_packet_t *)data;

  // Extract and validate new dimensions
  uint16_t width = NET_TO_HOST_U16(size_pkt->width);
  uint16_t height = NET_TO_HOST_U16(size_pkt->height);

  VALIDATE_NONZERO(client, width, "width", "SIZE");
  VALIDATE_NONZERO(client, height, "height", "SIZE");
  VALIDATE_RANGE(client, width, 1, 4096, "width", "SIZE");
  VALIDATE_RANGE(client, height, 1, 4096, "height", "SIZE");

  mutex_lock(&client->client_state_mutex);
  client->width = width;
  client->height = height;
  mutex_unlock(&client->client_state_mutex);

  log_info("Client %u updated terminal size: %ux%u", atomic_load(&client->client_id), width, height);
}

/* ============================================================================
 * Protocol Control Packet Handlers
 * ============================================================================
 */

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
  net_state.connected_client_count = HOST_TO_NET_U32(state.connected_client_count);
  net_state.active_client_count = HOST_TO_NET_U32(state.active_client_count);
  memset(net_state.reserved, 0, sizeof(net_state.reserved));

  // Send server state via ACIP transport
  // Protect socket writes with send_mutex to prevent race with send_thread.
  mutex_lock(&client->send_mutex);
  asciichat_error_t result = acip_send_server_state(client->transport, &net_state);
  mutex_unlock(&client->send_mutex);

  if (result != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_NETWORK, "Failed to send server state to client %u: %s", client->client_id,
              asciichat_error_string(result));
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
  SET_ERRNO(ERROR_INVALID_STATE, "broadcast_clear_console_to_all_clients() called - unexpected usage");
  log_warn("CLEAR_CONSOLE is now sent from render threads, not broadcast");
}
