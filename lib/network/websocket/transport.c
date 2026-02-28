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
#include <ascii-chat/log/log.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/debug/named.h>
#include <libwebsockets.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Maximum incoming message queue size (frames received from peer)
 *
 * Power of 2 for ringbuffer optimization.
 * Used for both client (recv from server) and server (recv from client).
 * Each slot holds one message (up to 921KB). With 4096 slots, can buffer ~3.7GB.
 */
#define WEBSOCKET_MESSAGE_QUEUE_SIZE_INCOMING 4096

/**
 * @brief Maximum outgoing message queue size (frames to send to peer)
 *
 * Must be large enough to buffer video + audio simultaneously.
 * With ~50 audio packets/sec + ~30 video frames/sec = ~80 packets/sec,
 * a 4096-message queue allows ~50ms of buffering at full load.
 * Used for both client (send to server) and server (send to client).
 */
#define WEBSOCKET_MESSAGE_QUEUE_SIZE_OUTGOING 4096

// Legacy names for backward compatibility
#define WEBSOCKET_RECV_QUEUE_SIZE WEBSOCKET_MESSAGE_QUEUE_SIZE_INCOMING
#define WEBSOCKET_SEND_QUEUE_SIZE WEBSOCKET_MESSAGE_QUEUE_SIZE_OUTGOING

// Shared internal types (websocket_recv_msg_t, websocket_transport_data_t)
#include <ascii-chat/network/websocket/internal.h>

// Forward declaration for libwebsockets callback
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

// =============================================================================
// Service Thread (Client-side only)
// =============================================================================

/**
 * @brief Queue a buffer for deferred freeing to prevent use-after-free with compression
 *
 * Permessage-deflate compression holds buffer references asynchronously after lws_write().
 * This function queues buffers for later freeing instead of immediate deallocation.
 */
static void deferred_buffer_free(websocket_transport_data_t *ws_data, uint8_t *ptr, size_t size) {
  if (!ws_data || !ptr)
    return;

  pending_free_item_t item = {.ptr = ptr, .size = size};
  mutex_lock(&ws_data->pending_free_mutex);
  ringbuffer_write(ws_data->pending_free_queue, &item);
  mutex_unlock(&ws_data->pending_free_mutex);
}

/**
 * @brief Drain pending-free queue to actually free deferred buffers
 *
 * Called periodically from the service thread to free buffers that were
 * queued for deferred freeing after their associated compression operations complete.
 */
static void drain_pending_free_queue(websocket_transport_data_t *ws_data) {
  if (!ws_data)
    return;

  pending_free_item_t item;
  mutex_lock(&ws_data->pending_free_mutex);
  while (ringbuffer_read(ws_data->pending_free_queue, &item)) {
    buffer_pool_free(NULL, item.ptr, item.size);
  }
  mutex_unlock(&ws_data->pending_free_mutex);
}

/**
 * @brief Service thread that continuously processes libwebsockets events
 *
 * This thread is necessary for client-side transports to receive incoming messages.
 * It continuously calls lws_service() to process network events and trigger callbacks.
 */
