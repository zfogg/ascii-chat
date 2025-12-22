/**
 * @file platform/windows/socket.c
 * @ingroup platform
 * @brief 🌐 Windows Winsock implementation with TCP/UDP support and network address handling
 */

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <stdio.h>
#include <stdbool.h>

#include "common.h"
#include "platform/socket.h"

#pragma comment(lib, "ws2_32.lib")

// Winsock initialization state
static int winsock_initialized = 0;
static WSADATA wsaData;

// Socket implementation
asciichat_error_t socket_init(void) {
  if (!winsock_initialized) {
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "WSAStartup failed");
    }
    winsock_initialized = 1;
  }

  return ASCIICHAT_OK;
}

void socket_cleanup(void) {
  if (winsock_initialized) {
    WSACleanup();
    winsock_initialized = 0;
  }
}

socket_t socket_create(int domain, int type, int protocol) {
  // Ensure Winsock is initialized
  if (socket_init() != ASCIICHAT_OK) {
    return INVALID_SOCKET;
  }

  // AF_UNIX is not supported on Windows - return error instead of silently converting
  if (domain == AF_UNIX) {
    SET_ERRNO(ERROR_NETWORK, "AF_UNIX sockets are not supported on Windows");
    return INVALID_SOCKET;
  }

  // For other domains, use as-is
  return socket(domain, type, protocol);
}

int socket_close(socket_t sock) {
  if (sock != INVALID_SOCKET) {
    return closesocket(sock);
  }
  return -1;
}

int socket_bind(socket_t sock, const struct sockaddr *addr, socklen_t addrlen) {
  return bind(sock, addr, addrlen);
}

int socket_listen(socket_t sock, int backlog) {
  return listen(sock, backlog);
}

socket_t socket_accept(socket_t sock, struct sockaddr *addr, socklen_t *addrlen) {
  socket_t client_sock = accept(sock, addr, addrlen);
  if (client_sock == INVALID_SOCKET) {
    return client_sock;
  }

  // Automatically optimize all accepted sockets for high-throughput video streaming
  // 1. Disable Nagle algorithm - CRITICAL for real-time video
  int nodelay = 1;
  setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay));

  // 2. Increase send buffer for video streaming (2MB with fallbacks)
  int send_buffer = 2 * 1024 * 1024; // 2MB
  if (setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&send_buffer, sizeof(send_buffer)) != 0) {
    send_buffer = 512 * 1024; // 512KB fallback
    if (setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&send_buffer, sizeof(send_buffer)) != 0) {
      send_buffer = 128 * 1024; // 128KB fallback
      setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (const char *)&send_buffer, sizeof(send_buffer));
    }
  }

  // 3. Increase receive buffer (2MB with fallbacks)
  int recv_buffer = 2 * 1024 * 1024; // 2MB
  if (setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, (const char *)&recv_buffer, sizeof(recv_buffer)) != 0) {
    recv_buffer = 512 * 1024; // 512KB fallback
    if (setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, (const char *)&recv_buffer, sizeof(recv_buffer)) != 0) {
      recv_buffer = 128 * 1024; // 128KB fallback
      setsockopt(client_sock, SOL_SOCKET, SO_RCVBUF, (const char *)&recv_buffer, sizeof(recv_buffer));
    }
  }

  // 4. Set timeouts to prevent blocking
  DWORD send_timeout = 5000; // 5 seconds
  setsockopt(client_sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&send_timeout, sizeof(send_timeout));

  DWORD recv_timeout = 10000; // 10 seconds
  setsockopt(client_sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&recv_timeout, sizeof(recv_timeout));

  // 5. Enable keepalive (optional)
  int keepalive = 1;
  setsockopt(client_sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&keepalive, sizeof(keepalive));

  return client_sock;
}

int socket_connect(socket_t sock, const struct sockaddr *addr, socklen_t addrlen) {
  return connect(sock, addr, addrlen);
}

ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags) {
  return send(sock, (const char *)buf, (int)len, flags);
}

ssize_t socket_recv(socket_t sock, void *buf, size_t len, int flags) {
  return recv(sock, (char *)buf, (int)len, flags);
}

ssize_t socket_sendto(socket_t sock, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                      socklen_t addrlen) {
  return sendto(sock, (const char *)buf, (int)len, flags, dest_addr, addrlen);
}

