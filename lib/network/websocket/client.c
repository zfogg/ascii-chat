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
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/util/fnv1a.h>
#include <ascii-chat/debug/named.h>

#include <string.h>
#include <stdatomic.h>

/**
 * @brief Create and initialize named WebSocket client
 */
websocket_client_t *websocket_client_create(const char *name) {
  if (!name) {
    log_error("WebSocket client name is required");
    return NULL;
  }

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
  client->transport = NULL;
  client->my_client_id = 0;
  client->encryption_enabled = false;
  client->should_reconnect = false;

  // Initialize thread-safe mutex for packet transmission
  if (mutex_init(&client->send_mutex, "client_send") != 0) {
    log_error("Failed to initialize send_mutex");
    SAFE_FREE(client);
    return NULL;
  }

  // Register WebSocket client with debug naming system
  NAMED_REGISTER_WEBSOCKET(client, name);

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

  NAMED_UNREGISTER(client);

  // Destroy mutex
  mutex_destroy(&client->send_mutex);

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
  acip_transport_t *transport = acip_websocket_client_transport_create("client", url, crypto_ctx);
  if (!transport) {
    log_error("Failed to create WebSocket transport");
    atomic_store(&client->connection_lost, true);
    return NULL;
  }

  // Derive client ID from URL hash using FNV-1a (proper implementation with 64-bit arithmetic)
  // This provides a stable, unique ID per connection URL without undefined behavior
  client->my_client_id = fnv1a_hash_string(url);

  // Update registration name now that we have a client ID
  {
    char client_name[64];
    SAFE_SNPRINTF(client_name, sizeof(client_name), "websocket_client_%u", client->my_client_id);
    named_update_name((uintptr_t)client, client_name);
  }

  // Mark encryption as enabled if crypto context provided
  client->encryption_enabled = (crypto_ctx != NULL);

  // Store transport and mark as active
  client->transport = transport;
  atomic_store(&client->connection_active, true);
  atomic_store(&client->connection_lost, false);

  log_info("WebSocket client connected to %s (ID: %u)", url, client->my_client_id);

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

/**
 * @brief Send a packet through WebSocket connection (thread-safe)
 *
 * Acquires send_mutex, transmits packet via transport, releases mutex.
 * Checks connection state before sending.
 */
int websocket_client_send_packet(websocket_client_t *client, packet_type_t type, const void *data, size_t len) {
  if (!client) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client");
  }

  if (!atomic_load(&client->connection_active)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  if (!client->transport) {
    return SET_ERRNO(ERROR_NETWORK, "No active transport");
  }

  // Acquire send mutex for thread-safe transmission
  mutex_lock(&client->send_mutex);

  // Send packet through transport with client ID
  asciichat_error_t result = packet_send_via_transport(client->transport, type, data, len, client->my_client_id);

  mutex_unlock(&client->send_mutex);

  if (result != ASCIICHAT_OK) {
    log_debug("Failed to send packet type %d: %s", type, asciichat_error_string(result));
    return -1;
  }

  return 0;
}

/**
 * @brief Send ping frame (keepalive heartbeat)
 *
 * Routes through websocket_client_send_packet() with PACKET_TYPE_PING.
 */
int websocket_client_send_ping(websocket_client_t *client) {
  if (!client)
    return -1;
  return websocket_client_send_packet(client, PACKET_TYPE_PING, NULL, 0);
}

/**
 * @brief Send pong frame (keepalive response)
 *
 * Routes through websocket_client_send_packet() with PACKET_TYPE_PONG.
 */
int websocket_client_send_pong(websocket_client_t *client) {
  if (!client)
    return -1;
  return websocket_client_send_packet(client, PACKET_TYPE_PONG, NULL, 0);
}

/**
 * @brief Get the client's unique ID
 */
uint32_t websocket_client_get_id(const websocket_client_t *client) {
  return client ? client->my_client_id : 0;
}

/**
 * @brief Check if encryption is enabled for this connection
 */
bool websocket_client_is_encrypted(const websocket_client_t *client) {
  if (!client)
    return false;
  return client->encryption_enabled;
}