static void *websocket_service_thread(void *arg) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)arg;

  log_info(">>> SERVICE_THREAD_START: owns_context=%d, wsi=%p, context=%p, send_queue=%p", ws_data->owns_context,
           (void *)ws_data->wsi, (void *)ws_data->context, (void *)ws_data->send_queue);

  int loop_count = 0;
  int total_messages_sent = 0;

  while (ws_data->service_running) {
    loop_count++;

    // CRITICAL: Check if we're destroying FIRST before doing anything else
    // This prevents accessing invalid pointers or contexts
    if (ws_data->is_destroying || !ws_data->context) {
      if (loop_count <= 10) {
        log_info("[LOOP %d] Destroying flag set or context NULL, exiting service thread", loop_count);
      }
      break;
    }

    // Periodically drain pending-free queue to free buffers deferred from compression
    // Do this at the start of each loop to ensure buffers are freed after compression completes
    if (loop_count % 10 == 0) {
      drain_pending_free_queue(ws_data);
    }

    // ALWAYS log first 10 loops to debug why queue checks aren't working
    if (loop_count <= 10) {
      log_info("[LOOP %d] owns_context=%d, wsi=%p, service_running=%d", loop_count, ws_data->owns_context,
               (void *)ws_data->wsi, ws_data->service_running);
    }

    // Check if THIS transport (client or server) has data queued to send
    // Note: we only check for CLIENT transports here (owns_context=true)
    // because SERVER transports are already handled by the SERVER_WRITEABLE callback.
    // Clients need explicit triggering via lws_callback_on_writable().
    if (ws_data->owns_context && ws_data->wsi) {
      if (loop_count <= 10) {
        log_info("[LOOP %d] CLIENT condition TRUE - checking queue", loop_count);
      }

      // Wait for connection to be established before sending
      // The CLIENT_ESTABLISHED callback sets is_connected = true
      // If we try to send before handshake completes, lws_write() fails
      mutex_lock(&ws_data->state_mutex);
      bool connected = ws_data->is_connected;
      mutex_unlock(&ws_data->state_mutex);

      if (!connected) {
        if (loop_count <= 10) {
          log_info("[LOOP %d] CLIENT not connected yet, skipping queue drain", loop_count);
        }

        // Check if connection attempt failed (CONNECTION_ERROR was called)
        // If so, break from service loop to avoid indefinite retry
        mutex_lock(&ws_data->state_mutex);
        bool failed = ws_data->connection_failed;
        mutex_unlock(&ws_data->state_mutex);

        if (failed) {
          log_info("[LOOP %d] Connection attempt failed, exiting service thread", loop_count);
          break;
        }

        // Sleep briefly to avoid busy-waiting
        platform_sleep_us(10000); // 10ms
      } else {
        // This is a CLIENT transport - request WRITEABLE callback to send queued messages
        // lws_write() must be called from within LWS callbacks, not from external threads
        int messages_sent = 0;

        mutex_lock(&ws_data->send_mutex);
        bool has_data = !ringbuffer_is_empty(ws_data->send_queue);

        if (loop_count <= 10) {
          log_info("[LOOP %d] Queue check: has_data=%d", loop_count, has_data);
        }

        if (has_data) {
          log_info(">>> SERVICE_THREAD: CLIENT queue has data, requesting CLIENT_WRITEABLE callback");
          // Request CLIENT_WRITEABLE callback instead of calling lws_write() directly
          // lws_write() MUST be called from within a callback context, not from external threads
          lws_callback_on_writable(ws_data->wsi);
          messages_sent++;
          total_messages_sent++;
        }
        mutex_unlock(&ws_data->send_mutex);

        if (messages_sent > 0) {
          log_info(">>> SERVICE_BATCH: requested %d writable callbacks (total: %d)", messages_sent,
                   total_messages_sent);
        }
      } // End of if (connected) block
    } else {
      if (loop_count <= 10) {
        log_info("[LOOP %d] CLIENT condition FALSE (owns_context=%d, wsi=%p)", loop_count, ws_data->owns_context,
                 (void *)ws_data->wsi);
      }
    }

    // Service libwebsockets (processes network events, triggers callbacks)
    // CRITICAL: Check context validity BEFORE calling lws_service
    // lws_service can crash if called on a partially destroyed or invalid context
    uint64_t service_start_ns = time_get_ns();
    int result = 0;

    // Guard against libwebsockets assertion failures
    // lws_service is called frequently and libwebsockets has internal assertions
    // that can crash the thread. We need to be defensive here.
    if (!ws_data->context || ws_data->is_destroying) {
      // Context is invalid or being destroyed, don't call lws_service
      if (loop_count <= 50) {
        log_info("[LOOP %d] Skipping lws_service: context=%p, destroying=%d",
                 loop_count, (void *)ws_data->context, ws_data->is_destroying);
      }
      platform_sleep_us(10000);  // Sleep 10ms to avoid busy-spin
    } else {
      // Call lws_service with valid context
      // Use 50ms timeout for client-side (needs responsive handshake)
      // but be aware that rapid consecutive calls can trigger libwebsockets assertions
      // in invalid states (e.g., trying to write before connection established)
      result = lws_service(ws_data->context, 50);
    }
    uint64_t service_end_ns = time_get_ns();

    if (loop_count <= 50) {
      char service_duration_str[32];
      time_pretty(service_end_ns - service_start_ns, -1, service_duration_str, sizeof(service_duration_str));
      log_info("[LOOP %d] lws_service() returned %d, duration %s", loop_count, result, service_duration_str);
    }

    if (result < 0) {
      log_fatal("🔴 lws_service error: %d at loop %d", result, loop_count);
      break;
    }

    // Check if connection is still alive
    if (loop_count <= 50 && ws_data->is_connected) {
      log_info("[LOOP %d] After lws_service: is_connected=true, wsi=%p", loop_count, (void *)ws_data->wsi);
    } else if (loop_count <= 50) {
      log_warn("[LOOP %d] After lws_service: is_connected=false, wsi=%p", loop_count, (void *)ws_data->wsi);
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
  case LWS_CALLBACK_CLIENT_ESTABLISHED: {
    uint64_t now_ns = time_get_ns();
    log_fatal("🟢🟢🟢 WebSocket CLIENT_ESTABLISHED! wsi=%p, ws_data=%p, timestamp=%llu", (void *)wsi, (void *)ws_data,
              (unsigned long long)now_ns);
    if (ws_data) {
      mutex_lock(&ws_data->state_mutex);
      log_fatal("    [ESTABLISHED] Setting is_connected=true (was %d)", ws_data->is_connected);
      ws_data->is_connected = true;
      cond_signal(&ws_data->state_cond);
      mutex_unlock(&ws_data->state_mutex);
      log_fatal("    [ESTABLISHED] State updated, wsi=%p ready for send/recv", (void *)wsi);
    }
    break;
  }

  case LWS_CALLBACK_CLIENT_RECEIVE: {
    // Received data from server - may be fragmented for large messages
    uint64_t now_ns = time_get_ns();
    if (!ws_data || !in || len == 0) {
      log_debug("CLIENT_RECEIVE: ws_data=%p, in=%p, len=%zu - skipping", (void *)ws_data, in, len);
      break;
    }

    // DIAGNOSTIC: Disable fragment detection to isolate if that triggers assertion
    // libwebsockets internal calls to detect fragments might corrupt state
    bool is_first = 1; // Assume all fragments are first/final
    bool is_final = 1;

    log_info("🟡 LWS_CALLBACK_CLIENT_RECEIVE: %zu bytes (first=%d, final=%d), wsi=%p, timestamp=%llu", len, is_first,
             is_final, (void *)wsi, (unsigned long long)now_ns);

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

    mutex_lock(&ws_data->recv_mutex);
    bool success = ringbuffer_write(ws_data->recv_queue, &msg);
    if (!success) {
      // Queue is full - drop the fragment and log warning
      log_warn("WebSocket receive queue full - dropping fragment (len=%zu, first=%d, final=%d)", len, is_first,
               is_final);
      buffer_pool_free(NULL, msg.data, msg.len);
      mutex_unlock(&ws_data->recv_mutex);
      break;
    }

    // Signal waiting recv() call that a fragment is available
    cond_signal(&ws_data->recv_cond);
    mutex_unlock(&ws_data->recv_mutex);
    break;
  }

  case LWS_CALLBACK_CLIENT_CLOSED:
  case LWS_CALLBACK_CLOSED: {
    uint64_t now_ns = time_get_ns();
    log_fatal("🔴🔴🔴 WebSocket connection CLOSED! reason=%d, wsi=%p, ws_data=%p, is_connected=%d, timestamp=%llu",
              reason, (void *)wsi, (void *)ws_data, ws_data ? ws_data->is_connected : -1, (unsigned long long)now_ns);
    if (ws_data) {
      mutex_lock(&ws_data->state_mutex);
      log_fatal("    [CLOSE] Setting is_connected=false (was %d)", ws_data->is_connected);
      ws_data->is_connected = false;
      mutex_unlock(&ws_data->state_mutex);

      // Wake any blocking recv() calls
      cond_broadcast(&ws_data->recv_cond);
    }
    break;
  }

  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
    uint64_t now_ns = time_get_ns();
    log_fatal("🔴🔴🔴 WebSocket CONNECTION ERROR! reason=%d, error=%s, wsi=%p, ws_data=%p, timestamp=%llu", reason,
              in ? (const char *)in : "unknown", (void *)wsi, (void *)ws_data, (unsigned long long)now_ns);
    if (ws_data) {
      mutex_lock(&ws_data->state_mutex);
      ws_data->is_connected = false;
      ws_data->connection_failed = true; // Signal service thread to exit
      cond_signal(&ws_data->state_cond); // Wake anyone waiting on connection
      mutex_unlock(&ws_data->state_mutex);

      // Wake any blocking recv() calls
      cond_broadcast(&ws_data->recv_cond);
    }
    break;
  }

  case LWS_CALLBACK_CLIENT_WRITEABLE: {
    // Socket is writable - process queued messages with FRAGMENTATION
    // CRITICAL FIX: Fragment large messages into ~4KB chunks to avoid internal buffering
    // libwebsockets #464: Sending messages > rx_buffer_size causes ultra-slow buffering
    uint64_t now_ns = time_get_ns();
    log_info("🟡 LWS_CALLBACK_CLIENT_WRITEABLE FIRED for wsi=%p, ws_data=%p, is_connected=%d, timestamp=%llu",
             (void *)wsi, (void *)ws_data, ws_data ? ws_data->is_connected : -1, (unsigned long long)now_ns);
    if (!ws_data) {
      log_warn("    [CLIENT_WRITEABLE] ws_data is NULL, breaking");
      break;
    }

    // CRITICAL: Don't try to write if not fully connected
    // libwebsockets can hit assertion if we write before connection establishment completes
    mutex_lock(&ws_data->state_mutex);
    bool connected = ws_data->is_connected;
    mutex_unlock(&ws_data->state_mutex);

    if (!connected) {
      log_debug("    [CLIENT_WRITEABLE] Skipping write - not connected yet");
      break;
    }

    const size_t CHUNK_SIZE = 4096;  // Fragment messages into 4KB chunks
    websocket_recv_msg_t msg;
    int message_count = 0;

    mutex_lock(&ws_data->send_mutex);

    // Process one message at a time with chunking
    if (ringbuffer_peek(ws_data->send_queue, &msg)) {
      ringbuffer_read(ws_data->send_queue, &msg);

      log_debug("WebSocket CLIENT_WRITEABLE: sending queued %zu bytes in fragments (msg %d)", msg.len, message_count + 1);

      // Fragment the message into ~4KB chunks
      size_t offset = 0;
      while (offset < msg.len && !lws_send_pipe_choked(ws_data->wsi)) {
        size_t chunk_size = (msg.len - offset > CHUNK_SIZE) ? CHUNK_SIZE : (msg.len - offset);
        bool is_first = (offset == 0);
        bool is_final = (offset + chunk_size >= msg.len);

        // Use lws_write_ws_flags to get correct flags for fragment sequence
        // First fragment uses LWS_WRITE_BINARY, subsequent use LWS_WRITE_CONTINUATION
        int write_flags = lws_write_ws_flags(LWS_WRITE_BINARY, is_first, is_final);

        log_debug("  Fragment: offset=%zu, chunk=%zu, is_first=%d, is_final=%d, flags=%d",
                  offset, chunk_size, is_first, is_final, write_flags);

        // CRITICAL: Keep send_mutex locked during all lws_write() calls
        // libwebsockets is not thread-safe for concurrent writes on same connection
        int written = lws_write(ws_data->wsi, msg.data + LWS_PRE + offset, chunk_size, write_flags);

        if (written < 0) {
          log_error("WebSocket fragment write failed at offset %zu (chunk %zu bytes)", offset, chunk_size);
          // Keep what we sent, but don't try to send more
          break;
        }

        if ((size_t)written != chunk_size) {
          log_warn("WebSocket partial write: %d/%zu bytes at offset %zu", written, chunk_size, offset);
        }

        offset += chunk_size;
      }

      // Only free AFTER entire message is sent
      if (offset >= msg.len) {
        // Message fully sent - queue for deferred free
        deferred_buffer_free(ws_data, msg.data, LWS_PRE + msg.len);
        message_count++;
      } else if (offset > 0) {
        // Partial send - re-queue the remainder for next writeable event
        // Create a new message with the unsent portion
        websocket_recv_msg_t remainder;
        size_t remaining_len = msg.len - offset;
        remainder.data = msg.data;  // Keep same buffer
        remainder.len = remaining_len;
        remainder.first = 0;  // Mark as continuation
        remainder.final = msg.final;

        // Since we already read this message, we need to queue the remainder back
        // Insert at front of queue by re-reading and writing remainder
        ringbuffer_write(ws_data->send_queue, &remainder);

        log_info("  Re-queued %zu bytes remainder (sent %zu of %zu)", remaining_len, offset, msg.len);
      } else {
        // No bytes sent - buffer is choked, re-queue this message
        ringbuffer_write(ws_data->send_queue, &msg);
      }
    }

    mutex_unlock(&ws_data->send_mutex);

    // Request another callback if more messages are queued
    // Must request even if pipe was choked so we drain queue once TCP buffer drains
    mutex_lock(&ws_data->send_mutex);
    bool has_more = ringbuffer_peek(ws_data->send_queue, &msg);
    mutex_unlock(&ws_data->send_mutex);

    if (has_more) {
      lws_callback_on_writable(ws_data->wsi);
    }
    break;
  }

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

  // For server-side transports (owns_context=false), the connection is already established
  // For client-side transports (owns_context=true), wait for connection to be established
  // before sending, instead of failing immediately. This allows sends to work even if
  // they're called before the service thread has fully established the connection.
  if (ws_data->owns_context) {
    // Wait for connection with same timeout as recv()
    const uint64_t CONNECT_WAIT_TIMEOUT_NS = 100 * 1000000ULL;  // 100ms per wait iteration
    uint64_t wait_start_ns = time_get_ns();

    mutex_lock(&ws_data->state_mutex);
    while (!ws_data->is_connected && !ws_data->connection_failed) {
      uint64_t elapsed_ns = time_get_ns() - wait_start_ns;
      if (elapsed_ns > 30 * 1000000000ULL) {  // 30 second total timeout
        log_error("[WEBSOCKET_SEND] Connection timeout after 30 seconds, cannot send");
        mutex_unlock(&ws_data->state_mutex);
        return SET_ERRNO(ERROR_NETWORK, "WebSocket connection timeout");
      }
      // Wait for connection with timeout
      cond_timedwait(&ws_data->state_cond, &ws_data->state_mutex, CONNECT_WAIT_TIMEOUT_NS);
    }
    bool connected = ws_data->is_connected;
    bool connection_failed = ws_data->connection_failed;
    mutex_unlock(&ws_data->state_mutex);

    if (connection_failed && !connected) {
      log_error("[WEBSOCKET_SEND] Connection failed during establishment");
      return SET_ERRNO(ERROR_NETWORK, "WebSocket connection failed");
    }

    log_dev_every(1000000, "websocket_send (client): is_connected=%d, wsi=%p, send_len=%zu", connected,
                  (void *)ws_data->wsi, len);
  } else {
    log_info("[WEBSOCKET_SEND_SERVER] ★★★ Server transport send: wsi=%p, len=%zu (bypassing is_connected check)",
             (void *)ws_data->wsi, len);
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
    // IMPORTANT: Allocate with LWS_PRE padding because lws_write() needs to write
    // the WebSocket frame header backwards into the LWS_PRE region
    // Use buffer_pool instead of SAFE_MALLOC to avoid use-after-free with
    // permessage-deflate compression (same issue as client-side sends)
    websocket_recv_msg_t msg;
    size_t buffer_size = LWS_PRE + send_len;
    msg.data = buffer_pool_alloc(NULL, buffer_size);
    if (!msg.data) {
      SAFE_FREE(send_buffer);
      if (encrypted_packet)
        buffer_pool_free(NULL, encrypted_packet, send_len);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate send queue buffer");
    }
    // Copy data AFTER the LWS_PRE region
    memcpy(msg.data + LWS_PRE, send_data, send_len);
    msg.len = send_len;
    msg.first = 1;
    msg.final = 1;

    mutex_lock(&ws_data->send_mutex);
    bool success = ringbuffer_write(ws_data->send_queue, &msg);

    if (!success) {
      mutex_unlock(&ws_data->send_mutex);
      log_error("WebSocket server send queue FULL - cannot queue %zu byte message for wsi=%p", send_len,
                (void *)ws_data->wsi);
      buffer_pool_free(NULL, msg.data, buffer_size);
      SAFE_FREE(send_buffer);
      if (encrypted_packet)
        buffer_pool_free(NULL, encrypted_packet, encrypted_packet_size);
      return SET_ERRNO(ERROR_NETWORK, "Send queue full (cannot queue %zu bytes)", send_len);
    }
    mutex_unlock(&ws_data->send_mutex);

    log_debug(">>> SERVER FRAME QUEUED: %zu bytes for wsi=%p", send_len, (void *)ws_data->wsi);

    // Notify libwebsockets that there's data to send - triggers LWS_CALLBACK_SERVER_WRITEABLE
    // This is essential! Without this, the queued frames never get transmitted.
    lws_callback_on_writable(ws_data->wsi);
    log_debug(">>> REQUESTED SERVER_WRITEABLE CALLBACK for wsi=%p", (void *)ws_data->wsi);

    SAFE_FREE(send_buffer);
    if (encrypted_packet)
      buffer_pool_free(NULL, encrypted_packet, encrypted_packet_size);
    return ASCIICHAT_OK;
  }

  // Client-side: queue entire message for service thread to send
  // Avoid fragmentation race conditions by sending atomic messages from service thread
  // libwebsockets handles automatic fragmentation internally when needed
  // Allocate with LWS_PRE padding because lws_write() needs to write
  // the WebSocket frame header backwards into the LWS_PRE region
  // Use buffer_pool instead of SAFE_MALLOC to avoid use-after-free with permessage-deflate
  // compression. libwebsockets may buffer this data asynchronously after lws_write() returns,
  // so we can't free it immediately. buffer_pool_free() safely returns it to the pool instead
  // of deallocating, preventing the use-after-free race condition with the compression layer.
  websocket_recv_msg_t msg;
  size_t buffer_size = LWS_PRE + send_len;
  msg.data = buffer_pool_alloc(NULL, buffer_size);
  if (!msg.data) {
    SAFE_FREE(send_buffer);
    if (encrypted_packet)
      buffer_pool_free(NULL, encrypted_packet, encrypted_packet_size);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate client send queue buffer");
  }
  // Copy data AFTER the LWS_PRE region
  memcpy(msg.data + LWS_PRE, send_data, send_len);
  msg.len = send_len;
  msg.first = 1;
  msg.final = 1;

  mutex_lock(&ws_data->send_mutex);
  bool success = ringbuffer_write(ws_data->send_queue, &msg);

  if (!success) {
    mutex_unlock(&ws_data->send_mutex);
    log_error("WebSocket client send queue FULL - cannot queue %zu byte message for wsi=%p", send_len,
              (void *)ws_data->wsi);
    buffer_pool_free(NULL, msg.data, buffer_size);
    SAFE_FREE(send_buffer);
    if (encrypted_packet)
      buffer_pool_free(NULL, encrypted_packet, encrypted_packet_size);
    return SET_ERRNO(ERROR_NETWORK, "Client send queue full (cannot queue %zu bytes)", send_len);
  }
  mutex_unlock(&ws_data->send_mutex);

  log_debug(">>> QUEUED CLIENT MESSAGE: %zu bytes queued at %p for service thread (wsi=%p)", send_len,
            (void *)ws_data->wsi, (void *)ws_data->wsi);

  log_dev_every(1000000, "WebSocket client: queued %zu bytes for service thread to send", send_len);
  SAFE_FREE(send_buffer);
  if (encrypted_packet)
    buffer_pool_free(NULL, encrypted_packet, encrypted_packet_size);
  return ASCIICHAT_OK;
}

