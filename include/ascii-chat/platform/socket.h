#pragma once

/**
 * @file platform/socket.h
 * @brief Cross-platform socket interface for ascii-chat
 * @ingroup platform
 * @addtogroup platform
 * @{
 *
 * @tableofcontents
 *
 * @section socket_overview Overview
 *
 * This header provides a unified socket interface that abstracts platform-specific
 * implementations (Windows Winsock2 vs POSIX sockets). It enables the same socket code
 * to work identically on Windows, Linux, and macOS without platform-specific `#ifdef` blocks
 * in application code.
 *
 * **Key abstractions:**
 * - Socket handle unification: `socket_t` (SOCKET on Windows, int on POSIX)
 * - Error code normalization: Same error checking across all platforms
 * - Type definitions: `socklen_t`, `nfds_t` for Windows compatibility
 *
 * @section socket_interface High-Level Interface
 *
 * The socket interface provides:
 * - **Initialization**: `socket_init()`, `socket_cleanup()` (required on Windows)
 * - **Lifecycle**: `socket_create()`, `socket_close()`
 * - **Connection**: `socket_bind()`, `socket_listen()`, `socket_accept()`, `socket_connect()`
 * - **I/O operations**: `socket_send()`, `socket_recv()`, `socket_sendto()`, `socket_recvfrom()`
 * - **Configuration**: `socket_setsockopt()`, `socket_getsockopt()`
 * - **Socket options**: `socket_set_*()` convenience functions (nodelay, reuseaddr, keepalive, etc.)
 * - **Timeouts**: `socket_set_timeout()`, `socket_set_timeout_ns()`
 * - **Polling/Selection**: `socket_poll()`, `socket_select()` for multiplexed I/O
 * - **Utilities**: Error checking, blocking/non-blocking modes, peer address retrieval
 * - **Optimization**: `socket_optimize_for_streaming()` for high-throughput video
 *
 * @section socket_lifecycle Socket Lifecycle
 *
 * A typical socket workflow follows this pattern:
 *
 * @subsection socket_lifecycle_server Server Socket Lifecycle
 *
 * ```
 * socket_init()                           // (Windows: initialize Winsock)
 *     ↓
 * socket_create(AF_INET, SOCK_STREAM, 0) // Create listening socket
 *     ↓
 * socket_set_reuseaddr(sock, true)        // Configure (optional)
 *     ↓
 * socket_bind(sock, addr, addrlen)        // Bind to address
 *     ↓
 * socket_listen(sock, backlog)            // Listen for connections
 *     ↓
 * loop:
 *     socket_accept(sock, ...)            // Accept incoming connection
 *     ↓
 *     socket_recv(client, ...)            // Receive data
 *     socket_send(client, ...)            // Send data
 *     ↓
 *     socket_close(client)                // Close client connection
 *     ↓ (back to loop)
 *
 * socket_close(sock)                      // Close server socket
 *     ↓
 * socket_cleanup()                        // Cleanup (Windows: cleanup Winsock)
 * ```
 *
 * @subsection socket_lifecycle_client Client Socket Lifecycle
 *
 * ```
 * socket_init()                                     // (Windows: initialize Winsock)
 *     ↓
 * socket_create(AF_INET, SOCK_STREAM, 0)           // Create socket
 *     ↓
 * socket_connect(sock, remote_addr, addrlen)       // Connect to server
 *     ↓
 * socket_send(sock, data, len, 0)                  // Send data
 *     ↓
 * socket_recv(sock, buffer, len, 0)                // Receive data
 *     ↓
 * socket_shutdown(sock, SHUT_RDWR)                 // Shutdown I/O (optional)
 *     ↓
 * socket_close(sock)                               // Close socket
 *     ↓
 * socket_cleanup()                                 // Cleanup (Windows: cleanup Winsock)
 * ```
 *
 * @section socket_addressing IPv4 and IPv6 Support
 *
 * The socket interface supports both IPv4 and IPv6 through standard `struct sockaddr`
 * structures. Socket creation requires specifying the address family:
 *
 * **IPv4 (AF_INET):**
 * ```c
 * struct sockaddr_in addr4;
 * addr4.sin_family = AF_INET;
 * inet_pton(AF_INET, "127.0.0.1", &addr4.sin_addr);
 * addr4.sin_port = htons(27224);
 * socket_t sock = socket_create(AF_INET, SOCK_STREAM, 0);
 * socket_bind(sock, (struct sockaddr *)&addr4, sizeof(addr4));
 * ```
 *
 * **IPv6 (AF_INET6):**
 * ```c
 * struct sockaddr_in6 addr6;
 * addr6.sin6_family = AF_INET6;
 * inet_pton(AF_INET6, "::1", &addr6.sin6_addr);
 * addr6.sin6_port = htons(27224);
 * socket_t sock = socket_create(AF_INET6, SOCK_STREAM, 0);
 * socket_bind(sock, (struct sockaddr *)&addr6, sizeof(addr6));
 * ```
 *
 * @section socket_error_handling Error Handling
 *
 * All socket functions that return error codes follow a consistent pattern:
 *
 * **Return value conventions:**
 * - Functions returning `socket_t`: Returns INVALID_SOCKET_VALUE on error
 * - Functions returning `int`: Returns 0 on success, -1 (or non-zero) on error
 * - Functions returning `ssize_t`: Returns bytes transferred (>= 0) on success, -1 on error
 *
 * **Error code retrieval:**
 * ```c
 * ssize_t result = socket_recv(sock, buf, len, 0);
 * if (result < 0) {
 *     int error = socket_get_last_error();
 *     const char *error_str = socket_get_error_string();
 *     log_error("recv failed: %s", error_str);
 * }
 * ```
 *
 * **Platform-specific error checking helper:**
 * Use the `socket_is_*_error()` family of functions for portable error detection:
 * - `socket_is_would_block_error()` - For non-blocking retries (EAGAIN/EWOULDBLOCK)
 * - `socket_is_connection_reset_error()` - For connection reset detection
 * - `socket_is_invalid_socket_error()` - For closed/invalid socket detection
 * - `socket_is_in_progress_error()` - For non-blocking connect detection
 *
 * @section socket_platform_notes Platform-Specific Behavior
 *
 * @subsection socket_windows Windows (Winsock2)
 * - Requires `socket_init()` call before creating any sockets
 * - Socket type: `SOCKET` (typedef'd as `socket_t`)
 * - Invalid socket constant: `INVALID_SOCKET` (typedef'd as `INVALID_SOCKET_VALUE`)
 * - Uses `closesocket()` instead of `close()` (abstracted in `socket_close()`)
 * - Error codes are Winsock-specific (WSAEWOULDBLOCK, WSAEINPROGRESS, etc.)
 * - `socket_poll()` uses `WSAPoll()` if available, falls back to `select()`
 *
 * @subsection socket_posix POSIX (Linux, macOS)
 * - Socket initialization is automatic (socket_init() is a no-op)
 * - Socket type: `int` (file descriptor)
 * - Invalid socket constant: `-1`
 * - Uses standard POSIX socket functions and error codes
 * - Error codes: EAGAIN, EWOULDBLOCK, EINPROGRESS, etc.
 * - `socket_poll()` uses `poll()` for efficient I/O multiplexing
 * - `socket_select()` uses standard `select()` API
 *
 * @section socket_blocking_modes Blocking and Non-Blocking Modes
 *
 * Sockets are blocking by default. Use `socket_set_nonblocking()` to enable non-blocking mode:
 *
 * **Blocking socket (default):**
 * ```c
 * socket_t sock = socket_create(AF_INET, SOCK_STREAM, 0);
 * // socket_recv() will wait indefinitely for data
 * // socket_connect() will wait indefinitely for connection
 * ```
 *
 * **Non-blocking socket:**
 * ```c
 * socket_set_nonblocking(sock, true);
 * ssize_t result = socket_recv(sock, buf, len, 0);
 * if (result < 0 && socket_is_would_block_error(socket_get_last_error())) {
 *     // No data available, can retry later
 * }
 * ```
 *
 * @section socket_performance Performance Optimization
 *
 * @subsection socket_video_streaming Video Streaming Optimization
 *
 * For high-throughput video streaming, use `socket_optimize_for_streaming()`:
 * ```c
 * socket_t client = socket_accept(server_sock, NULL, NULL);
 * socket_optimize_for_streaming(client);
 * // Automatically applies:
 * // - TCP_NODELAY (disables Nagle's algorithm)
 * // - Large send/receive buffers
 * // - Keepalive with tuned parameters
 * // - Appropriate timeouts
 * ```
 *
 * @subsection socket_custom_tuning Custom Socket Tuning
 *
 * For specialized use cases, manually configure sockets:
 * ```c
 * socket_set_nodelay(sock, true);           // Disable Nagle's algorithm
 * socket_set_reuseaddr(sock, true);         // Allow rapid rebinding
 * socket_set_keepalive(sock, true);         // Enable TCP keepalive
 * socket_set_buffer_sizes(sock, 2MB, 2MB);  // Large buffers for throughput
 * socket_set_timeout(sock, 30000000000LL);  // 30 second timeout in nanoseconds
 * ```
 *
 * @section socket_multiplexing I/O Multiplexing
 *
 * Use polling for efficient monitoring of multiple sockets:
 *
 * **Poll API (recommended):**
 * ```c
 * struct pollfd fds[2];
 * fds[0].fd = socket_get_fd(server_sock);
 * fds[0].events = POLLIN;  // Monitor for incoming connections
 * fds[1].fd = socket_get_fd(client_sock);
 * fds[1].events = POLLIN | POLLOUT;  // Monitor read and write
 *
 * int ready = socket_poll(fds, 2, 1000000000LL);  // 1 second timeout
 * if (ready > 0) {
 *     if (fds[0].revents & POLLIN) {
 *         // Server ready to accept
 *     }
 *     if (fds[1].revents & POLLIN) {
 *         // Client ready to read
 *     }
 * }
 * ```
 *
 * **Select API (legacy):**
 * ```c
 * fd_set readfds, writefds;
 * socket_fd_zero(&readfds);
 * socket_fd_set(server_sock, &readfds);
 * socket_fd_set(client_sock, &readfds);
 *
 * struct timeval timeout = {1, 0};  // 1 second
 * int ready = socket_select(client_sock + 1, &readfds, NULL, NULL, &timeout);
 * if (ready > 0) {
 *     if (socket_fd_isset(server_sock, &readfds)) {
 *         // Server ready to accept
 *     }
 * }
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date September 2025
 */

