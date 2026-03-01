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
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/debug/named.h>
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
#include <ascii-chat/network/websocket/callback_timing.h>

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
static atomic_t g_receive_callback_count = {0};
static atomic_t g_writeable_callback_count = {0};

// Forward declaration for websocket_protocols (defined later in file)
static struct lws_protocols websocket_protocols[];

/**
 * @brief Custom logging callback for libwebsockets
 * This captures LWS internal logging so we can see what's happening
 */
static void websocket_lws_log_callback(int level, const char *line) {
  if (level & LLL_ERR) {
    log_error("[LWS] %s", line);
  } else if (level & LLL_WARN) {
    log_warn("[LWS] %s", line);
  } else if (level & LLL_NOTICE) {
    log_info("[LWS] %s", line);
  }
  // INFO: Debug and info logging disabled to reduce noise. Uncomment below if needed.
  // else if (level & LLL_INFO) {
  //   log_info("[LWS] %s", line);
  // } else if (level & LLL_DEBUG) {
  //   log_debug("[LWS] %s", line);
  // }
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
    char ws_transport_name[64];
    snprintf(ws_transport_name, sizeof(ws_transport_name), "server_%p", (void *)wsi);
    conn_data->transport = acip_websocket_server_transport_create(ws_transport_name, wsi, NULL);
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

    // Queue handler to thread pool (no pthread_create from callback context)
    // The handler_pool was created at server startup with pre-allocated workers
    log_info("🔴 ABOUT TO QUEUE HANDLER: handler=%p, ctx=%p", (void *)server->handler, (void *)client_ctx);
    log_debug("[LWS_CALLBACK_ESTABLISHED] Queueing handler to work pool (handler=%p, ctx=%p)...",
              (void *)server->handler, (void *)client_ctx);

    if (!server->handler_pool) {
      log_error("[LWS_CALLBACK_ESTABLISHED] FAILED: handler_pool is NULL");
      SAFE_FREE(client_ctx);
      acip_transport_destroy(conn_data->transport);
      conn_data->transport = NULL;
      return -1;
    }

    asciichat_error_t queue_result =
        thread_pool_queue_work("websocket_handler_established", server->handler_pool, server->handler, client_ctx);
    log_info("🔴 thread_pool_queue_work returned: %s", queue_result == ASCIICHAT_OK ? "OK" : "ERROR");

    if (queue_result != ASCIICHAT_OK) {
      log_error("[LWS_CALLBACK_ESTABLISHED] FAILED: thread_pool_queue_work returned error");
      SAFE_FREE(client_ctx);
      acip_transport_destroy(conn_data->transport);
      conn_data->transport = NULL;
      return -1;
    }

    conn_data->handler_started = true;
    log_debug("[LWS_CALLBACK_ESTABLISHED] Handler work queued successfully");
    log_info("★★★ ESTABLISHED CALLBACK SUCCESS - handler work queued! ★★★");
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
      // Handler is managed by thread pool - no join needed
      // The pool worker will finish handling the transport and return to the pool
      // Transport was already closed above, signaling the handler to exit
      // DO NOT destroy the transport here - the handler might still be using it!
      // The handler will free the context, and the transport will be cleaned up
      // through the client structure (remove_client handles transport cleanup)
      conn_data->handler_started = false;
      log_debug("[LWS_CALLBACK_CLOSED] Handler was queued to pool (it will cleanup when done)");
      log_debug("[LWS_CALLBACK_CLOSED] NOT destroying transport here - handler is still executing");
      transport_snapshot = NULL; // Don't destroy below
    }

    // Destroy the transport only if the handler is NOT running
    // (Handler manages cleanup when it's running)
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
    time_pretty(close_callback_end_ns - close_callback_start_ns, -1, total_duration_str, sizeof(total_duration_str));
    log_info("[LWS_CALLBACK_CLOSED] Complete cleanup took %s", total_duration_str);
    break;
  }

  case LWS_CALLBACK_SERVER_WRITEABLE: {
    uint64_t writeable_callback_start_ns = time_get_ns();
    atomic_fetch_add_u64(&g_writeable_callback_count, 1);
    log_debug("=== LWS_CALLBACK_SERVER_WRITEABLE FIRED === wsi=%p, timestamp=%llu", (void *)wsi,
              (unsigned long long)writeable_callback_start_ns);

    // Validate preconditions
    if (!conn_data) {
      log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: No conn_data");
      break;
    }

    if (conn_data->cleaning_up) {
      log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: Cleanup in progress, skipping");
      break;
    }

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

    // Dequeue and send messages until pipe is choked or queue is empty
    // This follows the libwebsockets reference pattern (protocol_lws_mirror.c)
    websocket_recv_msg_t msg = {0};

    do {
      // Dequeue one message
      mutex_lock(&ws_data->send_mutex);
      if (ringbuffer_is_empty(ws_data->send_queue)) {
        mutex_unlock(&ws_data->send_mutex);
        break; // No more messages
      }
      if (!ringbuffer_read(ws_data->send_queue, &msg)) {
        mutex_unlock(&ws_data->send_mutex);
        break; // Failed to read
      }
      mutex_unlock(&ws_data->send_mutex);

      if (!msg.data || msg.len == 0) {
        log_error("SERVER_WRITEABLE: Dequeued invalid message (data=%p, len=%zu)", (void *)msg.data, msg.len);
        if (msg.data) {
          buffer_pool_free(NULL, msg.data, LWS_PRE + msg.len);
        }
        continue;
      }

      // Let libwebsockets handle ALL fragmentation and FIN bit management internally
      // This avoids manual fragmentation bugs with FIN bit handling
      // lws_write() with LWS_WRITE_BINARY automatically fragments based on MTU
      // and properly manages continuation frames and FIN bits

      log_debug("  SERVER sending complete message: %zu bytes", msg.len);

      int written = lws_write(wsi, msg.data + LWS_PRE, msg.len, LWS_WRITE_BINARY);

      if (written < 0) {
        log_error("Server WebSocket write error: %d", written);
        // Connection error
        buffer_pool_free(NULL, msg.data, LWS_PRE + msg.len);
        break;
      }

      size_t total_written = (size_t)written;

      if (total_written < msg.len) {
        // Partial send - re-queue the unsent portion for the next callback
        log_debug("Server WebSocket partial message: sent %zu/%zu bytes (re-queueing %zu bytes for next callback)",
                  total_written, msg.len, msg.len - total_written);

        // Create new message for the unsent data
        websocket_recv_msg_t unsent_msg;
        unsent_msg.data = msg.data; // Keep the same buffer allocation
        unsent_msg.len = msg.len - total_written;
        unsent_msg.first = 0;         // No longer first fragment
        unsent_msg.final = msg.final; // Preserve final flag

        // Re-queue unsent portion
        mutex_lock(&ws_data->send_mutex);
        bool requeue_success = ringbuffer_write(ws_data->send_queue, &unsent_msg);
        if (!requeue_success) {
          mutex_unlock(&ws_data->send_mutex);
          log_error("Failed to re-queue unsent data (%zu bytes)", unsent_msg.len);
          buffer_pool_free(NULL, msg.data, LWS_PRE + msg.len);
          break;
        }
        mutex_unlock(&ws_data->send_mutex);

        // Request immediate callback to continue sending
        lws_callback_on_writable(wsi);
        break; // Stop processing more messages, let next callback retry
      }

      log_debug(">>> lws_write() sent %zu/%zu bytes (fragmented), wsi=%p", total_written, msg.len, (void *)wsi);
      // Only free buffer when entire message was sent successfully
      buffer_pool_free(NULL, msg.data, LWS_PRE + msg.len);

      // Continue sending while pipe is not choked and we have data
      // This matches lws_mirror.c line 396: } while (!lws_send_pipe_choked(wsi));
    } while (!lws_send_pipe_choked(wsi));

    // If messages still queued and pipe not choked, request callback for next batch
    mutex_lock(&ws_data->send_mutex);
    bool has_more = !ringbuffer_is_empty(ws_data->send_queue);
    mutex_unlock(&ws_data->send_mutex);

    if (has_more && !lws_send_pipe_choked(wsi)) {
      log_dev_every(4500 * US_PER_MS_INT, "SERVER_WRITEABLE: More messages queued, requesting callback");
      lws_callback_on_writable(wsi);
    }

    // Record timing for this callback
    uint64_t writeable_callback_end_ns = time_get_ns();
    websocket_callback_timing_record(&g_ws_callback_timing.server_writeable, writeable_callback_start_ns,
                                     writeable_callback_end_ns);
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
      char ws_transport_name[64];
      snprintf(ws_transport_name, sizeof(ws_transport_name), "server_%p", (void *)wsi);
      conn_data->transport = acip_websocket_server_transport_create(ws_transport_name, wsi, NULL);
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

      // Queue handler to thread pool (fallback if not queued in ESTABLISHED)
      if (thread_pool_queue_work("websocket_handler_receive", server->handler_pool, server->handler, client_ctx) !=
          ASCIICHAT_OK) {
        log_error("LWS_CALLBACK_RECEIVE: Failed to queue handler work");
        SAFE_FREE(client_ctx);
        acip_transport_destroy(conn_data->transport);
        conn_data->transport = NULL;
        break;
      }

      conn_data->handler_started = true;
      log_info("LWS_CALLBACK_RECEIVE: Handler work queued in fallback");

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

    atomic_fetch_add_u64(&g_receive_callback_count, 1);
    log_debug("[WS_TIMING] incremented callback count");

    // OPTIMIZATION: Only enable TCP_QUICKACK on first fragment of each message.
    // Re-enabling on EVERY fragment is too expensive (syscall overhead).
    // Linux resets TCP_QUICKACK after each ACK, but we only need it for the first chunk.
#ifdef __linux__
    if (is_first) {
      int fd = lws_get_socket_fd(wsi);
      if (fd >= 0) {
        int quickack = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &quickack, sizeof(quickack));
      }
    }
