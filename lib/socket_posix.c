#ifndef _WIN32

#include "platform.h"
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

// Socket implementation (mostly pass-through for POSIX)
int socket_init(void) {
  // POSIX doesn't need socket initialization
  return 0;
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
  return accept(sock, addr, addrlen);
}

int socket_connect(socket_t sock, const struct sockaddr *addr, socklen_t addrlen) {
  return connect(sock, addr, addrlen);
}

ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags) {
  return send(sock, buf, len, flags);
}

ssize_t socket_recv(socket_t sock, void *buf, size_t len, int flags) {
  return recv(sock, buf, len, flags);
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
  } else {
    return fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
  }
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
  return strerror(error);
}

// Check if socket is valid
int socket_is_valid(socket_t sock) {
  return sock >= 0;
}

// Poll implementation
int socket_poll(struct pollfd *fds, nfds_t nfds, int timeout) {
  return poll(fds, nfds, timeout);
}

// Get socket fd for use with native APIs
int socket_get_fd(socket_t sock) {
  return sock;
}

#endif // !_WIN32