#include <ascii-chat/network/acip/backpressure.h>
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
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/network.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/socket.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/debug/named.h>
#include <string.h>
#include <ascii-chat/options/options.h>

/**
 * @brief TCP transport implementation data
 */
typedef struct {
  socket_t sockfd;        ///< Connected socket descriptor
  bool owns_socket;       ///< Close the socket when this transport is closed or destroyed
  bool is_connected;      ///< Connection state
  bool zerocopy_disabled; ///< Disable copy avoidance after unsupported or copied completion
  mutex_t send_mutex;     ///< Mutex to protect concurrent sends (multiple threads may send packets)
} tcp_transport_data_t;

static asciichat_error_t tcp_sendv(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  tcp_transport_data_t *tcp = transport->impl_data;
  mutex_lock(&tcp->send_mutex);
  if (!tcp->is_connected) {
    mutex_unlock(&tcp->send_mutex);
    return SET_ERRNO(ERROR_NETWORK, "TCP transport disconnected");
  }
  acip_packet_plan_t plan;
  asciichat_error_t result = acip_packet_plan(transport->crypto_ctx, buffers, count, &plan);
  if (result != ASCIICHAT_OK) {
    mutex_unlock(&tcp->send_mutex);
    return result;
  }
  uint64_t timeout = packet_send_timeout_ns(plan.plaintext_size);
  if (!plan.encrypt) {
    result = socket_sendv_all(tcp->sockfd, buffers, count, timeout);
  } else {
    size_t capacity = sizeof(packet_header_t) + plan.ciphertext_capacity;
    socket_send_buffer_t wire = {0};
    if (tcp->zerocopy_disabled || plan.domain) {
      wire.data = buffer_alloc(plan.domain, capacity);
      wire.capacity = capacity;
      result = wire.data ? ASCIICHAT_OK : SET_ERRNO(ERROR_MEMORY, "Cannot allocate ciphertext");
    } else {
      result = socket_send_buffer_alloc(capacity, &wire);
    }
    if (result != ASCIICHAT_OK) {
      mutex_unlock(&tcp->send_mutex);
      return result;
    }
    size_t ciphertext_len = 0;
    packet_header_t outer;
    result = acip_packet_encrypt(transport->crypto_ctx, &plan, plan.domain, (uint8_t *)wire.data + sizeof(outer),
                                 plan.ciphertext_capacity, &outer, &ciphertext_len);
    if (result != ASCIICHAT_OK) {
      socket_send_buffer_free(&wire);
      mutex_unlock(&tcp->send_mutex);
      return result;
    }
    memcpy(wire.data, &outer, sizeof(outer));
    size_t wire_len = sizeof(outer) + ciphertext_len;
    bool supported = false, copied = false;
    uint64_t send_start = time_get_ns();
    result = ASCIICHAT_OK;
    if (!tcp->zerocopy_disabled && !plan.domain && wire_len >= SOCKET_ZEROCOPY_THRESHOLD) {
      result = socket_send_zerocopy(tcp->sockfd, &wire, wire_len, timeout, &supported, &copied);
      tcp->zerocopy_disabled = !supported || copied;
      log_debug("TCP zerocopy: supported=%d copied=%d bytes=%zu", supported, copied, wire_len);
    }
    if (result == ASCIICHAT_OK && !supported) {
      socket_buffer_t slice = {wire.data, wire_len};
      uint64_t elapsed = time_get_ns() - send_start;
      result = elapsed >= timeout ? SET_ERRNO(ERROR_NETWORK_TIMEOUT, "TCP send deadline expired")
                                  : socket_sendv_all(tcp->sockfd, &slice, 1, timeout - elapsed);
    }
    socket_send_buffer_free(&wire);
  }
  if (result != ASCIICHAT_OK) {
    tcp->is_connected = false;
    socket_shutdown(tcp->sockfd, SHUT_RDWR);
    result = SET_ERRNO(ERROR_NETWORK, "TCP packet transmission failed");
  }
  mutex_unlock(&tcp->send_mutex);
  return result;
}

static asciichat_error_t tcp_send(acip_transport_t *transport, const void *data, size_t len) {
  socket_buffer_t slice = {data, len};
  return tcp_sendv(transport, &slice, 1);
}

asciichat_error_t acip_tcp_transport_set_zerocopy(acip_transport_t *transport, bool enabled) {
  if (!transport || acip_transport_get_type(transport) != ACIP_TRANSPORT_TCP)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Expected TCP transport");
  tcp_transport_data_t *tcp = transport->impl_data;
  mutex_lock(&tcp->send_mutex);
  tcp->zerocopy_disabled = !enabled;
  mutex_unlock(&tcp->send_mutex);
  return ASCIICHAT_OK;
}

