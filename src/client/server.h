/**
 * @file server.h
 * @brief ASCII-Chat Client Server Connection Management Interface
 *
 * Provides connection establishment, management, and thread-safe packet
 * transmission functions for communicating with ASCII-Chat servers.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date 2025
 */

#pragma once

#include "platform/abstraction.h"
#include "network.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * Connection Management Functions
 * ============================================================================ */

/**
 * @brief Initialize server connection management subsystem
 * @return 0 on success, negative on error
 */
int server_connection_init();

/**
 * @brief Establish connection to ASCII-Chat server
 * @param address Server IP address or hostname
 * @param port Server port number
 * @param reconnect_attempt Current reconnection attempt (0 for first)
 * @param first_connection True if this is the initial connection
 * @return 0 on success, negative on error
 */
int server_connection_establish(const char *address, int port, int reconnect_attempt, bool first_connection, bool has_ever_connected);

/**
 * @brief Check if server connection is currently active
 * @return true if connected, false otherwise
 */
bool server_connection_is_active();

/**
 * @brief Get current socket file descriptor
 * @return Socket FD or INVALID_SOCKET_VALUE if disconnected
 */
socket_t server_connection_get_socket();

/**
 * @brief Get client ID assigned by server
 * @return Client ID or 0 if not connected
 */
uint32_t server_connection_get_client_id();

/**
 * @brief Close server connection gracefully
 */
void server_connection_close();

/**
 * @brief Emergency shutdown for signal handlers
 */
void server_connection_shutdown();

/**
 * @brief Signal that connection has been lost
 */
void server_connection_lost();

/**
 * @brief Check if connection loss was detected
 * @return true if connection lost, false otherwise
 */
bool server_connection_is_lost();

/**
 * @brief Cleanup connection management subsystem
 */
void server_connection_cleanup();

// Thread-safe network functions
int threaded_send_packet(packet_type_t type, const void *data, size_t len);
int threaded_send_audio_batch_packet(const float *samples, int num_samples, int batch_count);
int threaded_send_ping_packet(void);
int threaded_send_pong_packet(void);
int threaded_send_stream_start_packet(uint32_t stream_type);
int threaded_send_terminal_size_with_auto_detect(unsigned short width, unsigned short height);
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
 */
int server_send_packet(packet_type_t type, const void *data, size_t len);

/**
 * @brief Send audio data to server
 * @param samples Audio sample buffer
 * @param num_samples Number of samples
 * @return 0 on success, negative on error
 */
int server_send_audio(const float *samples, int num_samples);

/**
 * @brief Send batched audio data to server
 * @param samples Batched audio samples
 * @param num_samples Total sample count
 * @param batch_count Number of packets in batch
 * @return 0 on success, negative on error
 */
int server_send_audio_batch(const float *samples, int num_samples, int batch_count);

/**
 * @brief Send terminal capabilities to server
 * @param width Terminal width in characters
 * @param height Terminal height in characters
 * @return 0 on success, negative on error
 */
int server_send_terminal_capabilities(unsigned short width, unsigned short height);

/**
 * @brief Send ping keepalive packet
 * @return 0 on success, negative on error
 */
int server_send_ping();

/**
 * @brief Send pong response packet
 * @return 0 on success, negative on error
 */
int server_send_pong();

/**
 * @brief Send stream start notification
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 */
int server_send_stream_start(uint32_t stream_type);

/**
 * @brief Send stream stop notification
 * @param stream_type Type of stream (audio/video)
 * @return 0 on success, negative on error
 */
int server_send_stream_stop(uint32_t stream_type);
