
/**
 * @file network/network.c
 * @ingroup network
 * @brief 🌐 Cross-platform socket I/O with timeout management and connection handling
 */

#include "network.h"
#include "common.h"
#include "asciichat_errno.h"
#include "platform/socket.h"
#include <stdint.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

/* ============================================================================
 * Core Network I/O Operations
 * ============================================================================
 * This module provides the fundamental network I/O operations including
 * socket management, timeouts, and basic send/receive operations.
 */

// Use network_network_is_test_environment() from network.h

/**
 * @brief Platform-specific error handling for send operations
 * @param error Error code
 * @return 1 if should retry, 0 if fatal error
 */
static int network_handle_send_error(int error) {
  if (error == EAGAIN || error == EWOULDBLOCK) {
    return 1; // Retry
  }
  if (error == EPIPE) {
    SET_ERRNO_SYS(ERROR_NETWORK, "Connection closed by peer during send");
    return 0; // Fatal
  }
  SET_ERRNO_SYS(ERROR_NETWORK, "send_with_timeout failed: %s", socket_get_error_string());
  return 0; // Fatal
}

/**
 * @brief Platform-specific error handling for recv operations
 * @param error Error code
 * @return 1 if should retry, 0 if fatal error
 */
static int network_handle_recv_error(int error) {
  // Check for "would block" - retry operation
  if (socket_is_would_block_error(error)) {
    return 1; // Retry
  }

  // Check for signal interruption - recoverable, retry
  if (error == EINTR) {
    log_debug("recv interrupted by signal, retrying");
    return 1; // Retry
  }

  // Check for invalid/closed socket - fatal
  if (socket_is_invalid_socket_error(error)) {
    SET_ERRNO_SYS(ERROR_NETWORK, "Socket is not a socket (closed by another thread)");
    return 0; // Fatal
  }

  SET_ERRNO_SYS(ERROR_NETWORK, "recv_with_timeout failed: %s", socket_get_error_string());
  return 0; // Fatal
}

/**
 * @brief Platform-specific select error handling
 * @param result Select result
 * @return 1 if should retry, 0 if fatal error
 */
static int network_handle_select_error(int result) {
  if (result == 0) {
    // Timeout - this is expected behavior for server waiting for connections
    asciichat_errno = ERROR_NETWORK_TIMEOUT;
    asciichat_errno_context.code = ERROR_NETWORK_TIMEOUT;
    asciichat_errno_context.has_system_error = true;
    asciichat_errno_context.system_errno = ETIMEDOUT;
    return 0; // Not an error, but don't retry
  }

  // Get error code (cross-platform)
  int error = socket_get_last_error();

  // Check for signal interruption - recoverable, retry
  if (error == EINTR) {
    log_debug("select interrupted by signal, retrying");
    return 1; // Retry
  }

  SET_ERRNO_SYS(ERROR_NETWORK, "select failed: %s", socket_get_error_string());
  return 0; // Fatal
}

/**
 * @brief Send data with timeout using chunked transmission
 * @param sockfd Socket file descriptor
 * @param data Data to send
 * @param len Length of data
 * @param timeout_seconds Timeout in seconds
 * @return Number of bytes sent, or -1 on error
 */
ssize_t send_with_timeout(socket_t sockfd, const void *data, size_t len, int timeout_seconds) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return -1;
  }

  size_t total_sent = 0;
  const char *data_ptr = (const char *)data;

  while (total_sent < len) {
    // Calculate chunk size
    size_t bytes_to_send = len - total_sent;
    const size_t MAX_CHUNK_SIZE = 65536; // 64KB chunks for reliable TCP transmission
    if (bytes_to_send > MAX_CHUNK_SIZE) {
      bytes_to_send = MAX_CHUNK_SIZE;
    }

    // Set up select for write timeout
    fd_set write_fds;
    struct timeval timeout;

    socket_fd_zero(&write_fds);
    socket_fd_set(sockfd, &write_fds);

    timeout.tv_sec = network_is_test_environment() ? 1 : timeout_seconds;
    timeout.tv_usec = 0;

    int result = socket_select(sockfd, NULL, &write_fds, NULL, &timeout);
    if (result <= 0) {
      if (result == 0) {
        SET_ERRNO_SYS(ERROR_NETWORK_TIMEOUT, "send_with_timeout timed out after %d seconds", timeout_seconds);
        return -1;
      }
      if (network_handle_select_error(result)) {
        continue; // Retry
      }
      return -1; // Fatal error
    }

    // Check if socket is ready for writing
    if (!socket_fd_isset(sockfd, &write_fds)) {
      SET_ERRNO_SYS(ERROR_NETWORK_TIMEOUT, "send_with_timeout socket not ready for writing after select");
      return -1;
    }

    // Use platform-specific send
    ssize_t sent = socket_send(sockfd, data_ptr + total_sent, bytes_to_send, 0);

    if (sent < 0) {
      int error = errno;
      if (network_handle_send_error(error)) {
        continue; // Retry
      }
      return -1; // Fatal error
    }

    if (sent > 0) {
      total_sent += (size_t)sent;
    }
  }

  return (ssize_t)total_sent;
}

