/**
 * @file network/websocket/transport.c
 * @brief WebSocket transport implementation for ACIP protocol
 *
 * Implements the acip_transport_t interface for WebSocket connections.
 * Enables browser clients to connect via WebSocket protocol.
 *
 * ARCHITECTURE:
 * =============
 * - Uses libwebsockets for WebSocket protocol handling
 * - Async libwebsockets callbacks bridge to sync recv() via ringbuffer
 * - Thread-safe receive queue handles async message arrival
 * - Same pattern as WebRTC transport for consistency
 *
 * MESSAGE FLOW:
 * =============
 * 1. send(): Synchronous write via lws_write()
 * 2. LWS callback: Async write to receive ringbuffer
 * 3. recv(): Blocking read from receive ringbuffer
 *
 * MEMORY OWNERSHIP:
 * =================
 * - Transport OWNS wsi (WebSocket instance)
 * - Receive queue owns buffered message data
 * - recv() allocates message buffer, caller must free
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/debug/memory.h>
#include <libwebsockets.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Maximum receive queue size (messages buffered before recv())
 *
 * Power of 2 for ringbuffer optimization.
 * Increased from 512 to buffer multiple large frames and reduce queue pressure.
 * Each slot holds one message (up to 921KB). With 4096 slots, can buffer ~3.7GB.
 */
#define WEBSOCKET_RECV_QUEUE_SIZE 4096

/**
 * @brief Maximum send queue size (messages buffered for server-side sending)
 *
 * Larger than receive queue because video frames are continuously sent.
 * Must be large enough to buffer frames while event loop processes them.
 */
#define WEBSOCKET_SEND_QUEUE_SIZE 256

// Shared internal types (websocket_recv_msg_t, websocket_transport_data_t)
#include <ascii-chat/network/websocket/internal.h>

// Forward declaration for libwebsockets callback
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

// =============================================================================
// Service Thread (Client-side only)
// =============================================================================

/**
 * @brief Service thread that continuously processes libwebsockets events
 *
 * This thread is necessary for client-side transports to receive incoming messages.
 * It continuously calls lws_service() to process network events and trigger callbacks.
 */
static void *websocket_service_thread(void *arg) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)arg;

  log_debug("WebSocket service thread started");

  while (ws_data->service_running) {
    // Service libwebsockets (processes network events, triggers callbacks)
    // 50ms timeout allows checking service_running flag regularly
    int result = lws_service(ws_data->context, 50);
    if (result < 0) {
      log_error("lws_service error: %d", result);
      break;
    }
  }

  log_debug("WebSocket service thread exiting");
  return NULL;
}

// =============================================================================
// libwebsockets Callbacks
// =============================================================================

/**
 * @brief libwebsockets callback - handles all WebSocket events
 *
 * This is the main callback function that libwebsockets uses to notify us
 * of events like connection, message arrival, closure, etc.
 */
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)user;

  switch (reason) {
  case LWS_CALLBACK_CLIENT_ESTABLISHED:
    log_info("WebSocket connection established");
    if (ws_data) {
      mutex_lock(&ws_data->state_mutex);
      ws_data->is_connected = true;
      mutex_unlock(&ws_data->state_mutex);
    }
    break;

  case LWS_CALLBACK_CLIENT_RECEIVE: {
    // Received data from server - may be fragmented for large messages
    if (!ws_data || !in || len == 0) {
      break;
    }

    bool is_first = lws_is_first_fragment(wsi);
    bool is_final = lws_is_final_fragment(wsi);

    log_dev_every(4500000, "WebSocket fragment: %zu bytes (first=%d, final=%d)", len, is_first, is_final);

    // Queue this fragment immediately with first/final flags.
    // Per LWS design, each fragment is processed individually by the callback.
    // We must NOT manually reassemble fragments - that breaks LWS's internal state machine.
    // Instead, queue each fragment with metadata, and let the receiver decide on reassembly.

    websocket_recv_msg_t msg;
    msg.data = buffer_pool_alloc(NULL, len);
    if (!msg.data) {
      log_error("Failed to allocate buffer for fragment (%zu bytes)", len);
      break;
    }

    memcpy(msg.data, in, len);
    msg.len = len;
    msg.first = is_first;
    msg.final = is_final;

    mutex_lock(&ws_data->queue_mutex);
    bool success = ringbuffer_write(ws_data->recv_queue, &msg);
    if (!success) {
      // Queue is full - drop the fragment and log warning
      log_warn("WebSocket receive queue full - dropping fragment (len=%zu, first=%d, final=%d)", len, is_first,
               is_final);
      buffer_pool_free(NULL, msg.data, msg.len);
      mutex_unlock(&ws_data->queue_mutex);
      break;
    }

    // Signal waiting recv() call that a fragment is available
    cond_signal(&ws_data->queue_cond);
    mutex_unlock(&ws_data->queue_mutex);
    break;
  }

  case LWS_CALLBACK_CLIENT_CLOSED:
  case LWS_CALLBACK_CLOSED:
    log_info("WebSocket connection closed");
    if (ws_data) {
      mutex_lock(&ws_data->state_mutex);
      ws_data->is_connected = false;
      mutex_unlock(&ws_data->state_mutex);

      // Wake any blocking recv() calls
      cond_broadcast(&ws_data->queue_cond);
    }
    break;

  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    log_error("WebSocket connection error: %s", in ? (const char *)in : "unknown");
    if (ws_data) {
      mutex_lock(&ws_data->state_mutex);
      ws_data->is_connected = false;
      mutex_unlock(&ws_data->state_mutex);

      // Wake any blocking recv() calls
      cond_broadcast(&ws_data->queue_cond);
    }
    break;

  case LWS_CALLBACK_CLIENT_WRITEABLE:
    // Socket is writable - we don't need to do anything here
    // as we handle writes synchronously in websocket_send()
    break;

  default:
    break;
  }

  return 0;
}

