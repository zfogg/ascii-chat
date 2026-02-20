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
#include <ascii-chat/util/time.h>
#include <libwebsockets.h>
#include <string.h>
#include <errno.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#endif

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
  bool cleaning_up;                  ///< True if cleanup is in progress (prevents race with remove_client)

  // Pending message send state (for LWS_CALLBACK_SERVER_WRITEABLE)
  uint8_t *pending_send_data; ///< Current message being sent
  size_t pending_send_len;    ///< Total length of message being sent
  size_t pending_send_offset; ///< Current offset in message (bytes already sent)
  bool has_pending_send;      ///< True if there's an in-progress message
} websocket_connection_data_t;

// Per-connection callback counters for diagnosing event loop interleaving.
// These track how many RECEIVE vs WRITEABLE callbacks fire during message assembly.
// TODO: Make these per-connection instead of global for proper multi-client support
static _Atomic uint64_t g_receive_callback_count = 0;
static _Atomic uint64_t g_writeable_callback_count = 0;

/**
 * @brief Custom logging callback for libwebsockets
 * This captures LWS internal logging so we can see what's happening
 */
static void websocket_lws_log_callback(int level, const char *line) {
  // Convert LWS log level to our log level
  if (level & LLL_ERR) {
    log_error("[LWS] %s", line);
  } else if (level & LLL_WARN) {
    log_warn("[LWS] %s", line);
  } else if (level & LLL_NOTICE) {
    log_info("[LWS] %s", line);
  } else if (level & LLL_INFO) {
    log_info("[LWS] %s", line);
  } else if (level & LLL_DEBUG) {
    log_debug("[LWS] %s", line);
  }
}

/**
 * @brief libwebsockets callback for ACIP protocol
 *
 * Handles connection lifecycle events for WebSocket clients.
 */
static int websocket_server_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in,
                                     size_t len) {
  websocket_connection_data_t *conn_data = (websocket_connection_data_t *)user;
  const char *proto_name = lws_get_protocol(wsi) ? lws_get_protocol(wsi)->name : "NULL";

  // LOG EVERY SINGLE CALLBACK WITH PROTOCOL NAME
  log_dev("🔴 CALLBACK: reason=%d, proto=%s, wsi=%p, len=%zu", reason, proto_name, (void *)wsi, len);

  switch (reason) {
  case LWS_CALLBACK_ESTABLISHED: {
    // New WebSocket connection established
    uint64_t established_ns = time_get_ns();
    log_info("🔴🔴🔴 LWS_CALLBACK_ESTABLISHED FIRED! wsi=%p", (void *)wsi);
    log_info("[LWS_CALLBACK_ESTABLISHED] WebSocket client connection established at timestamp=%llu",
             (unsigned long long)established_ns);
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
    conn_data->pending_send_data = NULL;
    conn_data->pending_send_len = 0;
    conn_data->pending_send_offset = 0;
    conn_data->has_pending_send = false;

    // Get client address
    char client_name[128];
    char client_ip[64];
    lws_get_peer_simple(wsi, client_name, sizeof(client_name));
    lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi), client_name, sizeof(client_name), client_ip, sizeof(client_ip));

    log_info("WebSocket client connected from %s", client_ip);
    log_debug("[LWS_CALLBACK_ESTABLISHED] Client IP: %s", client_ip);

    // Optimize TCP for high-throughput large message transfer.
    // TCP delayed ACK (default ~40ms) causes the sender to stall waiting for ACKs,
    // creating ~30ms gaps between 128KB chunk deliveries for large messages.
    // TCP_QUICKACK forces immediate ACKs so the sender can push data continuously (Linux only).
    // TCP_NODELAY disables Nagle's algorithm for the send path.
#ifdef __linux__
    {
      int fd = lws_get_socket_fd(wsi);
      if (fd >= 0) {
        int quickack = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack)) < 0) {
          log_warn("Failed to set TCP_QUICKACK: %s", SAFE_STRERROR(errno));
        }
        int nodelay = 1;
        if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) < 0) {
          log_warn("Failed to set TCP_NODELAY: %s", SAFE_STRERROR(errno));
        }
        // Do NOT set SO_RCVBUF/SO_SNDBUF manually.
        // Setting SO_RCVBUF disables TCP autotuning on Linux, which locks the receive buffer
        // at the rmem_default (212KB). Without manual override, the kernel can autotune up to
        // tcp_rmem max (typically 6MB), allowing the entire 921KB video frame to fit in one
        // TCP window without flow control stalls.
        int actual_rcv = 0, actual_snd = 0;
        socklen_t optlen = sizeof(int);
        getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &actual_rcv, &optlen);
        getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &actual_snd, &optlen);
        log_info("[WS_SOCKET] TCP_QUICKACK=1, TCP_NODELAY=1, SO_RCVBUF=%d (autotuned), SO_SNDBUF=%d", actual_rcv,
                 actual_snd);
      }
    }
