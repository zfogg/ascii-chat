/**
 * @file network/tcp_client.h
 * @brief TCP client connection state (network-layer only)
 *
 * This module provides TCP-specific connection management via tcp_client_t.
 * It handles only network concerns: socket, server address, connection state,
 * and thread-safe packet transmission.
 *
 * ## Architecture
 *
 * As part of the abstraction trilogy:
 * 1. **Network clients** (tcp_client_t, websocket_client_t, etc.) - transport-specific
 * 2. **Application context** (app_client_t in network/client.h) - transport-agnostic
 * 3. **Protocol handlers** (ACIP) - work with any transport via acip_transport_t
 *
 * tcp_client_t is now slim and focused: only socket, connection state, and send mutex.
 *
 * ## Related Types
 *
 * - app_client_t: Application state (audio, threads, display, crypto)
 * - acip_transport_t: Protocol-agnostic send/recv interface
 * - websocket_client_t: WebSocket transport equivalent
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 * @version 2.0
 */

#ifndef NETWORK_TCP_CLIENT_H
#define NETWORK_TCP_CLIENT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../platform/socket.h"
#include "../../network/packet/packet.h"

/**
 * @brief TCP client connection state (network-layer only)
 *
 * Encapsulates network-specific state for a single TCP client connection:
 * - Socket file descriptor
 * - Server address information
 * - Connection state flags
 * - Send mutex for thread-safe packet transmission
 * - Encryption flag
 *
 * Application-layer state (audio, threads, display, crypto) is now in
 * app_client_t (network/client.h) to enable transport-agnostic design.
 *
 * ## Ownership Model
 *
 * - Created by tcp_client_create() in main thread
 * - Owned by connection management layer
 * - Destroyed after network I/O is complete
 *
 * ## Thread Safety
 *
 * - Atomic fields: Safe for concurrent read/write without locks
 * - Mutex-protected fields: Acquire send_mutex before socket writes
 * - Immutable after init: server_ip is set once, then read-only
 *
 * @see app_client_t For application-layer state
 * @see websocket_client_t For WebSocket transport equivalent
 */
typedef struct tcp_client {
  /* ========================================================================
   * Connection State (TCP-Specific)
   * ======================================================================== */

  /** Socket file descriptor for server connection */
  socket_t sockfd;

  /** Connection is active and ready for I/O operations */
  atomic_bool connection_active;

  /** Connection was lost (triggers reconnection logic) */
  atomic_bool connection_lost;

  /** Reconnection should be attempted */
  atomic_bool should_reconnect;

  /** Client ID assigned by server during initial handshake */
  uint32_t my_client_id;

  /** Server IP address (for display and reconnection) */
  char server_ip[256];

  /** Mutex protecting concurrent socket send operations */
  mutex_t send_mutex;

  /** Encryption is enabled for this connection */
  bool encryption_enabled;

} tcp_client_t;

/**
 * @brief Create and initialize a TCP client instance
 *
 * Allocates a new tcp_client_t structure and initializes all fields to safe
 * defaults. This function must be called before starting any worker threads.
 *
 * ## Initialization Steps
 *
 * 1. Allocate tcp_client_t structure
 * 2. Zero-initialize all fields
 * 3. Set atomic flags to initial states
 * 4. Initialize mutexes and condition variables
 * 5. Set socket to INVALID_SOCKET_VALUE
 *
 * ## Error Handling
 *
 * Returns NULL if allocation fails or mutex initialization fails.
 * Check errno or use HAS_ERRNO() for detailed error information.
 *
 * @return Pointer to initialized client, or NULL on failure
 *
 * @note Caller must call tcp_client_destroy() when done
 * @see tcp_client_destroy() For proper cleanup
 */
tcp_client_t *tcp_client_create(void);