/**
 * @brief Receive data with timeout
 * @param sockfd Socket file descriptor
 * @param buf Buffer to receive data
 * @param len Length of buffer
 * @param timeout_seconds Timeout in seconds
 * @return Number of bytes received, or -1 on error
 */
ssize_t recv_with_timeout(socket_t sockfd, void *buf, size_t len, int timeout_seconds) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return -1;
  }

  fd_set read_fds;
  struct timeval timeout;
  ssize_t total_received = 0;
  char *data = (char *)buf;

  while (total_received < (ssize_t)len) {
    // Set up select for read timeout
    socket_fd_zero(&read_fds);
    socket_fd_set(sockfd, &read_fds);

    timeout.tv_sec = network_is_test_environment() ? 1 : timeout_seconds;
    timeout.tv_usec = 0;

    int result = socket_select(sockfd, &read_fds, NULL, NULL, &timeout);
    if (result <= 0) {
      if (result == 0) {
        SET_ERRNO_SYS(ERROR_NETWORK_TIMEOUT, "recv_with_timeout timed out after %d seconds", timeout_seconds);
        return -1;
      }
      if (network_handle_select_error(result)) {
        continue; // Retry
      }
      return -1; // Fatal error
    }

    // Check if socket is ready
    if (!socket_fd_isset(sockfd, &read_fds)) {
      SET_ERRNO_SYS(ERROR_NETWORK_TIMEOUT, "recv_with_timeout socket not ready after select");
      return -1;
    }

    // Calculate how much we still need to receive
    size_t bytes_to_recv = len - (size_t)total_received;
    ssize_t received = socket_recv(sockfd, data + total_received, bytes_to_recv, 0);

    if (received < 0) {
      int error = errno;
      if (network_handle_recv_error(error)) {
        continue; // Retry
      }
      return -1; // Fatal error
    }

    if (received == 0) {
      // Connection closed by peer
      log_debug("Connection closed by peer during recv");
      return total_received; // Return what we got so far
    }

    total_received += received;
  }

  return total_received;
}

/**
 * @brief Accept connection with timeout
 * @param listenfd Listening socket
 * @param addr Client address structure
 * @param addrlen Length of address structure
 * @param timeout_seconds Timeout in seconds
 * @return Client socket, or -1 on error
 */
int accept_with_timeout(socket_t listenfd, struct sockaddr *addr, socklen_t *addrlen, int timeout_seconds) {
  fd_set read_fds;
  struct timeval timeout;

  socket_fd_zero(&read_fds);
  socket_fd_set(listenfd, &read_fds);

  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  int result = socket_select(listenfd, &read_fds, NULL, NULL, &timeout);

  if (result <= 0) {
    if (result == 0) {
      // Timeout is expected behavior for server waiting for connections
      asciichat_errno = ERROR_NETWORK_TIMEOUT;
      asciichat_errno_context.code = ERROR_NETWORK_TIMEOUT;
      asciichat_errno_context.has_system_error = true;
      asciichat_errno_context.system_errno = ETIMEDOUT;
      return -1;
    }

    if (network_handle_select_error(result)) {
      return -1; // Don't retry for accept
    }
    return -1;
  }

  // Check if socket is ready
  if (!socket_fd_isset(listenfd, &read_fds)) {
    asciichat_errno = ERROR_NETWORK_TIMEOUT;
    asciichat_errno_context.code = ERROR_NETWORK_TIMEOUT;
    asciichat_errno_context.has_system_error = true;
    asciichat_errno_context.system_errno = ETIMEDOUT;
    return -1;
  }

  // Check if socket is still valid before attempting accept
  if (listenfd == INVALID_SOCKET_VALUE) {
    SET_ERRNO(ERROR_NETWORK, "accept_with_timeout: listening socket is closed");
    return -1;
  }

  socket_t accept_result = socket_accept(listenfd, addr, addrlen);

  if (accept_result == INVALID_SOCKET_VALUE) {
    // Check if this is a socket closed error (common during shutdown)
    int error_code = socket_get_last_error();

    if (socket_is_invalid_socket_error(error_code)) {
      // During shutdown, don't log this as an error since it's expected behavior
      asciichat_errno = ERROR_NETWORK;
      asciichat_errno_context.code = ERROR_NETWORK;
      asciichat_errno_context.has_system_error = false;
    } else {
      SET_ERRNO_SYS(ERROR_NETWORK_BIND, "accept_with_timeout accept failed: %s", socket_get_error_string());
    }
    return -1;
  }

  return (int)accept_result;
}

