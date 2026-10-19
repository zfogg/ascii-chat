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

#include "connection.h"
#include <ascii-chat/media/codecs.h>

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
int server_connection_init(client_connection_t *connection);

bool server_connection_is_active(client_connection_t *connection);

/**
 * @brief Get current socket file descriptor
 * @return Socket FD or INVALID_SOCKET_VALUE if disconnected
 *
 * @ingroup client_connection
 */
socket_t server_connection_get_socket(client_connection_t *connection);

/**
 * @brief Get ACIP transport instance
 * @return Transport instance or NULL if not connected
 *
 * @ingroup client_connection
 */
struct acip_transport *server_connection_get_transport(client_connection_t *connection);

/**
 * @brief Set ACIP transport instance from connection fallback
 * @param transport Transport instance created by connection_attempt_with_fallback()
 *
 * Used to integrate the transport from the 3-stage connection fallback orchestrator
 * (TCP → STUN → TURN) into the server connection management layer.
 *
 * @ingroup client_connection
 */
void server_connection_set_transport(client_connection_t *connection, struct acip_transport *transport);

/**
 * @brief Get client ID assigned by server
 * @return Client ID or 0 if not connected
 *
 * @ingroup client_connection
 */
uint32_t server_connection_get_client_id(client_connection_t *connection);

/**
 * @brief Get resolved server IP address
 * @return Server IP address string (IPv4 or IPv6)
 *
 * @ingroup client_connection
 */
const char *server_connection_get_ip(client_connection_t *connection);

/**
 * @brief Set the server IP address
 * @param ip Server IP address string
 *
 * @ingroup client_connection
 */
void server_connection_set_ip(client_connection_t *connection, const char *ip);

/**
 * @brief Set the server port
 * @param port Server port number
 *
 * @ingroup client_connection
 */
void server_connection_set_port(client_connection_t *connection, int port);

/**
 * @brief Get the server port
 * @return Server port number
 *
 * @ingroup client_connection
 */
int server_connection_get_port(client_connection_t *connection);

/**
 * @brief Interrupt connection I/O from normal thread context
 *
 * @ingroup client_connection
 */
void server_connection_shutdown(client_connection_t *connection);

/**
 * @brief Signal that connection has been lost
 *
 * @ingroup client_connection
 */
void server_connection_lost(client_connection_t *connection);
void server_connection_set_remote_error(client_connection_t *connection, asciichat_error_t code);
asciichat_error_t server_connection_get_remote_error(client_connection_t *connection);

/**
 * @brief Check if connection loss was detected
 * @return true if connection lost, false otherwise
 *
 * @ingroup client_connection
 */
bool server_connection_is_lost(client_connection_t *connection);

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
asciichat_error_t threaded_send_packet(client_connection_t *connection, packet_type_t type, const void *data,
                                       size_t len);

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
asciichat_error_t threaded_send_audio_batch_packet(client_connection_t *connection, const float *samples,
                                                   int num_samples, int batch_count);

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
asciichat_error_t threaded_send_audio_opus(client_connection_t *connection, const uint8_t *opus_data, size_t opus_size,
                                           int sample_rate, int frame_duration);

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
asciichat_error_t threaded_send_audio_opus_batch(client_connection_t *connection, const uint8_t *opus_data,
                                                 size_t opus_size, const uint16_t *frame_sizes, int frame_count);

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
asciichat_error_t threaded_send_image_frame(client_connection_t *connection, const void *pixel_data, uint32_t width,
                                            uint32_t height, uint32_t pixel_format);

/**
 * @brief Send HEVC/H.264-encoded video frame to server (thread-safe)
 *
 * Encodes RGB frame using HEVC/H.264 codec before transmission.
 * Much more bandwidth-efficient than raw pixel transmission.
 * Uses mutex to prevent interleaving with other packet sends.
 *
 * @param pixel_data RGB pixel data from camera
 * @param width Frame width in pixels
 * @param height Frame height in pixels
 * @param codec HEVC or H.264 packet and encoder selection
 * @param encoding_failed Set only for encoder failures, never transport failures
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_image_frame_encoded(client_connection_t *connection, const void *pixel_data,
                                                    uint32_t width, uint32_t height, video_codec_t codec,
                                                    bool *encoding_failed);

/**
 * @brief Thread-safe ping packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_ping_packet(client_connection_t *connection);

/**
 * @brief Thread-safe pong packet transmission
 *
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_pong_packet(client_connection_t *connection);

/**
 * @brief Thread-safe stream start packet transmission
 *
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_stream_start_packet(client_connection_t *connection, uint32_t stream_type);

/**
 * @brief Thread-safe terminal size packet transmission with auto-detection
 *
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_terminal_size_with_auto_detect(client_connection_t *connection, unsigned short width,
                                                               unsigned short height);

/**
 * @brief Thread-safe client join packet transmission
 *
 * @param display_name Client display name
 * @param capabilities Client capability flags
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
asciichat_error_t threaded_send_client_join_packet(client_connection_t *connection, const char *display_name,
                                                   uint32_t capabilities);

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
