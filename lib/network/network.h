#pragma once

/**
 * @file network/network.h
 * @brief Core network I/O operations
 *
 * This header provides fundamental network I/O operations including
 * socket management, timeouts, and basic send/receive operations.
 */

#include "platform/socket.h"
#include <sys/types.h>

// Timeout constants (in seconds) - tuned for real-time video streaming
#define CONNECT_TIMEOUT 3 // Reduced for faster connection attempts
#define SEND_TIMEOUT 5    // Video frames need timely delivery
#define RECV_TIMEOUT 15   // If no data in 15 sec, connection is likely dead
#define ACCEPT_TIMEOUT 3  // Balance between responsiveness and CPU usage

// Keep-alive settings
#define KEEPALIVE_IDLE 60
#define KEEPALIVE_INTERVAL 10
#define KEEPALIVE_COUNT 8

/**
 * @brief Send data with timeout using chunked transmission
 * @param sockfd Socket file descriptor
 * @param data Data to send
 * @param len Length of data
 * @param timeout_seconds Timeout in seconds
 * @return Number of bytes sent, or -1 on error
 */
ssize_t send_with_timeout(socket_t sockfd, const void *data, size_t len, int timeout_seconds);

/**
 * @brief Receive data with timeout
 * @param sockfd Socket file descriptor
 * @param buf Buffer to receive data
 * @param len Length of buffer
 * @param timeout_seconds Timeout in seconds
 * @return Number of bytes received, or -1 on error
 */
ssize_t recv_with_timeout(socket_t sockfd, void *buf, size_t len, int timeout_seconds);

/**
 * @brief Accept connection with timeout
 * @param listenfd Listening socket
 * @param addr Client address structure
 * @param addrlen Length of address structure
 * @param timeout_seconds Timeout in seconds
 * @return Client socket, or -1 on error
 */
int accept_with_timeout(socket_t listenfd, struct sockaddr *addr, socklen_t *addrlen, int timeout_seconds);

/**
 * @brief Set socket timeout
 * @param sockfd Socket file descriptor
 * @param timeout_seconds Timeout in seconds
 * @return 0 on success, -1 on error
 */
int set_socket_timeout(socket_t sockfd, int timeout_seconds);

/**
 * @brief Set socket keepalive
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int set_socket_keepalive(socket_t sockfd);

/**
 * @brief Set socket non-blocking
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int set_socket_nonblocking(socket_t sockfd);

/**
 * @brief Get human-readable error string for network errors
 * @return Error string
 */
const char *network_error_string();

/**
 * @brief Connect with timeout
 * @param sockfd Socket file descriptor
 * @param addr Address to connect to
 * @param addrlen Address length
 * @param timeout_seconds Timeout in seconds
 * @return true on success, false on failure
 */
bool connect_with_timeout(socket_t sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout_seconds);