#endif

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

    // Spawn handler thread - this calls websocket_client_handler which creates client_info_t
    log_info("🔴 ABOUT TO SPAWN HANDLER THREAD: handler=%p, ctx=%p", (void *)server->handler, (void *)client_ctx);
    log_debug("[LWS_CALLBACK_ESTABLISHED] Spawning handler thread (handler=%p, ctx=%p)...", (void *)server->handler,
              (void *)client_ctx);
    int handler_create_result = asciichat_thread_create(&conn_data->handler_thread, server->handler, client_ctx);
    log_info("🔴 asciichat_thread_create returned: %d", handler_create_result);
    if (handler_create_result != 0) {
      log_error("[LWS_CALLBACK_ESTABLISHED] FAILED: asciichat_thread_create returned error");
      SAFE_FREE(client_ctx);
      acip_transport_destroy(conn_data->transport);
      conn_data->transport = NULL;
      return -1;
    }

    conn_data->handler_started = true;
    log_debug("[LWS_CALLBACK_ESTABLISHED] Handler thread spawned successfully");
    log_info("★★★ ESTABLISHED CALLBACK SUCCESS - handler thread spawned! ★★★");
    break;
  }

  case LWS_CALLBACK_CLOSED: {
    // Connection closed
    uint64_t close_callback_start_ns = time_get_ns();
    log_info("★★★ LWS_CALLBACK_CLOSED FIRED - WHY IS CONNECTION CLOSING? ★★★");
    log_info("[LWS_CALLBACK_CLOSED] WebSocket client disconnected, wsi=%p, handler_started=%d, timestamp=%llu",
             (void *)wsi, conn_data ? conn_data->handler_started : -1, (unsigned long long)close_callback_start_ns);

    // Try to extract close code from 'in' parameter if available
    uint16_t close_code = 0;
    if (in && len >= 2) {
      close_code = (uint16_t)((uint8_t *)in)[0] << 8 | ((uint8_t *)in)[1];
      log_info("[LWS_CALLBACK_CLOSED] Close frame received with code=%u (1000=normal, 1001=going away, 1006=abnormal, "
               "1009=message too big)",
               close_code);
    } else {
      log_info(
          "[LWS_CALLBACK_CLOSED] No close frame (in=%p, len=%zu) - connection closed without WebSocket close handshake",
          in, len);
    }

    // Mark cleanup in progress to prevent race conditions with other threads accessing transport
    if (conn_data) {
      conn_data->cleaning_up = true;
    }

    // Clean up pending send data only if not already freed
    if (conn_data && conn_data->has_pending_send && conn_data->pending_send_data) {
      SAFE_FREE(conn_data->pending_send_data);
      conn_data->pending_send_data = NULL;
      conn_data->has_pending_send = false;
    }

    // Close the transport to mark it as disconnected.
    // This signals the receive thread to exit, allowing clean shutdown.
    // Guard against use-after-free: check both conn_data and transport are valid
    acip_transport_t *transport_snapshot = NULL;
    if (conn_data && conn_data->transport) {
      transport_snapshot = conn_data->transport;
      conn_data->transport = NULL; // NULL out immediately to prevent race condition

      log_debug("[LWS_CALLBACK_CLOSED] Closing transport=%p for wsi=%p", (void *)transport_snapshot, (void *)wsi);
      // Now safely close the transport (methods pointer is stable)
      if (transport_snapshot && transport_snapshot->methods) {
        transport_snapshot->methods->close(transport_snapshot);
      }
    }

    if (conn_data && conn_data->handler_started) {
      // Wait for handler thread to complete
      uint64_t join_start_ns = time_get_ns();
      log_debug("[LWS_CALLBACK_CLOSED] Waiting for handler thread to complete...");
      asciichat_thread_join(&conn_data->handler_thread, NULL);
      uint64_t join_end_ns = time_get_ns();
      char join_duration_str[32];
      format_duration_ns((double)(join_end_ns - join_start_ns), join_duration_str, sizeof(join_duration_str));
      conn_data->handler_started = false;
      log_info("[LWS_CALLBACK_CLOSED] Handler thread completed (join took %s)", join_duration_str);
    }

    // Destroy the transport and free all its resources (send_queue, recv_queue, etc)
    if (transport_snapshot) {
      log_debug("[LWS_CALLBACK_CLOSED] Destroying transport=%p", (void *)transport_snapshot);
      acip_transport_destroy(transport_snapshot);
    }

    // Ensure transport pointer is NULL
    if (conn_data) {
      conn_data->transport = NULL;
    }

    uint64_t close_callback_end_ns = time_get_ns();
    char total_duration_str[32];
    format_duration_ns((double)(close_callback_end_ns - close_callback_start_ns), total_duration_str,
                       sizeof(total_duration_str));
    log_info("[LWS_CALLBACK_CLOSED] Complete cleanup took %s", total_duration_str);
    break;
  }

  case LWS_CALLBACK_SERVER_WRITEABLE: {
    uint64_t writeable_callback_start_ns = time_get_ns();
    atomic_fetch_add(&g_writeable_callback_count, 1);
    log_dev_every(4500 * US_PER_MS_INT, "=== LWS_CALLBACK_SERVER_WRITEABLE FIRED === wsi=%p, timestamp=%llu",
                  (void *)wsi, (unsigned long long)writeable_callback_start_ns);

    // Dequeue and send pending data
    if (!conn_data) {
      log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: No conn_data");
      break;
    }

    // Check if cleanup is in progress to avoid race condition with remove_client
    if (conn_data->cleaning_up) {
      log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: Cleanup in progress, skipping");
      break;
    }

    // Snapshot the transport pointer to avoid race condition with cleanup thread
    acip_transport_t *transport_snapshot = conn_data->transport;
    if (!transport_snapshot) {
      log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: No transport");
      break;
    }

    websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport_snapshot->impl_data;
    if (!ws_data || !ws_data->send_queue) {
      log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: No ws_data or send_queue");
      break;
    }

    // Fragment size must match rx_buffer_size per LWS performance guidelines.
    // Sending fragments larger than rx_buffer_size causes lws_write() to internally
    // buffer data, leading to performance degradation. The recommended approach is to
    // send chunks equal to (or slightly smaller than) the rx_buffer_size.
    // See: https://github.com/warmcat/libwebsockets/issues/464
    // We configured rx_buffer_size=524288 (512KB) above in websocket_protocols.
    // This reduces 1.2MB frames from 300x4KB fragments to 2-3x512KB fragments,
    // dramatically improving throughput and FPS.
    const size_t FRAGMENT_SIZE = 262144; // 256KB - balance between throughput and stability

    // Check if we have a message in progress
    if (conn_data->has_pending_send) {
      size_t chunk_size = (conn_data->pending_send_len - conn_data->pending_send_offset > FRAGMENT_SIZE)
                              ? FRAGMENT_SIZE
                              : (conn_data->pending_send_len - conn_data->pending_send_offset);
      int is_start = (conn_data->pending_send_offset == 0);
      int is_end = (conn_data->pending_send_offset + chunk_size >= conn_data->pending_send_len);

      enum lws_write_protocol flags = lws_write_ws_flags(LWS_WRITE_BINARY, is_start, is_end);

      // Ensure send buffer is large enough
      size_t required_size = LWS_PRE + chunk_size;
      if (ws_data->send_buffer_capacity < required_size) {
        SAFE_FREE(ws_data->send_buffer);
        ws_data->send_buffer = SAFE_MALLOC(required_size, uint8_t *);
        if (!ws_data->send_buffer) {
          log_error("Failed to allocate send buffer");
          SAFE_FREE(conn_data->pending_send_data);
          conn_data->pending_send_data = NULL;
          conn_data->has_pending_send = false;
          break;
        }
        ws_data->send_buffer_capacity = required_size;
      }

      memcpy(ws_data->send_buffer + LWS_PRE, conn_data->pending_send_data + conn_data->pending_send_offset, chunk_size);

      uint64_t write_start_ns = time_get_ns();
      int written = lws_write(wsi, ws_data->send_buffer + LWS_PRE, chunk_size, flags);
      uint64_t write_end_ns = time_get_ns();
      char write_duration_str[32];
      format_duration_ns((double)(write_end_ns - write_start_ns), write_duration_str, sizeof(write_duration_str));
      log_dev_every(4500 * US_PER_MS_INT, "lws_write returned %d bytes in %s (chunk_size=%zu)", written,
                    write_duration_str, chunk_size);

      if (written < 0) {
        log_error("Server WebSocket write error: %d at offset %zu/%zu", written, conn_data->pending_send_offset,
                  conn_data->pending_send_len);
        SAFE_FREE(conn_data->pending_send_data);
        conn_data->pending_send_data = NULL;
        conn_data->has_pending_send = false;
        break;
      }

      if ((size_t)written != chunk_size) {
        log_warn("Server WebSocket partial write: %d/%zu bytes at offset %zu/%zu", written, chunk_size,
                 conn_data->pending_send_offset, conn_data->pending_send_len);
        // Don't fail on partial write - request another callback to continue
        conn_data->pending_send_offset += written;
        lws_callback_on_writable(wsi);
        break;
      }

      conn_data->pending_send_offset += chunk_size;

      if (is_end) {
        // Message fully sent
        log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: Message fully sent (%zu bytes)",
                      conn_data->pending_send_len);
        SAFE_FREE(conn_data->pending_send_data);
        conn_data->pending_send_data = NULL;
        conn_data->has_pending_send = false;
      } else {
        // More fragments to send
        log_dev_every(4500 * US_PER_MS_INT,
                      "SERVER_WRITEABLE: Sent fragment %zu/%zu bytes, requesting another callback",
                      conn_data->pending_send_offset, conn_data->pending_send_len);
        lws_callback_on_writable(wsi);
        break;
      }
    }

    // Try to dequeue next message if current one is done
    // Fix: Combine into single lock section to eliminate TOCTOU race condition
    websocket_recv_msg_t msg = {0};
    bool success = false;
    bool more_messages = false;

    mutex_lock(&ws_data->send_mutex);
    // Check and dequeue in one atomic operation to avoid race conditions
    if (!ringbuffer_is_empty(ws_data->send_queue)) {
      success = ringbuffer_read(ws_data->send_queue, &msg);
      // Check if there are more messages for the next callback
      more_messages = !ringbuffer_is_empty(ws_data->send_queue);
    }
    mutex_unlock(&ws_data->send_mutex);

    if (success && msg.data) {
        // Start sending this message
        conn_data->pending_send_data = msg.data;
        conn_data->pending_send_len = msg.len;
        conn_data->pending_send_offset = 0;
        conn_data->has_pending_send = true;

        log_dev_every(4500 * US_PER_MS_INT, ">>> SERVER_WRITEABLE: Dequeued message %zu bytes, sending first fragment",
                      msg.len);

        // Send first fragment
        size_t chunk_size = (msg.len > FRAGMENT_SIZE) ? FRAGMENT_SIZE : msg.len;
        int is_start = 1;
        int is_end = (chunk_size >= msg.len);

        enum lws_write_protocol flags = lws_write_ws_flags(LWS_WRITE_BINARY, is_start, is_end);

        size_t required_size = LWS_PRE + chunk_size;
        if (ws_data->send_buffer_capacity < required_size) {
          SAFE_FREE(ws_data->send_buffer);
          ws_data->send_buffer = SAFE_MALLOC(required_size, uint8_t *);
          if (!ws_data->send_buffer) {
            log_error("Failed to allocate send buffer");
            SAFE_FREE(msg.data);
            conn_data->has_pending_send = false;
            break;
          }
          ws_data->send_buffer_capacity = required_size;
        }

        memcpy(ws_data->send_buffer + LWS_PRE, msg.data, chunk_size);

        int written = lws_write(wsi, ws_data->send_buffer + LWS_PRE, chunk_size, flags);
        if (written < 0) {
          log_error("Server WebSocket write error on first fragment: %d", written);
          SAFE_FREE(msg.data);
          conn_data->has_pending_send = false;
          break;
        }

        if ((size_t)written != chunk_size) {
          log_warn("Server WebSocket partial write on first fragment: %d/%zu", written, chunk_size);
          conn_data->pending_send_offset = written;
        } else {
          conn_data->pending_send_offset = chunk_size;
        }

        if (!is_end) {
          log_dev_every(4500 * US_PER_MS_INT,
                        ">>> SERVER_WRITEABLE: First fragment sent, requesting callback for next fragment");
          lws_callback_on_writable(wsi);
        } else {
          log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: Message fully sent in first fragment (%zu bytes)",
                        chunk_size);
          SAFE_FREE(msg.data);
          conn_data->has_pending_send = false;

          // Request callback if more messages are queued (info from earlier dequeue with lock held)
          if (more_messages) {
            log_dev_every(4500 * US_PER_MS_INT, ">>> SERVER_WRITEABLE: More messages queued, requesting callback");
            lws_callback_on_writable(wsi);
          }
        }
      }

    break;
  }

  case LWS_CALLBACK_RECEIVE: {
    // Received data from client - may be fragmented for large messages
    log_dev_every(4500 * US_PER_MS_INT, "LWS_CALLBACK_RECEIVE: conn_data=%p, transport=%p, len=%zu", (void *)conn_data,
                  conn_data ? (void *)conn_data->transport : NULL, len);

    if (!conn_data) {
      log_error("LWS_CALLBACK_RECEIVE: conn_data is NULL! Need to initialize from ESTABLISHED or handle here");
      break;
    }

    // Check if cleanup is in progress to avoid race condition with remove_client
    if (conn_data->cleaning_up) {
      log_debug("LWS_CALLBACK_RECEIVE: Cleanup in progress, discarding received data");
      break;
    }

    // Snapshot the transport pointer to avoid race condition with cleanup thread
    acip_transport_t *transport_snapshot = conn_data->transport;
    log_info("🔴 [WS_RECEIVE] conn_data=%p transport_snapshot=%p handler_started=%d", (void *)conn_data,
             (void *)transport_snapshot, conn_data ? conn_data->handler_started : -1);
    if (!transport_snapshot) {
      log_error("LWS_CALLBACK_RECEIVE: transport is NULL! ESTABLISHED never called?");
      // Try to initialize the transport here as a fallback
      const struct lws_protocols *protocol = lws_get_protocol(wsi);
      if (!protocol || !protocol->user) {
        log_error("LWS_CALLBACK_RECEIVE: Cannot get protocol or user data for fallback initialization");
        break;
      }

      websocket_server_t *server = (websocket_server_t *)protocol->user;
      char client_ip[64];
      lws_get_peer_addresses(wsi, lws_get_socket_fd(wsi), NULL, 0, client_ip, sizeof(client_ip));

      log_info("LWS_CALLBACK_RECEIVE: Initializing transport as fallback (client_ip=%s)", client_ip);
      conn_data->server = server;
      conn_data->transport = acip_websocket_server_transport_create(wsi, NULL);
      conn_data->handler_started = false;
      conn_data->pending_send_data = NULL;
      conn_data->pending_send_len = 0;
      conn_data->pending_send_offset = 0;
      conn_data->has_pending_send = false;

      if (!conn_data->transport) {
        log_error("LWS_CALLBACK_RECEIVE: Failed to create transport in fallback");
        break;
      }

      log_debug("LWS_CALLBACK_RECEIVE: Spawning handler thread in fallback");
      websocket_client_context_t *client_ctx =
          SAFE_CALLOC(1, sizeof(websocket_client_context_t), websocket_client_context_t *);
      if (!client_ctx) {
        log_error("Failed to allocate client context");
        acip_transport_destroy(conn_data->transport);
        conn_data->transport = NULL;
        break;
      }

      client_ctx->transport = conn_data->transport;
      SAFE_STRNCPY(client_ctx->client_ip, client_ip, sizeof(client_ctx->client_ip));
      client_ctx->client_port = 0;
      client_ctx->user_data = server->user_data;

      if (asciichat_thread_create(&conn_data->handler_thread, server->handler, client_ctx) != 0) {
        log_error("LWS_CALLBACK_RECEIVE: Failed to spawn handler thread");
        SAFE_FREE(client_ctx);
        acip_transport_destroy(conn_data->transport);
        conn_data->transport = NULL;
        break;
      }

      conn_data->handler_started = true;
      log_info("LWS_CALLBACK_RECEIVE: Handler thread spawned in fallback");

      // Update snapshot after creating transport
      transport_snapshot = conn_data->transport;
    }

    if (!in || len == 0) {
      break;
    }

    // Check if cleanup is already in progress to avoid race condition with LWS_CALLBACK_CLOSED
    if (conn_data && conn_data->cleaning_up) {
      log_debug("RECEIVE: Cleanup in progress, ignoring fragment");
      break;
    }

    // Handler thread was spawned in LWS_CALLBACK_ESTABLISHED (or RECEIVE fallback)
    // (It fires for server-side connections and properly initializes client_info_t)

    // Get transport data using snapshotted pointer
    websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport_snapshot->impl_data;
    if (!ws_data || !ws_data->recv_queue) {
      log_error("WebSocket transport has no implementation data or recv_queue (ws_data=%p, recv_queue=%p)",
                (void *)ws_data, ws_data ? (void *)ws_data->recv_queue : NULL);
      break;
    }

    uint64_t callback_enter_ns = time_get_ns(); // Capture entry time for duration measurement
    log_debug("[WS_TIMING] callback_enter_ns captured: %llu", (unsigned long long)callback_enter_ns);

    bool is_first = lws_is_first_fragment(wsi);
    bool is_final = lws_is_final_fragment(wsi);
    log_debug("[WS_TIMING] is_first=%d is_final=%d, about to increment callback count", is_first, is_final);
    log_info("[WS_FRAG_DEBUG] === RECEIVE CALLBACK: is_first=%d is_final=%d len=%zu ===", is_first, is_final, len);

    atomic_fetch_add(&g_receive_callback_count, 1);
    log_debug("[WS_TIMING] incremented callback count");

    // Re-enable TCP_QUICKACK on EVERY fragment delivery (Linux only).
    // Linux resets TCP_QUICKACK after each ACK, reverting to delayed ACK mode (~40ms).
    // Without this, only the first fragment batch benefits from quick ACKs, and subsequent
    // batches see ~30ms gaps as the sender waits for delayed ACKs before sending more data.
#ifdef __linux__
    {
      int fd = lws_get_socket_fd(wsi);
      if (fd >= 0) {
        int quickack = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
      }
    }
#endif

    if (is_first) {
      // Reset fragment counters at start of new message
      atomic_store(&g_writeable_callback_count, 0);
      atomic_store(&g_receive_callback_count, 1);
    }

    // Log fragment arrival
    // Note: Timing calculation with global g_receive_first_fragment_ns is broken for multi-fragment
    // messages and will be fixed with per-connection tracking
    {
      uint64_t frag_num = atomic_load(&g_receive_callback_count);
      log_info("[WS_FRAG] Fragment #%llu: %zu bytes (first=%d final=%d)", (unsigned long long)frag_num, len, is_first,
               is_final);
    }

    // Debug: Log raw bytes of incoming fragment
    log_dev_every(4500 * US_PER_MS_INT, "WebSocket fragment: %zu bytes (first=%d, final=%d)", len, is_first, is_final);
    if (len > 0 && len <= 256) {
      const uint8_t *bytes = (const uint8_t *)in;
      char hex_buf[1024];
      size_t hex_pos = 0;
      for (size_t i = 0; i < len && hex_pos < sizeof(hex_buf) - 4; i++) {
        hex_pos += snprintf(hex_buf + hex_pos, sizeof(hex_buf) - hex_pos, "%02x ", bytes[i]);
      }
      log_dev_every(4500 * US_PER_MS_INT, "   Raw bytes: %s", hex_buf);
    }

    // If this is a single-fragment message (first and final), log the packet type for debugging
    if (is_first && is_final && len >= 18) {
      const uint8_t *data = (const uint8_t *)in;
      // ACIP packet header: magic(8) + type(2) + length(4) + crc(4) + client_id(2)
      uint16_t pkt_type = (data[8] << 8) | data[9];
      uint32_t pkt_len = (data[10] << 24) | (data[11] << 16) | (data[12] << 8) | data[13];
      log_dev_every(4500 * US_PER_MS_INT, "Single-fragment ACIP packet: type=%d (0x%x) len=%u total_size=%zu", pkt_type,
                    pkt_type, pkt_len, len);
    }

    // Queue this fragment immediately with first/final flags.
    // Per LWS design, each fragment is processed individually by the callback.
    // We must NOT manually reassemble fragments - that breaks LWS's internal state machine.
    // Instead, queue each fragment with metadata, and let the receiver decide on reassembly.
    // This follows the pattern in lws examples (minimal-ws-server-echo, etc).

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

    // Re-check recv_queue is still valid after acquiring lock
    // (LWS_CALLBACK_CLOSED may have cleared it while we were waiting for the lock)
    if (!ws_data->recv_queue) {
      log_warn("recv_queue was cleared during lock wait, dropping fragment");
      mutex_unlock(&ws_data->recv_mutex);
      buffer_pool_free(NULL, msg.data, msg.len);
      break;
    }

    // Snapshot queue status for logging (safe now that we hold the lock)
    size_t queue_current_size = ringbuffer_size(ws_data->recv_queue);
    size_t queue_capacity = ws_data->recv_queue->capacity;
    size_t queue_free = queue_capacity - queue_current_size;
    log_dev_every(4500 * US_PER_MS_INT, "[WS_FLOW] Queue: free=%zu/%zu (used=%zu)", queue_free, queue_capacity,
                  queue_current_size);

    bool success = ringbuffer_write(ws_data->recv_queue, &msg);
    if (!success) {
      // Queue is full - drop fragment (flow control would deadlock the dispatch thread)
      log_warn("[WS_FLOW] Receive queue FULL - dropping fragment (len=%zu, first=%d, final=%d)", len, is_first,
               is_final);
      buffer_pool_free(NULL, msg.data, msg.len);

      mutex_unlock(&ws_data->recv_mutex);
      break;
    }

    // Signal waiting recv() call that a fragment is available
    log_dev("[WS_DEBUG] RECEIVE: About to signal recv_cond (queue size=%zu)", ringbuffer_size(ws_data->recv_queue));
    cond_signal(&ws_data->recv_cond);
    log_dev("[WS_DEBUG] RECEIVE: Signaled recv_cond");
    mutex_unlock(&ws_data->recv_mutex);
    log_dev("[WS_DEBUG] RECEIVE: Unlocked recv_mutex");

    // Signal LWS to call WRITEABLE callback (matches lws example pattern)
    // This keeps the event loop active and allows server to send responses
    lws_callback_on_writable(wsi);

    log_info("[WS_FRAG] Queued fragment: %zu bytes (first=%d final=%d, total_fragments=%llu)", len, is_first, is_final,
             (unsigned long long)atomic_load(&g_receive_callback_count));

    // Log callback duration
    {
      uint64_t callback_exit_ns = time_get_ns();
      double callback_dur_us = (double)(callback_exit_ns - callback_enter_ns) / 1e3;
      if (callback_dur_us > 200) {
        log_warn("[WS_CALLBACK_DURATION] RECEIVE callback took %.1f µs (> 200µs threshold)", callback_dur_us);
      }
      log_debug("[WS_CALLBACK_DURATION] RECEIVE callback completed in %.1f µs (fragment: first=%d final=%d len=%zu)",
                callback_dur_us, is_first, is_final, len);
    }
    log_debug("[WS_RECEIVE] ===== RECEIVE CALLBACK COMPLETE, returning 0 to continue =====");
    log_info("[WS_RECEIVE_RETURN] Returning 0 from RECEIVE callback (success). fragmented=%d (first=%d final=%d)",
             (!is_final ? 1 : 0), is_first, is_final);
    break;
  }

  case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
    // WebSocket upgrade handshake - allow all connections
    log_info("[FILTER_HTTP_CONNECTION] WebSocket upgrade request (allow protocol upgrade)");
    return 0; // Allow the connection
  }

  case LWS_CALLBACK_PROTOCOL_INIT: {
    // Protocol initialization
    log_info("[PROTOCOL_INIT] Protocol initialized, proto=%s", proto_name);
    break;
  }

  case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
    // Fired on the service thread when lws_cancel_service() is called from another thread.
    // This is how we safely convert cross-thread send requests into writable callbacks.
    // lws_callback_on_writable() is only safe from the service thread context.
    log_dev_every(4500 * US_PER_MS_INT, "LWS_CALLBACK_EVENT_WAIT_CANCELLED triggered - requesting writable callbacks");
    const struct lws_protocols *protocol = lws_get_protocol(wsi);
    if (protocol) {
      log_dev_every(4500 * US_PER_MS_INT, "EVENT_WAIT_CANCELLED: Calling lws_callback_on_writable_all_protocol");
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
        "http",                              // Default HTTP protocol (required for WebSocket upgrade)
        websocket_server_callback,           // Use same callback for all protocols
        sizeof(websocket_connection_data_t), // Per-session data size
        524288,                              // RX buffer size (512KB for video frames)
        0,                                   // ID (auto-assigned)
        NULL,                                // User pointer (set to server instance)
        524288                               // TX packet size (512KB for video frames)
    },
    {
        "acip",                              // ACIP WebSocket subprotocol
        websocket_server_callback,           // Callback function
        sizeof(websocket_connection_data_t), // Per-session data size
        524288,                              // RX buffer size (512KB for video frames)
        0,                                   // ID (auto-assigned)
        NULL,                                // User pointer (set to server instance)
        524288                               // TX packet size (512KB for video frames)
    },
    {NULL, NULL, 0, 0, 0, NULL, 0} // Terminator
};