#include <stdbool.h>
#include <string.h> // For memcpy used in common.h
#include <sys/types.h>
#include "../platform/util.h" // For ssize_t definition on Windows

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h> // Includes TCP_NODELAY and other TCP options
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
#include <netinet/tcp.h> // For TCP_NODELAY and other TCP options
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
 * @param domain Socket domain (AF_INET for IPv4, AF_INET6 for IPv6, AF_UNIX for local)
 * @param type Socket type (SOCK_STREAM for TCP, SOCK_DGRAM for UDP)
 * @param protocol Protocol (typically 0 for automatic selection based on domain/type)
 * @return Socket handle on success, INVALID_SOCKET_VALUE on error
 *
 * Creates a new socket but does not connect it. Use with socket_bind() and
 * socket_listen() for servers, or socket_connect() for clients.
 *
 * **Common domain/type combinations:**
 * - AF_INET + SOCK_STREAM = TCP over IPv4
 * - AF_INET6 + SOCK_STREAM = TCP over IPv6
 * - AF_INET + SOCK_DGRAM = UDP over IPv4
 * - AF_INET6 + SOCK_DGRAM = UDP over IPv6
 *
 * **Platform-specific notes:**
 * - **Windows**: Requires socket_init() to be called first
 * - **POSIX**: No initialization required
 *
 * **Error handling:**
 * ```c
 * socket_t sock = socket_create(AF_INET, SOCK_STREAM, 0);
 * if (!socket_is_valid(sock)) {
 *     log_error("Socket creation failed: %s", socket_get_error_string());
 *     return false;
 * }
 * ```
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
 * @param sock Socket to connect (must be created but not yet connected)
 * @param addr Remote address to connect to (struct sockaddr_in for IPv4, struct sockaddr_in6 for IPv6)
 * @param addrlen Length of address structure
 * @return 0 on success, non-zero on error
 *
 * Initiates a connection to a remote address. For TCP sockets (SOCK_STREAM),
 * this blocks until the connection succeeds or fails (unless socket is non-blocking).
 *
 * **Typical usage (IPv4):**
 * ```c
 * struct sockaddr_in addr;
 * addr.sin_family = AF_INET;
 * addr.sin_port = htons(27224);
 * inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
 *
 * socket_t sock = socket_create(AF_INET, SOCK_STREAM, 0);
 * if (socket_connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
 *     log_error("Connection failed: %s", socket_get_error_string());
 *     socket_close(sock);
 * }
 * ```
 *
 * **Non-blocking connect:**
 * For non-blocking sockets, connect() returns immediately with EINPROGRESS/WSAEINPROGRESS.
 * Use socket_poll() or socket_select() to wait for connection completion:
 * ```c
 * socket_set_nonblocking(sock, true);
 * if (socket_connect(sock, &addr, sizeof(addr)) < 0) {
 *     if (socket_is_in_progress_error(socket_get_last_error())) {
 *         // Connection in progress, poll for completion
 *     }
 * }
 * ```
 *
 * @ingroup platform
 */
