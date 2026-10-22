#include <ascii-chat/otel.h>
#include "connection_internal.h"
#include <ascii-chat/network/acip/backpressure.h>
/**
 * @file client/server.c
 * @ingroup client_connection
 * @brief 🌐 Client connection manager: TCP connection, reconnection with exponential backoff, and thread-safe
 * transmission
 *
 * The connection management follows a robust state machine:
 * 1. **Initialization**: Socket creation and address resolution
 * 2. **Connection**: TCP handshake with configurable timeout
 * 3. **Capability Exchange**: Send terminal capabilities and client info
 * 4. **Active Monitoring**: Health checks and keepalive management
 * 5. **Disconnection**: Graceful or forced connection teardown
 * 6. **Reconnection**: Exponential backoff retry logic
 *
 * ## Thread Safety
 *
 * All packet transmission functions use a global send mutex to prevent
 * interleaved packets on the wire. The socket file descriptor is protected
 * with atomic operations for thread-safe access across multiple threads.
 *
 * ## Reconnection Strategy
 *
 * Implements exponential backoff with jitter:
 * - Initial delay: 10ms
 * - Exponential growth: delay = 10ms + (200ms * attempt)
 * - Maximum delay: 5 seconds
 * - Jitter: Small random component to prevent thundering herd
 *
 * ## Platform Compatibility
 *
 * Uses platform abstraction layer for:
 * - Socket creation and management (Winsock vs BSD sockets)
 * - Network error handling and error string conversion
 * - Address resolution and connection timeout handling
 * - Socket options (keepalive, nodelay)
 *
 * ## Integration Points
 *
 * - **main.c**: Calls connection establishment and monitoring functions
 * - **protocol.c**: Uses thread-safe send functions for packet transmission
 * - **keepalive.c**: Monitors connection health and triggers reconnection
 * - **capture.c**: Sends media data through connection
 * - **audio.c**: Sends audio data through connection
 *
 * ## Error Handling
 *
 * Connection errors are classified into:
 * - **Temporary**: Network congestion, temporary DNS failures (retry)
 * - **Permanent**: Invalid address, authentication failure (report and exit)
 * - **Timeout**: Connection establishment timeout (retry with backoff)
 * - **Loss**: Existing connection broken (immediate reconnection attempt)
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 * @version 2.0
 */

#include <ascii-chat/ui/notice.h>

#include "server.h"
#include "session/protocol.h"
#include "main.h"
#include "../main.h" // Global exit API
#include "crypto.h"
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/video/h265/encoder.h>

#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/network/acip/client.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/connection_endpoint.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/url.h> // For WebSocket URL detection
#include <ascii-chat/common.h>
#include "display.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h> // For RCU-based options access
#include <ascii-chat/video/ascii/palette.h>
#include <ascii-chat/media/codecs.h>
#include <ascii-chat/buffer.h>

#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <sys/types.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/debug/named.h> // For NAMED_REGISTER_ATOMIC macro

#include <ascii-chat/platform/network.h> // Consolidates platform-specific network headers (includes TCP options)

// Debug flags
#define DEBUG_NETWORK 1
#define DEBUG_THREADS 1
#define DEBUG_MEMORY 1

/* ============================================================================
 * Connection State Management
 * ============================================================================ */

/**
 * @brief Atomic flag indicating if connection is currently active
 *
 * Thread-safe flag indicating whether the connection is established and active.
 * Checked by all threads to determine if they can use the socket safely.
 * Set to true on successful connection, false on disconnection.
 *
 * @ingroup client_connection
 */

// Publish a remote rejection from the receive worker to the reconnect owner.
void server_connection_set_remote_error(client_connection_t *connection, asciichat_error_t code) {
  atomic_store_int(&connection->remote_error, code);
}
asciichat_error_t server_connection_get_remote_error(client_connection_t *connection) {
  return (asciichat_error_t)atomic_load_int(&connection->remote_error);
}

/* ============================================================================
 * Crypto State
 * ============================================================================ */

/* ============================================================================
 * Reconnection Logic
 * ============================================================================ */

/** Maximum delay between reconnection attempts (microseconds) */
#define MAX_RECONNECT_DELAY (5 * 1000 * 1000)

/* ============================================================================
 * Public Interface Functions
 * ============================================================================ */