// =============================================================================
// WebSocket Transport Methods
// =============================================================================

static asciichat_error_t websocket_send(acip_transport_t *transport, const void *data, size_t len) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  mutex_lock(&ws_data->state_mutex);
  bool connected = ws_data->is_connected;
  mutex_unlock(&ws_data->state_mutex);

  log_dev_every(1000000, "websocket_send: is_connected=%d, wsi=%p, send_len=%zu", connected, (void *)ws_data->wsi, len);

  if (!connected) {
    log_error("WebSocket send called but transport NOT connected! wsi=%p, len=%zu", (void *)ws_data->wsi, len);
    return SET_ERRNO(ERROR_NETWORK, "WebSocket transport not connected (wsi=%p)", (void *)ws_data->wsi);
  }

  // Check if encryption is needed (matching tcp_send logic)
  const void *send_data = data;
  size_t send_len = len;
  uint8_t *encrypted_packet = NULL;
  size_t encrypted_packet_size = 0;

  if (len >= sizeof(packet_header_t) && transport->crypto_ctx && crypto_is_ready(transport->crypto_ctx)) {
    const packet_header_t *header = (const packet_header_t *)data;
    uint16_t packet_type = NET_TO_HOST_U16(header->type);

    if (!packet_is_handshake_type((packet_type_t)packet_type)) {
      // Encrypt the entire packet (header + payload)
      size_t ciphertext_size = len + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
      // Use SAFE_MALLOC (not buffer pool - encrypted_packet also uses pool and causes overlap)
      uint8_t *ciphertext = SAFE_MALLOC(ciphertext_size, uint8_t *);
      if (!ciphertext) {
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate ciphertext buffer for WebSocket");
      }

      size_t ciphertext_len;
      crypto_result_t result =
          crypto_encrypt(transport->crypto_ctx, data, len, ciphertext, ciphertext_size, &ciphertext_len);
      if (result != CRYPTO_OK) {
        SAFE_FREE(ciphertext);
        return SET_ERRNO(ERROR_CRYPTO, "Failed to encrypt WebSocket packet: %s", crypto_result_to_string(result));
      }

      // Build PACKET_TYPE_ENCRYPTED wrapper: header + ciphertext
      size_t total_encrypted_size = sizeof(packet_header_t) + ciphertext_len;
      encrypted_packet = buffer_pool_alloc(NULL, total_encrypted_size);
      if (!encrypted_packet) {
        SAFE_FREE(ciphertext);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate encrypted packet buffer");
      }

      packet_header_t encrypted_header;
      encrypted_header.magic = HOST_TO_NET_U64(PACKET_MAGIC);
      encrypted_header.type = HOST_TO_NET_U16(PACKET_TYPE_ENCRYPTED);
      encrypted_header.length = HOST_TO_NET_U32((uint32_t)ciphertext_len);
      encrypted_header.crc32 = HOST_TO_NET_U32(asciichat_crc32(ciphertext, ciphertext_len));
      encrypted_header.client_id = 0;

      memcpy(encrypted_packet, &encrypted_header, sizeof(encrypted_header));
      memcpy(encrypted_packet + sizeof(encrypted_header), ciphertext, ciphertext_len);
      SAFE_FREE(ciphertext);

      send_data = encrypted_packet;
      send_len = total_encrypted_size;
      encrypted_packet_size = total_encrypted_size;

      log_dev_every(1000000, "WebSocket: encrypted packet (original type %d as PACKET_TYPE_ENCRYPTED, %zu bytes)",
                    packet_type, send_len);
    }
  }

  // libwebsockets requires LWS_PRE bytes before the payload for protocol headers
  // Allocate a temporary buffer for this send to avoid thread-safety issues
  // Each send() call gets its own buffer, preventing race conditions with concurrent sends
  size_t required_size = LWS_PRE + send_len;
  uint8_t *send_buffer = SAFE_MALLOC(required_size, uint8_t *);
  if (!send_buffer) {
    if (encrypted_packet)
      buffer_pool_free(NULL, encrypted_packet, encrypted_packet_size);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate WebSocket send buffer");
  }

  // Copy data after LWS_PRE offset
  memcpy(send_buffer + LWS_PRE, send_data, send_len);

  // Server-side transports cannot call lws_write() directly
  // They must queue data and send from LWS_CALLBACK_SERVER_WRITEABLE
  if (!ws_data->owns_context) {
    // Queue the data for server-side sending
    websocket_recv_msg_t msg;
    msg.data = SAFE_MALLOC(send_len, uint8_t *);
    if (!msg.data) {
      SAFE_FREE(send_buffer);
      if (encrypted_packet)
        buffer_pool_free(NULL, encrypted_packet, send_len);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate send queue buffer");
    }
    memcpy(msg.data, send_data, send_len);
    msg.len = send_len;
    msg.first = 1;
    msg.final = 1;

    mutex_lock(&ws_data->queue_mutex);
    bool success = ringbuffer_write(ws_data->send_queue, &msg);

    if (!success) {
      mutex_unlock(&ws_data->queue_mutex);
      log_error("WebSocket server send queue FULL - cannot queue %zu byte message for wsi=%p", send_len,
                (void *)ws_data->wsi);
      SAFE_FREE(msg.data);
      SAFE_FREE(send_buffer);
      if (encrypted_packet)
        buffer_pool_free(NULL, encrypted_packet, encrypted_packet_size);
      return SET_ERRNO(ERROR_NETWORK, "Send queue full (cannot queue %zu bytes)", send_len);
    }
    mutex_unlock(&ws_data->queue_mutex);

    // Wake the LWS event loop from this non-service thread.
    // Only lws_cancel_service() is thread-safe from non-service threads.
    // lws_callback_on_writable() must NOT be called here — it's only safe
    // from the service thread. LWS_CALLBACK_EVENT_WAIT_CANCELLED (in server.c)
    // handles calling lws_callback_on_writable_all_protocol() on the service thread.
    log_dev_every(1000000, ">>> FRAME QUEUED: %zu bytes for wsi=%p (send_len=%zu)", send_len, (void *)ws_data->wsi,
                  send_len);

    struct lws_context *ctx = lws_get_context(ws_data->wsi);
    lws_cancel_service(ctx);

    log_dev_every(1000000, "Server-side WebSocket: queued %zu bytes, cancel_service sent for wsi=%p", send_len,
                  (void *)ws_data->wsi);
    SAFE_FREE(send_buffer);
    if (encrypted_packet)
      buffer_pool_free(NULL, encrypted_packet, send_len);
    return ASCIICHAT_OK;
  }

  // Client-side: send in fragments using LWS_WRITE_BUFLIST
  // libwebsockets will buffer and send when socket is writable
  const size_t FRAGMENT_SIZE = 4096;
  size_t offset = 0;

  while (offset < send_len) {
    size_t chunk_size = (send_len - offset > FRAGMENT_SIZE) ? FRAGMENT_SIZE : (send_len - offset);
    int is_start = (offset == 0);
    int is_end = (offset + chunk_size >= send_len);

    // Get appropriate flags for this fragment
    enum lws_write_protocol flags = lws_write_ws_flags(LWS_WRITE_BINARY, is_start, is_end);

    // Use BUFLIST to let libwebsockets handle buffering and writable callbacks
    flags = (enum lws_write_protocol)((int)flags | LWS_WRITE_BUFLIST);

    // Send this fragment
    int written = lws_write(ws_data->wsi, send_buffer + LWS_PRE + offset, chunk_size, flags);

    if (written < 0) {
      SAFE_FREE(send_buffer);
      if (encrypted_packet)
        buffer_pool_free(NULL, encrypted_packet, send_len);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket write failed on fragment at offset %zu", offset);
    }

    if ((size_t)written != chunk_size) {
      SAFE_FREE(send_buffer);
      if (encrypted_packet)
        buffer_pool_free(NULL, encrypted_packet, send_len);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket partial write: %d/%zu bytes at offset %zu", written, chunk_size,
                       offset);
    }

    offset += chunk_size;
    log_dev_every(1000000, "WebSocket sent fragment %zu bytes (offset %zu/%zu)", chunk_size, offset, send_len);
  }

  log_dev_every(1000000, "WebSocket sent complete message: %zu bytes in fragments", send_len);
  SAFE_FREE(send_buffer);
  if (encrypted_packet)
    buffer_pool_free(NULL, encrypted_packet, encrypted_packet_size);
  return ASCIICHAT_OK;
}

