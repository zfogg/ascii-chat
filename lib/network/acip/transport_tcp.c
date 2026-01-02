/**
 * @file network/acip/transport_tcp.c
 * @brief TCP transport implementation for ACIP protocol
 *
 * Implements the acip_transport_t interface for raw TCP sockets.
 * This is the primary transport used by ascii-chat.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "network/acip/transport.h"
#include "network/packet.h"
#include "network/network.h"
#include "log/logging.h"
#include "platform/socket.h"
#include <string.h>

/**
 * @brief TCP transport implementation data
 */
typedef struct {
  socket_t sockfd;   ///< Socket descriptor (NOT owned - don't close)
  bool is_connected; ///< Connection state
} tcp_transport_data_t;

// =============================================================================
// TCP Transport Methods
// =============================================================================

static asciichat_error_t tcp_send(acip_transport_t *transport, const void *data, size_t len) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;

  if (!tcp->is_connected) {
    return SET_ERRNO(ERROR_NETWORK_SEND, "TCP transport not connected");
  }

  // Use existing packet send infrastructure (handles encryption if crypto_ctx present)
  return send_packet_raw(tcp->sockfd, data, len, transport->crypto_ctx);
}

static asciichat_error_t tcp_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                  void **out_allocated_buffer) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;

  if (!tcp->is_connected) {
    return SET_ERRNO(ERROR_NETWORK_RECV, "TCP transport not connected");
  }

  // Use existing packet receive infrastructure (handles decryption if crypto_ctx present)
  packet_header_t header;
  void *payload = NULL;
  void *allocated_buffer = NULL;

  asciichat_error_t result = receive_packet(tcp->sockfd, &header, &payload, &allocated_buffer, transport->crypto_ctx);

  if (result != ASCIICHAT_OK) {
    return result;
  }

  *buffer = payload;
  *out_len = header.length;
  *out_allocated_buffer = allocated_buffer;

  return ASCIICHAT_OK;
}

static asciichat_error_t tcp_close(acip_transport_t *transport) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;

  if (!tcp->is_connected) {
    return ASCIICHAT_OK; // Already closed
  }

  // Note: We do NOT close the socket - caller owns it
  // We just mark ourselves as disconnected
  tcp->is_connected = false;

  log_debug("TCP transport marked as disconnected (socket not closed)");
  return ASCIICHAT_OK;
}

static acip_transport_type_t tcp_get_type(acip_transport_t *transport) {
  (void)transport;
  return ACIP_TRANSPORT_TCP;
}

static socket_t tcp_get_socket(acip_transport_t *transport) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;
  return tcp->sockfd;
}

static bool tcp_is_connected(acip_transport_t *transport) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;
  return tcp->is_connected;
}

// =============================================================================
// TCP Transport Method Table
// =============================================================================

static const acip_transport_methods_t tcp_methods = {
    .send = tcp_send,
    .recv = tcp_recv,
    .close = tcp_close,
    .get_type = tcp_get_type,
    .get_socket = tcp_get_socket,
    .is_connected = tcp_is_connected,
};

// =============================================================================
// TCP Transport Creation
// =============================================================================

acip_transport_t *acip_tcp_transport_create(socket_t sockfd, crypto_context_t *crypto_ctx) {
  if (sockfd == INVALID_SOCKET_VALUE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket descriptor");
    return NULL;
  }

  // Allocate transport structure
  acip_transport_t *transport = SAFE_MALLOC(sizeof(acip_transport_t), acip_transport_t *);
  if (!transport) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate TCP transport");
    return NULL;
  }

  // Allocate TCP-specific data
  tcp_transport_data_t *tcp_data = SAFE_MALLOC(sizeof(tcp_transport_data_t), tcp_transport_data_t *);
  if (!tcp_data) {
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate TCP transport data");
    return NULL;
  }

  // Initialize TCP data
  tcp_data->sockfd = sockfd;
  tcp_data->is_connected = true;

  // Initialize transport
  transport->methods = &tcp_methods;
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = tcp_data;

  log_debug("Created TCP transport for socket %d (crypto: %s)", sockfd, crypto_ctx ? "enabled" : "disabled");

  return transport;
}

// =============================================================================
// Transport Destroy (shared implementation)
// =============================================================================

void acip_transport_destroy(acip_transport_t *transport) {
  if (!transport) {
    return;
  }

  // Close if still connected
  if (transport->methods && transport->methods->close && transport->methods->is_connected &&
      transport->methods->is_connected(transport)) {
    transport->methods->close(transport);
  }

  // Free implementation data
  if (transport->impl_data) {
    SAFE_FREE(transport->impl_data);
  }

  // Free transport structure
  SAFE_FREE(transport);

  log_debug("Destroyed ACIP transport");
}
