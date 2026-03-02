/**
 * @file client/server.h
 * @ingroup client_connection
 * @brief ascii-chat Client Server Connection Management Interface
 *
 * Provides connection establishment, management, and thread-safe packet
 * transmission functions for communicating with ascii-chat servers.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include <ascii-chat/platform/socket.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/log/log.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Connection Error Codes
 * ============================================================================ */

/**
 * @brief Connection establishment error codes
 *
 * These codes are returned by server_connection_establish() to indicate
 * different failure modes. Zero indicates success.
 */
typedef enum {
  CONNECTION_SUCCESS = 0,                ///< Connection established successfully
  CONNECTION_WARNING_NO_CLIENT_AUTH = 1, ///< Server not using client verification (warning)
  CONNECTION_ERROR_GENERIC = -1,         ///< Generic error (retry allowed)
  CONNECTION_ERROR_AUTH_FAILED = -2,     ///< Authentication failure (no retry)
  CONNECTION_ERROR_HOST_KEY_FAILED = -3  ///< Host key verification failed (no retry)
} connection_error_t;

/* ============================================================================
 * Connection Management Functions
 * ============================================================================ */

/**
 * @brief Initialize server connection management subsystem
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_connection_init();

/**
 * @brief Establish connection to ascii-chat server
 *
 * Establishes a TCP connection to the server and performs the cryptographic handshake.
 * This function handles DNS resolution, socket creation, connection establishment,
 * and authentication. It is designed for use in a reconnection loop with exponential backoff.
 *
 * @param address Server IP address, hostname, or ACDS session string (e.g., "localhost",
 *                 "192.168.1.100", or "blue-mountain-tiger")
 * @param port Server port number (typically 27224)
 * @param reconnect_attempt Current reconnection attempt number (0 for first attempt).
 *                          Used to track retry count for logging and backoff calculation.
 * @param first_connection Whether this is the client's first attempt to connect.
 *                         If true, displays more informative messages. If false,
 *                         this is a reconnection attempt after previous connection loss.
 * @param has_ever_connected Whether the client has EVER successfully connected in this session.
 *                           Used to distinguish "first attempt ever" from "reconnection attempt".
 * @return Connection establishment status:
 *         - CONNECTION_SUCCESS (0): Connection established and authenticated successfully
 *         - CONNECTION_WARNING_NO_CLIENT_AUTH (1): Connected but server is not verifying client identity
 *         - CONNECTION_ERROR_GENERIC (-1): Network error (resolve failed, connect refused, timeout).
 *           Caller should retry with exponential backoff.
 *         - CONNECTION_ERROR_AUTH_FAILED (-2): Authentication failed (wrong password, bad SSH key).
 *           Caller should NOT retry - user intervention required.
 *         - CONNECTION_ERROR_HOST_KEY_FAILED (-3): Server host key verification failed.
 *           Caller should NOT retry - user must update known_hosts.
 *
 * ## Connection Lifecycle
 *
 * 1. **Resolution**: Resolves hostname/IP to socket address (supports IPv4 and IPv6)
 * 2. **Socket Creation**: Creates TCP socket with appropriate options
 * 3. **Connection**: Attempts TCP connect with timeout
 * 4. **Handshake**: Performs cryptographic handshake (X25519 key exchange, Ed25519 auth)
 * 5. **Initialization**: Sends client capabilities and client join packet
 * 6. **State Update**: Updates global connection state atomically
 *
 * ## Error Recovery
 *
 * **Retryable (CONNECTION_ERROR_GENERIC)**:
 * - Connection refused: Server not listening yet
 * - Connection timeout: Network slow or firewall blocking
 * - DNS resolution failure: Temporary DNS issue
 * - Socket creation failure: System resource exhaustion
 *
 * Caller should implement exponential backoff:
 * @code{.c}
 * int result = server_connection_establish(address, port, attempt, first, ever_connected);
 * if (result == CONNECTION_ERROR_GENERIC) {
 *   // Exponential backoff: 10ms, 210ms, 410ms, 610ms, ..., capped at 5 seconds
 *   int delay_ms = 10 + (200 * attempt);
 *   if (delay_ms > 5000) delay_ms = 5000;
 *   sleep_ms(delay_ms);
 *   attempt++;
 *   continue;  // Retry
 * }
 * @endcode
 *
 * **Non-Retryable (AUTH or HOST_KEY failures)**:
 * - `CONNECTION_ERROR_AUTH_FAILED`: Password or SSH key doesn't match server's expectations
 * - `CONNECTION_ERROR_HOST_KEY_FAILED`: Server's public key not in client's known_hosts
 *
 * These indicate configuration problems that won't be fixed by waiting or retrying.
 *
 * @ingroup client_connection
 *
 * @see server_connection_is_active "Check connection status"
 * @see server_connection_close "Close connection gracefully"
 * @see server_connection_lost "Signal connection loss (called by other threads)"
 * @see topic_client_connection "Connection Management Architecture"
 */
int server_connection_establish(const char *address, int port, int reconnect_attempt, bool first_connection,
                                bool has_ever_connected);

bool server_connection_is_active();

/**
 * @brief Get current socket file descriptor
 * @return Socket FD or INVALID_SOCKET_VALUE if disconnected
 *
 * @ingroup client_connection
 */