/**
 * @brief Initialize the server connection management subsystem
 *
 * Sets up the send mutex and initializes connection state variables.
 * Must be called once during client startup before any connection attempts.
 *
 * @return 0 on success, non-zero on failure
 *
 * @ingroup client_connection
 */
int server_connection_init(client_connection_t *connection) {
  ASCII_OTEL_SCOPE(SERVER_CONNECTION_INIT);
  // Initialize mutex for thread-safe packet sending
  if (mutex_init(&connection->send_mutex, "send") != 0) {
    log_error("Failed to initialize send mutex");
    return -1;
  }

  connection->send_mutex_initialized = true;

  // Initialize connection state
  connection->transport = NULL;
  atomic_store_bool(&connection->connection_active, false);
  atomic_store_bool(&connection->connection_lost, false);
  connection->my_client_id = 0;

  return 0;
}

/**
 * @brief Establish connection to ascii-chat server
 *
 * Attempts to connect to the specified server with full capability negotiation.
 * Implements reconnection logic with exponential backoff for failed attempts.
 * On successful connection, performs initial handshake including terminal
 * capabilities and client join protocol.
 *
 * @param address Server IP address or hostname
 * @param port Server port number
 * @param reconnect_attempt Current reconnection attempt number (0 for first)
 * @param first_connection True if this is the initial connection attempt
 * @param has_ever_connected True if a connection was ever successfully established
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
bool server_connection_is_active(client_connection_t *connection) {
  if (!connection)
    return false;
  return atomic_load_bool(&connection->connection_active) && connection->transport != NULL;
}

/**
 * @brief Get ACIP transport instance
 *
 * @return Transport instance or NULL if not connected
 *
 * @ingroup client_connection
 */
acip_transport_t *server_connection_get_transport(client_connection_t *connection) {
  return connection->transport;
}

void server_connection_set_transport(client_connection_t *connection, acip_transport_t *transport) {
  connection->transport = transport;
  atomic_store_bool(&connection->connection_active, transport != NULL);
  if (transport)
    atomic_store_bool(&connection->connection_lost, false);
}

/**
 * @brief Get client ID assigned by server
 *
 * @return Client ID (based on local port) or 0 if not connected
 *
 * @ingroup client_connection
 */
uint32_t server_connection_get_client_id(client_connection_t *connection) {
  return connection->my_client_id;
}

/**
 * @brief Get resolved server IP address
 *
 * Returns the server's IP address that was resolved during connection
 * establishment. Used for known_hosts verification and logging.
 *
 * @return Server IP address string (IPv4 or IPv6), or empty string if not connected
 *
 * @ingroup client_connection
 */
const char *server_connection_get_ip(client_connection_t *connection) {
  return connection->server_ip;
}

/**
 * @brief Set the server IP address
 *
 * Updates the global server IP address for the active transport connection.
 *
 * @param ip Server IP address string
 *
 * @ingroup client_connection
 */
void server_connection_set_ip(client_connection_t *connection, const char *ip) {
  if (ip) {
    SAFE_STRNCPY(connection->server_ip, ip, sizeof(connection->server_ip));
    log_debug("Server IP set to: %s", connection->server_ip);
  } else {
    connection->server_ip[0] = '\0';
    log_debug("Server IP cleared");
  }
}

/**
 * @brief Set the server port
 *
 * Updates the global server port for the active transport connection.
 *
 * @param port Server port number
 *
 * @ingroup client_connection
 */
void server_connection_set_port(client_connection_t *connection, int port) {
  connection->server_port = port;
  log_debug("Server port set to: %d", connection->server_port);
}

/**
 * @brief Get the server port
 *
 * Returns the currently configured server port.
 *
 * @return Server port number (0 if not set)
 *
 * @ingroup client_connection
 */
int server_connection_get_port(client_connection_t *connection) {
  return connection->server_port;
}

/* Called by the owner and its workers, never by a signal handler. The
 * transport remains allocated until the controlling thread joins all users. */
void server_connection_shutdown(client_connection_t *connection) {
  ASCII_OTEL_SCOPE(SERVER_CONNECTION_SHUTDOWN);
  if (!connection)
    return;
  atomic_store_bool(&connection->connection_active, false);
  atomic_store_bool(&connection->connection_lost, true);
  session_connection_interrupt_transport(connection->transport);
}

