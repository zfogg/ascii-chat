/**
 * @file client/server.h
 * @ingroup client_connection
 * @brief ASCII-Chat Client Server Connection Management Interface
 *
 * Provides connection establishment, management, and thread-safe packet
 * transmission functions for communicating with ASCII-Chat servers.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include "platform/socket.h"
#include "network/packet_types.h"
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
 * @brief Establish connection to ASCII-Chat server
 * @param address Server IP address or hostname
 * @param port Server port number
 * @param reconnect_attempt Current reconnection attempt (0 for first)
 * @param first_connection True if this is the initial connection
 * @param has_ever_connected True if a connection was ever successfully established
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int server_connection_establish(const char *address, int port, int reconnect_attempt, bool first_connection,
                                bool has_ever_connected);

/**
 * @brief Check if server connection is currently active
 * @return true if connected, false otherwise
 *
 * @ingroup client_connection
 */
bool server_connection_is_active();

/**
 * @brief Get current socket file descriptor
 * @return Socket FD or INVALID_SOCKET_VALUE if disconnected
 *
 * @ingroup client_connection
 */
socket_t server_connection_get_socket();

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
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_packet(packet_type_t type, const void *data, size_t len);

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
int threaded_send_stream_start_packet(uint32_t stream_type);

/**
 * @brief Thread-safe terminal size packet transmission with auto-detection
 *
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 *
 * @ingroup client_connection
 */
int threaded_send_terminal_size_with_auto_detect(unsigned short width, unsigned short height);

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
