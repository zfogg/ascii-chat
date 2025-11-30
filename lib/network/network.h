/**
 * @defgroup network Network Module
 * @ingroup module_network
 * @brief 🌐 Core network I/O operations with timeout support
 *
 * @file network/network.h
 * @ingroup network
 * @brief 🌐 Core network I/O operations with timeout support
 *
 * This module provides fundamental network I/O operations including socket
 * management, timeout handling, and basic send/receive operations. All
 * operations support configurable timeouts to prevent indefinite blocking,
 * critical for real-time video streaming applications.
 *
 * CORE RESPONSIBILITIES:
 * ======================
 * 1. Socket I/O operations with timeout support
 * 2. Connection management (connect, accept) with timeouts
 * 3. Socket configuration (keepalive, non-blocking, timeouts)
 * 4. Chunked transmission for large data transfers
 * 5. Error reporting and diagnostics
 *
 * ARCHITECTURAL OVERVIEW:
 * =======================
 *
 * TIMEOUT SYSTEM:
 * - All I/O operations support configurable timeouts
 * - Timeouts are tuned for real-time video streaming requirements
 * - Chunked transmission prevents large data from blocking indefinitely
 * - Platform-specific timeout implementation (select/poll/epoll)
 *
 * SOCKET CONFIGURATION:
 * - Keepalive settings to detect dead connections
 * - Non-blocking mode support for asynchronous I/O
 * - Socket-level timeout configuration
 * - Platform abstraction for cross-platform compatibility
 *
 * ERROR HANDLING:
 * - Comprehensive error reporting with human-readable messages
 * - Platform-specific error code translation
 * - Network error string utilities
 *
 * INTEGRATION WITH OTHER MODULES:
 * ===============================
 * - platform/socket.h: Uses platform socket abstraction
 * - Used by packet.h for packet transmission
 * - Used by av.h for media packet sending
 * - Used by handshake.c for crypto handshake operations
 *
 * PERFORMANCE CHARACTERISTICS:
 * ============================
 * - Chunked transmission enables progressive sending of large frames
 * - Timeout handling prevents indefinite blocking
 * - Keepalive settings detect dead connections efficiently
 * - Platform-specific optimizations where available
 *
 * @note Timeout values are tuned for real-time video streaming. Adjust
 *       CONNECT_TIMEOUT, SEND_TIMEOUT, and RECV_TIMEOUT if needed for
 *       different network conditions.
 *
 * @note Chunked transmission is used automatically for large data to
 *       prevent timeouts and enable progress tracking.
 *
 * @warning Timeout values that are too short may cause false positives
 *          on slow networks. Values that are too long may delay error
 *          detection unnecessarily.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 * @version 2.0 (Post-Modularization)
 */

#pragma once

#include "platform/socket.h"
#include "tests/test_env.h"
#include <sys/types.h>

/**
 * @name Network Timeout Constants
 * @{
 * @ingroup network
 *
 * Timeout values tuned for real-time video streaming. All timeouts
 * are specified in seconds.
 */

/**
 * @brief Connection timeout in seconds (3 seconds)
 *
 * Maximum time to wait for connection establishment. Reduced from
 * default for faster connection attempts and quicker failure detection.
 *
 * @ingroup network
 */
#define CONNECT_TIMEOUT 3

/**
 * @brief Send timeout in seconds (5 seconds)
 *
 * Maximum time to wait for data transmission. Video frames need timely
 * delivery to maintain real-time performance.
 *
 * @ingroup network
 */
#define SEND_TIMEOUT 5

/**
 * @brief Receive timeout in seconds (15 seconds)
 *
 * Maximum time to wait for incoming data. If no data is received within
 * this time, connection is considered dead.
 *
 * @ingroup network
 */
#define RECV_TIMEOUT 15

/**
 * @brief Accept timeout in seconds (3 seconds)
 *
 * Maximum time to wait for incoming connections. Balanced between
 * responsiveness and CPU usage.
 *
 * @ingroup network
 */
#define ACCEPT_TIMEOUT 3

/** @} */

/**
 * @name Test Environment Detection
 * @{
 * @ingroup network
 */

/**
 * @brief Check if we're in a test environment
 *
 * Compatibility macro that calls is_test_environment() from tests/test_env.h.
 * Used to adjust timeouts for faster test execution.
 *
 * @return 1 if test environment, 0 otherwise
 * @ingroup network
 */
#define network_is_test_environment() ((int)is_test_environment())

/** @} */

/**
 * @name Socket Keepalive Settings
 * @{
 * @ingroup network
 *
 * Keepalive settings to detect dead connections. TCP keepalive probes
 * are sent when connection is idle to detect broken connections.
 */

/**
 * @brief Keepalive idle time in seconds (60 seconds)
 *
 * Time to wait before sending first keepalive probe after connection
 * becomes idle.
 *
 * @ingroup network
 */
#define KEEPALIVE_IDLE 60

/**
 * @brief Keepalive interval in seconds (10 seconds)
 *
 * Interval between subsequent keepalive probes.
 *
 * @ingroup network
 */
#define KEEPALIVE_INTERVAL 10

