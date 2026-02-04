/**
 * @file platform/posix/socket.c
 * @ingroup platform
 * @brief 🌐 POSIX socket implementation with TCP/UDP support and network address handling
 */

#ifndef _WIN32

#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#ifdef __linux__
#include <sys/epoll.h>
#endif

#include <ascii-chat/common.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/util/time.h>

// Socket implementation (mostly pass-through for POSIX)
asciichat_error_t socket_init(void) {
  // POSIX doesn't need socket initialization
  return ASCIICHAT_OK;
}

void socket_cleanup(void) {
  // POSIX doesn't need socket cleanup
}

socket_t socket_create(int domain, int type, int protocol) {
  return socket(domain, type, protocol);
}

int socket_close(socket_t sock) {
  return close(sock);
}

int socket_bind(socket_t sock, const struct sockaddr *addr, socklen_t addrlen) {
  return bind(sock, addr, addrlen);
}

int socket_listen(socket_t sock, int backlog) {
  return listen(sock, backlog);
}

socket_t socket_accept(socket_t sock, struct sockaddr *addr, socklen_t *addrlen) {
  socket_t client_sock = accept(sock, addr, addrlen);
  if (client_sock == INVALID_SOCKET_VALUE) {
    return client_sock;
  }

  // Optimize the socket for high-throughput video streaming
  socket_optimize_for_streaming(client_sock);

  return client_sock;
}

int socket_connect(socket_t sock, const struct sockaddr *addr, socklen_t addrlen) {
  return connect(sock, addr, addrlen);
}

ssize_t socket_sendto(socket_t sock, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                      socklen_t addrlen) {
  return sendto(sock, buf, len, flags, dest_addr, addrlen);
}

ssize_t socket_recvfrom(socket_t sock, void *buf, size_t len, int flags, struct sockaddr *src_addr,
                        socklen_t *addrlen) {
  return recvfrom(sock, buf, len, flags, src_addr, addrlen);
}

int socket_setsockopt(socket_t sock, int level, int optname, const void *optval, socklen_t optlen) {
  return setsockopt(sock, level, optname, optval, optlen);
}

int socket_getsockopt(socket_t sock, int level, int optname, void *optval, socklen_t *optlen) {
  return getsockopt(sock, level, optname, optval, optlen);
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
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1)
    return -1;
  if (nonblocking) {
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
  }
  return fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
}

int socket_set_blocking(socket_t sock) {
  int flags = fcntl(sock, F_GETFL, 0);
  if (flags == -1)
    return -1;
  return fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
}

int socket_set_reuseaddr(socket_t sock, bool reuse) {
  int yes = reuse ? 1 : 0;
  return setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
}

int socket_set_keepalive(socket_t sock, bool keepalive) {
  int yes = keepalive ? 1 : 0;
  return setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));
}

int socket_set_nodelay(socket_t sock, bool nodelay) {
  int yes = nodelay ? 1 : 0;
  return setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}

// Error handling
int socket_get_error(socket_t sock) {
  (void)sock; // POSIX doesn't need the socket for error retrieval
  return errno;
}

const char *socket_error_string(int error) {
  return SAFE_STRERROR(error);
}

// Check if socket is valid
bool socket_is_valid(socket_t sock) {
  return sock >= 0;
}

// Poll implementation with platform-specific precision
int socket_poll(struct pollfd *fds, nfds_t nfds, int64_t timeout_ns) {
#ifdef __linux__
  // Try epoll_pwait2() for nanosecond precision (Linux 5.11+)
  if (nfds > 0) {
    static int has_epoll_pwait2 = -1; // -1: untested, 0: not available, 1: available

    // Lazy-detect epoll_pwait2 support
    if (has_epoll_pwait2 == -1) {
      int epfd = epoll_create1(EPOLL_CLOEXEC);
      if (epfd >= 0) {
        struct epoll_event ev;
        struct timespec ts = {0, 1};
        int result = epoll_pwait2(epfd, &ev, 1, &ts, NULL);
        has_epoll_pwait2 = (result >= 0 || errno != EINVAL) ? 1 : 0;
        close(epfd);
      } else {
        has_epoll_pwait2 = 0;
      }
    }

    // Use epoll_pwait2 if available
    if (has_epoll_pwait2 == 1) {
      int epfd = epoll_create1(EPOLL_CLOEXEC);
      if (epfd >= 0) {
        struct epoll_event events[nfds];

        for (nfds_t i = 0; i < nfds; i++) {
          struct epoll_event ev;
          ev.events = fds[i].events;
          ev.data.u64 = i;
          if (epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i].fd, &ev) < 0) {
            close(epfd);
            goto fallback_poll_linux;
          }
        }

        struct timespec ts;
        if (timeout_ns < 0) {
          ts.tv_sec = -1;
          ts.tv_nsec = 0;
        } else {
          ts.tv_sec = timeout_ns / NS_PER_SEC_INT;
          ts.tv_nsec = timeout_ns % NS_PER_SEC_INT;
        }

        int result = epoll_pwait2(epfd, events, (int)nfds, timeout_ns < 0 ? NULL : &ts, NULL);

        if (result > 0) {
          for (nfds_t i = 0; i < nfds; i++) {
            fds[i].revents = 0;
          }
          for (int i = 0; i < result; i++) {
            nfds_t idx = (nfds_t)events[i].data.u64;
            fds[idx].revents = events[i].events;
          }
        }

        close(epfd);
        return result;
      }
    }
  }