static asciichat_error_t websocket_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                        void **out_allocated_buffer) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  // Check connection first without holding queue lock
  mutex_lock(&ws_data->state_mutex);
  bool connected = ws_data->is_connected;
  mutex_unlock(&ws_data->state_mutex);

  if (!connected) {
    return SET_ERRNO(ERROR_NETWORK, "Connection not established");
  }

  mutex_lock(&ws_data->queue_mutex);

  // Reassemble fragmented WebSocket messages - NON-BLOCKING
  // We queue each fragment from the LWS callback with first/final flags.
  // Here we check for complete messages without blocking. If no complete
  // message is ready, return immediately so the receive thread can retry.

  uint8_t *assembled_buffer = NULL;
  size_t assembled_size = 0;
  size_t assembled_capacity = 0;
  int fragment_count = 0;

  while (true) {
    // If queue is empty, return error immediately (non-blocking)
    // The receive thread will retry after a short sleep
    if (ringbuffer_is_empty(ws_data->recv_queue)) {
      if (assembled_size > 0) {
        // We have partial fragments but queue is empty - incomplete message
        log_dev_every(4500000, "🔄 WEBSOCKET_RECV: Incomplete frame (have %zu bytes), waiting for more fragments",
                      assembled_size);
        buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
        mutex_unlock(&ws_data->queue_mutex);
        return SET_ERRNO(ERROR_NETWORK, "Incomplete WebSocket frame (waiting for more fragments)");
      } else {
        // Queue is empty and no fragments accumulated yet
        log_dev_every(4500000, "🔄 WEBSOCKET_RECV: Queue empty, no packets ready");
        mutex_unlock(&ws_data->queue_mutex);
        return SET_ERRNO(ERROR_NETWORK, "No WebSocket packets available");
      }
    }

    // Read next fragment from queue
    websocket_recv_msg_t frag;
    bool success = ringbuffer_read(ws_data->recv_queue, &frag);
    if (!success) {
      if (assembled_buffer) {
        buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
      }
      mutex_unlock(&ws_data->queue_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Failed to read fragment from queue");
    }

    fragment_count++;
    log_warn("[WS_REASSEMBLE] Fragment #%d: %zu bytes, first=%d, final=%d, assembled_so_far=%zu", fragment_count,
             frag.len, frag.first, frag.final, assembled_size);

    // Sanity check: first fragment must have first=1, continuations must have first=0
    if (assembled_size == 0 && !frag.first) {
      log_error("[WS_REASSEMBLE] ERROR: Expected first=1 for first fragment, got first=%d", frag.first);
      buffer_pool_free(NULL, frag.data, frag.len);
      if (assembled_buffer) {
        buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
      }
      mutex_unlock(&ws_data->queue_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Protocol error: continuation fragment without first fragment");
    }

    // Grow assembled buffer if needed
    size_t required_size = assembled_size + frag.len;
    if (required_size > assembled_capacity) {
      size_t new_capacity = (assembled_capacity == 0) ? 8192 : (assembled_capacity * 3 / 2);
      if (new_capacity < required_size) {
        new_capacity = required_size;
      }

      uint8_t *new_buffer = buffer_pool_alloc(NULL, new_capacity);
      if (!new_buffer) {
        log_error("[WS_REASSEMBLE] Failed to allocate reassembly buffer (%zu bytes)", new_capacity);
        buffer_pool_free(NULL, frag.data, frag.len);
        if (assembled_buffer) {
          buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
        }
        mutex_unlock(&ws_data->queue_mutex);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate fragment reassembly buffer");
      }

      // Copy existing data to new buffer
      if (assembled_buffer) {
        memcpy(new_buffer, assembled_buffer, assembled_size);
        buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
      }

      assembled_buffer = new_buffer;
      assembled_capacity = new_capacity;
    }

    // Append this fragment
    memcpy(assembled_buffer + assembled_size, frag.data, frag.len);
    assembled_size += frag.len;
    buffer_pool_free(NULL, frag.data, frag.len);

    // If this is the final fragment, we're done
    if (frag.final) {
      uint64_t assembly_end_ns = time_get_ns();
      char assembly_duration_str[32];
      format_duration_ns((double)(assembly_end_ns - assembly_start_ns), assembly_duration_str,
                         sizeof(assembly_duration_str));
      log_info("[WS_REASSEMBLE] Complete message assembled: %zu bytes from %d fragments in %s", assembled_size,
               fragment_count, assembly_duration_str);
      break;
    }
  }

  mutex_unlock(&ws_data->queue_mutex);

  // Return reassembled message to caller
  *buffer = assembled_buffer;
  *out_len = assembled_size;
  *out_allocated_buffer = assembled_buffer;

  log_info_every(LOG_RATE_DEFAULT, "[WS_TIMING] websocket_recv dequeued %zu bytes (from %d fragments) at t=%llu",
                 assembled_size, fragment_count, (unsigned long long)time_get_ns());
  return ASCIICHAT_OK;
}

static asciichat_error_t websocket_close(acip_transport_t *transport) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  mutex_lock(&ws_data->state_mutex);

  if (!ws_data->is_connected) {
    mutex_unlock(&ws_data->state_mutex);
    log_debug("websocket_close: Already closed (is_connected=false), wsi=%p", (void *)ws_data->wsi);
    return ASCIICHAT_OK; // Already closed
  }

  log_info("websocket_close: Setting is_connected=false, wsi=%p", (void *)ws_data->wsi);
  ws_data->is_connected = false;
  mutex_unlock(&ws_data->state_mutex);

  // Close WebSocket connection
  if (ws_data->wsi) {
    log_debug("websocket_close: Calling lws_close_reason for wsi=%p", (void *)ws_data->wsi);
    lws_close_reason(ws_data->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
  }

  // Wake any blocking recv() calls
  cond_broadcast(&ws_data->queue_cond);

  log_debug("WebSocket transport closed");
  return ASCIICHAT_OK;
}

static acip_transport_type_t websocket_get_type(acip_transport_t *transport) {
  (void)transport;
  return ACIP_TRANSPORT_WEBSOCKET;
}

static socket_t websocket_get_socket(acip_transport_t *transport) {
  (void)transport;
  return INVALID_SOCKET_VALUE; // WebSocket has no underlying socket handle we can expose
}

static bool websocket_is_connected(acip_transport_t *transport) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  mutex_lock(&ws_data->state_mutex);
  bool connected = ws_data->is_connected;
  mutex_unlock(&ws_data->state_mutex);

  return connected;
}