static asciichat_error_t websocket_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                        void **out_allocated_buffer) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  // Wait for connection to be established (service thread may still be connecting)
  // The service thread creates the transport immediately and returns, allowing the main
  // thread to respond to input, but the connection may not be established yet.
  // We need to wait for it with a timeout to avoid hanging forever.
  const uint64_t CONNECT_WAIT_TIMEOUT_NS = 100 * 1000000ULL;  // 100ms per wait iteration
  uint64_t wait_start_ns = time_get_ns();

  mutex_lock(&ws_data->state_mutex);
  while (!ws_data->is_connected && !ws_data->connection_failed) {
    uint64_t elapsed_ns = time_get_ns() - wait_start_ns;
    if (elapsed_ns > 30 * 1000000000ULL) {  // 30 second total timeout
      log_error("🔴 WEBSOCKET_RECV: Connection timeout after 30 seconds, connection_failed=%d",
                ws_data->connection_failed);
      mutex_unlock(&ws_data->state_mutex);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket connection timeout");
    }
    // Wait for connection with timeout
    cond_timedwait(&ws_data->state_cond, &ws_data->state_mutex, CONNECT_WAIT_TIMEOUT_NS);
  }
  bool connected = ws_data->is_connected;
  bool connection_failed = ws_data->connection_failed;
  mutex_unlock(&ws_data->state_mutex);

  if (connection_failed && !connected) {
    log_error("🔴 WEBSOCKET_RECV: Connection failed during establishment");
    return SET_ERRNO(ERROR_NETWORK, "WebSocket connection failed");
  }

  mutex_lock(&ws_data->recv_mutex);

  // Even if connection is closed, we should try to deliver any buffered data
  // Check if there's data in the queue
  bool has_queued_data = !ringbuffer_is_empty(ws_data->recv_queue);

  if (!connected && !has_queued_data && ws_data->partial_size == 0) {
    // Only fail if connection is closed AND no buffered data AND no leftover from previous call
    uint64_t now_ns = time_get_ns();
    log_fatal("🔴 WEBSOCKET_RECV: Connection closed! connected=%d, has_queued_data=%d, partial_size=%zu, wsi=%p, timestamp=%llu",
              connected, has_queued_data, ws_data->partial_size, (void *)ws_data->wsi, (unsigned long long)now_ns);
    mutex_unlock(&ws_data->recv_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Connection closed");
  }

  // Reassemble fragmented WebSocket messages with SHORT timeout
  // We queue each fragment from the LWS callback with first/final flags.
  // Key insight: if we wait too long for final fragment, connection times out.
  // Return partial messages quickly (500ms) to avoid blocking handler thread.
  //
  // CRITICAL FIX: Use persistent partial_buffer to handle packet boundaries
  // that don't align with WebSocket frame boundaries. This prevents data loss
  // when a single WebSocket message contains multiple ACIP packets.

  uint8_t *assembled_buffer = NULL;
  size_t assembled_size = 0;
  size_t assembled_capacity = 0;
  int fragment_count = 0;

  // Start with leftover data from previous recv() call if available
  if (ws_data->partial_size > 0) {
    log_info("[WS_REASSEMBLE] Starting with leftover data from previous packet: %zu bytes", ws_data->partial_size);
    assembled_buffer = ws_data->partial_buffer;
    assembled_size = ws_data->partial_size;
    assembled_capacity = ws_data->partial_capacity;

    // Clear the partial buffer state so next call starts fresh if no more leftovers
    ws_data->partial_buffer = NULL;
    ws_data->partial_size = 0;
    ws_data->partial_capacity = 0;
  }

  while (true) {
    // Wait for fragment if queue is empty
    while (ringbuffer_is_empty(ws_data->recv_queue)) {
      // Don't timeout waiting for fragments. If the connection is dead, libwebsockets will
      // set is_connected=false and we'll detect it below. Arbitrary timeouts just cause
      // unnecessary disconnections while data is actually being transmitted.

      // Check connection state - but don't fail immediately if connection closes
      // while reassembling. Instead, return what we have so the handler can process it.
      // This prevents losing buffered data due to connection timeouts.
      mutex_lock(&ws_data->state_mutex);
      bool still_connected = ws_data->is_connected;
      mutex_unlock(&ws_data->state_mutex);

      if (!still_connected && assembled_size > 0) {
        // Connection closed but we have partial data - return it
        log_info("[WS_REASSEMBLE] Connection closed mid-reassembly, returning %zu bytes received so far",
                 assembled_size);
        *buffer = assembled_buffer;
        *out_len = assembled_size;
        *out_allocated_buffer = assembled_buffer;
        mutex_unlock(&ws_data->recv_mutex);
        return ASCIICHAT_OK;
      }

      if (!still_connected && assembled_size == 0) {
        // Connection closed and no data yet
        if (assembled_buffer) {
          buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
        }
        mutex_unlock(&ws_data->recv_mutex);
        return SET_ERRNO(ERROR_NETWORK, "Connection closed");
      }

      // Wait for next fragment with 1ms timeout to allow signals to interrupt
      cond_timedwait(&ws_data->recv_cond, &ws_data->recv_mutex, 1 * 1000000ULL);
    }

    // Read next fragment from queue
    websocket_recv_msg_t frag;
    bool success = ringbuffer_read(ws_data->recv_queue, &frag);
    if (!success) {
      if (assembled_buffer) {
        buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
      }
      mutex_unlock(&ws_data->recv_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Failed to read fragment from queue");
    }

    fragment_count++;
    if (frag.len > 100 || fragment_count == 1) {
      log_info("[WS_REASSEMBLE] Fragment #%d: %zu bytes, first=%d, final=%d, assembled_so_far=%zu", fragment_count,
               frag.len, frag.first, frag.final, assembled_size);
    } else {
      log_debug("[WS_REASSEMBLE] Fragment #%d: %zu bytes, first=%d, final=%d", fragment_count, frag.len, frag.first,
                frag.final);
    }

    // Sanity check: first fragment must have first=1, continuations must have first=0
    if (assembled_size == 0 && !frag.first) {
      log_error("[WS_REASSEMBLE] ERROR: Expected first=1 for first fragment, got first=%d", frag.first);
      buffer_pool_free(NULL, frag.data, frag.len);
      if (assembled_buffer) {
        buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
      }
      mutex_unlock(&ws_data->recv_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Protocol error: continuation fragment without first fragment");
    }

    // Grow assembled buffer if needed
    size_t required_size = assembled_size + frag.len;
    if (required_size > assembled_capacity) {
      // Start with 8KB, grow by 1.5x, but cap at 4MB to prevent unbounded allocation
      const size_t MAX_REASSEMBLY_SIZE = (4 * 1024 * 1024); // 4MB limit

      size_t new_capacity = (assembled_capacity == 0) ? 8192 : (assembled_capacity * 3 / 2);
      if (new_capacity < required_size) {
        new_capacity = required_size;
      }

      // Enforce maximum reassembly buffer size
      if (new_capacity > MAX_REASSEMBLY_SIZE) {
        log_error("[WS_REASSEMBLE] Frame too large: need %zu bytes, max allowed is %zu", new_capacity,
                  MAX_REASSEMBLY_SIZE);
        buffer_pool_free(NULL, frag.data, frag.len);
        if (assembled_buffer) {
          buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
        }
        mutex_unlock(&ws_data->recv_mutex);
        return SET_ERRNO(ERROR_NETWORK, "WebSocket frame exceeds maximum size (4MB)");
      }

      uint8_t *new_buffer = buffer_pool_alloc(NULL, new_capacity);
      if (!new_buffer) {
        log_error("[WS_REASSEMBLE] Failed to allocate reassembly buffer (%zu bytes)", new_capacity);
        buffer_pool_free(NULL, frag.data, frag.len);
        if (assembled_buffer) {
          buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
        }
        mutex_unlock(&ws_data->recv_mutex);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate fragment reassembly buffer");
      }

      // Copy existing data to new buffer
      if (assembled_size > 0) {
        memcpy(new_buffer, assembled_buffer, assembled_size);
      }

      // Free old buffer
      if (assembled_buffer) {
        buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
      }

      assembled_buffer = new_buffer;
      assembled_capacity = new_capacity;
    }

    // Append fragment data with bounds checking
    if (frag.len > 0 && frag.data) {
      // Safety check: ensure we don't overflow the buffer
      if (assembled_size + frag.len > assembled_capacity) {
        log_error("[WS_REASSEMBLE] CRITICAL: Buffer overflow detected! assembled_size=%zu, frag.len=%zu, capacity=%zu",
                  assembled_size, frag.len, assembled_capacity);
        buffer_pool_free(NULL, frag.data, frag.len);
        if (assembled_buffer) {
          buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
        }
        mutex_unlock(&ws_data->recv_mutex);
        return SET_ERRNO(ERROR_MEMORY, "Fragment reassembly buffer overflow");
      }

      memcpy(assembled_buffer + assembled_size, frag.data, frag.len);
      assembled_size += frag.len;
    }

    // Free fragment data after copying (allocated in LWS callback with buffer_pool_alloc)
    if (frag.data) {
      // Use the fragment length that was stored when we allocated it
      // This must match the size passed to buffer_pool_alloc in the LWS callback
      buffer_pool_free(NULL, frag.data, frag.len);
      frag.data = NULL; // Prevent accidental double-free
    }

    // Try to detect packet boundary using protocol structure
    // ACIP packet header: magic(8) + type(2) + length(4) + crc(4) + client_id(4) = 22 bytes
    // The length field (bytes 10-13) tells us the payload size
    if (assembled_size >= 14) { // Need at least 14 bytes to read length field at offset 10
      const uint8_t *data = assembled_buffer;

      // Parse length field at offset 10 (4 bytes, big-endian)
      uint32_t msg_payload_len = (data[10] << 24) | (data[11] << 16) | (data[12] << 8) | data[13];

      // Sanity check: payload length should be reasonable (< 5MB)
      if (msg_payload_len > 0 && msg_payload_len <= 5 * 1024 * 1024) {
        // Total packet size = header(14 bytes up to and including length field) + payload
        // Actually full header is 22 bytes but we can work with just the length field
        const size_t HEADER_SIZE = 22;
        size_t expected_size = HEADER_SIZE + msg_payload_len;

        if (assembled_size >= expected_size) {
          // Complete packet assembled based on header length field
          log_info("[WS_REASSEMBLE] Complete message by length field: %zu bytes in %d fragments (payload=%u)",
                   expected_size, fragment_count, msg_payload_len);

          // Check for leftover data after this packet
          size_t leftover_size = assembled_size - expected_size;
          if (leftover_size > 0) {
            // Save leftover data for next recv() call
            log_info("[WS_REASSEMBLE] Saving %zu bytes leftover for next recv() call", leftover_size);

            // Create buffer for leftover (or reuse if we can)
            uint8_t *leftover_buffer = buffer_pool_alloc(NULL, leftover_size);
            if (leftover_buffer) {
              memcpy(leftover_buffer, assembled_buffer + expected_size, leftover_size);
              ws_data->partial_buffer = leftover_buffer;
              ws_data->partial_size = leftover_size;
              ws_data->partial_capacity = leftover_size;
            } else {
              log_error("[WS_REASSEMBLE] Failed to allocate leftover buffer (%zu bytes)", leftover_size);
              // Fall through - return the packet anyway, we'll lose the leftover
            }
          }

          // Create a new buffer with just the complete packet (not the leftover)
          uint8_t *packet_buffer = buffer_pool_alloc(NULL, expected_size);
          if (!packet_buffer) {
            log_error("[WS_REASSEMBLE] Failed to allocate packet buffer (%zu bytes)", expected_size);
            buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
            mutex_unlock(&ws_data->recv_mutex);
            return SET_ERRNO(ERROR_MEMORY, "Failed to allocate packet buffer");
          }

          memcpy(packet_buffer, assembled_buffer, expected_size);
          buffer_pool_free(NULL, assembled_buffer, assembled_capacity);

          *buffer = packet_buffer;
          *out_len = expected_size;
          *out_allocated_buffer = packet_buffer;
          mutex_unlock(&ws_data->recv_mutex);
          return ASCIICHAT_OK;
        }
        // Need more fragments to complete this message
      }
    }

    // Fallback: check if we have the final WebSocket fragment
    // BUT: Only return if we have a COMPLETE ACIP packet!
    // ACIP packets can span multiple WebSocket frames, so final=true doesn't mean
    // we have the complete ACIP packet - we must verify using the ACIP header's length field
    if (frag.final) {
      // Check if this is actually a complete ACIP packet by validating the length field
      if (assembled_size >= 14) {  // Minimum to read ACIP length field
        const uint8_t *data = (const uint8_t *)assembled_buffer;
        uint32_t msg_payload_len = (data[10] << 24) | (data[11] << 16) | (data[12] << 8) | data[13];
        const size_t HEADER_SIZE = 22;
        size_t expected_size = HEADER_SIZE + msg_payload_len;

        if (assembled_size >= expected_size) {
          // We have a complete ACIP packet
          log_info("[WS_REASSEMBLE] Complete ACIP packet by WebSocket final fragment: %zu bytes in %d fragments",
                   expected_size, fragment_count);

          // Check for leftover data after this packet
          size_t leftover_size = assembled_size - expected_size;
          if (leftover_size > 0) {
            // Save leftover data for next recv() call
            log_info("[WS_REASSEMBLE] Saving %zu bytes leftover for next recv() call", leftover_size);

            // Create buffer for leftover
            uint8_t *leftover_buffer = buffer_pool_alloc(NULL, leftover_size);
            if (leftover_buffer) {
              memcpy(leftover_buffer, assembled_buffer + expected_size, leftover_size);
              ws_data->partial_buffer = leftover_buffer;
              ws_data->partial_size = leftover_size;
              ws_data->partial_capacity = leftover_size;
            } else {
              log_error("[WS_REASSEMBLE] Failed to allocate leftover buffer (%zu bytes)", leftover_size);
            }
          }

          // Create a new buffer with just the complete packet
          uint8_t *packet_buffer = buffer_pool_alloc(NULL, expected_size);
          if (!packet_buffer) {
            log_error("[WS_REASSEMBLE] Failed to allocate packet buffer (%zu bytes)", expected_size);
            buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
            mutex_unlock(&ws_data->recv_mutex);
            return SET_ERRNO(ERROR_MEMORY, "Failed to allocate packet buffer");
          }

          memcpy(packet_buffer, assembled_buffer, expected_size);
          buffer_pool_free(NULL, assembled_buffer, assembled_capacity);

          *buffer = packet_buffer;
          *out_len = expected_size;
          *out_allocated_buffer = packet_buffer;
          mutex_unlock(&ws_data->recv_mutex);
          return ASCIICHAT_OK;
        } else {
          // ACIP packet is incomplete even though WebSocket frame is final
          // This shouldn't happen - WebSocket is delivering corrupt data
          log_error("[WS_REASSEMBLE] ERROR: WebSocket final fragment but incomplete ACIP packet (have %zu, need %zu)",
                    assembled_size, expected_size);
          buffer_pool_free(NULL, assembled_buffer, assembled_capacity);
          mutex_unlock(&ws_data->recv_mutex);
          return SET_ERRNO(ERROR_NETWORK, "WebSocket final fragment but incomplete ACIP packet");
        }
      }
    }

    // More fragments coming, continue reassembling
  }

  // Unreachable unless while loop breaks without returning
  mutex_unlock(&ws_data->recv_mutex);
  return SET_ERRNO(ERROR_NETWORK, "Unexpected exit from fragment reassembly loop");
}

static asciichat_error_t websocket_close(acip_transport_t *transport) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;
  uint64_t now_ns = time_get_ns();

  // CRITICAL: Stop service thread BEFORE calling lws_close_reason()
  // Mark as destroying FIRST to signal service thread to stop immediately
  // This prevents service thread from trying to call lws_service on a destroying/destroyed context
  atomic_store(&ws_data->is_destroying, true);

  if (ws_data->service_running) {
    log_debug("[websocket_close] Stopping service thread to prevent deadlock during lws_close_reason()");
    ws_data->service_running = false;
    // Give service thread time to notice is_destroying flag
    platform_sleep_us(10000);  // 10ms
    asciichat_thread_join(&ws_data->service_thread, NULL);
    log_debug("[websocket_close] Service thread stopped");
  }

  mutex_lock(&ws_data->state_mutex);

  if (!ws_data->is_connected) {
    mutex_unlock(&ws_data->state_mutex);
    log_info("websocket_close: Already closed (is_connected=false), wsi=%p, timestamp=%llu", (void *)ws_data->wsi,
             (unsigned long long)now_ns);
    return ASCIICHAT_OK; // Already closed
  }

  log_fatal("🔴 websocket_close called! Setting is_connected=false, wsi=%p, timestamp=%llu", (void *)ws_data->wsi,
            (unsigned long long)now_ns);
  ws_data->is_connected = false;
  mutex_unlock(&ws_data->state_mutex);

  // Close WebSocket connection
  // Safe to call lws_close_reason() now - service thread is stopped, so no callback contention
  if (ws_data->wsi) {
    log_fatal("    [websocket_close] Calling lws_close_reason for wsi=%p", (void *)ws_data->wsi);
    lws_close_reason(ws_data->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
    log_fatal("    [websocket_close] lws_close_reason returned");
  }

  // Wake any blocking recv() calls and send() waits
  // CRITICAL: Signal both recv_cond and state_cond to unblock all waiting threads
  // before the transport is destroyed. This prevents use-after-free when threads
  // wake from cond_timedwait() after the structure has been freed.
  cond_broadcast(&ws_data->recv_cond);
  cond_broadcast(&ws_data->state_cond);

  log_info("WebSocket transport closed, wsi=%p", (void *)ws_data->wsi);
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

  // Mark transport as destroying and broadcast condition variables
  // This signals all waiting threads to wake and exit naturally.
  log_debug("[WEBSOCKET_DESTROY] Marking transport as destroying");
  atomic_store(&ws_data->is_destroying, true);

  // Broadcast all condition variables to wake waiting threads so they can check
  // the is_destroying flag and exit gracefully
  cond_broadcast(&ws_data->state_cond);
  cond_broadcast(&ws_data->recv_cond);

  // Give threads a brief moment to detect the flag and exit
  // Threads should check is_destroying before acquiring mutexes
  platform_sleep_us(100 * US_PER_MS_INT);  // 100ms for threads to detect flag

  // Stop service thread (client-side only)
  if (ws_data->service_running) {
    log_debug("Stopping WebSocket service thread");
    ws_data->service_running = false;
    asciichat_thread_join(&ws_data->service_thread, NULL);
    log_debug("WebSocket service thread stopped");
  }

  // Close WebSocket connection gracefully before destroying context
  // This prevents libwebsockets from triggering callbacks on a dead context
  if (ws_data->wsi) {
    log_debug("Closing WebSocket connection gracefully");
    // Use lws_wsi_close to trigger a clean close sequence
    lws_wsi_close(ws_data->wsi, 1);
    // Clear wsi pointer after closing to prevent double-close
    ws_data->wsi = NULL;
  }

  // Give libwebsockets a moment to process the close handshake
  // This ensures any pending callbacks complete before we destroy the context
  platform_sleep_us(50 * US_PER_MS_INT);  // 50ms for close handshake

  // Destroy WebSocket context (only if we own it - client transports only)
  if (ws_data->context && ws_data->owns_context) {
    log_debug("Destroying WebSocket context");
    lws_context_destroy(ws_data->context);
    ws_data->context = NULL;
  }

  // Clear receive queue and free buffered messages
  if (ws_data->recv_queue) {
    mutex_lock(&ws_data->recv_mutex);

    websocket_recv_msg_t msg;
    while (ringbuffer_read(ws_data->recv_queue, &msg)) {
      if (msg.data) {
        buffer_pool_free(NULL, msg.data, msg.len);
      }
    }

    mutex_unlock(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ws_data->recv_queue = NULL;

    if (ws_data->send_queue) {
      // Drain send queue before destroying to free allocated message data
      // Use buffer_pool_free to match buffer_pool_alloc used for send messages
      websocket_recv_msg_t msg;
      while (ringbuffer_read(ws_data->send_queue, &msg)) {
        if (msg.data) {
          buffer_pool_free(NULL, msg.data, LWS_PRE + msg.len);
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

  // Free partial buffer (leftover data from previous recv())
  if (ws_data->partial_buffer) {
    buffer_pool_free(NULL, ws_data->partial_buffer, ws_data->partial_capacity);
    ws_data->partial_buffer = NULL;
    ws_data->partial_size = 0;
    ws_data->partial_capacity = 0;
  }

  // Destroy synchronization primitives
  cond_destroy(&ws_data->state_cond);
  mutex_destroy(&ws_data->state_mutex);
  cond_destroy(&ws_data->recv_cond);
  mutex_destroy(&ws_data->recv_mutex);
  mutex_destroy(&ws_data->send_mutex);
  mutex_destroy(&ws_data->pending_free_mutex);

  // Destroy pending-free queue
  if (ws_data->pending_free_queue) {
    ringbuffer_destroy(ws_data->pending_free_queue);
    ws_data->pending_free_queue = NULL;
  }

  // Deregister websocket implementation data
  NAMED_UNREGISTER(ws_data);

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
acip_transport_t *acip_websocket_client_transport_create(const char *name, const char *url,
                                                         crypto_context_t *crypto_ctx) {
  if (!name) {
    SET_ERRNO(ERROR_INVALID_STATE, "Transport name is required");
    return NULL;
  }

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

  // Initialize destruction state flag (CALLOC zero-initializes, but explicit for clarity)
  atomic_store(&ws_data->is_destroying, false);

  // Create receive queue
  ws_data->recv_queue = ringbuffer_create(sizeof(websocket_recv_msg_t), WEBSOCKET_RECV_QUEUE_SIZE);
  if (!ws_data->recv_queue) {
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to create receive queue");
    return NULL;
  }

  // Create send queue for client transport to buffer outgoing messages
  ws_data->send_queue = ringbuffer_create(sizeof(websocket_msg_t), WEBSOCKET_SEND_QUEUE_SIZE);
  if (!ws_data->send_queue) {
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to create send queue");
    return NULL;
  }

  // Initialize synchronization primitives with transport-aware names
  char recv_name[64], state_name[64];
  snprintf(recv_name, sizeof(recv_name), "recv_%s", name);
  snprintf(state_name, sizeof(state_name), "state_%s", name);

  if (mutex_init(&ws_data->recv_mutex, recv_name) != 0) {
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize recv mutex");
    return NULL;
  }

  if (cond_init(&ws_data->recv_cond, recv_name) != 0) {
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize recv condition variable");
    return NULL;
  }

  if (mutex_init(&ws_data->state_mutex, state_name) != 0) {
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize state mutex");
    return NULL;
  }

  if (cond_init(&ws_data->state_cond, state_name) != 0) {
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize state condition variable");
    return NULL;
  }

  // Allocate initial send buffer
  ws_data->send_buffer_capacity = LWS_PRE + 524288; // Match tx_packet_size (512KB)
  ws_data->send_buffer = SAFE_MALLOC(ws_data->send_buffer_capacity, uint8_t *);
  if (!ws_data->send_buffer) {
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate send buffer");
    return NULL;
  }

  // Create libwebsockets context
  // Protocol array must persist for lifetime of context - use static
  // IMPORTANT: "http" protocol MUST be first for WebSocket upgrade handshake to work
  static struct lws_protocols client_protocols[] = {
      {
          "http",                              // Required first for WebSocket upgrade
          websocket_callback,
          0,                                   // Per-session data (unused, using connect_info.userdata instead)
          524288,                              // RX buffer size
          0,                                   // ID
          NULL,                                // User pointer (will be set from connect_info.userdata)
          524288                               // TX packet size
      },
      {
          "acip",                              // ACIP protocol
          websocket_callback,
          0,                                   // Per-session data (unused, using connect_info.userdata instead)
          524288,                              // RX buffer size
          0,                                   // ID
          NULL,                                // User pointer (will be set from connect_info.userdata)
          524288                               // TX packet size
      },
      {NULL, NULL, 0, 0, 0, NULL, 0}           // Terminator
  };

  // Disable client compression for now - causes assertion in lws_set_extension_option()
  // This is a known issue with libwebsockets permessage-deflate negotiation
  // Server-side compression is still enabled, data will be compressed from server to client
  // but client->server traffic remains uncompressed (acceptable since client sends less data)
  // static const struct lws_extension client_extensions[] = {
  //     {"permessage-deflate", lws_extension_callback_pm_deflate, "permessage-deflate; client_max_window_bits=15"},
  //     {NULL, NULL, NULL}};

  struct lws_context_creation_info info;
  memset(&info, 0, sizeof(info));
  info.port = CONTEXT_PORT_NO_LISTEN; // Client mode - no listening
  info.protocols = client_protocols;
  info.gid = (gid_t)-1; // Cast to avoid undefined behavior with unsigned type
  info.uid = (uid_t)-1; // Cast to avoid undefined behavior with unsigned type
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
  info.extensions = NULL; // Disable client compression due to lws_set_extension_option() assertion

  // Increase per-thread service buffer to prevent fragmentation of large messages
  // Default is 4KB, causing 291KB frames to fragment into 73 × 4KB chunks
  // Increase to 512KB to match server and allow larger WebSocket frames without fragmentation
  info.pt_serv_buf_size = 512 * 1024; // 512KB per-thread service buffer

  // Configure keep-alive to prevent idle disconnects during handshake
  // PING every 30 seconds if idle, close after 35 seconds total with no response
  static const lws_retry_bo_t client_keep_alive_policy = {
      .secs_since_valid_ping = 30,   // Send PING after 30s idle
      .secs_since_valid_hangup = 35, // Hangup if still idle after 35s
  };
  info.retry_and_idle_policy = &client_keep_alive_policy;

  ws_data->context = lws_create_context(&info);
  if (!ws_data->context) {
    SAFE_FREE(ws_data->send_buffer);
    cond_destroy(&ws_data->state_cond);
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->send_queue);
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
    cond_destroy(&ws_data->state_cond);
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to connect to WebSocket server");
    return NULL;
  }

  ws_data->is_connected = false;      // Will be set to true in LWS_CALLBACK_CLIENT_ESTABLISHED
  ws_data->connection_failed = false; // Set to true in LWS_CALLBACK_CLIENT_CONNECTION_ERROR
  ws_data->owns_context = true;       // Client transport owns the context

  // Initialize transport
  transport->methods = &websocket_methods;
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = ws_data;

  // CRITICAL: Give libwebsockets time to initialize connection before starting service thread
  // libwebsockets needs to process the initial connection handshake callbacks before lws_service()
  // is called repeatedly. Concurrent access to context during connection setup can trigger assertions.
  // Sleep 50ms to let lws_client_connect_via_info() callbacks complete initialization.
  log_debug("Delaying service thread start to allow libwebsockets connection initialization...");
  platform_sleep_us(50000);  // 50ms delay

  // Start service thread after connection initialization
  // Only the service thread should call lws_service() on this context
  log_debug("Starting WebSocket service thread...");
  ws_data->service_running = true;
  if (asciichat_thread_create(&ws_data->service_thread, "ws_service", websocket_service_thread, ws_data) != 0) {
    log_error("Failed to create WebSocket service thread");
    ws_data->service_running = false;
    lws_context_destroy(ws_data->context);
    SAFE_FREE(ws_data->send_buffer);
    cond_destroy(&ws_data->state_cond);
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to create service thread");
    return NULL;
  }
  log_debug("WebSocket service thread started");

  // CRITICAL FIX: Do NOT block the main thread waiting for connection!
  // This prevents stdin/keyboard input from being processed and causes the client to hang.
  // The service thread will establish the connection asynchronously.
  // The protocol layer (recv) will detect if connection fails and handle it there.
  // Return immediately so the main thread can respond to keyboard input.
  log_debug("WebSocket transport created, service thread will establish connection asynchronously");

  // Register websocket implementation data
  NAMED_REGISTER_WEBSOCKET_IMPL(ws_data, name);
  NAMED_REGISTER_TRANSPORT(transport, name);

  // Return transport immediately - connection will be established by service thread
  // If connection fails, recv() will detect is_connected=false and return error
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
acip_transport_t *acip_websocket_server_transport_create(const char *name, struct lws *wsi,
                                                         crypto_context_t *crypto_ctx) {
  if (!name) {
    SET_ERRNO(ERROR_INVALID_STATE, "Transport name is required");
    return NULL;
  }

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

  // Initialize pending-free queue for deferred buffer freeing
  // permessage-deflate compression holds buffer references asynchronously,
  // so we defer freeing to prevent use-after-free errors
  ws_data->pending_free_queue = ringbuffer_create(sizeof(pending_free_item_t), 256);
  if (!ws_data->pending_free_queue) {
    ringbuffer_destroy(ws_data->recv_queue);
    ringbuffer_destroy(ws_data->send_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to create pending-free queue");
    return NULL;
  }

  // Initialize synchronization primitives with transport-aware names
  char recv_name[64], send_name[64], state_name[64], pending_free_name[64];
  snprintf(recv_name, sizeof(recv_name), "recv_%s", name);
  snprintf(send_name, sizeof(send_name), "send_%s", name);
  snprintf(state_name, sizeof(state_name), "state_%s", name);
  snprintf(pending_free_name, sizeof(pending_free_name), "pending_free_%s", name);

  if (mutex_init(&ws_data->recv_mutex, recv_name) != 0) {
    ringbuffer_destroy(ws_data->recv_queue);
    ringbuffer_destroy(ws_data->send_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize recv mutex");
    return NULL;
  }

  if (cond_init(&ws_data->recv_cond, recv_name) != 0) {
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ringbuffer_destroy(ws_data->send_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize recv condition variable");
    return NULL;
  }

  if (mutex_init(&ws_data->send_mutex, send_name) != 0) {
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ringbuffer_destroy(ws_data->send_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize send mutex");
    return NULL;
  }

  if (mutex_init(&ws_data->state_mutex, state_name) != 0) {
    mutex_destroy(&ws_data->send_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ringbuffer_destroy(ws_data->send_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize state mutex");
    return NULL;
  }

  if (cond_init(&ws_data->state_cond, state_name) != 0) {
    mutex_destroy(&ws_data->state_mutex);
    mutex_destroy(&ws_data->send_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ringbuffer_destroy(ws_data->send_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize state condition variable");
    return NULL;
  }

  if (mutex_init(&ws_data->pending_free_mutex, pending_free_name) != 0) {
    cond_destroy(&ws_data->state_cond);
    mutex_destroy(&ws_data->state_mutex);
    mutex_destroy(&ws_data->send_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->pending_free_queue);
    SAFE_FREE(ws_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_NETWORK, "Failed to initialize pending-free mutex");
    return NULL;
  }

  // Allocate send buffer with LWS_PRE padding
  size_t initial_capacity = 4096 + LWS_PRE;
  ws_data->send_buffer = SAFE_MALLOC(initial_capacity, uint8_t *);
  if (!ws_data->send_buffer) {
    mutex_destroy(&ws_data->state_mutex);
    mutex_destroy(&ws_data->send_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ringbuffer_destroy(ws_data->send_queue);
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
  log_info("[WEBSOCKET_TRANSPORT_CREATE] ★★★ SERVER TRANSPORT CREATED: is_connected=true, wsi=%p, ws_data=%p",
           (void *)wsi, (void *)ws_data);
  log_debug("Server transport created: is_connected=true, wsi=%p", (void *)wsi);

  // Initialize transport
  transport->methods = &websocket_methods;
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = ws_data;

  log_info("Created WebSocket server transport (crypto: %s)", crypto_ctx ? "enabled" : "disabled");

  // Register websocket implementation data
  NAMED_REGISTER_WEBSOCKET_IMPL(ws_data, name);

  NAMED_REGISTER_TRANSPORT(transport, name);
  return transport;
}
