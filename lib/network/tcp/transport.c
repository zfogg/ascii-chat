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

#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/debug/named.h>
#include <string.h>

/**
 * @brief TCP transport implementation data
 */
typedef struct {
  socket_t sockfd;   ///< Socket descriptor (NOT owned - don't close)
  bool is_connected; ///< Connection state
} tcp_transport_data_t;

// =============================================================================
// Helper Functions
// =============================================================================

/**
 * @brief Send all bytes on socket (handles partial sends)
 */
static asciichat_error_t tcp_send_all(socket_t sockfd, const void *data, size_t len) {
  const uint8_t *ptr = (const uint8_t *)data;
  size_t remaining = len;
  size_t total_sent = 0;

  log_debug("★ TCP_SEND_ALL: sockfd=%d, len=%zu", sockfd, len);

  while (remaining > 0) {
    ssize_t sent = socket_send(sockfd, ptr, remaining, 0);
    if (sent < 0) {
      log_error("★ TCP_SEND_ALL: socket_send failed at offset %zu/%zu", total_sent, len);
      return SET_ERRNO_SYS(ERROR_NETWORK,
                           "Socket send failed (tried to send %zu bytes, %zu remaining, already sent %zu)", len,
                           remaining, total_sent);
    }
    if (sent == 0) {
      log_error("★ TCP_SEND_ALL: socket closed at offset %zu/%zu", total_sent, len);
      return SET_ERRNO(ERROR_NETWORK, "Socket closed (tried to send %zu bytes, %zu remaining, already sent %zu)", len,
                       remaining, total_sent);
    }
    ptr += sent;
    remaining -= (size_t)sent;
    total_sent += (size_t)sent;
    log_debug("★ TCP_SEND_ALL: sent %zd bytes, total=%zu/%zu, remaining=%zu", sent, total_sent, len, remaining);
  }

  log_debug("★ TCP_SEND_ALL: SUCCESS - sent all %zu bytes", len);
  return ASCIICHAT_OK;
}

// =============================================================================
// TCP Transport Methods
// =============================================================================

