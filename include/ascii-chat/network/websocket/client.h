/**
 * @file network/websocket/client.h
 * @brief WebSocket client abstraction for ascii-chat connections
 *
 * This module provides a reusable WebSocket client implementation that parallels
 * tcp_client.h, enabling WebSocket connections as a transport alternative to TCP.
 *
 * ## Architecture
 *
 * The websocket_client module encapsulates WebSocket-specific connection state,
 * mirroring the structure of tcp_client_t:
 * - Connection lifecycle management (create, connect, destroy)
 * - Connection state tracking (active, lost, signals)
 * - Transport abstraction (returns acip_transport_t for protocol agnostic use)
 *
 * Unlike tcp_client_t, this module does NOT contain:
 * - Audio/video thread management (handled by client_context_t)
 * - Capture threads (handled by client_context_t)
 * - Protocol-specific packet builders (handled by application)
 *
 * ## Usage Pattern
 *
 * ```c
 * // Create WebSocket client
 * websocket_client_t *ws_client = websocket_client_create();
 *
 * // Connect to server
 * acip_transport_t *transport = websocket_client_connect(ws_client, "ws://localhost:27226", crypto_ctx);
 * if (!transport) {
 *   log_error("Connection failed");
 *   websocket_client_destroy(&ws_client);
 *   return;
 * }
 *
 * // Use transport with ACIP protocol handlers
 * acip_send_packet(transport, packet_type, data, len);
 *
 * // Check connection state
 * if (!websocket_client_is_active(ws_client)) {
 *   log_warn("Connection lost");
 * }
 *
 * // Cleanup when done
 * websocket_client_close(ws_client);
 * websocket_client_destroy(&ws_client);
 * ```
 *
 * ## Comparison with tcp_client_t
 *
 * | Aspect              | tcp_client_t        | websocket_client_t     |
 * |---------------------|---------------------|------------------------|
 * | Connection type     | TCP socket          | WebSocket (TCP-based)  |
 * | State tracking      | Yes                 | Yes                    |
 * | Audio queues        | Yes (TO REMOVE)     | No (in client_context) |
 * | Thread management   | Yes (TO REMOVE)     | No (in client_context) |
 * | Transport return    | N/A                 | acip_transport_t*      |
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 * @version 1.0
 */

#ifndef NETWORK_WEBSOCKET_CLIENT_H
#define NETWORK_WEBSOCKET_CLIENT_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../common.h"
#include "../../asciichat_errno.h"
#include "../acip/transport.h"
#include "../packet.h"
#include "../../platform/abstraction.h"

/* Forward declarations */
struct crypto_context_t;

/**
 * @brief WebSocket client connection state
 *
 * Encapsulates WebSocket-specific connection state, including:
 * - Connection URL and state flags
 * - Client ID and encryption state
 * - Active transport (owned by websocket_client)
 * - Thread-safe packet transmission mutex
 *
 * This structure mirrors tcp_client_t for API compatibility.
 * Application state (audio, threads, crypto) lives in client_context_t instead.
 *
 * ## Thread Safety
 *
 * - Atomic fields: Safe for concurrent read/write without locks
 * - Immutable after init: url is set once, then read-only
 * - Mutex: Protects concurrent packet transmission
 */
typedef struct websocket_client {
  /** WebSocket server URL (e.g., "ws://localhost:27226") */
  char url[512];

  /** Connection is active and ready for I/O operations */
  atomic_bool connection_active;

  /** Connection was lost (triggers reconnection logic) */
  atomic_bool connection_lost;

  /** Reconnection should be attempted */
  atomic_bool should_reconnect;

  /** This client's unique ID (derived from URL hash or transport-provided) */
  uint32_t my_client_id;

  /** Whether encryption is enabled for this connection */
  bool encryption_enabled;

  /** Mutex protecting concurrent packet transmission */
  mutex_t send_mutex;

  /** Transport instance (owned by websocket_client) - NULL until connected */
  acip_transport_t *transport;

} websocket_client_t;

/**
 * @brief Create and initialize a WebSocket client instance
 *
 * Allocates a new websocket_client_t structure and initializes all fields
 * to safe defaults. The transport remains NULL until websocket_client_connect()
 * is called.
 *
 * @return Pointer to initialized client, or NULL on failure
 * @note Caller must call websocket_client_destroy() when done
 * @see websocket_client_destroy() For proper cleanup
 */
websocket_client_t *websocket_client_create(void);

/**
 * @brief Destroy WebSocket client and free all resources
 *
 * Destroys the transport and frees the client structure.
 * Must be called AFTER the transport is no longer in use.
 *
 * @param client_ptr Pointer to client pointer (set to NULL after free)
 * @warning All operations on transport must be complete before calling
 * @note No-op if client_ptr is NULL or *client_ptr is NULL
 */
void websocket_client_destroy(websocket_client_t **client_ptr);