#endif

    if (is_first) {
      // Reset fragment counters at start of new message
      atomic_store_u64(&g_writeable_callback_count, 0);
      atomic_store_u64(&g_receive_callback_count, 1);
    }

    // Log fragment arrival
    // Note: Timing calculation with global g_receive_first_fragment_ns is broken for multi-fragment
    // messages and will be fixed with per-connection tracking
    {
      uint64_t frag_num = atomic_load_u64(&g_receive_callback_count);
      log_info("[WS_FRAG] Fragment #%llu: %zu bytes (first=%d final=%d)", (unsigned long long)frag_num, len, is_first,
               is_final);
    }

    // Debug: Log raw bytes of incoming fragment
    // OPTIMIZATION: Only log hex dumps if explicitly enabled to reduce callback overhead
#ifdef WS_DEBUG_VERBOSE_FRAGMENTS
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
#endif

    // Queue this fragment immediately with first/final flags.
    // Per LWS design, each fragment is processed individually by the callback.
    // We must NOT manually reassemble fragments - that breaks LWS's internal state machine.
    // Instead, queue each fragment with metadata, and let the receiver decide on reassembly.
    // This follows the pattern in lws examples (minimal-ws-server-echo, etc).

    uint64_t alloc_start_ns = time_get_ns();
    websocket_recv_msg_t msg;
    msg.data = buffer_pool_alloc(NULL, len);
    if (!msg.data) {
      log_error("Failed to allocate buffer for fragment (%zu bytes)", len);
      break;
    }
    uint64_t alloc_end_ns = time_get_ns();

    memcpy(msg.data, in, len);
    msg.len = len;
    msg.first = is_first;
    msg.final = is_final;

    uint64_t lock_start_ns = time_get_ns();
    mutex_lock(&ws_data->recv_mutex);
    uint64_t lock_end_ns = time_get_ns();

    // Re-check recv_queue is still valid after acquiring lock
    // (LWS_CALLBACK_CLOSED may have cleared it while we were waiting for the lock)
    if (!ws_data->recv_queue) {
      log_warn("recv_queue was cleared during lock wait, dropping fragment");
      mutex_unlock(&ws_data->recv_mutex);
      buffer_pool_free(NULL, msg.data, msg.len);
      break;
    }

    // OPTIMIZATION: Only log queue status on first fragments to reduce callback overhead
    if (is_first) {
      size_t queue_current_size = ringbuffer_size(ws_data->recv_queue);
      size_t queue_capacity = ws_data->recv_queue->capacity;
      size_t queue_free = queue_capacity - queue_current_size;
      log_dev_every(4500 * US_PER_MS_INT, "[WS_FLOW] Queue: free=%zu/%zu (used=%zu)", queue_free, queue_capacity,
                    queue_current_size);
    }

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

    // Signal handler to process queued fragments via the condition variable
    // (cond_signal already called above - do not call lws_callback_on_writable here as it
    // interferes with fragmented frame processing in libwebsockets)

    log_info("[WS_FRAG] Queued fragment: %zu bytes (first=%d final=%d, total_fragments=%llu)", len, is_first, is_final,
             (unsigned long long)atomic_load_u64(&g_receive_callback_count));

    // Log callback duration with breakdown
    {
      uint64_t callback_exit_ns = time_get_ns();
      uint64_t callback_dur_ns = callback_exit_ns - callback_enter_ns;
      uint64_t alloc_dur_ns = alloc_end_ns - alloc_start_ns;
      uint64_t lock_wait_dur_ns = lock_end_ns - lock_start_ns;

      if (callback_dur_ns > 500000) { // 500 µs in nanoseconds
        char callback_str[32], alloc_str[32], lock_str[32];
        time_pretty(callback_dur_ns, -1, callback_str, sizeof(callback_str));
        time_pretty(alloc_dur_ns, -1, alloc_str, sizeof(alloc_str));
        time_pretty(lock_wait_dur_ns, -1, lock_str, sizeof(lock_str));
        log_warn("[WS_CALLBACK_DURATION] RECEIVE callback took %s (alloc=%s, lock_wait=%s)", callback_str, alloc_str,
                 lock_str);
      }
      char callback_str[32], alloc_str[32], lock_str[32], other_str[32];
      time_pretty(callback_dur_ns, -1, callback_str, sizeof(callback_str));
      time_pretty(alloc_dur_ns, -1, alloc_str, sizeof(alloc_str));
      time_pretty(lock_wait_dur_ns, -1, lock_str, sizeof(lock_str));
      uint64_t other_dur_ns = callback_dur_ns - alloc_dur_ns - lock_wait_dur_ns;
      time_pretty(other_dur_ns, -1, other_str, sizeof(other_str));
      log_debug("[WS_CALLBACK_TIMING] total=%s (alloc=%s, lock_wait=%s, other=%s)", callback_str, alloc_str, lock_str,
                other_str);
    }
    log_debug("[WS_RECEIVE] ===== RECEIVE CALLBACK COMPLETE, returning 0 to continue =====");
    log_info("[WS_RECEIVE_RETURN] Returning 0 from RECEIVE callback (success). fragmented=%d (first=%d final=%d)",
             (!is_final ? 1 : 0), is_first, is_final);

    // Record timing for this callback
    websocket_callback_timing_record(&g_ws_callback_timing.receive, callback_enter_ns,
                                     websocket_callback_timing_start());
    break;
  }

  case LWS_CALLBACK_FILTER_HTTP_CONNECTION: {
    // WebSocket upgrade handshake - allow all connections
    log_info("[FILTER_HTTP_CONNECTION] WebSocket upgrade request (allow protocol upgrade)");
    return 0; // Allow the connection
  }

  case LWS_CALLBACK_PROTOCOL_INIT: {
    // Protocol initialization with timing instrumentation
    uint64_t callback_start_ns = websocket_callback_timing_start();
    log_info("[PROTOCOL_INIT] Protocol initialized, proto=%s", proto_name);
    uint64_t callback_end_ns = websocket_callback_timing_start();
    websocket_callback_timing_record(&g_ws_callback_timing.protocol_init, callback_start_ns, callback_end_ns);
    break;
  }

  case LWS_CALLBACK_PROTOCOL_DESTROY: {
    // Protocol destruction with timing instrumentation
    uint64_t callback_start_ns = websocket_callback_timing_start();
    log_info("[PROTOCOL_DESTROY] Protocol destroyed, proto=%s", proto_name);
    uint64_t callback_end_ns = websocket_callback_timing_start();
    websocket_callback_timing_record(&g_ws_callback_timing.protocol_destroy, callback_start_ns, callback_end_ns);
    break;
  }

  case LWS_CALLBACK_EVENT_WAIT_CANCELLED: {
    // Fired on the service thread when lws_cancel_service() is called from another thread.
    // This is how we safely convert cross-thread send requests into writable callbacks.
    // lws_callback_on_writable() is only safe from the service thread context.
    //
    // CRITICAL FIX: lws_cancel_service() wakes the event loop, which fires this callback.
    // The `wsi` parameter may be the listening socket or any random connection, so we
    // CANNOT rely on lws_get_protocol(wsi) to get the correct protocol.
    // Instead, we MUST trigger WRITEABLE on ALL protocols that handle WebSocket frames:
    // - "http" protocol (browser connects here for initial HTTP upgrade to WebSocket)
    // - "acip" protocol (for ACIP over WebSocket connections)
    // Both protocols use the same callback and handle frame transmission.
    log_debug_every(
        1 * NS_PER_SEC_INT,
        ">>> LWS_CALLBACK_EVENT_WAIT_CANCELLED triggered - requesting writable callbacks for all protocols");

    struct lws_context *ctx = lws_get_context(wsi);
    if (!ctx) {
      log_error("EVENT_WAIT_CANCELLED: Could not get context from wsi");
      break;
    }

    // Trigger WRITEABLE on both protocols
    // Browser clients connect with "http" protocol, so they MUST have WRITEABLE triggered
    for (int i = 0; i < 2; i++) {
      log_dev_every(1 * NS_PER_SEC_INT,
                    ">>> EVENT_WAIT_CANCELLED: Calling lws_callback_on_writable_all_protocol for protocol '%s'",
                    websocket_protocols[i].name);
      lws_callback_on_writable_all_protocol(ctx, &websocket_protocols[i]);
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
/**
 * @brief Keep-alive policy for WebSocket connections
 *
 * Configures libwebsockets to send PING frames during idle periods to maintain connections
 * during long handshakes or quiet periods with no application data.
 *
 * - secs_since_valid_ping: Send a PING after 30 seconds of idle
 * - secs_since_valid_hangup: Close connection if no PONG after 35 seconds total idle
 *
 * This prevents lws from closing the connection during the crypto handshake.
 * Without this, connections close at the default HTTP keepalive timeout (5 seconds),
 * which is too short for crypto negotiation.
 */
static const lws_retry_bo_t keep_alive_policy = {
    .secs_since_valid_ping = 30,   // Send PING after 30s idle
    .secs_since_valid_hangup = 35, // Hangup if still idle after 35s
};

asciichat_error_t websocket_server_init(websocket_server_t *server, const websocket_server_config_t *config) {
  if (!server || !config || !config->client_handler) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
  }

  memset(server, 0, sizeof(*server));
  server->handler = config->client_handler;
  server->user_data = config->user_data;
  server->port = config->port;
  atomic_store_bool(&server->running, true);

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
  info.gid = (gid_t)-1;                                // Cast to avoid undefined behavior with unsigned type
  info.uid = (uid_t)-1;                                // Cast to avoid undefined behavior with unsigned type
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT; // Initialize SSL/TLS support (required for server binding)
  info.extensions = NULL;                              // Disable permessage-deflate - causing connection issues
  info.retry_and_idle_policy = &keep_alive_policy; // Configure keep-alive to prevent idle disconnects during handshake

  // Increase per-thread service buffer to prevent fragmentation of large messages
  // Default is 4KB, causing 291KB frames to fragment into 74 × 4KB chunks
  // Increase to 512KB to allow larger WebSocket frames without fragmentation
  info.pt_serv_buf_size = 512 * 1024; // 512KB per-thread service buffer

  // Disable ALL default timeouts and keep-alive mechanisms
  // Use only the explicit retry_and_idle_policy we set above (30/35 seconds)
  info.ka_time = 0;      // Disable TCP keep-alive probes (use WebSocket pings instead)
  info.ka_probes = 0;    // Disable TCP probes
  info.ka_interval = 0;  // Disable TCP probe intervals
  info.keepalive_timeout = 0; // Disable HTTP keep-alive timeout entirely

  // Explicitly set a very large idle timeout to prevent early disconnection
  // Without this, libwebsockets defaults to 5 seconds for HTTP connections
  // Set to effectively infinite (100 hours) since we use retry_and_idle_policy instead
  info.timeout_secs = 360000; // 100 hours - effectively disabled

  // Create libwebsockets context
  server->context = lws_create_context(&info);
  if (!server->context) {
    return SET_ERRNO(ERROR_NETWORK_BIND, "Failed to create libwebsockets context");
  }

  // Create thread pool for handling client connections
  // Use 4 worker threads to handle concurrent client handlers without blocking LWS event loop
  server->handler_pool = thread_pool_create_with_workers("websocket_handlers", 4);
  if (!server->handler_pool) {
    lws_context_destroy(server->context);
    server->context = NULL;
    return SET_ERRNO(ERROR_THREAD, "Failed to create WebSocket handler thread pool");
  }

  /* Register WebSocket context with named registry, identified by port */
  char ws_port_name[32];
  snprintf(ws_port_name, sizeof(ws_port_name), "ws:%d", server->port);
  NAMED_REGISTER_WEBSOCKET(server->context, ws_port_name);

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
  while (atomic_load_bool(&server->running)) {
    // Service libwebsockets with 16ms timeout to match 60 FPS frame rate.
    // This provides frequent event processing (~60 callback invocations per second) so that
    // WebSocket frames queued by the render loop are sent promptly, matching TCP performance.
    // All client connections share this single server context, so a single lws_service() call
    // services fragments and events for all clients simultaneously.
    uint64_t service_start_ns = time_get_ns();
    if (last_service_ns && service_start_ns - last_service_ns > 30 * US_PER_MS_INT) {
      // > 30ms gap between service calls
      double gap_ms = (double)(service_start_ns - last_service_ns) / 1e6;
      log_info_every(1 * US_PER_MS_INT, "[LWS_SERVICE_GAP] %.1fms gap between lws_service calls", gap_ms);
    }
    service_call_count++;
    log_debug_every(500 * US_PER_MS_INT, "[LWS_SERVICE] Call #%d, context=%s", service_call_count,
                    NAMED_DESCRIBE(server->context, "websocket_server"));
    int result = lws_service(server->context, 16);
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
    NAMED_UNREGISTER(server->context);
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

  atomic_store_bool(&server->running, false);

  // Destroy handler thread pool (waits for pending work to complete)
  if (server->handler_pool) {
    thread_pool_destroy(server->handler_pool);
    server->handler_pool = NULL;
  }

  // Context is normally destroyed by websocket_server_run (from the event loop
  // thread) for fast shutdown. This handles the case where run() wasn't called
  // or didn't complete normally.
  if (server->context) {
    NAMED_UNREGISTER(server->context);
    log_debug("WebSocket context still alive in destroy, cleaning up");
    lws_context_destroy(server->context);
    server->context = NULL;
  }

  log_debug("WebSocket server destroyed");
}