static asciichat_error_t tcp_send(acip_transport_t *transport, const void *data, size_t len) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;

  log_info("[TCP_SEND_STATE] Entry: transport=%p, sockfd=%d, len=%zu, is_connected=%s, data=%p", (void *)transport,
           tcp->sockfd, len, tcp->is_connected ? "true" : "false", data);

  if (!tcp->is_connected) {
    log_error("[TCP_SEND_STATE] ❌ DISCONNECTED: Cannot send - transport marked disconnected! sockfd=%d, len=%zu",
              tcp->sockfd, len);
    return SET_ERRNO(ERROR_NETWORK, "TCP transport not connected");
  }

  // Data already has packet header from send.c, so we need to extract the type
  // to determine if this is a handshake packet (which should NOT be encrypted)
  if (len < sizeof(packet_header_t)) {
    log_error("[TCP_SEND_STATE] ❌ PACKET_TOO_SMALL: len=%zu < header_size=%zu", len, sizeof(packet_header_t));
    return SET_ERRNO(ERROR_NETWORK, "Packet too small: %zu < %zu", len, sizeof(packet_header_t));
  }

  // Extract packet type from header
  const packet_header_t *header = (const packet_header_t *)data;
  uint16_t packet_type = NET_TO_HOST_U16(header->type);
  log_info("[TCP_SEND_STATE] 📦 PACKET_TYPE: type=%d (0x%04x), len=%zu, magic_check=%p", packet_type, packet_type, len,
           (void *)header);

  // Check if encryption is needed
  bool should_encrypt = false;
  bool crypto_ready =
      (transport->crypto_ctx && transport->crypto_ctx->encrypt_data && crypto_is_ready(transport->crypto_ctx));
  bool is_handshake = packet_is_handshake_type((packet_type_t)packet_type);

  if (crypto_ready) {
    // Handshake packets are ALWAYS sent unencrypted
    if (!is_handshake) {
      should_encrypt = true;
    }
  }

  log_info("[TCP_SEND_STATE] 🔐 CRYPTO_CHECK: crypto_ready=%s, is_handshake=%s, will_encrypt=%s",
           crypto_ready ? "yes" : "no", is_handshake ? "yes" : "no", should_encrypt ? "yes" : "no");

  // If no encryption needed, send raw data
  if (!should_encrypt) {
    log_info("[TCP_SEND_STATE] 📤 PLAINTEXT_SEND: sockfd=%d, len=%zu bytes (packet_type=%d)", tcp->sockfd, len,
             packet_type);
    asciichat_error_t result = tcp_send_all(tcp->sockfd, data, len);
    if (result == ASCIICHAT_OK) {
      log_info("[TCP_SEND_STATE] ✅ PLAINTEXT_SEND_OK: sockfd=%d, %zu bytes sent successfully", tcp->sockfd, len);
    } else {
      log_error("[TCP_SEND_STATE] ❌ PLAINTEXT_SEND_FAILED: sockfd=%d, len=%zu - error code set", tcp->sockfd, len);
    }
    return result;
  }

  // Encrypt the entire packet (header + payload)
  size_t ciphertext_size = len + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  log_info("[TCP_SEND_STATE] 🔐 ENCRYPT_START: original_len=%zu, with_nonce=%zu, will_allocate=%zu", len,
           CRYPTO_NONCE_SIZE, ciphertext_size);

  uint8_t *ciphertext = buffer_pool_alloc(NULL, ciphertext_size);
  if (!ciphertext) {
    log_error("[TCP_SEND_STATE] ❌ ENCRYPT_ALLOC_FAILED: needed %zu bytes", ciphertext_size);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ciphertext buffer");
  }

  log_debug("[TCP_SEND_STATE] 🔐 ENCRYPT_BUFFER_ALLOCATED: ciphertext=%p, capacity=%zu", (void *)ciphertext,
            ciphertext_size);

  size_t ciphertext_len;
  crypto_result_t result =
      crypto_encrypt(transport->crypto_ctx, data, len, ciphertext, ciphertext_size, &ciphertext_len);
  log_info("[TCP_SEND_STATE] 🔐 ENCRYPT_RESULT: code=%d, input=%zu, output=%zu", result, len, ciphertext_len);

  if (result != CRYPTO_OK) {
    log_error("[TCP_SEND_STATE] ❌ ENCRYPT_FAILED: %s (code=%d)", crypto_result_to_string(result), result);
    buffer_pool_free(NULL, ciphertext, ciphertext_size);
    return SET_ERRNO(ERROR_CRYPTO, "Failed to encrypt packet: %s", crypto_result_to_string(result));
  }

  // Build PACKET_TYPE_ENCRYPTED header
  packet_header_t encrypted_header;
  encrypted_header.magic = HOST_TO_NET_U64(PACKET_MAGIC);
  encrypted_header.type = HOST_TO_NET_U16(PACKET_TYPE_ENCRYPTED);
  encrypted_header.length = HOST_TO_NET_U32((uint32_t)ciphertext_len);
  encrypted_header.crc32 = HOST_TO_NET_U32(asciichat_crc32(ciphertext, ciphertext_len));
  encrypted_header.client_id = 0;

  log_info("[TCP_SEND_STATE] 🔐 ENCRYPTED_HEADER_BUILT: magic=0x%llx, type=%d, len=%zu, crc=0x%x",
           (unsigned long long)HOST_TO_NET_U64(PACKET_MAGIC), PACKET_TYPE_ENCRYPTED, ciphertext_len,
           encrypted_header.crc32);

  // Send encrypted packet: header + ciphertext
  asciichat_error_t send_result = ASCIICHAT_OK;
  log_info("[TCP_SEND_STATE] 📤 ENCRYPTED_SEND_HEADER: sockfd=%d, header_size=%zu", tcp->sockfd,
           sizeof(encrypted_header));
  send_result = tcp_send_all(tcp->sockfd, &encrypted_header, sizeof(encrypted_header));
  if (send_result == ASCIICHAT_OK) {
    log_info("[TCP_SEND_STATE] 📤 ENCRYPTED_SEND_PAYLOAD: sockfd=%d, payload_size=%zu", tcp->sockfd, ciphertext_len);
    send_result = tcp_send_all(tcp->sockfd, ciphertext, ciphertext_len);
    if (send_result == ASCIICHAT_OK) {
      log_info("[TCP_SEND_STATE] ✅ ENCRYPTED_SEND_OK: sockfd=%d, total=%zu (header=%zu + payload=%zu)", tcp->sockfd,
               sizeof(encrypted_header) + ciphertext_len, sizeof(encrypted_header), ciphertext_len);
    } else {
      log_error("[TCP_SEND_STATE] ❌ ENCRYPTED_SEND_PAYLOAD_FAILED: sockfd=%d, tried %zu bytes", tcp->sockfd,
                ciphertext_len);
    }
  } else {
    log_error("[TCP_SEND_STATE] ❌ ENCRYPTED_SEND_HEADER_FAILED: sockfd=%d, header_size=%zu", tcp->sockfd,
              sizeof(encrypted_header));
  }

  buffer_pool_free(NULL, ciphertext, ciphertext_size);

  log_debug_every(LOG_RATE_SLOW, "Sent encrypted packet (original type %d as PACKET_TYPE_ENCRYPTED)", packet_type);
  return send_result;
}