/**
 * @brief Check if connection is currently active
 *
 * @param client WebSocket client instance
 * @return true if connection is active, false otherwise
 */
bool websocket_client_is_active(const websocket_client_t *client);

/**
 * @brief Check if connection was lost
 *
 * @param client WebSocket client instance
 * @return true if connection loss was detected, false otherwise
 */
bool websocket_client_is_lost(const websocket_client_t *client);

/**
 * @brief Check if reconnection should be attempted
 *
 * @param client WebSocket client instance
 * @return true if reconnection is needed, false otherwise
 */
bool websocket_client_should_reconnect(const websocket_client_t *client);

/**
 * @brief Signal that connection was lost (triggers reconnection)
 *
 * @param client WebSocket client instance
 */
void websocket_client_signal_lost(websocket_client_t *client);

/**
 * @brief Signal that reconnection should be attempted
 *
 * @param client WebSocket client instance
 */
void websocket_client_signal_reconnect(websocket_client_t *client);

/**
 * @brief Clear reconnection flag (called after successful reconnect)
 *
 * @param client WebSocket client instance
 */
void websocket_client_clear_reconnect_flag(websocket_client_t *client);

/**
 * @brief Check if encryption is enabled
 *
 * @param client WebSocket client instance
 * @return true if encryption is enabled, false otherwise
 */
bool websocket_client_is_encryption_enabled(const websocket_client_t *client);

/**
 * @brief Enable encryption for this connection
 *
 * @param client WebSocket client instance
 */
void websocket_client_enable_encryption(websocket_client_t *client);

/**
 * @brief Disable encryption for this connection
 *
 * @param client WebSocket client instance
 */
void websocket_client_disable_encryption(websocket_client_t *client);

/**
 * @brief Close connection gracefully
 *
 * @param client WebSocket client instance
 */
void websocket_client_close(websocket_client_t *client);

/**
 * @brief Shutdown connection forcefully (for signal handlers)
 *
 * @param client WebSocket client instance
 */
void websocket_client_shutdown(websocket_client_t *client);

/**
 * @brief Establish WebSocket connection to server
 *
 * Performs full connection lifecycle including URL resolution and WebSocket
 * handshake. Does NOT perform crypto handshake - that's application responsibility.
 *
 * @param client WebSocket client instance
 * @param url WebSocket server URL (e.g., "ws://localhost:27226")
 * @param crypto_ctx Cryptographic context for encrypted connections
 * @return acip_transport_t pointer on success, NULL on failure
 *
 * @note The returned transport is owned by websocket_client_t
 * @note Call websocket_client_is_active() after connecting to verify success
 */
acip_transport_t *websocket_client_connect(websocket_client_t *client, const char *url,
                                           struct crypto_context_t *crypto_ctx);

/**
 * @brief Get active transport instance
 *
 * @param client WebSocket client instance
 * @return acip_transport_t pointer or NULL if not connected
 */
acip_transport_t *websocket_client_get_transport(const websocket_client_t *client);

/**
 * @brief Send a packet through WebSocket connection (thread-safe)
 *
 * Acquires send_mutex, transmits packet, releases mutex.
 * Checks connection state before sending.
 *
 * @param client WebSocket client instance
 * @param type Packet type to send
 * @param data Packet payload (NULL for empty packets)
 * @param len Payload length in bytes
 * @return 0 on success, -1 on failure
 *
 * @note Equivalent to tcp_client_send_packet() for API compatibility
 * @note Thread-safe: multiple threads can call concurrently
 */
int websocket_client_send_packet(websocket_client_t *client, packet_type_t type,
                                  const void *data, size_t len);

/**
 * @brief Send ping frame (keepalive heartbeat)
 *
 * Routes through websocket_client_send_packet() with PACKET_TYPE_PING.
 *
 * @param client WebSocket client instance
 * @return 0 on success, -1 on failure
 *
 * @note Equivalent to tcp_client_send_ping() for API compatibility
 */
int websocket_client_send_ping(websocket_client_t *client);

/**
 * @brief Send pong frame (keepalive response)
 *
 * Routes through websocket_client_send_packet() with PACKET_TYPE_PONG.
 *
 * @param client WebSocket client instance
 * @return 0 on success, -1 on failure
 *
 * @note Equivalent to tcp_client_send_pong() for API compatibility
 */
int websocket_client_send_pong(websocket_client_t *client);

/**
 * @brief Get the client's unique ID
 *
 * @param client WebSocket client instance
 * @return Client ID, or 0 if not set
 *
 * @note Equivalent to tcp_client_get_id() for API compatibility
 */
uint32_t websocket_client_get_id(const websocket_client_t *client);

/**
 * @brief Check if encryption is enabled for this connection
 *
 * @param client WebSocket client instance
 * @return true if encryption is enabled, false otherwise
 */
bool websocket_client_is_encrypted(const websocket_client_t *client);

#endif /* NETWORK_WEBSOCKET_CLIENT_H */