// =============================================================================
// WebSocket Transport Destroy Implementation
// =============================================================================

/**
 * @brief Destroy WebSocket transport and free all resources
 *
 * This is called by the generic acip_transport_destroy() after calling close().
 * Frees WebSocket-specific resources including context, receive queue,
 * and synchronization primitives.
 */
static void websocket_destroy_impl(acip_transport_t *transport) {
  if (!transport || !transport->impl_data) {
    return;
  }

  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  // Stop service thread (client-side only)
  if (ws_data->service_running) {
    log_debug("Stopping WebSocket service thread");
    ws_data->service_running = false;
    asciichat_thread_join(&ws_data->service_thread, NULL);
    log_debug("WebSocket service thread stopped");
  }

  // Destroy WebSocket instance (if not already destroyed)
  if (ws_data->wsi) {
    // libwebsockets will clean up the wsi when we destroy the context
    ws_data->wsi = NULL;
  }

  // Destroy WebSocket context (only if we own it - client transports only)
  if (ws_data->context && ws_data->owns_context) {
    lws_context_destroy(ws_data->context);
    ws_data->context = NULL;
  }

  // Clear receive queue and free buffered messages
  if (ws_data->recv_queue) {
    mutex_lock(&ws_data->queue_mutex);

    websocket_recv_msg_t msg;
    while (ringbuffer_read(ws_data->recv_queue, &msg)) {
      if (msg.data) {
        buffer_pool_free(NULL, msg.data, msg.len);
      }
    }

    mutex_unlock(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ws_data->recv_queue = NULL;

    if (ws_data->send_queue) {
      // Drain send queue before destroying to free allocated message data
      websocket_recv_msg_t msg;
      while (ringbuffer_read(ws_data->send_queue, &msg)) {
        if (msg.data) {
          SAFE_FREE(msg.data);
        }
      }
      ringbuffer_destroy(ws_data->send_queue);
      ws_data->send_queue = NULL;
    }
  }

  // Free send buffer
  if (ws_data->send_buffer) {
    SAFE_FREE(ws_data->send_buffer);
    ws_data->send_buffer = NULL;
  }

  // Destroy synchronization primitives
  mutex_destroy(&ws_data->state_mutex);
  cond_destroy(&ws_data->queue_cond);
  mutex_destroy(&ws_data->queue_mutex);

  // Clear impl_data pointer BEFORE freeing to prevent use-after-free in callbacks
  transport->impl_data = NULL;

  // Free the websocket transport data structure
  SAFE_FREE(ws_data);

  log_debug("Destroyed WebSocket transport resources");
}

// =============================================================================
// WebSocket Transport Method Table
// =============================================================================

static const acip_transport_methods_t websocket_methods = {
    .send = websocket_send,
    .recv = websocket_recv,
    .close = websocket_close,
    .get_type = websocket_get_type,
    .get_socket = websocket_get_socket,
    .is_connected = websocket_is_connected,
    .destroy_impl = websocket_destroy_impl,
};

// =============================================================================
// WebSocket Transport Creation
// =============================================================================

/**
 * @brief Create WebSocket client transport
 *
 * @param url WebSocket URL (e.g., "ws://localhost:27225")
 * @param crypto_ctx Optional encryption context (can be NULL)
 * @return Transport instance or NULL on failure
 */
acip_transport_t *acip_websocket_client_transport_create(const char *url, crypto_context_t *crypto_ctx) {
  if (!url) {
    SET_ERRNO(ERROR_INVALID_PARAM, "url is required");
    return NULL;
  }

  // Parse URL to extract host, port, and path
  // Format: ws://host:port/path or wss://host:port/path
  const char *protocol_end = strstr(url, "://");
  if (!protocol_end) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid WebSocket URL format (missing ://)");
    return NULL;
  }

  bool use_ssl = (strncmp(url, "wss://", 6) == 0);
  const char *host_start = protocol_end + 3;

  // Find port (if specified)
  const char *port_start = strchr(host_start, ':');
  const char *path_start = strchr(host_start, '/');

  char host[256] = {0};
  int port = use_ssl ? 443 : 27226; // Default: wss:// uses 443, ws:// uses 27226 (ascii-chat WebSocket port)
  char path[256] = "/";

  if (port_start && (!path_start || port_start < path_start)) {
    // Port is specified
    size_t host_len = port_start - host_start;
    if (host_len >= sizeof(host)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Host name too long");
      return NULL;
    }
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    // Extract port
    port = atoi(port_start + 1);
    if (port <= 0 || port > 65535) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port number");
      return NULL;
    }
  } else {
    // No port specified, use default
    size_t host_len = path_start ? (size_t)(path_start - host_start) : strlen(host_start);
    if (host_len >= sizeof(host)) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Host name too long");
      return NULL;
    }
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';
  }

  // Extract path
  if (path_start) {
    SAFE_STRNCPY(path, path_start, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
  }

  log_info("Connecting to WebSocket: %s (host=%s, port=%d, path=%s, ssl=%d)", url, host, port, path, use_ssl);

  // Allocate transport structure
  acip_transport_t *transport = SAFE_MALLOC(sizeof(acip_transport_t), acip_transport_t *);
  if (!transport) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate WebSocket transport");
    return NULL;
  }

  // Allocate WebSocket-specific data
  websocket_transport_data_t *ws_data =
      SAFE_CALLOC(1, sizeof(websocket_transport_data_t), websocket_transport_data_t *);
  if (!ws_data) {
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate WebSocket transport data");
    return NULL;
  }

  // Create receive queue
  ws_data->recv_queue = ringbuffer_create(sizeof(websocket_recv_msg_t), WEBSOCKET_RECV_QUEUE_SIZE);
  if (!ws_data->recv_queue) {
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to create receive queue");
    return NULL;
  }

  // Initialize synchronization primitives
  if (mutex_init(&ws_data->queue_mutex) != 0) {
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize queue mutex");
    return NULL;
  }

  if (cond_init(&ws_data->queue_cond) != 0) {
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize queue condition variable");
    return NULL;
  }

  if (mutex_init(&ws_data->state_mutex) != 0) {
    cond_destroy(&ws_data->queue_cond);
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize state mutex");
    return NULL;
  }

  // Allocate initial send buffer
  ws_data->send_buffer_capacity = LWS_PRE + 8192; // Initial 8KB buffer
  ws_data->send_buffer = SAFE_MALLOC(ws_data->send_buffer_capacity, uint8_t *);
  if (!ws_data->send_buffer) {
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->queue_cond);
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate send buffer");
    return NULL;
  }

  // Create libwebsockets context
  // Protocol array must persist for lifetime of context - use static
  static struct lws_protocols client_protocols[] = {
      {"acip", websocket_callback, 0, 4096, 0, NULL, 0}, {NULL, NULL, 0, 0, 0, NULL, 0} // Terminator
  };

  struct lws_context_creation_info info;
  memset(&info, 0, sizeof(info));
  info.port = CONTEXT_PORT_NO_LISTEN; // Client mode - no listening
  info.protocols = client_protocols;
  info.gid = (gid_t)-1; // Cast to avoid undefined behavior with unsigned type
  info.uid = (uid_t)-1; // Cast to avoid undefined behavior with unsigned type
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

  ws_data->context = lws_create_context(&info);
  if (!ws_data->context) {
    SAFE_FREE(ws_data->send_buffer);
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->queue_cond);
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to create libwebsockets context");
    return NULL;
  }

  // Connect to WebSocket server
  log_debug("Initiating WebSocket connection to %s:%d%s", host, port, path);
  struct lws_client_connect_info connect_info;
  memset(&connect_info, 0, sizeof(connect_info));
  connect_info.context = ws_data->context;
  connect_info.address = host;
  connect_info.port = port;
  connect_info.path = path;
  connect_info.host = host;
  connect_info.origin = host;
  connect_info.protocol = "acip";
  connect_info.ssl_connection = use_ssl ? LCCSCF_USE_SSL : 0;
  connect_info.userdata = ws_data;

  log_debug("Calling lws_client_connect_via_info...");
  ws_data->wsi = lws_client_connect_via_info(&connect_info);
  log_debug("lws_client_connect_via_info returned: %p", (void *)ws_data->wsi);
  if (!ws_data->wsi) {
    lws_context_destroy(ws_data->context);
    SAFE_FREE(ws_data->send_buffer);
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->queue_cond);
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to connect to WebSocket server");
    return NULL;
  }

  ws_data->is_connected = false; // Will be set to true in LWS_CALLBACK_CLIENT_ESTABLISHED
  ws_data->owns_context = true;  // Client transport owns the context

  // Initialize transport
  transport->methods = &websocket_methods;
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = ws_data;

  // Wait for connection to establish (synchronous connection)
  // Service the libwebsockets event loop until connected or timeout
  log_debug("Waiting for WebSocket connection to establish...");
  int timeout_ms = 5000; // 5 second timeout
  int elapsed_ms = 0;
  while (!ws_data->is_connected && elapsed_ms < timeout_ms) {
    // Service libwebsockets (processes network events, triggers callbacks)
    int result = lws_service(ws_data->context, 50); // 50ms timeout per iteration
    if (result < 0) {
      log_error("lws_service error during connection: %d", result);
      lws_context_destroy(ws_data->context);
      SAFE_FREE(ws_data->send_buffer);
      mutex_destroy(&ws_data->state_mutex);
      cond_destroy(&ws_data->queue_cond);
      mutex_destroy(&ws_data->queue_mutex);
      ringbuffer_destroy(ws_data->recv_queue);
      SAFE_FREE(ws_data);
      SAFE_FREE(transport);
      SET_ERRNO(ERROR_NETWORK, "WebSocket connection failed");
      return NULL;
    }
    elapsed_ms += 50;
  }

  if (!ws_data->is_connected) {
    log_error("WebSocket connection timeout after %d ms", elapsed_ms);
    lws_context_destroy(ws_data->context);
    SAFE_FREE(ws_data->send_buffer);
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->queue_cond);
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "WebSocket connection timeout");
    return NULL;
  }

  log_info("WebSocket connection established (crypto: %s)", crypto_ctx ? "enabled" : "disabled");

  // Start service thread to process incoming messages
  ws_data->service_running = true;
  if (asciichat_thread_create(&ws_data->service_thread, websocket_service_thread, ws_data) != 0) {
    log_error("Failed to create WebSocket service thread");
    ws_data->service_running = false;
    lws_context_destroy(ws_data->context);
    SAFE_FREE(ws_data->send_buffer);
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->queue_cond);
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to create service thread");
    return NULL;
  }

  log_debug("WebSocket service thread started for client transport");

  return transport;
}

