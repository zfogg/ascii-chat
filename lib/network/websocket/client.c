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
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/endian.h>

#include <string.h>
#include <stdatomic.h>
#include <time.h>

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
  client->transport = NULL;
  client->my_client_id = 0;
  client->encryption_enabled = false;

  // Initialize send mutex
  if (mutex_init(&client->send_mutex) != 0) {
    log_error("Failed to initialize send mutex");
    SAFE_FREE(client);
    return NULL;
  }

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

  // Destroy send mutex
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
 *
 * Also initializes client-side state including client ID and encryption tracking.
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

  // Generate client ID (use random value since WebSocket doesn't have a local port)
  // For now, use a simple pseudo-random ID based on time and crypto context
  uint32_t generated_id;
  if (crypto_ctx) {
    // If crypto is enabled, use a random ID
    generated_id = (uint32_t)(time(NULL) ^ (uintptr_t)crypto_ctx);
  } else {
    // Otherwise, use time-based ID
    generated_id = (uint32_t)time(NULL);
  }
  // Ensure non-zero ID
  if (generated_id == 0) {
    generated_id = 1;
  }

  client->my_client_id = generated_id;
  client->encryption_enabled = (crypto_ctx != NULL);

  // Store transport and mark as active
  client->transport = transport;
  atomic_store(&client->connection_active, true);
  atomic_store(&client->connection_lost, false);

  log_info("WebSocket client connected to %s (client_id=%u, encryption=%s)", url, client->my_client_id,
           client->encryption_enabled ? "enabled" : "disabled");

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
 * @brief Get client ID assigned by server or generated locally
 */
uint32_t websocket_client_get_id(const websocket_client_t *client) {
  return client ? client->my_client_id : 0;
}

/**
 * @brief Send packet with thread-safe mutex protection
 *
 * All packet transmission goes through this function to ensure
 * packets aren't interleaved on the wire. Builds a complete packet
 * (header + payload) and sends via transport.
 */
int websocket_client_send_packet(websocket_client_t *client, packet_type_t type, const void *data, size_t len) {
  if (!client) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "NULL client");
  }

  if (!atomic_load(&client->connection_active)) {
    return SET_ERRNO(ERROR_NETWORK, "Connection not active");
  }

  if (len > MAX_PACKET_SIZE) {
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Packet too large: %zu > %d", len, MAX_PACKET_SIZE);
  }

  // Get transport
  acip_transport_t *transport = client->transport;
  if (!transport) {
    return SET_ERRNO(ERROR_NETWORK, "Transport not available");
  }

  // Calculate total packet size
  size_t packet_size = sizeof(packet_header_t) + len;

  // Allocate buffer for complete packet (header + payload)
  uint8_t *packet_buffer = buffer_pool_alloc(NULL, packet_size);
  if (!packet_buffer) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate packet buffer for %zu bytes", packet_size);
  }

  // Build packet header at start of buffer
  packet_header_t *header = (packet_header_t *)packet_buffer;
  header->magic = HOST_TO_NET_U64(PACKET_MAGIC);
  header->type = HOST_TO_NET_U16((uint16_t)type);
  header->length = HOST_TO_NET_U32((uint32_t)len);
  header->crc32 = HOST_TO_NET_U32(len > 0 ? asciichat_crc32(data, len) : 0);
  header->client_id = HOST_TO_NET_U32(0);  // Client ID is managed by transport encryption

  // Copy payload after header
  if (len > 0 && data) {
    memcpy(packet_buffer + sizeof(packet_header_t), data, len);
  }

  // Acquire send mutex for thread-safe transmission
  mutex_lock(&client->send_mutex);

  // Send complete packet via transport
  asciichat_error_t result = acip_transport_send(transport, packet_buffer, packet_size);

  mutex_unlock(&client->send_mutex);

  // Free packet buffer
  buffer_pool_free(NULL, packet_buffer, packet_size);

  if (result != ASCIICHAT_OK) {
    log_debug("Failed to send packet type %d via WebSocket: %s", type, asciichat_error_string(result));
    return -1;
  }

  return 0;
}

/**
 * @brief Send ping packet
 */
int websocket_client_send_ping(websocket_client_t *client) {
  if (!client)
    return -1;
  return websocket_client_send_packet(client, PACKET_TYPE_PING, NULL, 0);
}

/**
 * @brief Send pong packet
 */
int websocket_client_send_pong(websocket_client_t *client) {
  if (!client)
    return -1;
  return websocket_client_send_packet(client, PACKET_TYPE_PONG, NULL, 0);
}
