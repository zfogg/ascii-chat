#pragma once

/**
 * @file platform/socket.h
 * @ingroup platform
 * @brief Cross-platform socket interface for ASCII-Chat
 *
 * This header provides a unified socket interface that abstracts platform-specific
 * implementations (Windows Winsock2 vs POSIX sockets).
 *
 * The interface provides:
 * - Socket creation and lifecycle management
 * - Network I/O operations (send/recv)
 * - Socket configuration and options
 * - Connection management (bind, listen, accept, connect)
 * - Polling and event handling
 * - Error handling utilities
 *
 * @note On Windows, uses Winsock2 API (SOCKET type).
 *       On POSIX systems, uses standard BSD sockets (int type).
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>
#include <sys/types.h>
#include "internal.h" // For ssize_t definition on Windows

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
/** @brief Socket handle type (Windows: SOCKET) */
typedef SOCKET socket_t;
/** @brief Invalid socket value (Windows: INVALID_SOCKET) */
#define INVALID_SOCKET_VALUE INVALID_SOCKET
/** @brief Socket address length type (Windows: int) */
typedef int socklen_t;
/** @brief Number of file descriptors type (Windows: unsigned long) */
typedef unsigned long nfds_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
/** @brief Socket handle type (POSIX: int) */
typedef int socket_t;
/** @brief Invalid socket value (POSIX: -1) */
#define INVALID_SOCKET_VALUE (-1)
#endif

#include "../common.h"

// ============================================================================
// Socket Functions
// ============================================================================

/**
 * @brief Initialize socket subsystem (required on Windows)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Initializes the socket subsystem. On Windows, this initializes Winsock.
 * Must be called before any socket operations.
 *
 * @ingroup platform
 */
asciichat_error_t socket_init(void);

/**
 * @brief Cleanup socket subsystem
 *
 * Cleans up the socket subsystem. On Windows, this cleans up Winsock.
 * Should be called during program shutdown.
 *
 * @ingroup platform
 */
void socket_cleanup(void);

/**
 * @brief Create a new socket
 * @param domain Socket domain (e.g., AF_INET, AF_INET6, AF_UNIX)
 * @param type Socket type (e.g., SOCK_STREAM, SOCK_DGRAM)
 * @param protocol Protocol (typically 0 for automatic)
 * @return Socket handle on success, INVALID_SOCKET_VALUE on error
 *
 * @ingroup platform
 */
socket_t socket_create(int domain, int type, int protocol);

/**
 * @brief Close a socket
 * @param sock Socket to close
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_close(socket_t sock);

/**
 * @brief Bind a socket to an address
 * @param sock Socket to bind
 * @param addr Address to bind to
 * @param addrlen Length of address structure
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_bind(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief Listen for incoming connections
 * @param sock Socket to listen on (must be bound)
 * @param backlog Maximum length of the queue of pending connections
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_listen(socket_t sock, int backlog);

/**
 * @brief Accept an incoming connection
 * @param sock Listening socket
 * @param addr Pointer to store peer address (or NULL)
 * @param addrlen Pointer to address length (input/output)
 * @return New socket handle on success, INVALID_SOCKET_VALUE on error
 *
 * @ingroup platform
 */
