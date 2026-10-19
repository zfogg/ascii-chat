#pragma once

/**
 * @file network/tcp_server.h
 * @brief 🌐 Generic TCP server with dual-stack IPv4/IPv6 support
 * @ingroup network
 *
 * Provides a reusable TCP server implementation with:
 * - Dual-stack IPv4 and IPv6 binding
 * - Event-driven accept and stream I/O
 * - Thread-safe client registry with arbitrary user data
 * - Configurable client handler callbacks
 * - Clean shutdown support
 *
 * This module abstracts the common TCP server patterns used by both
 * the main ascii-chat server and the discovery service (acds), avoiding
 * code duplication and providing a consistent API.
 *
 * ## Usage Pattern
 *
 * 1. Configure server with tcp_server_config_t
 * 2. Call tcp_server_init() to bind sockets
 * 3. Call tcp_server_run() to start accept loop (blocks)
 * 4. Signal shutdown by setting running flag to false
 * 5. Call tcp_server_destroy() to clean up
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <ascii-chat/platform/event.h>
#include "../../atomic.h"
#include "../../common.h"
#include "../../platform/socket.h"
#include "../../platform/abstraction.h"
#include <ascii-chat/uthash.h> // For UT_hash_handle

// Forward declarations
typedef struct tcp_client_entry tcp_client_entry_t;
typedef struct tcp_server tcp_server_t;

/**
 * @brief Callback for client cleanup
 *
 * Called when a client is removed from the registry.
 * Use this to free any allocated client_data.
 *
 * @param client_data User-provided client data to clean up
 */
typedef void (*tcp_client_cleanup_fn)(void *client_data);

/**
 * @brief Callback for iterating over clients
 *
 * @param socket Client socket
 * @param client_data User-provided client data
 * @param user_arg User argument passed to foreach function
 */
typedef void (*tcp_client_foreach_fn)(socket_t socket, void *client_data, void *user_arg);

/**
 * @brief TCP server configuration
 *
 * Configures server binding, timeouts, and client handler.
 */
typedef struct {
  int port;                              ///< TCP listen port
  const char *ipv4_address;              ///< IPv4 bind address (NULL or empty = don't bind)
  const char *ipv6_address;              ///< IPv6 bind address (NULL or empty = don't bind)
  bool bind_ipv4;                        ///< Whether to bind IPv4 socket
  bool bind_ipv6;                        ///< Whether to bind IPv6 socket
  platform_event_accept_fn event_handler; ///< Required asynchronous accept handler
  void (*tick_fn)(void *user_data);       ///< Optional periodic callback on the tcp_server_run thread
  void *tick_user_data;
  void *user_data;                       ///< User data passed to each client handler
} tcp_server_config_t;

/**
 * @brief Client registry entry
 *
 * Internal structure for tracking connected clients.
 * Uses uthash for efficient socket-based lookup.
 */
struct tcp_client_entry {
  socket_t socket;        ///< Client socket (hash key)
  void *client_data;      ///< User-provided client data
  UT_hash_handle hh;      ///< uthash handle
};

/**
 * @brief TCP server state
 *
 * Maintains server sockets, client registry, and runtime state.
 */
struct tcp_server {
  platform_event_loop_t *event_loop; ///< Owned while the asynchronous server runs
  socket_t listen_socket;     ///< IPv4 listen socket
  socket_t listen_socket6;    ///< IPv6 listen socket
  atomic_t running;           ///< Server running flag (set false to shutdown)
  tcp_server_config_t config; ///< Server configuration

  // Client registry (thread-safe with read-write lock)
  tcp_client_entry_t *clients;      ///< Hash table of connected clients
  rwlock_t clients_rwlock;          ///< Read-write lock (allows concurrent readers, exclusive writers)
  tcp_client_cleanup_fn cleanup_fn; ///< Callback for cleaning up client data
};

/**
 * @brief Initialize TCP server
 *
 * Creates and binds TCP sockets according to configuration.
 * At least one of IPv4 or IPv6 must be successfully bound.
 *
 * @param server Server structure to initialize
 * @param config Server configuration
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t tcp_server_init(tcp_server_t *server, const tcp_server_config_t *config);

/**
 * @brief Run TCP server accept loop
 *
 * Accepts connections and dispatches stream readiness and completion events.
 * Blocks until server->running is set to false.
 *
 * Uses the platform event loop for dual-stack sockets and responsive shutdown.
 *
 * @param server Initialized server structure
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t tcp_server_run(tcp_server_t *server);

/**
 * @brief Shutdown TCP server
 *
 * Closes listen sockets and cleans up resources.
 * Does NOT wait for client threads to exit (caller's responsibility).
 *
 * @param server Server structure to clean up
 */
void tcp_server_destroy(tcp_server_t *server);

/**
 * @brief Set client cleanup callback
 *
 * Sets the callback function that will be called when a client is removed
 * from the registry. Use this to free any allocated client_data.
 *
 * @param server Server structure
 * @param cleanup_fn Cleanup callback (or NULL to disable)
 */
void tcp_server_set_cleanup_callback(tcp_server_t *server, tcp_client_cleanup_fn cleanup_fn);

/**
 * @brief Add client to registry
 *
 * Thread-safe registration of a connected client with arbitrary user data.
 * The client_data pointer is stored as-is (caller retains ownership).
 *
 * @param server Server structure
 * @param socket Client socket (used as lookup key)
 * @param client_data User-provided client data (can be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t tcp_server_add_client(tcp_server_t *server, socket_t socket, void *client_data);

/**
 * @brief Remove client from registry
 *
 * Thread-safe removal of a client. If a cleanup callback is set,
 * it will be called with the client_data before removal.
 *
 * @param server Server structure
 * @param socket Client socket to remove
 * @return ASCIICHAT_OK if removed, ERROR_NOT_FOUND if not in registry
 */
asciichat_error_t tcp_server_remove_client(tcp_server_t *server, socket_t socket);

/**
 * @brief Get client data
 *
 * Thread-safe lookup of client data by socket.
 *
 * @param server Server structure
 * @param socket Client socket to look up
 * @param out_data Output pointer for client data (set to NULL if not found)
 * @return ASCIICHAT_OK if found, ERROR_NOT_FOUND if not in registry
 */
asciichat_error_t tcp_server_get_client(tcp_server_t *server, socket_t socket, void **out_data);

/**
 * @brief Iterate over all clients
 *
 * Thread-safe iteration over all connected clients.
 * The callback is called once per client while holding the clients mutex.
 *
 * @param server Server structure
 * @param callback Function to call for each client
 * @param user_arg User argument passed to callback
 */
void tcp_server_foreach_client(tcp_server_t *server, tcp_client_foreach_fn callback, void *user_arg);

/**
 * @brief Get client count
 *
 * Thread-safe count of connected clients.
 *
 * @param server Server structure
 * @return Number of clients in registry
 */
size_t tcp_server_get_client_count(tcp_server_t *server);
