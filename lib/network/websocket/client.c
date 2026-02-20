/**
 * @file network/websocket/client.c
 * @brief WebSocket client implementation
 *
 * Thin implementation that delegates to acip_websocket_client_transport_create()
 * for actual connection setup. Mirrors tcp_client.c structure for consistency.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <ascii-chat/network/websocket/client.h>
#include <ascii-chat/common.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/network/acip/transport.h>

#include <string.h>
#include <stdatomic.h>

/**
 * @brief Create and initialize WebSocket client
 */
websocket_client_t *websocket_client_create(void) {
  websocket_client_t *client = SAFE_MALLOC(sizeof(websocket_client_t), websocket_client_t *);
  if (!client) {
    log_error("Failed to allocate websocket_client_t");
    return NULL;
  }

  // Zero-initialize all fields
  memset(client, 0, sizeof(*client));

  // Initialize connection state
  atomic_store(&client->connection_active, false);
  atomic_store(&client->connection_lost, false);
  atomic_store(&client->should_reconnect, false);
  client->transport = NULL;
  client->encryption_enabled = false;

  log_debug("WebSocket client created");

  return client;
}

/**
 * @brief Destroy WebSocket client and free resources
 */
void websocket_client_destroy(websocket_client_t **client_ptr) {
  if (!client_ptr || !*client_ptr) {
    return; // No-op if NULL
  }

  websocket_client_t *client = *client_ptr;

  log_debug("Destroying WebSocket client");

  // Close and destroy transport if it exists
  if (client->transport) {
    acip_transport_destroy(client->transport);
    client->transport = NULL;
  }

  SAFE_FREE(*client_ptr);
}

/**
 * @brief Check if connection is currently active
 */
bool websocket_client_is_active(const websocket_client_t *client) {
  if (!client) {
    return false;
  }
  return atomic_load(&client->connection_active);
}

/**
 * @brief Check if connection was lost
 */
bool websocket_client_is_lost(const websocket_client_t *client) {
  if (!client) {
    return false;
  }
  return atomic_load(&client->connection_lost);
}

/**
 * @brief Check if reconnection should be attempted
 */
bool websocket_client_should_reconnect(const websocket_client_t *client) {
  if (!client) {
    return false;
  }
  return atomic_load(&client->should_reconnect);
}

/**
 * @brief Signal that reconnection should be attempted
 */
void websocket_client_signal_reconnect(websocket_client_t *client) {
  if (!client) {
    return;
  }
  atomic_store(&client->should_reconnect, true);
  atomic_store(&client->connection_active, false);
  log_debug("WebSocket reconnection signaled");
}

/**
 * @brief Clear reconnection flag
 */
void websocket_client_clear_reconnect_flag(websocket_client_t *client) {
  if (!client) {
    return;
  }
  atomic_store(&client->should_reconnect, false);
}

/**
 * @brief Signal that connection was lost
 */
void websocket_client_signal_lost(websocket_client_t *client) {
  if (!client) {
    return;
  }
  atomic_store(&client->connection_lost, true);
  atomic_store(&client->connection_active, false);
  log_debug("WebSocket connection marked as lost");
}

/**
 * @brief Check if encryption is enabled
 */
bool websocket_client_is_encryption_enabled(const websocket_client_t *client) {
  if (!client) {
    return false;
  }
  return client->encryption_enabled;
}

/**
 * @brief Enable encryption for this connection
 */
void websocket_client_enable_encryption(websocket_client_t *client) {
  if (!client) {
    return;
  }
  client->encryption_enabled = true;
  log_debug("WebSocket encryption enabled");
}

/**
 * @brief Disable encryption for this connection
 */
void websocket_client_disable_encryption(websocket_client_t *client) {
  if (!client) {
    return;
  }
  client->encryption_enabled = false;
  log_debug("WebSocket encryption disabled");
}

/**
 * @brief Close connection gracefully
 */
void websocket_client_close(websocket_client_t *client) {
  if (!client) {
    return;
  }

  log_debug("Closing WebSocket client");

  if (client->transport) {
    acip_transport_close(client->transport);
  }

  atomic_store(&client->connection_active, false);
  atomic_store(&client->should_reconnect, false);
}

/**
 * @brief Shutdown connection forcefully
 */
void websocket_client_shutdown(websocket_client_t *client) {
  if (!client) {
    return;
  }

  log_debug("Shutting down WebSocket client");

  // Force close the transport
  if (client->transport) {
    acip_transport_close(client->transport);
  }

  atomic_store(&client->connection_active, false);
  atomic_store(&client->connection_lost, true);
  atomic_store(&client->should_reconnect, false);
}

/**
 * @brief Establish WebSocket connection to server
 *
 * Delegates to acip_websocket_client_transport_create() which handles:
 * - URL parsing and validation
 * - WebSocket handshake
 * - Transport setup and lifecycle
 */
acip_transport_t *websocket_client_connect(websocket_client_t *client, const char *url,
                                           struct crypto_context_t *crypto_ctx) {
  if (!client || !url) {
    log_error("Invalid arguments to websocket_client_connect");
    return NULL;
  }

  log_info("Connecting WebSocket client to %s", url);

  // Store URL for reference
  strncpy(client->url, url, sizeof(client->url) - 1);
  client->url[sizeof(client->url) - 1] = '\0';

  // Create transport using ACIP layer
  acip_transport_t *transport = acip_websocket_client_transport_create(url, crypto_ctx);
  if (!transport) {
    log_error("Failed to create WebSocket transport");
    atomic_store(&client->connection_lost, true);
    return NULL;
  }

  // Store transport and mark as active
  client->transport = transport;
  atomic_store(&client->connection_active, true);
  atomic_store(&client->connection_lost, false);

  log_info("WebSocket client connected to %s", url);

  return transport;
}

/**
 * @brief Get active transport instance
 */
acip_transport_t *websocket_client_get_transport(const websocket_client_t *client) {
  if (!client) {
    return NULL;
  }
  return client->transport;
}