ssize_t socket_recvfrom(socket_t sock, void *buf, size_t len, int flags, struct sockaddr *src_addr,
                        socklen_t *addrlen) {
  return recvfrom(sock, (char *)buf, (int)len, flags, src_addr, addrlen);
}

int socket_setsockopt(socket_t sock, int level, int optname, const void *optval, socklen_t optlen) {
  // Map POSIX SOL_SOCKET to Windows
  if (level == SOL_SOCKET) {
    return setsockopt(sock, SOL_SOCKET, optname, (const char *)optval, optlen);
  }
  return setsockopt(sock, level, optname, (const char *)optval, optlen);
}

int socket_getsockopt(socket_t sock, int level, int optname, void *optval, socklen_t *optlen) {
  return getsockopt(sock, level, optname, (char *)optval, optlen);
}

int socket_shutdown(socket_t sock, int how) {
  return shutdown(sock, how);
}

int socket_getpeername(socket_t sock, struct sockaddr *addr, socklen_t *addrlen) {
  return getpeername(sock, addr, addrlen);
}

int socket_getsockname(socket_t sock, struct sockaddr *addr, socklen_t *addrlen) {
  return getsockname(sock, addr, addrlen);
}

// Socket utility functions
int socket_set_nonblocking(socket_t sock, bool nonblocking) {
  u_long mode = nonblocking ? 1 : 0;
  return ioctlsocket(sock, (long)FIONBIO, &mode);
}

int socket_set_blocking(socket_t sock) {
  u_long mode = 0;
  return ioctlsocket(sock, (long)FIONBIO, &mode);
}

int socket_set_reuseaddr(socket_t sock, bool reuse) {
  int yes = reuse ? 1 : 0;
  return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
}

int socket_set_keepalive(socket_t sock, bool keepalive) {
  int yes = keepalive ? 1 : 0;
  return setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&yes, sizeof(yes));
}

int socket_set_nodelay(socket_t sock, bool nodelay) {
  int yes = nodelay ? 1 : 0;
  return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
}

// Error handling
int socket_get_error(socket_t sock) {
  (void)sock; // Windows doesn't need the socket for error retrieval
  return WSAGetLastError();
}

const char *socket_error_string(int error) {
  // Use thread-local storage to avoid race conditions between threads
  static __declspec(thread) char buffer[256] = {0};
  buffer[0] = '\0'; // Clear buffer on each call
  FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, (DWORD)error,
                 MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer, sizeof(buffer), NULL);
  return buffer;
}

// Check if socket is valid
bool socket_is_valid(socket_t sock) {
  return sock != INVALID_SOCKET;
}

// Poll implementation using WSAPoll (available on Windows Vista+)
int socket_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  // WSAPoll is the Windows equivalent of poll()
  // It's available on Windows Vista and later
  // The pollfd structure is compatible between poll() and WSAPoll()

  // WSAPoll takes ULONG for nfds parameter, cast our nfds_t
  int result = WSAPoll((LPWSAPOLLFD)fds, (ULONG)nfds, timeout);

  if (result == SOCKET_ERROR) {
    // WSAPoll returns SOCKET_ERROR (-1) on error, same as poll()
    return -1;
  }

  return result;
}

// Platform-aware select wrapper
int socket_select(socket_t max_fd, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  // On Windows, the first parameter is ignored by select()
  (void)max_fd; // Suppress unused parameter warning
  return select(0, readfds, writefds, exceptfds, timeout);
}

// Get socket fd for use with native APIs
int socket_get_fd(socket_t sock) {
  return (int)sock;
}

// ============================================================================
// Extended Socket Options
// ============================================================================

/**
 * @brief Set keepalive parameters for socket
 * @param sock Socket descriptor
 * @param enable Enable/disable keepalive
 * @param idle Time before first keepalive probe (seconds)
 * @param interval Interval between probes (seconds)
 * @param count Number of probes before connection drop
 * @return 0 on success, -1 on failure
 */