/**
 * @brief Keepalive probe count (8 probes)
 *
 * Number of keepalive probes to send before considering connection dead.
 *
 * @ingroup network
 */
#define KEEPALIVE_COUNT 8

/** @} */

/**
 * @name Socket I/O Operations
 * @{
 * @ingroup network
 */

/**
 * @brief Send data with timeout using chunked transmission
 * @param sockfd Socket file descriptor
 * @param data Data to send
 * @param len Length of data in bytes
 * @param timeout_seconds Timeout in seconds
 * @return Number of bytes sent on success, -1 on error
 *
 * Sends data to socket with timeout support. Uses chunked transmission
 * for large data to prevent blocking and enable progress tracking.
 *
 * @note Chunked transmission automatically handles large frames by
 *       sending in smaller chunks, preventing timeout issues.
 *
 * @note Partial sends are handled automatically - function retries
 *       until all data is sent or timeout occurs.
 *
 * @ingroup network
 */
ssize_t send_with_timeout(socket_t sockfd, const void *data, size_t len, int timeout_seconds);

/**
 * @brief Receive data with timeout
 * @param sockfd Socket file descriptor
 * @param buf Buffer to receive data
 * @param len Length of buffer in bytes
 * @param timeout_seconds Timeout in seconds
 * @return Number of bytes received on success, -1 on error
 *
 * Receives data from socket with timeout support. Waits up to
 * timeout_seconds for data to arrive.
 *
 * @note Partial receives are possible - function returns number
 *       of bytes actually received.
 *
 * @note Returns -1 on timeout or error. Use network_error_string()
 *       to get human-readable error description.
 *
 * @ingroup network
 */
ssize_t recv_with_timeout(socket_t sockfd, void *buf, size_t len, int timeout_seconds);

/**
 * @brief Accept connection with timeout
 * @param listenfd Listening socket file descriptor
 * @param addr Output: Client address structure (can be NULL)
 * @param addrlen Input/output: Length of address structure
 * @param timeout_seconds Timeout in seconds
 * @return Client socket file descriptor on success, -1 on error
 *
 * Accepts incoming connection with timeout support. Waits up to
 * timeout_seconds for incoming connection.
 *
 * @note If addr is NULL, client address information is not returned.
 *
 * @note addrlen must point to initialized value containing size of
 *       addr buffer. On return, contains actual size of address.
 *
 * @ingroup network
 */
int accept_with_timeout(socket_t listenfd, struct sockaddr *addr, socklen_t *addrlen, int timeout_seconds);

/**
 * @brief Connect to server with timeout
 * @param sockfd Socket file descriptor
 * @param addr Server address structure
 * @param addrlen Address structure length
 * @param timeout_seconds Timeout in seconds
 * @return true on success, false on failure
 *
 * Establishes connection to server with timeout support. Waits up to
 * timeout_seconds for connection to complete.
 *
 * @note Uses platform-specific connection timeout mechanism (select/poll).
 *
 * @note Returns false on timeout or connection failure. Use network_error_string()
 *       to get human-readable error description.
 *
 * @ingroup network
 */
bool connect_with_timeout(socket_t sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout_seconds);

/** @} */

/**
 * @name Socket Configuration Functions
 * @{
 * @ingroup network
 */

/**
 * @brief Set socket timeout for send/receive operations
 * @param sockfd Socket file descriptor
 * @param timeout_seconds Timeout in seconds
 * @return 0 on success, -1 on error
 *
 * Configures socket-level timeout for send and receive operations.
 * This sets SO_SNDTIMEO and SO_RCVTIMEO socket options.
 *
 * @note Socket-level timeouts work in addition to application-level
 *       timeouts in send_with_timeout() and recv_with_timeout().
 *
 * @ingroup network
 */
int set_socket_timeout(socket_t sockfd, int timeout_seconds);

/**
 * @brief Enable TCP keepalive on socket
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 *
 * Enables TCP keepalive probes on socket using KEEPALIVE_IDLE,
 * KEEPALIVE_INTERVAL, and KEEPALIVE_COUNT settings.
 *
 * @note Keepalive helps detect dead connections (broken network path)
 *       without application-level heartbeats.
 *
 * @ingroup network
 */
int set_socket_keepalive(socket_t sockfd);

/**
 * @brief Set socket to non-blocking mode
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 *
 * Sets socket to non-blocking mode. I/O operations return immediately
 * with error if data is not available.
 *
 * @note Non-blocking sockets require different error handling -
 *       EAGAIN/EWOULDBLOCK indicates operation would block.
 *
 * @note Useful for asynchronous I/O patterns with select/poll/epoll.
 *
 * @ingroup network
 */
int set_socket_nonblocking(socket_t sockfd);

/** @} */

/**
 * @name Error Reporting Functions
 * @{
 * @ingroup network
 */

/**
 * @brief Get human-readable error string for network errors
 * @return Error string describing last network error
 *
 * Returns a human-readable description of the last network error.
 * Useful for error logging and user-facing error messages.
 *
 * @note Error string is thread-local - each thread has its own
 *       error state.
 *
 * @ingroup network
 */
const char *network_error_string();

/** @} */