int socket_connect(socket_t sock, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief Send data on a socket
 * @param sock Socket to send on (must be connected)
 * @param buf Data buffer to send
 * @param len Number of bytes to send
 * @param flags Socket flags (typically 0; MSG_DONTWAIT for non-blocking on POSIX)
 * @return Number of bytes sent (0 to len) on success, -1 on error
 *
 * Sends data on a connected socket. The return value indicates how many bytes
 * were actually sent, which may be less than requested for non-blocking sockets
 * or when the send buffer is full.
 *
 * **Important semantics:**
 * - Returns 0 when socket is non-blocking and send buffer is full
 * - Returns -1 on error (check socket_get_last_error())
 * - Returns 1 to len on success
 * - For reliable delivery, loop until all bytes are sent
 *
 * **Error handling:**
 * ```c
 * size_t total_sent = 0;
 * while (total_sent < len) {
 *     ssize_t sent = socket_send(sock, (char *)buf + total_sent, len - total_sent, 0);
 *     if (sent < 0) {
 *         if (socket_is_would_block_error(socket_get_last_error())) {
 *             // Non-blocking socket, buffer full, retry later
 *             break;
 *         } else {
 *             // Connection reset or other fatal error
 *             return false;
 *         }
 *     }
 *     total_sent += sent;
 * }
 * ```
 *
 * **Platform-specific notes:**
 * - **Windows**: socket_send() is a thin wrapper around send()
 * - **POSIX**: socket_send() is a thin wrapper around send() (or write())
 * - Partial sends are normal and should be handled
 *
 * @ingroup platform
 */
ssize_t socket_send(socket_t sock, const void *buf, size_t len, int flags);

/**
 * @brief Receive data from a socket
 * @param sock Socket to receive from (must be connected)
 * @param buf Buffer to store received data
 * @param len Maximum number of bytes to receive (buffer must be at least len bytes)
 * @param flags Socket flags (typically 0; MSG_DONTWAIT for non-blocking on POSIX)
 * @return Number of bytes received (1 to len) on success, 0 on connection closed, -1 on error
 *
 * Receives data from a connected socket. This is the primary function for reading
 * data from established connections (TCP streams, connected UDP sockets).
 *
 * **Return value semantics:**
 * - Returns 0: Connection closed by peer (graceful shutdown)
 * - Returns > 0: Data received (1 to len bytes)
 * - Returns -1: Error (check socket_get_last_error())
 *
 * **Common error cases:**
 * - EAGAIN/EWOULDBLOCK on non-blocking socket with no data available
 * - ECONNRESET on forcible connection closure
 * - EBADF on invalid socket or closed socket
 *
 * **Typical receive loop:**
 * ```c
 * char buffer[4096];
 * ssize_t received = socket_recv(sock, buffer, sizeof(buffer) - 1, 0);
 * if (received == 0) {
 *     // Connection closed gracefully
 *     return;
 * } else if (received < 0) {
 *     if (socket_is_would_block_error(socket_get_last_error())) {
 *         // Non-blocking socket, no data available
 *     } else if (socket_is_connection_reset_error(socket_get_last_error())) {
 *         // Connection reset by peer
 *     } else {
 *         // Other error
 *     }
 * } else {
 *     // Process received bytes
 *     process_data(buffer, (size_t)received);
 * }
 * ```
 *
 * **For UDP (datagram) sockets:**
 * Use socket_recvfrom() instead to receive data with source address information.
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
 * @param level Option level (SOL_SOCKET for socket-level, IPPROTO_TCP for TCP, IPPROTO_IPV6 for IPv6, etc.)
 * @param optname Option name (SO_REUSEADDR, TCP_NODELAY, SO_RCVTIMEO, SO_SNDBUF, etc.)
 * @param optval Pointer to option value (type depends on optname)
 * @param optlen Length of option value
 * @return 0 on success, non-zero on error
 *
 * Sets socket options. Common options are available via convenience functions
 * (socket_set_nodelay(), socket_set_reuseaddr(), etc.), but socket_setsockopt()
 * provides direct access for advanced or platform-specific options.
 *
 * **Common socket-level options (SOL_SOCKET):**
 * - SO_REUSEADDR: Allow rapid socket rebinding (int, 0 or 1)
 * - SO_KEEPALIVE: Enable TCP keepalive probes (int, 0 or 1)
 * - SO_RCVBUF: Receive buffer size in bytes (int)
 * - SO_SNDBUF: Send buffer size in bytes (int)
 * - SO_RCVTIMEO: Receive timeout (struct timeval on POSIX, DWORD ms on Windows)
 * - SO_SNDTIMEO: Send timeout (struct timeval on POSIX, DWORD ms on Windows)
 *
 * **Common TCP options (IPPROTO_TCP):**
 * - TCP_NODELAY: Disable Nagle's algorithm for reduced latency (int, 0 or 1)
 * - TCP_KEEPIDLE/TCP_KEEPINTVL/TCP_KEEPCNT: Keepalive parameters (Linux/POSIX)
 *
 * **Common IPv6 options (IPPROTO_IPV6):**
 * - IPV6_V6ONLY: Restrict to IPv6-only or allow IPv4 too (int, 0 or 1)
 *
 * **Example: Set receive timeout (cross-platform):**
 * ```c
 * #ifdef _WIN32
 *     DWORD timeout_ms = 5000;  // 5 second timeout
 *     socket_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
 * #else
 *     struct timeval timeout = {5, 0};  // 5 seconds, 0 microseconds
 *     socket_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
 * #endif
 * ```
 *
 * **Recommendation:** Use convenience functions when available:
 * - socket_set_nodelay(), socket_set_reuseaddr(), socket_set_buffer_sizes() provide
 *   cross-platform abstractions and error checking.
 *
 * @ingroup platform
 */
int socket_setsockopt(socket_t sock, int level, int optname, const void *optval, socklen_t optlen);

/**
 * @brief Get socket option
 * @param sock Socket to query
 * @param level Option level (SOL_SOCKET, IPPROTO_TCP, IPPROTO_IPV6, etc.)
 * @param optname Option name (SO_REUSEADDR, TCP_NODELAY, SO_RCVBUF, etc.)
 * @param optval Pointer to store option value (type depends on optname)
 * @param optlen Pointer to option length (on input: buffer size, on output: actual length)
 * @return 0 on success, non-zero on error
 *
 * Retrieves the current value of a socket option. The caller must provide
 * a buffer (optval) and specify its size (optlen). On return, optlen contains
 * the actual size of the option value.
 *
 * **Important implementation detail:**
 * optlen is both an input and output parameter:
 * - On input: Maximum size of the buffer pointed to by optval
 * - On output: Actual size of the option value
 *
 * **Example: Get receive buffer size:**
 * ```c
 * int recv_buf_size;
 * socklen_t optlen = sizeof(recv_buf_size);
 * if (socket_getsockopt(sock, SOL_SOCKET, SO_RCVBUF, &recv_buf_size, &optlen) == 0) {
 *     log_info("Receive buffer size: %d bytes", recv_buf_size);
 * }
 * ```
 *
 * **Platform-specific variations:**
 * - Windows: Some timeout options use DWORD (milliseconds)
 * - POSIX: Some timeout options use struct timeval (seconds + microseconds)
 * - Option availability varies by OS (e.g., TCP_KEEPIDLE is Linux-specific)
 *
 * @ingroup platform
 */
int socket_getsockopt(socket_t sock, int level, int optname, void *optval, socklen_t *optlen);

/**
 * @brief Set socket send/receive timeout in nanoseconds
 * @param sock Socket to configure
 * @param timeout_ns Timeout in nanoseconds (0 to disable timeout)
 * @return 0 on success, non-zero on error
 *
 * Sets both SO_SNDTIMEO and SO_RCVTIMEO socket options.
 * Nanosecond values are converted to platform-specific time units internally.
 *
 * @ingroup platform
 */
int socket_set_timeout_ns(socket_t sock, uint64_t timeout_ns);

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
 * @brief Set socket receive and send timeouts
 * @param sock Socket to configure
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, non-zero on error
 *
 * Sets both SO_RCVTIMEO (receive timeout) and SO_SNDTIMEO (send timeout)
 * to prevent indefinite blocking on socket operations.
 *
 * Platform-specific implementations:
 * - Windows: Converts nanoseconds to milliseconds for DWORD timeout
 * - POSIX: Converts nanoseconds to struct timeval (seconds + microseconds)
 *
 * @ingroup platform
 */
int socket_set_timeout(socket_t sock, uint64_t timeout_ns);

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
 * @brief Optimize socket for high-throughput video streaming
 * @param sock Socket to optimize
 *
 * Applies multiple socket optimizations for video streaming:
 * - Disables Nagle's algorithm (TCP_NODELAY)
 * - Sets large send/receive buffers (2MB with fallbacks to 512KB and 128KB)
 * - Enables keepalive
 * - Sets timeouts to prevent blocking indefinitely
 *
 * This function consolidates socket configuration that is needed for real-time
 * video streaming. It gracefully handles buffer size negotiation by falling back
 * to smaller sizes if the OS doesn't support large buffers.
 *
 * @note Warnings are logged if individual options fail, but the function
 *       continues to apply the remaining options.
 *
 * @ingroup platform
 */
void socket_optimize_for_streaming(socket_t sock);

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
 * @brief Poll sockets for events (multiplexed I/O)
 * @param fds Array of pollfd structures (contains fd, events, and output revents)
 * @param nfds Number of file descriptors to poll
 * @param timeout_ns Timeout in nanoseconds (0 for immediate return, -1 for infinite, >0 for specific timeout)
 * @return Number of sockets with ready events, 0 on timeout, -1 on error
 *
 * Monitors multiple sockets for readiness events. This is the recommended way
 * to wait on multiple sockets efficiently, especially in high-performance scenarios.
 *
 * **pollfd structure:**
 * ```c
 * struct pollfd {
 *     socket_t fd;        // Socket to monitor
 *     short events;       // Requested events (POLLIN, POLLOUT, POLLERR, etc.)
 *     short revents;      // Returned events (output, set by socket_poll)
 * };
 * ```
 *
 * **Event flags (events/revents):**
 * - POLLIN: Data available for reading (or new connection on listening socket)
 * - POLLOUT: Socket is writable (buffer has space)
 * - POLLERR: Error condition
 * - POLLHUP: Connection closed by peer
 * - POLLNVAL: Invalid socket
 *
 * **Example: Monitor server and client sockets:**
 * ```c
 * struct pollfd fds[2];
 * fds[0].fd = socket_get_fd(server_sock);
 * fds[0].events = POLLIN;  // Wait for incoming connections
 * fds[1].fd = socket_get_fd(client_sock);
 * fds[1].events = POLLIN | POLLOUT;  // Wait for read or write readiness
 *
 * int ready = socket_poll(fds, 2, 5000000000LL);  // 5 second timeout
 * if (ready < 0) {
 *     // Error
 * } else if (ready == 0) {
 *     // Timeout, no sockets ready
 * } else {
 *     // Check which sockets are ready
 *     if (fds[0].revents & POLLIN) {
 *         // Server ready to accept
 *         socket_t client = socket_accept(server_sock, NULL, NULL);
 *     }
 *     if (fds[1].revents & POLLIN) {
 *         // Client has data to read
 *     }
 *     if (fds[1].revents & POLLERR) {
 *         // Client socket error
 *     }
 * }
 * ```
 *
 * **Platform-specific implementation:**
 * - **POSIX**: Uses poll() system call efficiently (O(n) complexity)
 * - **Windows**: Uses WSAPoll() if available (Windows Vista+), falls back to select()
 *
 * **Works with both IPv4 and IPv6:**
 * socket_poll() works transparently with both IPv4 and IPv6 sockets created with
 * AF_INET and AF_INET6 respectively.
 *
 * @ingroup platform
 */
int socket_poll(struct pollfd *fds, nfds_t nfds, int64_t timeout_ns);

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

/**
 * @brief Check if error code indicates "would block" (non-blocking socket would wait)
 * @param error_code Platform-specific error code (from socket_get_last_error())
 * @return true if error is EAGAIN/EWOULDBLOCK (POSIX) or WSAEWOULDBLOCK (Windows)
 *
 * Detects when a non-blocking socket operation needs to be retried later because
 * the operation would have blocked. This is the standard way to implement
 * non-blocking I/O patterns.
 *
 * **Platform abstraction:**
 * - **POSIX**: Detects both EAGAIN and EWOULDBLOCK (they're often the same)
 * - **Windows**: Detects WSAEWOULDBLOCK (WSA-prefixed Winsock error)
 *
 * **Typical non-blocking receive pattern:**
 * ```c
 * socket_set_nonblocking(sock, true);
 *
 * while (more_data_expected) {
 *     ssize_t result = socket_recv(sock, buf, len, 0);
 *     if (result > 0) {
 *         // Process received data
 *         process_data(buf, (size_t)result);
 *     } else if (result < 0) {
 *         if (socket_is_would_block_error(socket_get_last_error())) {
 *             // No data available, check other sockets or wait
 *             break;
 *         } else if (socket_is_connection_reset_error(socket_get_last_error())) {
 *             // Connection closed abruptly
 *             handle_error();
 *         } else {
 *             // Other error
 *             handle_error();
 *         }
 *     } else {
 *         // result == 0: Connection closed gracefully
 *         break;
 *     }
 * }
 * ```
 *
 * **Typical non-blocking send pattern:**
 * ```c
 * size_t total_sent = 0;
 * while (total_sent < len) {
 *     ssize_t sent = socket_send(sock, (char *)buf + total_sent, len - total_sent, 0);
 *     if (sent > 0) {
 *         total_sent += sent;
 *     } else if (sent < 0) {
 *         if (socket_is_would_block_error(socket_get_last_error())) {
 *             // Send buffer full, retry later with poll/select
 *             break;
 *         } else {
 *             // Fatal error
 *             return false;
 *         }
 *     } else {
 *         // sent == 0 shouldn't happen on send, but handle it
 *         break;
 *     }
 * }
 * ```
 *
 * @ingroup platform
 */
bool socket_is_would_block_error(int error_code);

/**
 * @brief Check if error code indicates connection reset
 * @param error_code Platform-specific error code (from socket_get_last_error())
 * @return true if error is ECONNRESET (POSIX) or WSAECONNRESET (Windows)
 *
 * Used to detect when the remote peer forcibly closed the connection.
 * Abstracts platform differences between POSIX and Windows.
 *
 * @ingroup platform
 */
bool socket_is_connection_reset_error(int error_code);

/**
 * @brief Check if error code indicates a closed/invalid socket
 * @param error_code Platform-specific error code (from socket_get_last_error())
 * @return true if error indicates socket is closed or invalid
 *
 * Detects errors like EBADF (bad file descriptor) on POSIX or
 * WSAENOTSOCK (socket operation on non-socket) on Windows.
 *
 * @ingroup platform
 */
bool socket_is_invalid_socket_error(int error_code);

/**
 * @brief Check if error indicates operation in progress (non-blocking connect)
 * @param error_code Platform-specific error code (from socket_get_last_error())
 * @return true if error is EINPROGRESS (POSIX) or WSAEINPROGRESS (Windows)
 *
 * Used for non-blocking connect() operations. When connect() is called on a
 * non-blocking socket, it returns immediately with EINPROGRESS/WSAEINPROGRESS
 * if the connection is still being established.
 *
 * @par Example:
 * @code{.c}
 * int result = connect(sock, addr, addrlen);
 * if (result < 0 && socket_is_in_progress_error(socket_get_last_error())) {
 *   // Connection in progress - use select/poll to wait
 * }
 * @endcode
 *
 * @ingroup platform
 */
bool socket_is_in_progress_error(int error_code);

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

/** @} */

// ============================================================================
// Socket Timeout Operations
// ============================================================================

/**
 * @brief Set send/receive timeout for a socket
 *
 * Configures the timeout for socket send and receive operations.
 *
 * Platform-specific behavior:
 *   - Windows: Uses ioctlsocket() with SO_RCVTIMEO/SO_SNDTIMEO options
 *   - POSIX: Uses setsockopt() with SO_RCVTIMEO/SO_SNDTIMEO options
 *
 * @param sock Socket to configure
 * @param timeout_ms Timeout in milliseconds (0 = blocking, -1 = infinite)
 * @return 0 on success, -1 on error
 *
 * @note Timeout applies to both send and receive operations
 * @note Some platforms may require SO_SNDTIMEO and SO_RCVTIMEO separately
 *
 * @ingroup platform
 */
int platform_socket_set_timeout(socket_t sock, uint64_t timeout_ns);

/**
 * @brief Connect to remote address with timeout
 *
 * Attempts to connect to a remote address with an optional timeout.
 *
 * Platform-specific behavior:
 *   - Windows: Uses ioctlsocket() to set non-blocking, connect(), then select()
 *   - POSIX: Uses fcntl() to set non-blocking, connect(), then poll()
 *
 * @param sock Socket to connect
 * @param addr Address structure to connect to
 * @param addr_len Length of address structure
 * @param timeout_ms Timeout in milliseconds (0 = infinite wait)
 * @return 0 on success, -1 on timeout or error
 *
 * @note Socket must be created but not yet connected
 * @note After call, socket is set back to blocking mode on success
 *
 * @ingroup platform
 */
int platform_socket_connect_timeout(socket_t sock, const struct sockaddr *addr, socklen_t addr_len,
                                    uint64_t timeout_ns);