int socket_set_keepalive_params(socket_t sock, bool enable, int idle, int interval, int count) {
  // First enable/disable keepalive
  int keepalive = enable ? 1 : 0;
  if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, (const char *)&keepalive, sizeof(keepalive)) != 0) {
    return -1;
  }

  if (enable) {
    // Windows Vista+ supports TCP keepalive parameters
    // Use system-defined struct tcp_keepalive from mstcpip.h
    struct tcp_keepalive keepalive_params;

    keepalive_params.onoff = 1;
    keepalive_params.keepalivetime = ((ULONG)idle) * 1000UL;         // Convert to milliseconds
    keepalive_params.keepaliveinterval = ((ULONG)interval) * 1000UL; // Convert to milliseconds

    DWORD bytes_returned;
    if (WSAIoctl(sock, SIO_KEEPALIVE_VALS, &keepalive_params, sizeof(keepalive_params), NULL, 0, &bytes_returned, NULL,
                 NULL) != 0) {
      return -1;
    }

    // Note: Windows doesn't support setting probe count directly
    (void)count;
  }

  return 0;
}

/**
 * @brief Set linger options for socket
 * @param sock Socket descriptor
 * @param enable Enable/disable linger
 * @param timeout Linger timeout in seconds
 * @return 0 on success, -1 on failure
 */
int socket_set_linger(socket_t sock, bool enable, int timeout) {
  struct linger ling;
  ling.l_onoff = enable ? 1 : 0;
  ling.l_linger = (u_short)(unsigned int)timeout;
  return setsockopt(sock, SOL_SOCKET, SO_LINGER, (const char *)&ling, sizeof(ling));
}

/**
 * @brief Set receive and send buffer sizes
 * @param sock Socket descriptor
 * @param recv_size Receive buffer size (0 to keep current)
 * @param send_size Send buffer size (0 to keep current)
 * @return 0 on success, -1 on failure
 */
int socket_set_buffer_sizes(socket_t sock, int recv_size, int send_size) {
  int result = 0;

  if (recv_size > 0) {
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char *)&recv_size, sizeof(recv_size)) != 0) {
      result = -1;
    }
  }

  if (send_size > 0) {
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, (const char *)&send_size, sizeof(send_size)) != 0) {
      result = -1;
    }
  }

  return result;
}

/**
 * @brief Get peer address for connected socket
 * @param sock Socket descriptor
 * @param addr Address structure to fill
 * @param addrlen Address structure size
 * @return 0 on success, -1 on failure
 */
int socket_get_peer_address(socket_t sock, struct sockaddr *addr, socklen_t *addrlen) {
  return getpeername(sock, addr, addrlen);
}

/**
 * @brief Get last socket error (WSAGetLastError)
 * @return Error code
 */
int socket_get_last_error(void) {
  return WSAGetLastError();
}

/**
 * @brief Get error string for last socket error
 * @return Error string
 */
const char *socket_get_error_string(void) {
  return socket_error_string(WSAGetLastError());
}

/**
 * @brief Robust socket optimization for high-throughput video streaming on Windows
 *
 * This function applies critical TCP optimizations for real-time video streaming.
 * It is designed to be resilient - partial failures are logged but do not prevent
 * the connection from working. Only critical failures cause the function to fail.
 *
 * OPTIMIZATIONS APPLIED:
 * ======================
 * 1. TCP_NODELAY: Disables Nagle algorithm for low latency (CRITICAL)
 * 2. Large send/receive buffers: 2MB each for high throughput (IMPORTANT)
 * 3. Keepalive: Faster detection of dead connections (NICE TO HAVE)
 * 4. Linger settings: Clean connection shutdown (NICE TO HAVE)
 * 5. Send/receive timeouts: Prevent blocking on slow clients (IMPORTANT)
 *
 * ERROR HANDLING STRATEGY:
 * ========================
 * - Critical options (TCP_NODELAY): Function fails if these fail
 * - Important options (buffers, timeouts): Logged but continue
 * - Nice-to-have options (keepalive): Ignored if they fail
 *
 * @param sock Socket descriptor to optimize
 * @return 0 on success, -1 on critical failure
 */
// Platform-safe FD set wrappers
void socket_fd_zero(fd_set *set) {
  FD_ZERO(set);
}

void socket_fd_set(socket_t sock, fd_set *set) {
  // On Windows, FD_SET handles SOCKET type correctly
  FD_SET(sock, set);
}

int socket_fd_isset(socket_t sock, fd_set *set) {
  return FD_ISSET(sock, set);
}

#endif // _WIN32
