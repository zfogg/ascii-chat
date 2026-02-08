#pragma once

/**
 * @file network/websocket/server.h
 * @brief WebSocket server for accepting browser client connections
 * @ingroup network
 *
 * Provides a WebSocket server implementation using libwebsockets to accept
 * connections from browser-based WASM clients alongside the TCP server.
 *
 * This module mirrors the tcp_server API to provide consistent server-side
 * connection handling regardless of transport type.
 *
 * ## Usage Pattern
 *
 * 1. Configure server with websocket_server_config_t
 * 2. Call websocket_server_init() to create libwebsockets context
 * 3. Call websocket_server_run() to start event loop (blocks)
 * 4. Signal shutdown by setting running flag to false
 * 5. Call websocket_server_destroy() to clean up
 *
 * ## Example
 *
 * ```c
 * // Define client handler (same as TCP)
 * void *my_client_handler(void *arg) {
 *     websocket_client_context_t *ctx = (websocket_client_context_t *)arg;
 *     // Process client connection via transport
 *     acip_transport_destroy(ctx->transport);
 *     SAFE_FREE(ctx);
 *     return NULL;
 * }
 *
 * // Configure and run server
 * websocket_server_config_t config = {
 *     .port = 27224,
 *     .client_handler = my_client_handler,
 *     .user_data = NULL
 * };
 *
 * websocket_server_t server;
 * if (websocket_server_init(&server, &config) == ASCIICHAT_OK) {
 *     websocket_server_run(&server);
 *     websocket_server_destroy(&server);
 * }
 * ```
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include "../../common.h"
#include "../../platform/abstraction.h"
#include <libwebsockets.h>

// Forward declarations
typedef struct websocket_server websocket_server_t;
typedef struct acip_transport acip_transport_t;

/**
 * @brief Client handler function type
 *
 * Called when a new WebSocket client connects.
 * The handler receives a fully initialized transport ready for ACIP packets.
 *
 * @param arg websocket_client_context_t* containing connection info
 * @return Thread exit value (ignored)
 */
typedef void *(*websocket_client_handler_fn)(void *arg);

/**
 * @brief Context passed to client handler thread
 *
 * Contains all information needed to handle a WebSocket client connection.
 */
typedef struct {
  acip_transport_t *transport; ///< ACIP transport for this client
  char client_ip[64];          ///< Client IP address (for logging/rate limiting)
  int client_port;             ///< Client port
  void *user_data;             ///< User-provided data from server config
} websocket_client_context_t;

/**
 * @brief WebSocket server configuration
 */
typedef struct {
  int port;                                   ///< Port to listen on
  websocket_client_handler_fn client_handler; ///< Handler for new connections
  void *user_data;                            ///< User data passed to handlers
} websocket_server_config_t;

/**
 * @brief WebSocket server state
 */
struct websocket_server {
  struct lws_context *context;         ///< libwebsockets context
  websocket_client_handler_fn handler; ///< Client handler function
  void *user_data;                     ///< User data for handlers
  atomic_bool running;                 ///< Server running flag
  int port;                            ///< Listening port
};

/**
 * @brief Initialize WebSocket server
 *
 * Creates libwebsockets context and prepares for accepting connections.
 *
 * @param server Server instance to initialize
 * @param config Server configuration
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t websocket_server_init(websocket_server_t *server, const websocket_server_config_t *config);

/**
 * @brief Run WebSocket server event loop
 *
 * Blocks until server is signaled to stop via running flag.
 *
 * @param server Server instance
 * @return ASCIICHAT_OK on success, error code on failure
 */
asciichat_error_t websocket_server_run(websocket_server_t *server);

/**
 * @brief Destroy WebSocket server and free resources
 *
 * @param server Server instance to destroy
 */
void websocket_server_destroy(websocket_server_t *server);