fallback_poll_linux:
  // Convert nanoseconds to milliseconds for poll()
  int timeout_ms;
  if (timeout_ns < 0) {
    timeout_ms = -1;
  } else {
    timeout_ms = (int)time_ns_to_ms((uint64_t)timeout_ns);
  }
  return poll(fds, nfds, timeout_ms);

#elif defined(__APPLE__)
  // macOS/BSD: Convert nanoseconds to milliseconds for poll
  // (kqueue with kevent could provide nanosecond precision but poll is simpler)
  int timeout_ms;
  if (timeout_ns < 0) {
    timeout_ms = -1;
  } else {
    timeout_ms = (int)time_ns_to_ms((uint64_t)timeout_ns);
  }
  return poll(fds, nfds, timeout_ms);

#else
  // Other POSIX systems: Convert nanoseconds to milliseconds for poll
  int timeout_ms;
  if (timeout_ns < 0) {
    timeout_ms = -1;
  } else {
    timeout_ms = (int)time_ns_to_ms((uint64_t)timeout_ns);
  }
  return poll(fds, nfds, timeout_ms);
#endif
}

// Get socket fd for use with native APIs
int socket_get_fd(socket_t sock) {
  return sock;
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
  if (setsockopt(sock, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive)) != 0) {
    return -1;
  }

  if (enable) {
#ifdef __linux__
    // Linux supports TCP keepalive parameters
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) != 0) {
      return -1;
    }
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) != 0) {
      return -1;
    }
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count)) != 0) {
      return -1;
    }
#elif defined(__APPLE__)
    // macOS uses different names for keepalive parameters
    if (setsockopt(sock, IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle)) != 0) {
      return -1;
    }
    // macOS doesn't support setting interval and count directly
    (void)interval;
    (void)count;
#else
    // Other POSIX systems may not support these parameters
    (void)idle;
    (void)interval;
    (void)count;
#endif
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
  ling.l_linger = timeout;
  return setsockopt(sock, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
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
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_size, sizeof(recv_size)) != 0) {
      result = -1;
    }
  }

  if (send_size > 0) {
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &send_size, sizeof(send_size)) != 0) {
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
 * @brief Get last socket error (errno)
 * @return Error code
 */
int socket_get_last_error(void) {
  return errno;
}

/**
 * @brief Get error string for last socket error
 * @return Error string
 */
const char *socket_get_error_string(void) {
  return SAFE_STRERROR(errno);
}

// Platform-aware select wrapper
int socket_select(socket_t max_fd, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
  // On POSIX, select() needs the max file descriptor + 1
  return select(max_fd + 1, readfds, writefds, exceptfds, timeout);
}

// Platform-safe FD set wrappers
void socket_fd_zero(fd_set *set) {
  FD_ZERO(set);
}

void socket_fd_set(socket_t sock, fd_set *set) {
  // On POSIX, socket_t is int, so this works directly
  FD_SET(sock, set);
}

int socket_fd_isset(socket_t sock, fd_set *set) {
  return FD_ISSET(sock, set);
}

/**
 * @brief Check if error indicates "would block" (POSIX)
 * @param error_code Platform-specific error code
 * @return true if error is EAGAIN or EWOULDBLOCK
 */
bool socket_is_would_block_error(int error_code) {
  return error_code == EAGAIN || error_code == EWOULDBLOCK;
}

/**
 * @brief Check if error indicates connection reset (POSIX)
 * @param error_code Platform-specific error code
 * @return true if error is ECONNRESET
 */
bool socket_is_connection_reset_error(int error_code) {
  return error_code == ECONNRESET;
}

/**
 * @brief Check if error indicates invalid/closed socket (POSIX)
 * @param error_code Platform-specific error code
 * @return true if error is EBADF or ENOTSOCK
 */
bool socket_is_invalid_socket_error(int error_code) {
  return error_code == EBADF || error_code == ENOTSOCK;
}

/**
 * @brief Check if error indicates operation in progress (POSIX)
 * @param error_code Platform-specific error code
 * @return true if error is EINPROGRESS
 */
bool socket_is_in_progress_error(int error_code) {
  return error_code == EINPROGRESS;
}

ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags) {
#if defined(MSG_NOSIGNAL)
  /* Linux: Use MSG_NOSIGNAL to avoid SIGPIPE */
  return send(sock, buf, len, flags | MSG_NOSIGNAL);
#else
  /* macOS/BSD: No MSG_NOSIGNAL, but we ignore SIGPIPE signal instead */
  (void)flags; /* Unused on macOS */
  return send(sock, buf, len, 0);
#endif
}

ssize_t socket_recv(socket_t sock, void *buf, size_t len, int flags) {
  (void)flags; /* Unix recv doesn't need special flags for basic operation */
  return recv(sock, buf, len, 0);
}

#endif // !_WIN32