/**
 * @brief WebSocket extensions - permessage-deflate compression (RFC 7692)
 *
 * Enables permessage-deflate compression for WebSocket messages.
 * Browsers negotiate this automatically during handshake.
 *
 * Performance benefits:
 * - Compresses raw RGB video frames (~921KB) to ~50-100KB (10:1+ ratio)
 * - Reduces multi-fragment receive stalls
 * - Improves throughput by reducing TCP round-trips
 */
// RX BUFFER UNDERFLOW FIX: Configure permessage-deflate with smaller window bits
// LWS 4.5.2 underflows when decompressing large fragmented messages with max window (15 bits).
// Using server_max_window_bits=11 (2KB window) reduces decompression buffer needs and
// prevents the "rx buffer underflow" error while maintaining reasonable compression.
static const struct lws_extension websocket_extensions[] = {
    {"permessage-deflate", lws_extension_callback_pm_deflate, "permessage-deflate; server_max_window_bits=8"},
    {NULL, NULL, NULL}};

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
  // Both the HTTP and ACIP protocols need access to the server
  websocket_protocols[0].user = server; // http protocol
  websocket_protocols[1].user = server; // acip protocol

  // Enable libwebsockets debug logging with custom callback
  lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_DEBUG, websocket_lws_log_callback);

  // Configure libwebsockets context
  struct lws_context_creation_info info = {0};
  info.port = config->port;
  info.protocols = websocket_protocols;
  info.gid = (gid_t)-1;                   // Cast to avoid undefined behavior with unsigned type
  info.uid = (uid_t)-1;                   // Cast to avoid undefined behavior with unsigned type
  info.options = 0;                       // Don't validate UTF8 - we send binary ACIP packets
  info.extensions = websocket_extensions; // Enable permessage-deflate with small window bits
  // Permessage-deflate configuration (RFC 7692):
  // - server_max_window_bits=8: Use 256-byte (2^8) sliding window instead of 32KB (2^15)
  // - Reduces decompression buffer size to prevent "rx buffer underflow" in LWS 4.5.2
  // - Still achieves good compression for video frames (10:1 ratio typical for 921KB frames)
  // - Validated with comprehensive test suite (permessage_deflate_test.c)

  // Create libwebsockets context
  server->context = lws_create_context(&info);
  if (!server->context) {
    return SET_ERRNO(ERROR_NETWORK_BIND, "Failed to create libwebsockets context");
  }

  log_info("WebSocket server initialized on port %d with static file serving", config->port);
  return ASCIICHAT_OK;
}

asciichat_error_t websocket_server_run(websocket_server_t *server) {
  if (!server || !server->context) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid server");
  }

  log_info("WebSocket server starting event loop on port %d", server->port);

  // Run libwebsockets event loop
  uint64_t last_service_ns = 0;
  int service_call_count = 0;
  while (atomic_load(&server->running)) {
    // Service libwebsockets with 50ms timeout.
    // This provides frequent event processing (~20 callback invocations per second) for all
    // connected WebSocket clients while avoiding excessive CPU usage from polling.
    // All client connections share this single server context, so a single lws_service() call
    // services fragments and events for all clients simultaneously.
    uint64_t service_start_ns = time_get_ns();
    if (last_service_ns && service_start_ns - last_service_ns > 30 * US_PER_MS_INT) {
      // > 30ms gap between service calls
      double gap_ms = (double)(service_start_ns - last_service_ns) / 1e6;
      log_info_every(1 * US_PER_MS_INT, "[LWS_SERVICE_GAP] %.1fms gap between lws_service calls", gap_ms);
    }
    service_call_count++;
    log_debug_every(500 * US_PER_MS_INT, "[LWS_SERVICE] Call #%d, context=%p", service_call_count,
                    (void *)server->context);
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