/**
 * @brief Create WebSocket server transport from existing connection
 *
 * Wraps an already-established libwebsockets connection (from server accept).
 * Used by websocket_server module to create transports for incoming clients.
 *
 * @param wsi Established libwebsockets connection (not owned by transport)
 * @param crypto_ctx Optional crypto context
 * @return Transport instance or NULL on error
 */
acip_transport_t *acip_websocket_server_transport_create(struct lws *wsi, crypto_context_t *crypto_ctx) {
  if (!wsi) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid wsi parameter");
    return NULL;
  }

  // Allocate transport structure
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(acip_transport_t), acip_transport_t *);
  if (!transport) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate WebSocket transport");
    return NULL;
  }

  // Allocate transport-specific data
  websocket_transport_data_t *ws_data =
      SAFE_CALLOC(1, sizeof(websocket_transport_data_t), websocket_transport_data_t *);
  if (!ws_data) {
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate WebSocket transport data");
    return NULL;
  }

  // Initialize receive queue
  ws_data->recv_queue = ringbuffer_create(sizeof(websocket_recv_msg_t), WEBSOCKET_RECV_QUEUE_SIZE);
  if (!ws_data->recv_queue) {
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to create receive queue");
    return NULL;
  }

  // Initialize send queue (for server-side transports)
  ws_data->send_queue = ringbuffer_create(sizeof(websocket_recv_msg_t), WEBSOCKET_SEND_QUEUE_SIZE);
  if (!ws_data->send_queue) {
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to create send queue");
    return NULL;
  }

  // Initialize synchronization primitives
  if (mutex_init(&ws_data->queue_mutex) != 0) {
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize queue mutex");
    return NULL;
  }

  if (cond_init(&ws_data->queue_cond) != 0) {
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize queue condition variable");
    return NULL;
  }

  if (mutex_init(&ws_data->state_mutex) != 0) {
    cond_destroy(&ws_data->queue_cond);
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize state mutex");
    return NULL;
  }

  // Allocate send buffer with LWS_PRE padding
  size_t initial_capacity = 4096 + LWS_PRE;
  ws_data->send_buffer = SAFE_MALLOC(initial_capacity, uint8_t *);
  if (!ws_data->send_buffer) {
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->queue_cond);
    mutex_destroy(&ws_data->queue_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate send buffer");
    return NULL;
  }
  ws_data->send_buffer_capacity = initial_capacity;

  // Store connection info (server-side: no context ownership, connection already established)
  ws_data->wsi = wsi;
  ws_data->context = lws_get_context(wsi); // Get context from wsi (not owned)
  ws_data->owns_context = false;           // Server owns context, not transport
  ws_data->is_connected = true;            // Already connected (server-side)
  log_debug("Server transport created: is_connected=true, wsi=%p", (void *)wsi);

  // Initialize transport
  transport->methods = &websocket_methods;
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = ws_data;

  log_info("Created WebSocket server transport (crypto: %s)", crypto_ctx ? "enabled" : "disabled");

  return transport;
}