static asciichat_error_t tcp_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                  void **out_allocated_buffer) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;

  log_info("[TCP_RECV_STATE] Entry: transport=%p, sockfd=%d, is_connected=%s", (void *)transport, tcp->sockfd,
           tcp->is_connected ? "true" : "false");

  if (!tcp->is_connected) {
    log_error("[TCP_RECV_STATE] ❌ DISCONNECTED: Cannot recv - transport marked disconnected! sockfd=%d", tcp->sockfd);
    return SET_ERRNO(ERROR_NETWORK, "TCP transport not connected");
  }

  // Use secure packet receive with envelope
  packet_envelope_t envelope;
  bool enforce_encryption = (transport->crypto_ctx != NULL && transport->crypto_ctx->encrypt_data);
  log_info("[TCP_RECV_STATE] 📥 RECV_WAITING: sockfd=%d, enforce_encryption=%s", tcp->sockfd,
           enforce_encryption ? "yes" : "no");

  packet_recv_result_t result =
      receive_packet_secure(tcp->sockfd, transport->crypto_ctx, enforce_encryption, &envelope);
  log_info("[TCP_RECV_STATE] 📥 RECV_RESULT: code=%d (0=success, 1=eof, 2=crypto_error, 3=other), data_size=%zu",
           result, result == PACKET_RECV_SUCCESS ? envelope.len : 0);

  if (result != PACKET_RECV_SUCCESS) {
    if (result == PACKET_RECV_EOF) {
      log_warn("[TCP_RECV_STATE] ⚠️  RECV_EOF: Connection closed by remote (sockfd=%d)", tcp->sockfd);
      return SET_ERRNO(ERROR_NETWORK, "Connection closed");
    } else if (result == PACKET_RECV_SECURITY_VIOLATION) {
      log_error("[TCP_RECV_STATE] ❌ RECV_SECURITY_VIOLATION: Crypto error on sockfd=%d", tcp->sockfd);
      return SET_ERRNO(ERROR_CRYPTO, "Security violation");
    } else {
      log_error("[TCP_RECV_STATE] ❌ RECV_FAILED: result=%d on sockfd=%d", result, tcp->sockfd);
      return SET_ERRNO(ERROR_NETWORK, "Failed to receive packet");
    }
  }

  *buffer = envelope.data;
  *out_len = envelope.len;
  *out_allocated_buffer = envelope.allocated_buffer;

  if (envelope.len >= sizeof(packet_header_t)) {
    const packet_header_t *hdr = (const packet_header_t *)envelope.data;
    uint16_t pkt_type = NET_TO_HOST_U16(hdr->type);
    log_info("[TCP_RECV_STATE] ✅ RECV_OK: sockfd=%d, packet_type=%d (0x%04x), len=%zu", tcp->sockfd, pkt_type,
             pkt_type, envelope.len);
  } else {
    log_warn("[TCP_RECV_STATE] ⚠️  RECV_OK_SMALL_PACKET: sockfd=%d, len=%zu (< header)", tcp->sockfd, envelope.len);
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t tcp_close(acip_transport_t *transport) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;

  log_warn("[TCP_CLOSE_STATE] 🔴 CLOSE_REQUESTED: transport=%p, sockfd=%d, was_connected=%s", (void *)transport,
           tcp->sockfd, tcp->is_connected ? "yes" : "no");

  if (!tcp->is_connected) {
    log_debug("[TCP_CLOSE_STATE] ✅ CLOSE_IDEMPOTENT: transport already disconnected (sockfd=%d)", tcp->sockfd);
    return ASCIICHAT_OK; // Already closed
  }

  // Note: We do NOT close the socket - caller owns it
  // We just mark ourselves as disconnected
  log_warn("[TCP_CLOSE_STATE] 🔴 MARKING_DISCONNECTED: transport=%p, sockfd=%d (socket NOT closed - caller owns it)",
           (void *)transport, tcp->sockfd);
  tcp->is_connected = false;

  log_warn("[TCP_CLOSE_STATE] ✅ CLOSE_COMPLETE: TCP transport marked as disconnected. transport=%p, sockfd=%d",
           (void *)transport, tcp->sockfd);
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
  bool result = tcp->is_connected;
  log_debug_every(LOG_RATE_FAST, "[TCP_STATE_CHECK] tcp_is_connected(): transport=%p, sockfd=%d, result=%s",
                  (void *)transport, tcp->sockfd, result ? "true" : "false");
  return result;
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
    .destroy_impl = NULL, // No custom cleanup needed
};

