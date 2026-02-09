/**
 * @file network/websocket/server.c
 * @brief WebSocket server implementation
 * @ingroup network
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <ascii-chat/network/websocket/server.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/log/logging.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/cond.h>
#include <libwebsockets.h>
#include <string.h>

// Shared internal types (websocket_recv_msg_t, websocket_transport_data_t)
#include <ascii-chat/network/websocket/internal.h>

/**
 * @brief Per-connection user data
 *
 * Stored in libwebsockets per-session user data (wsi user pointer).
 */
typedef struct {
  websocket_server_t *server;        ///< Back-reference to server
  acip_transport_t *transport;       ///< ACIP transport for this connection
  asciichat_thread_t handler_thread; ///< Client handler thread
  bool handler_started;              ///< True if handler thread was started

  // Fragment assembly for large messages
  uint8_t *fragment_buffer; ///< Buffer for assembling fragmented messages
  size_t fragment_size;     ///< Current size of assembled fragments
  size_t fragment_capacity; ///< Allocated capacity of fragment buffer
} websocket_connection_data_t;

/**
 * @brief libwebsockets callback for ACIP protocol
 *
 * Handles connection lifecycle events for WebSocket clients.
 */
static int websocket_server_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
                                     size_t len) {
  websocket_connection_data_t *conn_data = (websocket_connection_data_t *)user;

  if (reason == LWS_CALLBACK_ESTABLISHED) {
    log_error("!!!!! LWS_CALLBACK_ESTABLISHED TRIGGERED !!!!!");
  }

  switch (reason) {
  case LWS_CALLBACK_ESTABLISHED: {
    // New WebSocket connection established
    log_error("[LWS_CALLBACK_ESTABLISHED] ===== WebSocket client connection ESTABLISHED =====");
    log_info("[LWS_CALLBACK_ESTABLISHED] ===== WebSocket client connection ESTABLISHED =====");
    log_info("WebSocket client connected");

    // Get server instance from protocol user data
    const struct lws_protocols *protocol = lws_get_protocol(wsi);
    log_debug("[LWS_CALLBACK_ESTABLISHED] Got protocol: %p", (void *)protocol);
    if (!protocol || !protocol->user) {
      log_error("[LWS_CALLBACK_ESTABLISHED] FAILED: Missing protocol user data (protocol=%p, user=%p)",
                (void *)protocol, protocol ? protocol->user : NULL);
      return -1;
    }
    websocket_server_t *server = (websocket_server_t *)protocol->user;
    log_debug("[LWS_CALLBACK_ESTABLISHED] Got server: %p, handler: %p", (void *)server, (void *)server->handler);

    // Initialize connection data
    conn_data->server = server;
    conn_data->transport = NULL;
    conn_data->handler_started = false;
    conn_data->fragment_buffer = NULL;
    conn_data->fragment_size = 0;
    conn_data->fragment_capacity = 0;

    // Get client address
    char client_name[128];
    char client_ip[64];
    lws_get_peer_simple(wsi, client_name, sizeof(client_name));
    lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi), client_name, sizeof(client_name), client_ip, sizeof(client_ip));

    log_info("WebSocket client connected from %s", client_ip);
    log_debug("[LWS_CALLBACK_ESTABLISHED] Client IP: %s", client_ip);

    // Create ACIP WebSocket server transport for this connection
    // Note: We pass NULL for crypto_ctx here - crypto handshake happens at ACIP level
    log_debug("[LWS_CALLBACK_ESTABLISHED] Creating ACIP WebSocket transport...");
    conn_data->transport = acip_websocket_server_transport_create(wsi, NULL);
    if (!conn_data->transport) {
      log_error("[LWS_CALLBACK_ESTABLISHED] FAILED: acip_websocket_server_transport_create returned NULL");
      return -1;
    }
    log_debug("[LWS_CALLBACK_ESTABLISHED] Transport created: %p", (void *)conn_data->transport);

    // Create client context for handler
    websocket_client_context_t *client_ctx =
        SAFE_CALLOC(1, sizeof(websocket_client_context_t), websocket_client_context_t *);
    if (!client_ctx) {
      log_error("Failed to allocate client context");
      acip_transport_destroy(conn_data->transport);
      conn_data->transport = NULL;
      return -1;
    }

    client_ctx->transport = conn_data->transport;
    SAFE_STRNCPY(client_ctx->client_ip, client_ip, sizeof(client_ctx->client_ip));
    client_ctx->client_port = 0; // WebSocket doesn't expose client port easily
    client_ctx->user_data = server->user_data;

    // Spawn handler thread (matches TCP server behavior)
    log_debug("[LWS_CALLBACK_ESTABLISHED] Spawning handler thread (handler=%p, ctx=%p)...", (void *)server->handler,
              (void *)client_ctx);
    if (asciichat_thread_create(&conn_data->handler_thread, server->handler, client_ctx) != 0) {
      log_error("[LWS_CALLBACK_ESTABLISHED] FAILED: asciichat_thread_create returned error");
      SAFE_FREE(client_ctx);
      acip_transport_destroy(conn_data->transport);
      conn_data->transport = NULL;
      return -1;
    }

    conn_data->handler_started = true;
    log_debug("WebSocket client handler thread started");
    log_debug("[LWS_CALLBACK_ESTABLISHED] ===== Handler thread started successfully =====");
    break;
  }

  case LWS_CALLBACK_CLOSED:
    // Connection closed
    log_debug("WebSocket client disconnected");

    // Clean up fragment buffer
    if (conn_data->fragment_buffer) {
      SAFE_FREE(conn_data->fragment_buffer);
      conn_data->fragment_size = 0;
      conn_data->fragment_capacity = 0;
    }

    // Close the transport to mark it as disconnected.
    // This signals the receive thread to exit, allowing clean shutdown.
    if (conn_data->transport) {
      conn_data->transport->methods->close(conn_data->transport);
    }

    if (conn_data->handler_started) {
      // Wait for handler thread to complete
      asciichat_thread_join(&conn_data->handler_thread, NULL);
      conn_data->handler_started = false;
    }

    // NULL the transport pointer so no subsequent callbacks access it.
    // The transport object itself is owned by the client_info_t structure
    // and will be freed by remove_client → acip_transport_destroy.
    conn_data->transport = NULL;
    break;

  case LWS_CALLBACK_SERVER_WRITEABLE: {
    log_error("!!! LWS_CALLBACK_SERVER_WRITEABLE triggered !!!");
    log_debug("LWS_CALLBACK_SERVER_WRITEABLE triggered");

    // Dequeue and send pending data
    if (!conn_data || !conn_data->transport) {
      log_debug("SERVER_WRITEABLE: No conn_data or transport");
      break;
    }

    websocket_transport_data_t *ws_data = (websocket_transport_data_t *)conn_data->transport->impl_data;
    if (!ws_data || !ws_data->send_queue) {
      log_debug("SERVER_WRITEABLE: No ws_data or send_queue");
      break;
    }

    mutex_lock(&ws_data->queue_mutex);

    if (ringbuffer_is_empty(ws_data->send_queue)) {
      mutex_unlock(&ws_data->queue_mutex);
      log_debug("SERVER_WRITEABLE: Send queue is empty, nothing to send");
      break;
    }

    log_error(">>> SERVER_WRITEABLE: Processing message from send queue");

    // Dequeue message
    websocket_recv_msg_t msg;
    bool success = ringbuffer_read(ws_data->send_queue, &msg);
    mutex_unlock(&ws_data->queue_mutex);

    log_debug("SERVER_WRITEABLE: Dequeued message %zu bytes (success=%d)", msg.len, success);

    if (!success || !msg.data) {
      break;
    }

    // Send in fragments using the same logic as client-side
    const size_t FRAGMENT_SIZE = 4096;
    size_t offset = 0;
    bool send_failed = false;

    while (offset < msg.len && !send_failed) {
      size_t chunk_size = (msg.len - offset > FRAGMENT_SIZE) ? FRAGMENT_SIZE : (msg.len - offset);
      int is_start = (offset == 0);
      int is_end = (offset + chunk_size >= msg.len);

      enum lws_write_protocol flags = lws_write_ws_flags(LWS_WRITE_BINARY, is_start, is_end);

      // Ensure send buffer is large enough
      size_t required_size = LWS_PRE + chunk_size;
      if (ws_data->send_buffer_capacity < required_size) {
        SAFE_FREE(ws_data->send_buffer);
        ws_data->send_buffer = SAFE_MALLOC(required_size, uint8_t *);
        if (!ws_data->send_buffer) {
          send_failed = true;
          break;
        }
        ws_data->send_buffer_capacity = required_size;
      }

      memcpy(ws_data->send_buffer + LWS_PRE, msg.data + offset, chunk_size);

      int written = lws_write(wsi, ws_data->send_buffer + LWS_PRE, chunk_size, flags);
      if (written < 0 || (size_t)written != chunk_size) {
        log_error("Server WebSocket write failed: %d/%zu bytes at offset %zu", written, chunk_size, offset);
        send_failed = true;
        break;
      }

      offset += chunk_size;
    }

    // Free the message data (allocated with SAFE_MALLOC in websocket_send)
    SAFE_FREE(msg.data);

    if (!send_failed) {
      log_debug("SERVER_WRITEABLE: Successfully sent %zu bytes in fragments", offset);
    }

    // If there are more messages, request another writable callback
    mutex_lock(&ws_data->queue_mutex);
    bool has_more = !ringbuffer_is_empty(ws_data->send_queue);
    mutex_unlock(&ws_data->queue_mutex);

    if (has_more) {
      log_debug("SERVER_WRITEABLE: More messages queued, requesting another callback");
      lws_callback_on_writable(wsi);
    }

    break;
  }

  case LWS_CALLBACK_RECEIVE: {
    // Received data from client - may be fragmented for large messages
    if (!conn_data || !conn_data->transport || !in || len == 0) {
      break;
    }

    // Get transport data
    websocket_transport_data_t *ws_data = (websocket_transport_data_t *)conn_data->transport->impl_data;
    if (!ws_data) {
      log_error("WebSocket transport has no implementation data");
      break;
    }

    bool is_first = lws_is_first_fragment(wsi);
    bool is_final = lws_is_final_fragment(wsi);

    // Log all fragments for debugging
    log_debug("WebSocket fragment: %zu bytes (first=%d, final=%d, buffered=%zu)", len, is_first, is_final,
              conn_data->fragment_size);

    // If this is a single-fragment message (first and final), log the packet type for debugging
    if (is_first && is_final && len >= 10) {
      const uint8_t *data = (const uint8_t *)in;
      uint16_t pkt_type = (data[8] << 8) | data[9];
      log_debug("  Single-fragment message: packet_type=%d (0x%x), total_size=%zu", pkt_type, pkt_type, len);
    }

    // Allocate or expand fragment buffer
    size_t required_size = conn_data->fragment_size + len;
    if (required_size > conn_data->fragment_capacity) {
      size_t new_capacity = required_size * 2; // Over-allocate to reduce reallocations
      uint8_t *new_buffer = SAFE_MALLOC(new_capacity, uint8_t *);
      if (!new_buffer) {
        log_error("Failed to allocate fragment buffer");
        // Reset fragment state
        SAFE_FREE(conn_data->fragment_buffer);
        conn_data->fragment_size = 0;
        conn_data->fragment_capacity = 0;
        break;
      }

      // Copy existing fragments
      if (conn_data->fragment_buffer) {
        memcpy(new_buffer, conn_data->fragment_buffer, conn_data->fragment_size);
        SAFE_FREE(conn_data->fragment_buffer);
      }

      conn_data->fragment_buffer = new_buffer;
      conn_data->fragment_capacity = new_capacity;
    }

    // Append this fragment
    memcpy(conn_data->fragment_buffer + conn_data->fragment_size, in, len);
    conn_data->fragment_size += len;

    // If this is the final fragment, push complete message to receive queue
    if (is_final) {
      log_debug("Complete message assembled: %zu bytes", conn_data->fragment_size);

      // Allocate message buffer using buffer pool
      websocket_recv_msg_t msg;
      msg.data = buffer_pool_alloc(NULL, conn_data->fragment_size);
      if (!msg.data) {
        log_error("Failed to allocate buffer for complete message");
        conn_data->fragment_size = 0; // Reset for next message
        break;
      }

      // Copy assembled message
      memcpy(msg.data, conn_data->fragment_buffer, conn_data->fragment_size);
      msg.len = conn_data->fragment_size;

      // Reset fragment buffer for next message
      conn_data->fragment_size = 0;

      // Push to receive queue
      mutex_lock(&ws_data->queue_mutex);

      bool success = ringbuffer_write(ws_data->recv_queue, &msg);
      if (!success) {
        // Queue full - drop oldest message to make room
        websocket_recv_msg_t dropped_msg;
        if (ringbuffer_read(ws_data->recv_queue, &dropped_msg)) {
          buffer_pool_free(NULL, dropped_msg.data, dropped_msg.len);
          log_warn("WebSocket receive queue full, dropped oldest message");
        }

        // Try again
        success = ringbuffer_write(ws_data->recv_queue, &msg);
        if (!success) {
          buffer_pool_free(NULL, msg.data, msg.len);
          log_error("Failed to write to WebSocket receive queue after drop");
        }
      }

      // Signal waiting recv() call
      cond_signal(&ws_data->queue_cond);
      mutex_unlock(&ws_data->queue_mutex);
    }
    break;
  }

  case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
    // Fired on the service thread when lws_cancel_service() is called from another thread.
    // This is how we safely convert cross-thread send requests into writable callbacks.
    // lws_callback_on_writable() is only safe from the service thread context.
    log_error("!!! LWS_CALLBACK_EVENT_WAIT_CANCELLED triggered - requesting writable callbacks !!!");
    const struct lws_protocols *protocol = lws_get_protocol(wsi);
    if (protocol) {
      log_debug("EVENT_WAIT_CANCELLED: Calling lws_callback_on_writable_all_protocol");
      lws_callback_on_writable_all_protocol(lws_get_context(wsi), protocol);
    } else {
      log_error("EVENT_WAIT_CANCELLED: No protocol found on wsi");
    }
    break;
  }

  default:
    break;
  }

  return 0;
}