/**
 * @brief Signal that connection has been lost
 *
 * Called by other modules (typically protocol handlers) when they
 * detect connection failure. Triggers reconnection logic in main loop.
 *
 * @ingroup client_connection
 */
void server_connection_pong_received(client_connection_t *connection) {
  uint64_t sent = atomic_load_u64(&connection->telemetry_ping_ns);
  if (sent && sent != UINT64_MAX && atomic_cas_u64(&connection->telemetry_ping_ns, &sent, 0))
    ascii_otel_metric(ASCII_OTEL_METRIC_RTT, (double)(time_get_ns() - sent) / 1e9);
}
void server_connection_lost(client_connection_t *connection) {
  ASCII_OTEL_SCOPE(CONNECTION_LOST);
  if (!atomic_exchange_bool(&connection->connection_lost, true))
    ascii_otel_metric(ASCII_OTEL_METRIC_DISCONNECT, 1);
  atomic_store_bool(&connection->connection_active, false);

  // Don't re-enable terminal logging here - let the splash screen handle it
  // The reconnection splash will capture and display logs properly
  display_full_reset();
}

/**
 * @brief Check if connection loss has been detected
 *
 * @return true if connection loss was flagged, false otherwise
 *
 * @ingroup client_connection
 */
bool server_connection_is_lost(client_connection_t *connection) {
  if (!connection)
    return true;
  return atomic_load_bool(&connection->connection_lost);
}

/* ============================================================================
 * Thread Safety Interface
 * ============================================================================ */

/* ============================================================================
 * Thread-Safe Wrapper Functions
 * ============================================================================ */

