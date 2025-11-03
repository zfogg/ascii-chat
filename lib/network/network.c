
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

// Check if we're in a test environment
static int is_test_environment(void) {
  return SAFE_GETENV("CRITERION_TEST") != NULL || SAFE_GETENV("TESTING") != NULL;
}

/**
 * @brief Platform-specific send operation
 * @param sockfd Socket file descriptor
 * @param data Data to send
 * @param len Length of data
 * @return Number of bytes sent, or -1 on error
 */
static ssize_t network_platform_send(socket_t sockfd, const void *data, size_t len) {
#ifdef _WIN32
  // Windows send() expects int for length and const char* for buffer
  if (len > INT_MAX) {
    len = INT_MAX;
  }
  int raw_sent = send(sockfd, (const char *)data, (int)len, 0);

  // CRITICAL FIX: Check for SOCKET_ERROR before casting to avoid corruption
  ssize_t sent;
  if (raw_sent == SOCKET_ERROR) {
    sent = -1;
    // On Windows, use WSAGetLastError() and save to WSA error field
    errno = EIO; // Set a generic errno, but save the real WSA error
  } else {
    sent = (ssize_t)raw_sent;
    // CORRUPTION DETECTION: Check Windows send() return value
    if (raw_sent > (int)len) {
      SET_ERRNO(ERROR_INVALID_STATE, "CRITICAL: Windows send() returned more than requested: raw_sent=%d > len=%zu",
                raw_sent, len);
    }
  }
  return sent;
#elif defined(MSG_NOSIGNAL)
  return send(sockfd, data, len, MSG_NOSIGNAL);
#else
  // macOS doesn't have MSG_NOSIGNAL, but we ignore SIGPIPE signal instead
  return send(sockfd, data, len, 0);
#endif
}

/**
 * @brief Platform-specific recv operation
 * @param sockfd Socket file descriptor
 * @param buf Buffer to receive data
 * @param len Length of buffer
 * @return Number of bytes received, or -1 on error
 */
static ssize_t network_platform_recv(socket_t sockfd, void *buf, size_t len) {
#ifdef _WIN32
  int raw_received = recv(sockfd, (char *)buf, (int)len, 0);

  if (raw_received == SOCKET_ERROR) {
    return -1;
  }
  return (ssize_t)raw_received;
#else
  return recv(sockfd, buf, len, 0);
#endif
}

/**
 * @brief Platform-specific error string retrieval
 * @param error Error code
 * @return Error string
 */
static const char *network_get_error_string(int error) {
#ifdef _WIN32
  // For socket errors, use socket-specific function
  (void)error; // Windows doesn't use the error parameter
  return socket_get_error_string();
#else
  // For standard errno, use platform abstraction
  return SAFE_STRERROR(error);
#endif
}

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
  SET_ERRNO_SYS(ERROR_NETWORK, "send_with_timeout failed: %s", network_get_error_string(error));
  return 0; // Fatal
}

/**
 * @brief Platform-specific error handling for recv operations
 * @param error Error code
 * @return 1 if should retry, 0 if fatal error
 */