/**
 * @brief Protocol definition for ACIP over WebSocket
 */
static struct lws_protocols websocket_protocols[] = {
    {
        "acip",                              // Protocol name
        websocket_server_callback,           // Callback function
        sizeof(websocket_connection_data_t), // Per-session data size
        524288,                              // RX buffer size (512KB for video frames)
        0,                                   // ID (auto-assigned)
        NULL,                                // User pointer (set to server instance)
        524288                               // TX packet size (512KB for video frames)
    },
    {NULL, NULL, 0, 0, 0, NULL, 0} // Terminator
};

asciichat_error_t websocket_server_init(websocket_server_t *server, const websocket_server_config_t *config) {
  if (!server || !config || !config->client_handler) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  memset(server, 0, sizeof(*server));
  server->handler = config->client_handler;
  server->user_data = config->user_data;
  server->port = config->port;
  atomic_store(&server->running, true);

  // Store server pointer in protocol user data so callbacks can access it
  websocket_protocols[0].user = server;

  // Enable libwebsockets debug logging
  lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_DEBUG, NULL);

  // Configure libwebsockets context
  struct lws_context_creation_info info = {0};
  info.port = config->port;
  info.protocols = websocket_protocols;
  info.gid = (gid_t)-1; // Cast to avoid undefined behavior with unsigned type
  info.uid = (uid_t)-1; // Cast to avoid undefined behavior with unsigned type
  info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;
  info.pt_serv_buf_size = 524288; // 512KB per-thread server buffer for large video frames

  // Create libwebsockets context
  server->context = lws_create_context(&info);
  if (!server->context) {
    return SET_ERRNO(ERROR_NETWORK_BIND, "Failed to create libwebsockets context");
  }

  log_info("WebSocket server initialized on port %d", config->port);
  return ASCIICHAT_OK;
}