/**
 * @brief Destroy TCP client and free all resources
 *
 * Destroys all synchronization primitives and frees the client structure.
 * This function must be called AFTER all worker threads have been joined.
 *
 * ## Cleanup Steps
 *
 * 1. Verify all threads have exited (debug builds only)
 * 2. Destroy all mutexes and condition variables
 * 3. Free tcp_client_t structure
 * 4. Set pointer to NULL (via parameter)
 *
 * ## Thread Safety
 *
 * This function is NOT thread-safe. All worker threads must be joined
 * before calling this function.
 *
 * @param client_ptr Pointer to client pointer (set to NULL after free)
 *
 * @warning All threads must be stopped and joined before calling
 * @note No-op if client_ptr is NULL or *client_ptr is NULL
 */
void tcp_client_destroy(tcp_client_t **client_ptr);

/* ============================================================================
 * Connection State Queries
 * ============================================================================ */

/**
 * @brief Check if connection is currently active
 * @param client TCP client instance
 * @return true if connection is active, false otherwise
 */
bool tcp_client_is_active(const tcp_client_t *client);

/**
 * @brief Check if connection was lost
 * @param client TCP client instance
 * @return true if connection loss was detected, false otherwise
 */
bool tcp_client_is_lost(const tcp_client_t *client);

/**
 * @brief Get current socket descriptor
 * @param client TCP client instance
 * @return Socket descriptor or INVALID_SOCKET_VALUE if not connected
 */
socket_t tcp_client_get_socket(const tcp_client_t *client);

/**
 * @brief Get client ID assigned by server
 * @param client TCP client instance
 * @return Client ID (from local port) or 0 if not connected
 */
uint32_t tcp_client_get_id(const tcp_client_t *client);

/* ============================================================================
 * Connection Control
 * ============================================================================ */

/**
 * @brief Signal that connection was lost (triggers reconnection)
 * @param client TCP client instance
 */
void tcp_client_signal_lost(tcp_client_t *client);

/**
 * @brief Close connection gracefully
 * @param client TCP client instance
 */
void tcp_client_close(tcp_client_t *client);

/**
 * @brief Shutdown connection forcefully (for signal handlers)
 * @param client TCP client instance
 */
void tcp_client_shutdown(tcp_client_t *client);

/* ============================================================================
 * Connection Establishment
 * ============================================================================ */

/**
 * @brief Establish TCP connection to server
 *
 * Performs full connection lifecycle including DNS resolution, socket creation,
 * connection with timeout, and socket configuration. Does NOT perform crypto
 * handshake or send initial packets - those are application responsibilities.
 *
 * @param client TCP client instance
 * @param address Server hostname or IP address
 * @param port Server port number
 * @param reconnect_attempt Current reconnection attempt (0 for first, 1+ for retries)
 * @param first_connection True if this is the very first connection since program start
 * @param has_ever_connected True if client has successfully connected at least once
 * @return 0 on success, negative on error
 */
int tcp_client_connect(tcp_client_t *client, const char *address, int port, int reconnect_attempt,
                       bool first_connection, bool has_ever_connected);

/* ============================================================================
 * Thread-Safe Packet Transmission
 * ============================================================================ */

/**
 * @brief Send packet with thread-safe mutex protection
 *
 * All packet transmission goes through this function to ensure packets
 * aren't interleaved on the wire. Automatically handles encryption if
 * crypto context is ready.
 *
 * @param client TCP client instance
 * @param type Packet type identifier
 * @param data Packet payload
 * @param len Payload length
 * @return 0 on success, negative on error
 */
int tcp_client_send_packet(tcp_client_t *client, packet_type_t type, const void *data, size_t len);

/**
 * @brief Send ping packet
 * @param client TCP client instance
 * @return 0 on success, negative on error
 */
int tcp_client_send_ping(tcp_client_t *client);

/**
 * @brief Send pong packet
 * @param client TCP client instance
 * @return 0 on success, negative on error
 */
int tcp_client_send_pong(tcp_client_t *client);

#endif /* NETWORK_TCP_CLIENT_H */