static int network_handle_recv_error(int error) {
#ifdef _WIN32
  if (error == WSAEWOULDBLOCK) {
    return 1; // Retry
  }
  if (error == WSAEINTR) {
    SET_ERRNO_SYS(ERROR_SIGNAL_INTERRUPT, "Signal interrupt occurred");
    return 0; // Fatal
  }
#else
  if (error == EAGAIN || error == EWOULDBLOCK) {
    return 1; // Retry
  }
  if (error == EINTR) {
    SET_ERRNO_SYS(ERROR_SIGNAL_INTERRUPT, "Signal interrupt occurred");
    return 0; // Fatal
  }
  if (error == EBADF) {
    // Socket is not a socket (WSAENOTSOCK on Windows) - socket was closed
    SET_ERRNO_SYS(ERROR_NETWORK, "Socket is not a socket (closed by another thread)");
    return 0; // Fatal
  }
#endif
  SET_ERRNO_SYS(ERROR_NETWORK, "recv_with_timeout failed: %s", network_get_error_string(error));
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

#ifdef _WIN32
  int error = WSAGetLastError();
  if (error == WSAEINTR) {
    SET_ERRNO_SYS(ERROR_SIGNAL_INTERRUPT, "Signal interrupt occurred");
    return 0; // Fatal
  }
  SET_ERRNO_SYS(ERROR_NETWORK, "select failed: %s", network_get_error_string(error));
#else
  if (errno == EINTR) {
    SET_ERRNO_SYS(ERROR_SIGNAL_INTERRUPT, "Signal interrupt occurred");
    return 0; // Fatal
  }
  SET_ERRNO_SYS(ERROR_NETWORK, "select failed: %s", network_get_error_string(errno));
#endif
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

    timeout.tv_sec = is_test_environment() ? 1 : timeout_seconds;
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
    ssize_t sent = network_platform_send(sockfd, data_ptr + total_sent, bytes_to_send);

    if (sent < 0) {
      int error = errno;
      if (network_handle_send_error(error)) {
        continue; // Retry
      }
      return -1; // Fatal error
    }

    total_sent += sent;
  }

  return total_sent;
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
    log_error("NETWORK_DEBUG: recv_with_timeout called with INVALID_SOCKET_VALUE");
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

    timeout.tv_sec = is_test_environment() ? 1 : timeout_seconds;
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
    size_t bytes_to_recv = len - total_received;
    ssize_t received = network_platform_recv(sockfd, data + total_received, bytes_to_recv);

    if (received < 0) {
      int error = errno;
      log_error("NETWORK_DEBUG: network_platform_recv failed with error %d (errno=%d), sockfd=%d, buf=%p, len=%zu",
                received, error, sockfd, data + total_received, bytes_to_recv);
      if (network_handle_recv_error(error)) {
        log_debug("NETWORK_DEBUG: retrying after error %d", error);
        continue; // Retry
      }
      log_error("NETWORK_DEBUG: fatal error %d, giving up", error);
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
#ifdef _WIN32
    // On Windows, use WSAGetLastError() instead of errno
    int error_code = WSAGetLastError();
    // Windows-specific error codes for closed/invalid sockets
    // WSAENOTSOCK = 10038, WSAEBADF = 10009, WSAENOTCONN = 10057
    if (error_code == WSAENOTSOCK || error_code == WSAEBADF || error_code == WSAENOTCONN) {
      // During shutdown, don't log this as an error since it's expected behavior
      asciichat_errno = ERROR_NETWORK;
      asciichat_errno_context.code = ERROR_NETWORK;
      asciichat_errno_context.has_system_error = false;
    } else {
      // Save Windows socket error to WSA error field for debugging
      errno = EIO; // Set a generic errno
#ifdef NDEBUG
      asciichat_set_errno_with_wsa_error(ERROR_NETWORK_BIND, NULL, 0, NULL, error_code);
#else
      asciichat_set_errno_with_wsa_error(ERROR_NETWORK_BIND, __FILE__, __LINE__, __func__, error_code);
#endif
    }
#else
    // POSIX error codes for closed/invalid sockets
    int error_code = errno;
    if (error_code == EBADF || error_code == ENOTSOCK) {
      // During shutdown, don't log this as an error since it's expected behavior
      asciichat_errno = ERROR_NETWORK;
      asciichat_errno_context.code = ERROR_NETWORK;
      asciichat_errno_context.has_system_error = false;
    } else {
      SET_ERRNO_SYS(ERROR_NETWORK_BIND, "accept_with_timeout accept failed: %s", network_get_error_string(errno));
    }
#endif
    return -1;
  }

  return (int)accept_result;
}

/**
 * @brief Set socket timeout
 * @param sockfd Socket file descriptor
 * @param timeout_seconds Timeout in seconds
 * @return 0 on success, -1 on error
 */
int set_socket_timeout(socket_t sockfd, int timeout_seconds) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return -1;
  }

  struct timeval timeout;
  timeout.tv_sec = timeout_seconds;
  timeout.tv_usec = 0;

  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
    return -1;
  }

  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
    return -1;
  }

  return 0;
}

/**
 * @brief Set socket keepalive
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int set_socket_keepalive(socket_t sockfd) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return -1;
  }
  return socket_set_keepalive_params(sockfd, true, KEEPALIVE_IDLE, KEEPALIVE_INTERVAL, KEEPALIVE_COUNT);
}

/**
 * @brief Set socket non-blocking
 * @param sockfd Socket file descriptor
 * @return 0 on success, -1 on error
 */
int set_socket_nonblocking(socket_t sockfd) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    errno = EBADF;
    return -1;
  }
  return socket_set_nonblocking(sockfd, true);
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
  if (set_socket_nonblocking(sockfd) != 0) {
    return false;
  }

  // Attempt connection
  int result = connect(sockfd, addr, addrlen);

  if (result == 0) {
    // Connected immediately
    return true;
  }

#ifdef _WIN32
  int error = WSAGetLastError();
  if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS) {
    return false;
  }
#else
  if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
    return false;
  }
#endif

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

  return error_code == 0;
}