// =============================================================================
// TCP Transport Creation
// =============================================================================

acip_transport_t *acip_tcp_transport_create(const char *name, socket_t sockfd, crypto_context_t *crypto_ctx) {
  if (!name) {
    SET_ERRNO(ERROR_INVALID_STATE, "Transport name is required");
    return NULL;
  }

  if (sockfd == INVALID_SOCKET_VALUE) {
    log_error("[TCP_CREATE_STATE] ❌ INVALID_SOCKET: sockfd=%d", sockfd);
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid socket descriptor");
    return NULL;
  }

  log_info("[TCP_CREATE_STATE] 🟢 CREATE_START: sockfd=%d, crypto=%s", sockfd, crypto_ctx ? "yes" : "no");

  // Allocate transport structure
  acip_transport_t *transport = SAFE_MALLOC(sizeof(acip_transport_t), acip_transport_t *);
  if (!transport) {
    log_error("[TCP_CREATE_STATE] ❌ ALLOC_TRANSPORT_FAILED: size=%zu", sizeof(acip_transport_t));
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate TCP transport");
    return NULL;
  }

  log_debug("[TCP_CREATE_STATE] 🟢 TRANSPORT_ALLOCATED: transport=%p", (void *)transport);

  // Allocate TCP-specific data
  tcp_transport_data_t *tcp_data = SAFE_MALLOC(sizeof(tcp_transport_data_t), tcp_transport_data_t *);
  if (!tcp_data) {
    log_error("[TCP_CREATE_STATE] ❌ ALLOC_TCP_DATA_FAILED: size=%zu", sizeof(tcp_transport_data_t));
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate TCP transport data");
    return NULL;
  }

  log_debug("[TCP_CREATE_STATE] 🟢 TCP_DATA_ALLOCATED: tcp_data=%p", (void *)tcp_data);

  // Initialize TCP data
  tcp_data->sockfd = sockfd;
  tcp_data->is_connected = true;
  log_info("[TCP_CREATE_STATE] 🟢 TCP_DATA_INITIALIZED: sockfd=%d, is_connected=true", sockfd);

  // Register impl_data in named registry for debug logging
  NAMED_REGISTER(tcp_data, name, "tcp_impl", "0x%tx");

  // Enable TCP_NODELAY to disable Nagle's algorithm
  // This ensures small packets are sent immediately instead of being buffered
  int nodelay = 1;
  if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, (const char *)&nodelay, sizeof(nodelay)) < 0) {
    log_warn("[TCP_CREATE_STATE] ⚠️  TCP_NODELAY_FAILED: sockfd=%d, %s", sockfd, SAFE_STRERROR(errno));
    // Continue anyway - this is not fatal
  } else {
    log_debug("[TCP_CREATE_STATE] 🟢 TCP_NODELAY_SET: sockfd=%d", sockfd);
  }

  // Initialize transport
  transport->methods = &tcp_methods;
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = tcp_data;

  log_info("[TCP_CREATE_STATE] ✅ CREATE_COMPLETE: transport=%p, sockfd=%d, is_connected=true, crypto=%s",
           (void *)transport, sockfd, crypto_ctx ? "enabled" : "disabled");

  NAMED_REGISTER_TRANSPORT(transport, name);
  return transport;
}