/**
 * @brief Thread-safe packet transmission
 *
 * Sends a packet to the server with proper mutex protection and connection
 * state checking. Automatically handles encryption if crypto is ready.
 *
 * @param type Packet type identifier
 * @param data Packet payload
 * @param len Payload length
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_packet(client_connection_t *connection, packet_type_t type, const void *data,
                                       size_t len) {
  // Lock mutex for entire send operation to prevent concurrent socket writes
  mutex_lock(&connection->send_mutex);

  // Check connection status and get transport reference
  if (!atomic_load_bool(&connection->connection_active) || !connection->transport) {
    log_debug("[TRANSPORT_LIFECYCLE] threaded_send_packet() check failed: active=%s, transport=%p",
              atomic_load_bool(&connection->connection_active) ? "true" : "false", (void *)connection->transport);
    mutex_unlock(&connection->send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection not active or transport unavailable");
  }

  // Get transport reference - transport has its own internal synchronization
  acip_transport_t *transport = connection->transport;
  log_debug_every(LOG_RATE_SLOW, "[TRANSPORT_LIFECYCLE] threaded_send_packet() using transport=%p, is_connected=%s",
                  (void *)transport, acip_transport_is_connected(transport) ? "true" : "false");

  // Network I/O happens while holding mutex to prevent concurrent socket writes
  asciichat_error_t result = packet_send_via_transport(transport, type, data, len, 0);

  // Unlock after send completes
  mutex_unlock(&connection->send_mutex);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    log_debug("Packet send failed");
    if (!network_error_is_local_rejection(result))
      server_connection_lost(connection);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Thread-safe batched audio packet transmission
 *
 * Sends a batched audio packet to the server with proper mutex protection
 * and connection state checking. Automatically handles encryption if crypto
 * is ready.
 *
 * @param samples Audio sample buffer containing batched samples
 * @param num_samples Total number of samples in the batch
 * @param batch_count Number of audio chunks in this batch
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_audio_batch_packet(client_connection_t *connection, const float *samples,
                                                   int num_samples, int batch_count) {
  // Lock mutex for entire send operation to prevent concurrent socket writes
  mutex_lock(&connection->send_mutex);

  // Check connection status and get transport reference
  if (!atomic_load_bool(&connection->connection_active) || !connection->transport) {
    mutex_unlock(&connection->send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  // Get transport reference - transport has its own internal synchronization
  acip_transport_t *transport = connection->transport;

  // Network I/O happens while holding mutex to prevent concurrent socket writes
  asciichat_error_t result = acip_send_audio_batch(transport, samples, (uint32_t)num_samples, (uint32_t)batch_count);

  // Unlock after send completes
  mutex_unlock(&connection->send_mutex);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    if (!network_error_is_local_rejection(result))
      server_connection_lost(connection);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Thread-safe Opus audio frame transmission
 *
 * Sends a single Opus-encoded audio frame to the server with proper
 * synchronization and encryption support.
 *
 * @param opus_data Opus-encoded audio data
 * @param opus_size Size of encoded frame
 * @param sample_rate Sample rate in Hz
 * @param frame_duration Frame duration in milliseconds
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_audio_opus(client_connection_t *connection, const uint8_t *opus_data, size_t opus_size,
                                           int sample_rate, int frame_duration) {
  // Get transport reference while holding mutex (brief lock)
  mutex_lock(&connection->send_mutex);

  // Check connection status and get transport reference
  if (!atomic_load_bool(&connection->connection_active) || !connection->transport) {
    mutex_unlock(&connection->send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection not active or transport unavailable");
  }

  // Get transport reference - transport has its own internal synchronization
  acip_transport_t *transport = connection->transport;
  mutex_unlock(&connection->send_mutex);

  // Build Opus packet with header (outside mutex - no blocking I/O yet)
  size_t header_size = 16; // sample_rate (4), frame_duration (4), reserved (8)
  size_t total_size = header_size + opus_size;
  void *packet_data = buffer_alloc(NULL, total_size);
  if (!packet_data) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate buffer for Opus packet: %zu bytes", total_size);
  }

  // Write header in network byte order
  uint8_t *buf = (uint8_t *)packet_data;
  uint32_t sr = HOST_TO_NET_U32((uint32_t)sample_rate);
  uint32_t fd = HOST_TO_NET_U32((uint32_t)frame_duration);
  memcpy(buf, &sr, 4);
  memcpy(buf + 4, &fd, 4);
  memset(buf + 8, 0, 8); // Reserved

  // Copy Opus data
  memcpy(buf + header_size, opus_data, opus_size);

  // Network I/O happens OUTSIDE the mutex to prevent deadlock on TCP buffer full
  asciichat_error_t result =
      packet_send_via_transport(transport, PACKET_TYPE_AUDIO_OPUS_BATCH, packet_data, total_size, 0);

  // Clean up
  buffer_release(packet_data);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    if (!network_error_is_local_rejection(result))
      server_connection_lost(connection);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Thread-safe Opus audio batch packet transmission
 *
 * Sends a batch of Opus-encoded audio frames to the server with proper
 * synchronization and encryption support.
 *
 * @param opus_data Opus-encoded audio data (multiple frames concatenated)
 * @param opus_size Total size of Opus data in bytes
 * @param frame_sizes Array of individual frame sizes (variable-length frames)
 * @param frame_count Number of Opus frames in the batch
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_audio_opus_batch(client_connection_t *connection, const uint8_t *opus_data,
                                                 size_t opus_size, const uint16_t *frame_sizes, int frame_count) {
  // Lock mutex for entire send operation to prevent concurrent socket writes
  mutex_lock(&connection->send_mutex);

  // Check connection status and get transport reference
  if (!atomic_load_bool(&connection->connection_active) || !connection->transport) {
    mutex_unlock(&connection->send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection not active or transport unavailable");
  }

  // Get transport reference - transport has its own internal synchronization
  acip_transport_t *transport = connection->transport;

  // Network I/O happens while holding mutex to prevent concurrent socket writes
  // Opus uses 20ms frames at 48kHz (960 samples = 20ms)
  asciichat_error_t result =
      acip_send_audio_opus_batch(transport, opus_data, opus_size, frame_sizes, frame_count, 48000, 20);

  // Unlock after send completes
  mutex_unlock(&connection->send_mutex);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    if (!network_error_is_local_rejection(result))
      server_connection_lost(connection);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Thread-safe image frame transmission
 *
 * Sends image frames with serialization via g_send_mutex to prevent
 * race conditions when multiple threads write to the same TCP socket.
 *
 * @param pixel_data Pixel data buffer
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param pixel_format Pixel format (1=RGB24)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_image_frame(client_connection_t *connection, const void *pixel_data, uint32_t width,
                                            uint32_t height, uint32_t pixel_format) {
  if (!pixel_data) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Pixel data is NULL");
  }

  mutex_lock(&connection->send_mutex);

  // Get transport reference - may be NULL if connection was lost
  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport || !server_connection_is_active(connection)) {
    mutex_unlock(&connection->send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Transport unavailable");
  }

  // Send image frame with socket lock held
  asciichat_error_t result = acip_send_image_frame(transport, pixel_data, width, height, pixel_format);

  // Unlock after send completes
  mutex_unlock(&connection->send_mutex);

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    if (!network_error_is_local_rejection(result))
      server_connection_lost(connection);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Send H.265-encoded video frame to server (thread-safe)
 *
 * Encodes RGB frame using H.265 codec before transmission.
 * Much more bandwidth-efficient than raw pixel transmission.
 * Uses mutex to prevent interleaving with other packet sends.
 *
 * @param pixel_data RGB pixel data from camera
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_image_frame_encoded(client_connection_t *connection, const void *pixel_data,
                                                    uint32_t width, uint32_t height, video_codec_t codec,
                                                    bool *encoding_failed) {
  if (!encoding_failed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing video encoding status output");
  *encoding_failed = false;
  if (!pixel_data || width > UINT16_MAX || height > UINT16_MAX) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid video frame");
  }
  // The capture thread creates the selected encoder only when a frame is ready.
  if (!connection->video_encoder) {
    connection->video_encoder = video_encoder_create(codec, (uint16_t)width, (uint16_t)height);
    if (!connection->video_encoder) {
      *encoding_failed = true;
      return GET_ERRNO();
    }
  }
  h265_encoder_t *encoder = connection->video_encoder;
#ifndef __EMSCRIPTEN__
  mutex_lock(&connection->send_mutex);
  acip_transport_t *encoder_transport = server_connection_get_transport(connection);
  if (acip_outbound_needs_keyframe(
          encoder_transport, codec == VIDEO_CODEC_H264 ? PACKET_TYPE_IMAGE_FRAME_H264 : PACKET_TYPE_IMAGE_FRAME_H265,
          0))
    h265_encoder_request_keyframe(encoder);
  mutex_unlock(&connection->send_mutex);
#endif
  size_t output_buffer_size = 5 + ((size_t)width * height * 3) + 65536;
  if (buffer_workspace_reserve(&connection->encoded_video_workspace, NULL, output_buffer_size) != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate video output buffer: %zu bytes", output_buffer_size);
  uint8_t *output_buffer = connection->encoded_video_workspace.data;

  // h265_encode already writes the five-byte ACIP envelope for either codec.
  size_t total_size = output_buffer_size;
  asciichat_error_t encode_result =
      h265_encode(encoder, (uint16_t)width, (uint16_t)height, (const uint8_t *)pixel_data, output_buffer, &total_size);
  if (encode_result != ASCIICHAT_OK || total_size == 0) {
    *encoding_failed = encode_result != ASCIICHAT_OK;
    return encode_result;
  }

  // NOW acquire the send mutex just for the actual network transmission
  log_debug_every(US_PER_SEC_INT, "[H265_SEND_FRAME_3] About to acquire g_send_mutex for network send");
  mutex_lock(&connection->send_mutex);
  log_debug_every(US_PER_SEC_INT, "[H265_SEND_FRAME_4] ✅ Acquired g_send_mutex, checking transport");

  // Get transport reference - may be NULL if connection was lost
  acip_transport_t *transport = server_connection_get_transport(connection);
  if (!transport || !server_connection_is_active(connection)) {
    mutex_unlock(&connection->send_mutex);
    log_debug_every(US_PER_SEC_INT, "[H265_SEND_FRAME_5] Transport unavailable, releasing mutex");
    return SET_ERRNO(ERROR_NETWORK, "Transport unavailable");
  }

  // Send compressed frame with socket lock held (short critical section)
  log_debug_every(US_PER_SEC_INT, "[H265_SEND_FRAME_6] Sending packet via transport (%zu bytes)", total_size);
  asciichat_error_t result = packet_send_via_transport(
      transport, codec == VIDEO_CODEC_H264 ? PACKET_TYPE_IMAGE_FRAME_H264 : PACKET_TYPE_IMAGE_FRAME_H265, output_buffer,
      total_size, 0);
  log_debug_every(US_PER_SEC_INT, "[H265_SEND_FRAME_7] packet_send_via_transport returned: %d", result);

  // Unlock after send completes
  mutex_unlock(&connection->send_mutex);
  log_debug_every(US_PER_SEC_INT, "[H265_SEND_FRAME_8] ✅ Released g_send_mutex");

  log_debug_every(US_PER_SEC_INT, "[H265_SEND_FRAME_9] Retained video output workspace");

  // If send failed due to network error, signal connection loss
  if (result != ASCIICHAT_OK) {
    if (!network_error_is_local_rejection(result))
      server_connection_lost(connection);
    return result;
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Thread-safe ping packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_ping_packet(client_connection_t *connection) {
  // Use threaded_send_packet which handles encryption, mutex locking, and connection state
  // Without a probe ID, overlapping pings make subsequent pongs ambiguous.
  // Keep sending normally, but suppress RTT samples for this connection.
  uint64_t empty = 0;
  if (!atomic_cas_u64(&connection->telemetry_ping_ns, &empty, time_get_ns()))
    atomic_store_u64(&connection->telemetry_ping_ns, UINT64_MAX);
  asciichat_error_t result = threaded_send_packet(connection, PACKET_TYPE_PING, NULL, 0);
  if (result != ASCIICHAT_OK)
    atomic_store_u64(&connection->telemetry_ping_ns, UINT64_MAX);
  return result;
}

/**
 * @brief Thread-safe pong packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_pong_packet(client_connection_t *connection) {
  // Use threaded_send_packet which handles encryption, mutex locking, and connection state
  return threaded_send_packet(connection, PACKET_TYPE_PONG, NULL, 0);
}

/**
 * @brief Thread-safe stream start packet transmission
 *
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_stream_start_packet(client_connection_t *connection, uint32_t stream_type) {
  // Connection and transport availability is checked by threaded_send_packet()

  // Build STREAM_START packet locally
  uint32_t type_data = HOST_TO_NET_U32(stream_type);

  // Use threaded_send_packet() which handles encryption
  return threaded_send_packet(connection, PACKET_TYPE_STREAM_START, &type_data, sizeof(type_data));
}

/**
 * @brief Thread-safe terminal size packet transmission with auto-detection
 *
 * Sends terminal capabilities packet to the server including terminal size,
 * color capabilities, and rendering preferences. Auto-detects terminal
 * capabilities if not explicitly specified.
 *
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_terminal_size_with_auto_detect(client_connection_t *connection, unsigned short width,
                                                               unsigned short height) {
  terminal_capabilities_packet_t packet;
  session_protocol_build_capabilities(width, height, &packet);
  asciichat_error_t result = threaded_send_packet(connection, PACKET_TYPE_CLIENT_CAPABILITIES, &packet, sizeof(packet));
  if (result == ASCIICHAT_OK)
    connection->advertised_dimensions = (terminal_size_t){.cols = width, .rows = height};
  return result;
}

/**
 * @brief Thread-safe client join packet transmission
 *
 * Sends client join packet with display name and capabilities to the server.
 *
 * @param display_name Client display name
 * @param capabilities Client capability flags
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_client_join_packet(client_connection_t *connection, const char *display_name,
                                                   uint32_t capabilities) {
  // Connection and transport availability is checked by threaded_send_packet()

  // Build CLIENT_JOIN packet locally
  client_info_packet_t join_packet;
  SAFE_MEMSET(&join_packet, sizeof(join_packet), 0, sizeof(join_packet));
  join_packet.client_id = HOST_TO_NET_U32(0); // Will be assigned by server
  SAFE_SNPRINTF(join_packet.display_name, MAX_DISPLAY_NAME_LEN, "%s", display_name ? display_name : "Unknown");
  join_packet.capabilities = HOST_TO_NET_U32(capabilities);

  // Use threaded_send_packet() which handles encryption
  int send_result = threaded_send_packet(connection, PACKET_TYPE_CLIENT_JOIN, &join_packet, sizeof(join_packet));
  if (send_result == 0) {
    mutex_lock(&connection->send_mutex);
    bool active = atomic_load_u64(&connection->connection_active);
    if (active && connection->transport) {
      log_net_message(connection->transport, LOG_INFO, REMOTE_LOG_DIRECTION_CLIENT_TO_SERVER, __FILE__, __LINE__,
                      __func__, "CLIENT_JOIN sent (display=\"%s\", capabilities=0x%x)", join_packet.display_name,
                      capabilities);
    }
    mutex_unlock(&connection->send_mutex);
  }
  return send_result;
}
