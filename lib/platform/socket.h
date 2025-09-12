#pragma once

/**
 * @file socket.h
 * @brief Cross-platform socket interface for ASCII-Chat
 *
 * This header provides a unified socket interface that abstracts platform-specific
 * implementations (Windows Winsock2 vs POSIX sockets).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define INVALID_SOCKET_VALUE INVALID_SOCKET
typedef int socklen_t;
typedef int ssize_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
typedef int socket_t;
#define INVALID_SOCKET_VALUE (-1)
#endif

// ============================================================================
// Socket Functions
// ============================================================================

// Initialization (required on Windows)
int socket_init(void);
void socket_cleanup(void);

// Basic socket operations
socket_t socket_create(int domain, int type, int protocol);
int socket_close(socket_t sock);
int socket_bind(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);
int socket_listen(socket_t sock, int backlog);
socket_t socket_accept(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);
int socket_connect(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);

// Data transfer
ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags);
ssize_t socket_recv(socket_t sock, void *buf, size_t len, int flags);
ssize_t socket_sendto(socket_t sock, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                      socklen_t addrlen);
ssize_t socket_recvfrom(socket_t sock, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);

// Socket options
int socket_setsockopt(socket_t sock, int level, int optname, const void *optval, socklen_t optlen);
int socket_getsockopt(socket_t sock, int level, int optname, void *optval, socklen_t *optlen);
int socket_shutdown(socket_t sock, int how);
int socket_getpeername(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);
int socket_getsockname(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);

// Socket configuration
int socket_set_blocking(socket_t sock);
int socket_set_nonblocking(socket_t sock, bool nonblocking);
int socket_set_reuseaddr(socket_t sock, bool reuse);
int socket_set_nodelay(socket_t sock, bool nodelay);
int socket_set_keepalive(socket_t sock, bool keepalive);

// Extended socket options
int socket_set_keepalive_params(socket_t sock, bool enable, int idle, int interval, int count);
int socket_set_linger(socket_t sock, bool enable, int timeout);
int socket_set_buffer_sizes(socket_t sock, int recv_size, int send_size);
int socket_get_peer_address(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);

// Utility functions
int socket_get_error(socket_t sock);
int socket_get_last_error(void);
const char *socket_get_error_string(void);
int socket_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int socket_get_fd(socket_t sock);
int socket_is_valid(socket_t sock);

// Platform-specific error codes
#ifdef _WIN32
#define SOCKET_ERROR_WOULDBLOCK WSAEWOULDBLOCK
#define SOCKET_ERROR_INPROGRESS WSAEINPROGRESS
#define SOCKET_ERROR_AGAIN WSAEWOULDBLOCK
#else
#define SOCKET_ERROR_WOULDBLOCK EWOULDBLOCK
#define SOCKET_ERROR_INPROGRESS EINPROGRESS
#define SOCKET_ERROR_AGAIN EAGAIN
#endif

// Poll structure for Windows compatibility
#ifdef _WIN32
#ifndef POLLIN
#define POLLIN 0x001
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020
#endif

#ifndef HAVE_STRUCT_POLLFD
struct pollfd {
  socket_t fd;
  short events;
  short revents;
};
#endif

typedef unsigned long nfds_t;
#endif