asciichat_error_t websocket_server_run(websocket_server_t *server) {
  if (!server || !server->context) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid server");
  }

  log_info("WebSocket server starting event loop on port %d", server->port);

  // Run libwebsockets event loop
  while (atomic_load(&server->running)) {
    // Service the context with 50ms timeout
    // Returns 0 on success, negative on error
    int result = lws_service(server->context, 50);
    if (result < 0) {
      log_error("libwebsockets service error: %d", result);
      break;
    }
  }

  log_info("WebSocket server event loop exited, destroying context from event loop thread");

  // Destroy context from the event loop thread. When called from a different
  // thread after the event loop has stopped, lws_context_destroy tries to
  // gracefully close WebSocket connections but can't process close responses
  // (no event loop running), so it waits for the close handshake timeout
  // (5+ seconds). Destroying from the event loop thread avoids this.
  if (server->context) {
    lws_context_destroy(server->context);
    server->context = NULL;
  }

  log_info("WebSocket server context destroyed");
  return ASCIICHAT_OK;
}

void websocket_server_cancel_service(websocket_server_t *server) {
  if (server && server->context) {
    lws_cancel_service(server->context);
  }
}

void websocket_server_destroy(websocket_server_t *server) {
  if (!server) {
    return;
  }

  atomic_store(&server->running, false);

  // Context is normally destroyed by websocket_server_run (from the event loop
  // thread) for fast shutdown. This handles the case where run() wasn't called
  // or didn't complete normally.
  if (server->context) {
    log_debug("WebSocket context still alive in destroy, cleaning up");
    lws_context_destroy(server->context);
    server->context = NULL;
  }

  log_debug("WebSocket server destroyed");
}