socket_t server_connection_get_socket();

/**
 * @brief Get ACIP transport instance
 * @return Transport instance or NULL if not connected
 *
 * @ingroup client_connection
 */
struct acip_transport *server_connection_get_transport(void);

/**
 * @brief Set ACIP transport instance from connection fallback
 * @param transport Transport instance created by connection_attempt_with_fallback()
 *
 * Used to integrate the transport from the 3-stage connection fallback orchestrator
 * (TCP → STUN → TURN) into the server connection management layer.
 *
 * @ingroup client_connection
 */
void server_connection_set_transport(struct acip_transport *transport);

/**
 * @brief Get client ID assigned by server
 * @return Client ID or 0 if not connected
 *
 * @ingroup client_connection
 */
uint32_t server_connection_get_client_id();

/**
 * @brief Get resolved server IP address
 * @return Server IP address string (IPv4 or IPv6)
 *
 * @ingroup client_connection
 */
const char *server_connection_get_ip();

/**
 * @brief Set the server IP address
 * @param ip Server IP address string
 *
 * @ingroup client_connection
 */
void server_connection_set_ip(const char *ip);

/**
 * @brief Set the server port
 * @param port Server port number
 *
 * @ingroup client_connection
 */
void server_connection_set_port(int port);

/**
 * @brief Get the server port
 * @return Server port number
 *
 * @ingroup client_connection
 */
int server_connection_get_port(void);

/**
 * @brief Close server connection gracefully
 *
 * @ingroup client_connection
 */
void server_connection_close();

/**
 * @brief Emergency shutdown for signal handlers
 *
 * @ingroup client_connection
 */
void server_connection_shutdown();

/**
 * @brief Signal that connection has been lost
 *
 * @ingroup client_connection
 */
void server_connection_lost();

/**
 * @brief Check if connection loss was detected
 * @return true if connection lost, false otherwise
 *
 * @ingroup client_connection
 */
bool server_connection_is_lost();

/**
 * @brief Cleanup connection management subsystem
 *
 * @ingroup client_connection
 */
void server_connection_cleanup();

/**
 * @brief Thread-safe packet transmission
 *
 * Sends a packet to the server with proper mutex protection and connection
 * state checking. Automatically handles encryption if crypto is ready.
 *
 * @param type Packet type identifier
 * @param data Packet payload
 * @param len Payload length
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_packet(packet_type_t type, const void *data, size_t len);

/**
 * @brief Thread-safe batched audio packet transmission
 *
 * @param samples Audio sample buffer containing batched samples
 * @param num_samples Total number of samples in the batch
 * @param batch_count Number of audio chunks in this batch
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_audio_batch_packet(const float *samples, int num_samples, int batch_count);

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
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_audio_opus(const uint8_t *opus_data, size_t opus_size, int sample_rate,
                                           int frame_duration);

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
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_audio_opus_batch(const uint8_t *opus_data, size_t opus_size,
                                                 const uint16_t *frame_sizes, int frame_count);

/**
 * @brief Thread-safe image frame transmission
 *
 * Sends image frames with serialization via mutex to prevent race conditions
 * when multiple threads write to the same TCP socket.
 *
 * @param pixel_data Pixel data buffer (RGB24 format)
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param pixel_format Pixel format (1=RGB24)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_image_frame(const void *pixel_data, uint32_t width, uint32_t height,
                                            uint32_t pixel_format);

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
asciichat_error_t threaded_send_image_frame_h265(const void *pixel_data, uint32_t width, uint32_t height);

/**
 * @brief Thread-safe ping packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_ping_packet(void);

/**
 * @brief Thread-safe pong packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_pong_packet(void);

/**
 * @brief Thread-safe stream start packet transmission
 *
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_stream_start_packet(uint32_t stream_type);

/**
 * @brief Thread-safe terminal size packet transmission with auto-detection
 *
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_terminal_size_with_auto_detect(unsigned short width, unsigned short height);

/**
 * @brief Thread-safe client join packet transmission
 *
 * @param display_name Client display name
 * @param capabilities Client capability flags
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_client_join_packet(const char *display_name, uint32_t capabilities);

/* ============================================================================
 * Thread-Safe Packet Sending Functions
 * ============================================================================ */

/**
 * @brief Send general packet to server
 * @param type Packet type identifier
 * @param data Packet payload
 * @param len Payload length
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_send_packet(packet_type_t type, const void *data, size_t len);

/**
 * @brief Send audio data to server
 * @param samples Audio sample buffer
 * @param num_samples Number of samples
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_send_audio(const float *samples, int num_samples);

/**
 * @brief Send batched audio data to server
 * @param samples Batched audio samples
 * @param num_samples Total sample count
 * @param batch_count Number of packets in batch
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_send_audio_batch(const float *samples, int num_samples, int batch_count);

/**
 * @brief Send terminal capabilities to server
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_send_terminal_capabilities(unsigned short width, unsigned short height);

/**
 * @brief Send ping keepalive packet
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_send_ping();

/**
 * @brief Send pong response packet
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_send_pong();

/**
 * @brief Send stream start notification
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_send_stream_start(uint32_t stream_type);

/**
 * @brief Send stream stop notification
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_send_stream_stop(uint32_t stream_type);