static asciichat_error_t tcp_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                  void **out_allocated_buffer) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;

  log_debug("[TCP_RECV_STATE] Entry: transport=%p, sockfd=%d, is_connected=%s", (void *)transport, tcp->sockfd,
            tcp->is_connected ? "true" : "false");

  if (!tcp->is_connected) {
    log_error("[TCP_RECV_STATE] ❌ DISCONNECTED: Cannot recv - transport marked disconnected! sockfd=%d", tcp->sockfd);
    return SET_ERRNO(ERROR_NETWORK, "TCP transport not connected");
  }

  // Use secure packet receive with envelope
  packet_envelope_t envelope;
  bool enforce_encryption = (transport->crypto_ctx != NULL && transport->crypto_ctx->encrypt_data);
  log_debug("[TCP_RECV_STATE] 📥 RECV_WAITING: sockfd=%d, enforce_encryption=%s", tcp->sockfd,
            enforce_encryption ? "yes" : "no");

  uint64_t receive_timeout_ns =
      transport->receive_timeout_ns != 0 ? transport->receive_timeout_ns : RECV_TIMEOUT * NS_PER_SEC_INT;
  asciichat_errno_scope_t receive_scope = asciichat_errno_checkpoint();
  packet_recv_result_t result = receive_packet_secure_with_timeout(tcp->sockfd, transport->crypto_ctx,
                                                                   enforce_encryption, &envelope, receive_timeout_ns);
  log_debug("[TCP_RECV_STATE] 📥 RECV_RESULT: code=%d (0=success, -1=eof, -2=error, -3=security), data_size=%zu",
            result, result == PACKET_RECV_SUCCESS ? envelope.len : 0);

  if (result != PACKET_RECV_SUCCESS) {
    if (result == PACKET_RECV_EOF) {
      tcp->is_connected = false;
      log_warn("[TCP_RECV_STATE] ⚠️  RECV_EOF: Connection closed by remote (sockfd=%d)", tcp->sockfd);
      return SET_ERRNO(ERROR_NETWORK, "Connection closed");
    } else if (result == PACKET_RECV_SECURITY_VIOLATION) {
      log_error("[TCP_RECV_STATE] ❌ RECV_SECURITY_VIOLATION: Crypto error on sockfd=%d", tcp->sockfd);
      return SET_ERRNO(ERROR_CRYPTO, "Security violation");
    } else {
      if (GET_ERRNO() == ERROR_NETWORK_TIMEOUT && HAS_ERRNO_CODE_SINCE(receive_scope, ERROR_NETWORK_TIMEOUT)) {
        return ERROR_NETWORK_TIMEOUT;
      }
      // A non-timeout receive error is terminal for this stream. Retaining a
      // connected state after the peer has gone away causes callers to retry a
      // socket that can no longer deliver packets.
      tcp->is_connected = false;
      log_error("[TCP_RECV_STATE] ❌ RECV_FAILED: result=%d on sockfd=%d", result, tcp->sockfd);
      return SET_ERRNO(ERROR_NETWORK, "Failed to receive packet");
    }
  }

  *buffer = envelope.data;
  *out_len = envelope.len;
  *out_allocated_buffer = envelope.allocated_buffer;

  // The decrypted header remains at the allocation base. Return that complete
  // packet directly rather than copying and releasing the envelope's storage.
  if (envelope.data != envelope.allocated_buffer) {
    *buffer = envelope.allocated_buffer;
    *out_len = sizeof(packet_header_t) + envelope.len;
  }
  if (*out_len >= sizeof(packet_header_t)) {
    const packet_header_t *hdr = (const packet_header_t *)*buffer;
    log_debug("TCP received packet type=%u, bytes=%zu", NET_TO_HOST_U16(hdr->type), *out_len);
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t tcp_close(acip_transport_t *transport) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;

  if (tcp->owns_socket && tcp->sockfd != INVALID_SOCKET_VALUE) {
    socket_close(tcp->sockfd);
    tcp->sockfd = INVALID_SOCKET_VALUE;
  }

  log_warn("[TCP_CLOSE_STATE] 🔴 CLOSE_REQUESTED: transport=%p, sockfd=%d, was_connected=%s", (void *)transport,
           tcp->sockfd, tcp->is_connected ? "yes" : "no");

  if (!tcp->is_connected) {
    log_debug("[TCP_CLOSE_STATE] ✅ CLOSE_IDEMPOTENT: transport already disconnected (sockfd=%d)", tcp->sockfd);
    return ASCIICHAT_OK; // Already closed
  }

  // Borrowed sockets remain open for their owner.
  log_warn("[TCP_CLOSE_STATE] 🔴 MARKING_DISCONNECTED: transport=%p, sockfd=%d (owned socket closed; borrowed socket "
           "retained)",
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

static bool tcp_has_pending_data(acip_transport_t *transport) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;
  if (!tcp || !socket_is_valid(tcp->sockfd)) {
    return false;
  }

  fd_set readfds;
  struct timeval tv = {0};
  socket_fd_zero(&readfds);
  socket_fd_set(tcp->sockfd, &readfds);

  int select_result = socket_select(tcp->sockfd, &readfds, NULL, NULL, &tv);
  return select_result > 0 && socket_fd_isset(tcp->sockfd, &readfds);
}

static void tcp_destroy_impl(acip_transport_t *transport) {
  tcp_transport_data_t *tcp = (tcp_transport_data_t *)transport->impl_data;
  if (tcp) {
    // Receive errors may have cleared is_connected before destruction.
    if (tcp->owns_socket)
      tcp_close(transport);
    // Destroy the send mutex
    int result = mutex_destroy(&tcp->send_mutex);
    if (result != 0) {
      log_warn("[TCP_DESTROY] ⚠️ MUTEX_DESTROY_FAILED: result=%d", result);
    }
  }
}

// =============================================================================
// TCP Transport Method Table
// =============================================================================

static void tcp_interrupt(acip_transport_t *transport) {
  tcp_transport_data_t *tcp = transport->impl_data;
  if (tcp->sockfd != INVALID_SOCKET_VALUE)
    socket_shutdown(tcp->sockfd, SHUT_RDWR);
}

static const acip_transport_methods_t tcp_methods = {
    .send = tcp_send,
    .reserved_bytes = SOCKET_STREAM_SEND_RESERVE,
    .sendv = tcp_sendv,
    .recv = tcp_recv,
    .close = tcp_close,
    .interrupt = tcp_interrupt,
    .get_type = tcp_get_type,
    .get_socket = tcp_get_socket,
    .is_connected = tcp_is_connected,
    .has_pending_data = tcp_has_pending_data,
    .destroy_impl = tcp_destroy_impl, // Destroy send_mutex
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
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(acip_transport_t), acip_transport_t *);
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
  tcp_data->owns_socket = false;
  tcp_data->zerocopy_disabled = false;

  // Initialize send mutex to protect concurrent sends from multiple threads
  int mutex_result = mutex_init(&tcp_data->send_mutex, "tcp_send");
  if (mutex_result != 0) {
    log_error("[TCP_CREATE_STATE] ❌ MUTEX_INIT_FAILED: result=%d", mutex_result);
    SAFE_FREE(tcp_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to initialize send mutex");
    return NULL;
  }

  log_info("[TCP_CREATE_STATE] 🟢 TCP_DATA_INITIALIZED: sockfd=%d, is_connected=true, send_mutex=initialized", sockfd);

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
  transport->stats_peer = stats_runtime_peer_open("TCP");
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = tcp_data;
  int send_buffer = SOCKET_STREAM_SEND_BUFFER;
  if (socket_setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) != 0) {
    acip_transport_destroy(transport);
    SET_ERRNO_SYS(ERROR_NETWORK, "Cannot bound TCP send buffer");
    return NULL;
  }

  // Register transport and impl_data before logging so named replacements work
  NAMED_REGISTER_TRANSPORT(transport, name, NULL);
  NAMED_REGISTER(tcp_data, "impl", "tcp_impl", "0x%tx", (uintptr_t)(const void *)(transport));

  log_info("[TCP_CREATE_STATE] ✅ CREATE_COMPLETE: transport=%p, sockfd=%d, is_connected=true, crypto=%s",
           (void *)transport, sockfd, crypto_ctx ? "enabled" : "disabled");

#ifndef __EMSCRIPTEN__
  if (transport && acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
#endif
  return transport;
}

acip_transport_t *acip_tcp_transport_create_owned(const char *name, socket_t sockfd, crypto_context_t *crypto_ctx) {
  acip_transport_t *transport = acip_tcp_transport_create(name, sockfd, crypto_ctx);
  if (transport)
    ((tcp_transport_data_t *)transport->impl_data)->owns_socket = true;
#ifndef __EMSCRIPTEN__
  if (transport && acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
#endif
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

#ifndef __EMSCRIPTEN__
  acip_outbound_destroy(transport);
#endif

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

  stats_runtime_peer_close(transport->stats_peer);
  transport->stats_peer = NULL;

  // Free transport structure
  log_debug("[TRANSPORT_DESTROY] 🗑️  FREEING_TRANSPORT: %p", (void *)transport);
  SAFE_FREE(transport);

  log_warn("[TRANSPORT_DESTROY] ✅ DESTROY_COMPLETE: type=%d", type);
}