/**
 * @brief Set socket timeout
 * @param sockfd Socket file descriptor
 * @param timeout_seconds Timeout in seconds
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t set_socket_timeout(socket_t sockfd, int timeout_seconds) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket file descriptor");
  }

  struct timeval timeout;
  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to set socket receive timeout");
  }

  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to set socket send timeout");
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Set socket keepalive
 * @param sockfd Socket file descriptor
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t set_socket_keepalive(socket_t sockfd) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket file descriptor");
  }
  int result = socket_set_keepalive_params(sockfd, true, KEEPALIVE_IDLE, KEEPALIVE_INTERVAL, KEEPALIVE_COUNT);
  if (result != 0) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to set socket keepalive parameters");
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Set socket non-blocking
 * @param sockfd Socket file descriptor
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t set_socket_nonblocking(socket_t sockfd) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket file descriptor");
  }
  int result = socket_set_nonblocking(sockfd, true);
  if (result != 0) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to set socket non-blocking mode");
  }
  return ASCIICHAT_OK;
}

/**
 * @brief Configure socket buffers and TCP_NODELAY for optimal performance
 * @param sockfd Socket file descriptor
 * @return ASCIICHAT_OK on success, ERROR_NETWORK_CONFIG on failure
 */
asciichat_error_t socket_configure_buffers(socket_t sockfd) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Invalid socket file descriptor");
  }

  int failed_options = 0;

  // Attempt to configure 1MB send buffer for optimal frame transmission
  int send_buffer_size = 1024 * 1024;
  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &send_buffer_size, sizeof(send_buffer_size)) < 0) {
    log_warn("Failed to set send buffer size to 1MB: %s", network_error_string());
    failed_options++;
  }

  // Attempt to configure 1MB receive buffer for optimal frame reception
  int recv_buffer_size = 1024 * 1024;
  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &recv_buffer_size, sizeof(recv_buffer_size)) < 0) {
    log_warn("Failed to set receive buffer size to 1MB: %s", network_error_string());
    failed_options++;
  }

  // Attempt to enable TCP_NODELAY to disable Nagle's algorithm for low-latency transmission
  // CRITICAL: This must be set even if buffer configuration fails, as it's essential for real-time video
  int tcp_nodelay = 1;
  if (socket_setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &tcp_nodelay, sizeof(tcp_nodelay)) < 0) {
    log_warn("Failed to set TCP_NODELAY: %s", network_error_string());
    failed_options++;
  }

  // Return error only if ALL options failed
  if (failed_options >= 3) {
    return SET_ERRNO_SYS(ERROR_NETWORK, "Failed to configure all socket options");
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Get human-readable error string for network errors
 * @param error_code Error code
 * @return Error string
 */
const char *network_error_string() {
  return socket_get_error_string();
}

/**
 * @brief Connect with timeout
 * @param sockfd Socket file descriptor
 * @param addr Address to connect to
 * @param addrlen Address length
 * @param timeout_seconds Timeout in seconds
 * @return true on success, false on failure
 */
bool connect_with_timeout(socket_t sockfd, const struct sockaddr *addr, socklen_t addrlen, int timeout_seconds) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return false;
  }

  // Set socket to non-blocking for timeout control
  if (set_socket_nonblocking(sockfd) != ASCIICHAT_OK) {
    return false;
  }

  // Attempt connection
  int result = connect(sockfd, addr, addrlen);

  if (result == 0) {
    // Connected immediately
    return true;
  }

  // Check if connection is in progress (expected for non-blocking sockets)
  int error = socket_get_last_error();
  if (!socket_is_in_progress_error(error) && !socket_is_would_block_error(error)) {
    return false;
  }

  // Use select to wait for connection with timeout
  fd_set write_fds;
  struct timeval timeout;

  socket_fd_zero(&write_fds);
  socket_fd_set(sockfd, &write_fds);

  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  result = socket_select(sockfd, NULL, &write_fds, NULL, &timeout);

  if (result <= 0) {
    return false; // Timeout or error
  }

  if (!socket_fd_isset(sockfd, &write_fds)) {
    return false; // Socket not ready
  }

  // Check if connection was successful
  int error_code = 0;
  socklen_t error_len = sizeof(error_code);

  if (socket_getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error_code, &error_len) != 0) {
    return false;
  }

  if (error_code != 0) {
    return false;
  }

  // Connection successful - restore blocking mode
  if (socket_set_blocking(sockfd) != 0) {
    log_warn("Failed to restore socket to blocking mode after connect");
    // Continue anyway - non-blocking mode works but may cause issues with send/recv
  }

  return true;
}
