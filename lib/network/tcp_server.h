#pragma once

/**
 * @file network/tcp_server.h
 * @brief 🌐 Generic TCP server with dual-stack IPv4/IPv6 support
 * @ingroup network
 *
 * Provides a reusable TCP server implementation with:
 * - Dual-stack IPv4 and IPv6 binding
 * - select()-based accept loop for multi-socket handling
 * - Per-client thread spawning
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
 * 5. Call tcp_server_shutdown() to clean up
 *
 * ## Example
 *
 * ```c
 * // Define client handler
 * void *my_client_handler(void *arg) {
 *     tcp_client_context_t *ctx = (tcp_client_context_t *)arg;
 *     // Process client connection
 *     socket_close(ctx->client_socket);
 *     SAFE_FREE(ctx);
 *     return NULL;
 * }
 *
 * // Configure and run server
 * tcp_server_config_t config = {
 *     .port = 27225,
 *     .ipv4_address = "0.0.0.0",
 *     .ipv6_address = "::",
 *     .bind_ipv4 = true,
 *     .bind_ipv6 = true,
 *     .accept_timeout_sec = 1,
 *     .client_handler = my_client_handler,
 *     .user_data = NULL
 * };
 *
 * tcp_server_t server;
 * if (tcp_server_init(&server, &config) == ASCIICHAT_OK) {
 *     tcp_server_run(&server);
 *     tcp_server_shutdown(&server);
 * }
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date December 2025
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "common.h"
#include "platform/socket.h"
#include "platform/abstraction.h"
#include "uthash.h"

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
 * @brief Per-client connection context
 *
 * Passed to client handler threads with connection information.
 * The handler is responsible for closing the socket and freeing
 * this structure when done.
 */
typedef struct {
  socket_t client_socket;       ///< Client connection socket
  struct sockaddr_storage addr; ///< Client address
  socklen_t addr_len;           ///< Address length
  void *user_data;              ///< User-provided data from config
} tcp_client_context_t;

/**
 * @brief Client handler thread function type
 *
 * @param arg Pointer to tcp_client_context_t
 * @return NULL (thread exit value)
 *
 * The handler must:
 * - Close client_socket when done
 * - Free the context structure
 */
typedef void *(*tcp_client_handler_fn)(void *arg);

/**
 * @brief TCP server configuration
 *
 * Configures server binding, timeouts, and client handler.
 */
typedef struct {
  int port;                             ///< TCP listen port
  const char *ipv4_address;             ///< IPv4 bind address (NULL or empty = don't bind)
  const char *ipv6_address;             ///< IPv6 bind address (NULL or empty = don't bind)
  bool bind_ipv4;                       ///< Whether to bind IPv4 socket
  bool bind_ipv6;                       ///< Whether to bind IPv6 socket
  int accept_timeout_sec;               ///< select() timeout in seconds (for responsive shutdown)
  tcp_client_handler_fn client_handler; ///< Client handler callback
  void *user_data;                      ///< User data passed to each client handler
} tcp_server_config_t;

/**
 * @brief Thread entry for client thread pool
 *
 * Tracks individual worker threads spawned for a client.
 * Threads are stopped in order based on stop_id (lower values stopped first).
 */
typedef struct tcp_client_thread {
  asciithread_t thread;           ///< Thread handle
  int stop_id;                    ///< Cleanup order (lower = stop first)
  void *(*thread_func)(void *);   ///< Thread function
  void *thread_arg;               ///< Thread argument
  char name[64];                  ///< Thread name for debugging
  struct tcp_client_thread *next; ///< Linked list next pointer
} tcp_client_thread_t;

/**
 * @brief Client registry entry
 *
 * Internal structure for tracking connected clients.
 * Uses uthash for efficient socket-based lookup.
 * Each client can have multiple worker threads tracked in a thread pool.
 */
struct tcp_client_entry {
  socket_t socket;              ///< Client socket (hash key)
  void *client_data;            ///< User-provided client data
  tcp_client_thread_t *threads; ///< Linked list of worker threads
  mutex_t threads_mutex;        ///< Mutex protecting thread list
  size_t thread_count;          ///< Number of threads in pool
  UT_hash_handle hh;            ///< uthash handle
};

/**
 * @brief TCP server state
 *
 * Maintains server sockets, client registry, and runtime state.
 */
struct tcp_server {
  socket_t listen_socket;     ///< IPv4 listen socket
  socket_t listen_socket6;    ///< IPv6 listen socket
  atomic_bool running;        ///< Server running flag (set false to shutdown)
  tcp_server_config_t config; ///< Server configuration

  // Client registry (thread-safe)
  tcp_client_entry_t *clients;      ///< Hash table of connected clients
  mutex_t clients_mutex;            ///< Mutex protecting client registry
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
 * Accepts client connections and spawns handler threads.
 * Blocks until server->running is set to false.
 *
 * Uses select() with timeout to handle dual-stack sockets and
 * allow responsive shutdown.
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
void tcp_server_shutdown(tcp_server_t *server);

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

// ============================================================================
// Client Thread Pool Management
// ============================================================================

/**
 * @brief Spawn a worker thread for a client
 *
 * Creates and tracks a new worker thread for the specified client.
 * Threads are identified by stop_id for ordered cleanup - lower stop_id
 * values are stopped first when the client disconnects.
 *
 * Example stop_id ordering:
 * - stop_id=1: Receive thread (stop first to prevent new data)
 * - stop_id=2: Render threads (stop after receive)
 * - stop_id=3: Send thread (stop last after all processing done)
 *
 * @param server Server structure
 * @param client_socket Client socket to spawn thread for
 * @param thread_func Thread function to execute
 * @param thread_arg Argument passed to thread function
 * @param stop_id Cleanup order (lower = stop first)
 * @param thread_name Thread name for debugging (max 63 chars)
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t tcp_server_spawn_thread(tcp_server_t *server, socket_t client_socket, void *(*thread_func)(void *),
                                          void *thread_arg, int stop_id, const char *thread_name);

/**
 * @brief Stop all threads for a client in stop_id order
 *
 * Stops all worker threads spawned for the specified client.
 * Threads are stopped in ascending stop_id order (lower values first).
 * Joins each thread to ensure it has fully exited before proceeding.
 *
 * @param server Server structure
 * @param client_socket Client socket whose threads to stop
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t tcp_server_stop_client_threads(tcp_server_t *server, socket_t client_socket);

/**
 * @brief Get thread count for a client
 *
 * Thread-safe count of worker threads spawned for a client.
 *
 * @param server Server structure
 * @param client_socket Client socket to query
 * @param[out] count Output pointer for thread count (set to 0 if client not found)
 * @return ASCIICHAT_OK on success, ERROR_NOT_FOUND if client not in registry
 */
asciichat_error_t tcp_server_get_thread_count(tcp_server_t *server, socket_t client_socket, size_t *count);
