/**
 * @file network/acip/receive.c
 * @brief ACIP transport receive API implementation
 *
 * Implements high-level receive functions that wrap low-level packet
 * reception with automatic ACIP handler dispatch.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "network/acip/receive.h"
#include "network/packet.h"
#include "buffer_pool.h"
#include "log/logging.h"
#include "common.h"

asciichat_error_t acip_transport_receive_and_dispatch_server(acip_transport_t *transport, void *client_ctx,
                                                             const acip_server_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }

  // Get socket from transport for low-level packet reception
  socket_t sock = transport->methods->get_socket(transport);
  if (sock == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_NETWORK, "Transport has no valid socket");
  }

  // Check if transport is connected
  if (!transport->methods->is_connected(transport)) {
    return SET_ERRNO(ERROR_NETWORK, "Transport not connected");
  }

  // Receive packet with automatic decryption
  packet_envelope_t envelope;
  bool enforce_encryption = (transport->crypto_ctx != NULL);
  packet_recv_result_t result = receive_packet_secure(sock, transport->crypto_ctx, enforce_encryption, &envelope);

  // Handle receive errors
  if (result != PACKET_RECV_SUCCESS) {
    if (result == PACKET_RECV_EOF) {
      return SET_ERRNO(ERROR_NETWORK, "Connection closed (EOF)");
    } else if (result == PACKET_RECV_SECURITY_VIOLATION) {
      return SET_ERRNO(ERROR_CRYPTO, "Security violation: unencrypted packet when encryption required");
    } else {
      return SET_ERRNO(ERROR_NETWORK, "Failed to receive packet");
    }
  }

  // Dispatch packet to appropriate ACIP handler
  asciichat_error_t dispatch_result =
      acip_handle_server_packet(transport, envelope.type, envelope.data, envelope.len, client_ctx, callbacks);

  // Always free the allocated buffer (even if handler failed)
  if (envelope.allocated_buffer) {
    buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
  }

  // Return handler result
  return dispatch_result;
}

asciichat_error_t acip_transport_receive_and_dispatch_client(acip_transport_t *transport,
                                                             const acip_client_callbacks_t *callbacks) {
  if (!transport || !callbacks) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid transport or callbacks");
  }

  // Get socket from transport for low-level packet reception
  socket_t sock = transport->methods->get_socket(transport);
  if (sock == INVALID_SOCKET_VALUE) {
    return SET_ERRNO(ERROR_NETWORK, "Transport has no valid socket");
  }

  // Check if transport is connected
  if (!transport->methods->is_connected(transport)) {
    return SET_ERRNO(ERROR_NETWORK, "Transport not connected");
  }

  // Receive packet with automatic decryption
  packet_envelope_t envelope;
  bool enforce_encryption = (transport->crypto_ctx != NULL);
  packet_recv_result_t result = receive_packet_secure(sock, transport->crypto_ctx, enforce_encryption, &envelope);

  // Handle receive errors
  if (result != PACKET_RECV_SUCCESS) {
    if (result == PACKET_RECV_EOF) {
      return SET_ERRNO(ERROR_NETWORK, "Connection closed (EOF)");
    } else if (result == PACKET_RECV_SECURITY_VIOLATION) {
      return SET_ERRNO(ERROR_CRYPTO, "Security violation: unencrypted packet when encryption required");
    } else {
      return SET_ERRNO(ERROR_NETWORK, "Failed to receive packet");
    }
  }

  // Dispatch packet to appropriate ACIP handler
  asciichat_error_t dispatch_result =
      acip_handle_client_packet(transport, envelope.type, envelope.data, envelope.len, callbacks);

  // Always free the allocated buffer (even if handler failed)
  if (envelope.allocated_buffer) {
    buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
  }

  // Return handler result
  return dispatch_result;
}