socket_t socket_accept(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Connect to a remote address
 * @param sock Socket to connect
 * @param addr Remote address to connect to
 * @param addrlen Length of address structure
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_connect(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief Send data on a socket
 * @param sock Socket to send on
 * @param buf Data buffer to send
 * @param len Number of bytes to send
 * @param flags Socket flags (typically 0)
 * @return Number of bytes sent on success, -1 on error
 *
 * @ingroup platform
 */
ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags);

/**
 * @brief Receive data from a socket
 * @param sock Socket to receive from
 * @param buf Buffer to store received data
 * @param len Maximum number of bytes to receive
 * @param flags Socket flags (typically 0)
 * @return Number of bytes received on success, -1 on error, 0 on connection closed
 *
 * @ingroup platform
 */
ssize_t socket_recv(socket_t sock, void *buf, size_t len, int flags);

/**
 * @brief Send data to a specific address (UDP)
 * @param sock Socket to send on
 * @param buf Data buffer to send
 * @param len Number of bytes to send
 * @param flags Socket flags (typically 0)
 * @param dest_addr Destination address
 * @param addrlen Length of destination address
 * @return Number of bytes sent on success, -1 on error
 *
 * @ingroup platform
 */
ssize_t socket_sendto(socket_t sock, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr,
                      socklen_t addrlen);

/**
 * @brief Receive data from a specific address (UDP)
 * @param sock Socket to receive from
 * @param buf Buffer to store received data
 * @param len Maximum number of bytes to receive
 * @param flags Socket flags (typically 0)
 * @param src_addr Pointer to store source address (or NULL)
 * @param addrlen Pointer to address length (input/output)
 * @return Number of bytes received on success, -1 on error
 *
 * @ingroup platform
 */
ssize_t socket_recvfrom(socket_t sock, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * @brief Set socket option
 * @param sock Socket to configure
 * @param level Option level (e.g., SOL_SOCKET, IPPROTO_TCP)
 * @param optname Option name (e.g., SO_REUSEADDR, TCP_NODELAY)
 * @param optval Pointer to option value
 * @param optlen Length of option value
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_setsockopt(socket_t sock, int level, int optname, const void *optval, socklen_t optlen);

/**
 * @brief Get socket option
 * @param sock Socket to query
 * @param level Option level (e.g., SOL_SOCKET, IPPROTO_TCP)
 * @param optname Option name (e.g., SO_REUSEADDR, TCP_NODELAY)
 * @param optval Pointer to store option value
 * @param optlen Pointer to option length (input/output)
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_getsockopt(socket_t sock, int level, int optname, void *optval, socklen_t *optlen);

/**
 * @brief Shutdown socket I/O
 * @param sock Socket to shutdown
 * @param how Shutdown mode (SHUT_RD, SHUT_WR, SHUT_RDWR)
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_shutdown(socket_t sock, int how);

/**
 * @brief Get peer address
 * @param sock Connected socket
 * @param addr Pointer to store peer address
 * @param addrlen Pointer to address length (input/output)
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_getpeername(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Get socket local address
 * @param sock Socket to query
 * @param addr Pointer to store local address
 * @param addrlen Pointer to address length (input/output)
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_getsockname(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Set socket to blocking mode
 * @param sock Socket to configure
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_set_blocking(socket_t sock);

/**
 * @brief Set socket to non-blocking mode
 * @param sock Socket to configure
 * @param nonblocking true for non-blocking, false for blocking
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_set_nonblocking(socket_t sock, bool nonblocking);

/**
 * @brief Set SO_REUSEADDR socket option
 * @param sock Socket to configure
 * @param reuse true to enable reuse, false to disable
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_set_reuseaddr(socket_t sock, bool reuse);

/**
 * @brief Set TCP_NODELAY socket option (disable Nagle's algorithm)
 * @param sock Socket to configure
 * @param nodelay true to disable Nagle's algorithm, false to enable
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_set_nodelay(socket_t sock, bool nodelay);

/**
 * @brief Set SO_KEEPALIVE socket option
 * @param sock Socket to configure
 * @param keepalive true to enable keepalive, false to disable
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_set_keepalive(socket_t sock, bool keepalive);

/**
 * @brief Set TCP keepalive parameters
 * @param sock Socket to configure
 * @param enable Enable/disable keepalive
 * @param idle Idle time before sending first keepalive probe (seconds)
 * @param interval Interval between keepalive probes (seconds)
 * @param count Number of keepalive probes before connection failure
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_set_keepalive_params(socket_t sock, bool enable, int idle, int interval, int count);

/**
 * @brief Set SO_LINGER socket option
 * @param sock Socket to configure
 * @param enable Enable/disable lingering
 * @param timeout Linger timeout in seconds
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_set_linger(socket_t sock, bool enable, int timeout);

/**
 * @brief Set socket buffer sizes
 * @param sock Socket to configure
 * @param recv_size Receive buffer size in bytes
 * @param send_size Send buffer size in bytes
 * @return 0 on success, non-zero on error
 *
 * @ingroup platform
 */
int socket_set_buffer_sizes(socket_t sock, int recv_size, int send_size);

/**
 * @brief Get peer address (convenience function)
 * @param sock Connected socket
 * @param addr Pointer to store peer address
 * @param addrlen Pointer to address length (input/output)
 * @return 0 on success, non-zero on error
 *
 * @note This is a convenience wrapper around socket_getpeername().
 *
 * @ingroup platform
 */
int socket_get_peer_address(socket_t sock, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Get socket-specific error code
 * @param sock Socket to query
 * @return Error code (platform-specific)
 *
 * @ingroup platform
 */
int socket_get_error(socket_t sock);

/**
 * @brief Get last socket error code
 * @return Error code (platform-specific)
 *
 * @ingroup platform
 */
int socket_get_last_error(void);

/**
 * @brief Get last socket error as string
 * @return Pointer to error string (may be static, do not free)
 *
 * @note The returned string may be a static buffer. Do not modify or free it.
 *
 * @ingroup platform
 */
const char *socket_get_error_string(void);

/**
 * @brief Poll sockets for events
 * @param fds Array of pollfd structures
 * @param nfds Number of file descriptors to poll
 * @param timeout Timeout in milliseconds (-1 for infinite)
 * @return Number of sockets with events, -1 on error
 *
 * @ingroup platform
 */
int socket_poll(struct pollfd *fds, nfds_t nfds, int timeout);

/**
 * @brief Select sockets for I/O readiness
 * @param max_fd Highest file descriptor number (plus 1)
 * @param readfds Set of sockets to check for read readiness (or NULL)
 * @param writefds Set of sockets to check for write readiness (or NULL)
 * @param exceptfds Set of sockets to check for exceptions (or NULL)
 * @param timeout Timeout value (or NULL for infinite)
 * @return Number of ready sockets, -1 on error
 *
 * @ingroup platform
 */
int socket_select(socket_t max_fd, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

/**
 * @brief Clear an fd_set
 * @param set fd_set to clear
 *
 * @ingroup platform
 */
void socket_fd_zero(fd_set *set);

/**
 * @brief Add a socket to an fd_set
 * @param sock Socket to add
 * @param set fd_set to add to
 *
 * @ingroup platform
 */
void socket_fd_set(socket_t sock, fd_set *set);

/**
 * @brief Check if a socket is in an fd_set
 * @param sock Socket to check
 * @param set fd_set to check in
 * @return Non-zero if socket is in set, 0 otherwise
 *
 * @ingroup platform
 */
int socket_fd_isset(socket_t sock, fd_set *set);

/**
 * @brief Get the underlying file descriptor (POSIX compatibility)
 * @param sock Socket handle
 * @return File descriptor value (on POSIX, same as socket handle)
 *
 * @ingroup platform
 */
int socket_get_fd(socket_t sock);

/**
 * @brief Check if a socket handle is valid
 * @param sock Socket handle to check
 * @return true if socket is valid, false otherwise
 *
 * @ingroup platform
 */
bool socket_is_valid(socket_t sock);

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

// Windows 10 SDK already defines pollfd and nfds_t in winsock2.h
// Only define them if not available (e.g., older Windows or custom builds)
#if !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#ifndef HAVE_STRUCT_POLLFD
struct pollfd {
  socket_t fd;
  short events;
  short revents;
};
#endif

typedef unsigned long nfds_t;
#endif
#endif