// =============================================================================
// Transport Destroy (shared implementation)
// =============================================================================

void acip_transport_destroy(acip_transport_t *transport) {
  if (!transport) {
    log_debug("[TRANSPORT_DESTROY] ⚠️  NULL_TRANSPORT: nothing to destroy");
    return;
  }

  log_warn("[TRANSPORT_DESTROY] 🔴 DESTROY_START: transport=%p, impl_data=%p", (void *)transport, transport->impl_data);

  // Get type before we destroy for logging
  acip_transport_type_t type = 0;
  if (transport->methods && transport->methods->get_type) {
    type = transport->methods->get_type(transport);
    log_info("[TRANSPORT_DESTROY] 📋 TRANSPORT_TYPE: type=%d (1=TCP, 2=WebSocket, 3=WebRTC, ...)", type);
  }

  // Close if still connected
  if (transport->methods && transport->methods->close && transport->methods->is_connected &&
      transport->methods->is_connected(transport)) {
    log_warn("[TRANSPORT_DESTROY] 🔴 STILL_CONNECTED: calling close() first (type=%d)", type);
    asciichat_error_t close_result = transport->methods->close(transport);
    log_info("[TRANSPORT_DESTROY] 🔴 CLOSE_CALLED: result=%d", close_result != ASCIICHAT_OK ? -1 : 0);
  } else {
    log_debug("[TRANSPORT_DESTROY] ✅ ALREADY_CLOSED: skipping close (type=%d)", type);
  }

  // Call custom destroy implementation if provided
  if (transport->methods && transport->methods->destroy_impl) {
    log_info("[TRANSPORT_DESTROY] 🔧 CALLING_DESTROY_IMPL: type=%d", type);
    transport->methods->destroy_impl(transport);
    log_info("[TRANSPORT_DESTROY] ✅ DESTROY_IMPL_COMPLETE: type=%d", type);
  } else {
    log_debug("[TRANSPORT_DESTROY] ⏭️  NO_CUSTOM_DESTROY: type=%d (using generic cleanup)", type);
  }

  // Free implementation data
  if (transport->impl_data) {
    log_debug("[TRANSPORT_DESTROY] 🗑️  FREEING_IMPL_DATA: %p", transport->impl_data);
    NAMED_UNREGISTER(transport->impl_data);
    SAFE_FREE(transport->impl_data);
    log_debug("[TRANSPORT_DESTROY] ✅ IMPL_DATA_FREED");
  }

  // Free transport structure
  log_debug("[TRANSPORT_DESTROY] 🗑️  FREEING_TRANSPORT: %p", (void *)transport);
  SAFE_FREE(transport);

  log_warn("[TRANSPORT_DESTROY] ✅ DESTROY_COMPLETE: type=%d", type);
}
