#include <ascii-chat/network/acip/backpressure.h>
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
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/log/websocket.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/debug/memory.h>
#include <ascii-chat/debug/named.h>
#include <libwebsockets.h>
#include <openssl/err.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>

/**
 * @brief Maximum incoming message queue size (frames received from peer)
 *
 * Set to 64 for balance: allows buffering across one large frame's fragments
 * while keeping latency reasonable. With ~70 fragments per 291KB frame,
 * we need at least 64 slots to avoid dropping the frame while it's fragmenting.
 */
#define WEBSOCKET_MESSAGE_QUEUE_SIZE_INCOMING 64

/**
 * @brief Maximum outgoing message queue size (frames to send to peer)
 *
 * Set to 64 to match incoming queue and prevent "BINARY after FIN" errors.
 * Large video frames fragment into ~70 chunks; queue must be large enough
 * to buffer one complete message worth of fragments.
 */
#define WEBSOCKET_MESSAGE_QUEUE_SIZE_OUTGOING 512

// Legacy names for backward compatibility
#define WEBSOCKET_RECV_QUEUE_SIZE WEBSOCKET_MESSAGE_QUEUE_SIZE_INCOMING
#define WEBSOCKET_SEND_QUEUE_SIZE WEBSOCKET_MESSAGE_QUEUE_SIZE_OUTGOING

// Shared internal types (websocket_recv_msg_t, websocket_transport_data_t)
#include <ascii-chat/network/websocket/internal.h>

// Forward declaration for libwebsockets callback
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);

// REMOVED: lws_service_with_timeout() was creating a race condition
// by spawning multiple threads that all call lws_service() on the same context.
// LibWebSockets is NOT thread-safe for concurrent calls.
//
// Instead: Service thread calls lws_service(1) directly in a tight loop.
// timeout=1 means: block up to 1ms on internal poll(), then return.
// This is fast enough to keep 60fps (16.6ms per frame budget) while
// letting the TLS state machine progress incrementally.

// =============================================================================
// Service Thread (Client-side only)
// =============================================================================

/**
 * @brief Request a writable callback when connected; return false after connection failure.
 */
static bool websocket_schedule_client_write(websocket_transport_data_t *ws_data, int loop_count) {
  // Check if THIS transport (client or server) has data queued to send
  // Note: we only check for CLIENT transports here (owns_context=true)
  // because SERVER transports are already handled by the SERVER_WRITEABLE callback.
  // Clients need explicit triggering via lws_callback_on_writable().
  if (ws_data->owns_context && ws_data->wsi) {
    if (loop_count <= 10) {
      log_debug_every(US_PER_SEC_INT, "[LOOP %d] CLIENT condition TRUE - checking queue", loop_count);
    }

    // Wait for connection to be established before sending
    // The CLIENT_ESTABLISHED callback sets is_connected = true
    // If we try to send before handshake completes, lws_write() fails
    mutex_lock(&ws_data->state_mutex);
    bool connected = ws_data->is_connected;
    mutex_unlock(&ws_data->state_mutex);

    if (!connected) {
      if (loop_count <= 10) {
        log_debug_every(US_PER_SEC_INT, "[LOOP %d] CLIENT not connected yet, skipping queue drain", loop_count);
      }

      // Check if connection attempt failed (CONNECTION_ERROR was called)
      // If so, break from service loop to avoid indefinite retry
      mutex_lock(&ws_data->state_mutex);
      bool failed = ws_data->connection_failed;
      mutex_unlock(&ws_data->state_mutex);

      if (loop_count <= 50) {
        log_debug_every(US_PER_SEC_INT, "[LOOP %d] Connection failed check: %d", loop_count, failed);
      }

      if (failed) {
        log_debug_every(US_PER_SEC_INT, "[LOOP %d] Connection attempt failed, exiting service thread", loop_count);
        return false;
      }

      // Do NOT sleep here - let lws_service() loop control timing
      // During TLS handshake, lws_service() needs to be called every 100us
      // Sleeping here blocks that and causes the handshake to stall
    } else {
      // This is a CLIENT transport - request WRITEABLE callback to send queued messages
      // lws_write() must be called from within LWS callbacks, not from external threads

      // Only request writeable callback when there is data queued to send.
      // Unconditionally requesting on every loop iteration causes a tight spam loop
      // of LWS_CALLBACK_CLIENT_WRITEABLE callbacks (~300/s), flooding logs and causing
      // deadlocks when stderr pipe fills and blocks the service thread while holding
      // the log mutex (other threads that try to log then deadlock).
      mutex_lock(&ws_data->send_mutex);
      bool has_data = !ringbuffer_is_empty(ws_data->send_queue);

      if (loop_count <= 10) {
        log_debug_every(US_PER_SEC_INT, "[LOOP %d] Queue check: has_data=%d", loop_count, has_data);
      }

      if (has_data) {
        lws_callback_on_writable(ws_data->wsi);
      }

      mutex_unlock(&ws_data->send_mutex);

      if (loop_count <= 10 && has_data) {
        log_debug_every(US_PER_SEC_INT, ">>> SERVICE_THREAD: CLIENT queue has data (callback will process it)");
      }
    } // End of if (connected) block
  } else {
    if (loop_count <= 10) {
      log_debug_every(US_PER_SEC_INT, "[LOOP %d] CLIENT condition FALSE (owns_context=%d, wsi=%p)", loop_count,
                      ws_data->owns_context, (void *)ws_data->wsi);
    }
  }

  return true;
}

/**
 * @brief Service thread that continuously processes libwebsockets events
 *
 * This thread is necessary for client-side transports to receive incoming messages.
 * It continuously calls lws_service() to process network events and trigger callbacks.
 */
static void *websocket_service_thread(void *arg) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)arg;

  log_debug_every(US_PER_SEC_INT, ">>> SERVICE_THREAD_START: owns_context=%d, wsi=%p, context=%p, send_queue=%p",
                  ws_data->owns_context, (void *)ws_data->wsi, (void *)ws_data->context, (void *)ws_data->send_queue);

  int loop_count = 0;
  uint64_t last_service_call = 0; // Force lws_service() on first iteration

  while (ws_data->service_running) {
    loop_count++;

    // Check if we're destroying FIRST before doing anything else
    // This prevents accessing invalid pointers or contexts
    if (atomic_load_bool(&ws_data->is_destroying) || !ws_data->context) {
      if (loop_count <= 10) {
        log_debug_every(US_PER_SEC_INT, "[LOOP %d] Destroying flag set or context NULL, exiting service thread",
                        loop_count);
      }
      break;
    }

    // ALWAYS log first 10 loops to debug why queue checks aren't working
    if (loop_count <= 10) {
      log_debug_every(US_PER_SEC_INT, "[LOOP %d] owns_context=%d, wsi=%p, service_running=%d", loop_count,
                      ws_data->owns_context, (void *)ws_data->wsi, ws_data->service_running);
    }

    if (!websocket_schedule_client_write(ws_data, loop_count)) {
      break;
    }

    // Service libwebsockets (processes network events, triggers callbacks)
    // Call frequently to handle both incoming AND outgoing data responsively
    // During TLS handshake: every 100us (1/10 of 1ms) for rapid protocol progression
    // During data transfer: every 1ms to balance responsiveness vs CPU usage
    uint64_t now_ns = time_get_ns();
    uint64_t time_since_last_service = now_ns - last_service_call;
    int result = 0;

    // Call lws_service() frequently during handshake, less frequently after connection
    // Use 100us interval during handshake, 1ms after connected
    uint64_t service_interval = ws_data->is_connected ? 1000000ULL : 100000ULL; // 1ms : 100us

    if (loop_count <= 10) {
      log_debug_every(US_PER_SEC_INT,
                      "[LOOP %d] Timing check: time_since_last=%lu us, interval=%lu us, will_service=%d", loop_count,
                      time_since_last_service / 1000, service_interval / 1000,
                      (int)(time_since_last_service >= service_interval));
    }

    if (time_since_last_service >= service_interval) {
      last_service_call = now_ns;
      uint64_t service_start_ns = now_ns;

      bool is_destroying = atomic_load_bool(&ws_data->is_destroying);
      if (!ws_data->context || is_destroying) {
        // Context is invalid or being destroyed, don't call lws_service
        if (loop_count <= 50) {
          log_debug_every(US_PER_SEC_INT, "[LOOP %d] Skipping lws_service: context=%p, destroying=%d", loop_count,
                          (void *)ws_data->context, (int)is_destroying);
        }
      } else {
        // Call lws_service with short timeout (1ms)
        // This lets lws_service() block briefly on internal poll() if there's no socket activity,
        // but returns quickly to let service loop continue if needed.
        // 1ms timeout is short enough to maintain 60fps (16.6ms per frame) while being responsive.
        //
        // Why timeout=1, not timeout=0?
        // - timeout=0: select() returns immediately, but TLS state machine might not progress
        // - timeout=1: select() blocks up to 1ms for socket events, letting TLS negotiate faster
        //   while still being responsive to multiple service calls per frame
        if (loop_count <= 10) {
          log_debug_every(US_PER_SEC_INT,
                          "[LOOP %d] >>> CALLING lws_service() NOW (is_connected=%d, wsi=%p, timeout=1ms)", loop_count,
                          ws_data->is_connected, (void *)ws_data->wsi);
        }

        result = lws_service(ws_data->context, 1);

        if (loop_count <= 10) {
          log_debug_every(US_PER_SEC_INT, "[LOOP %d] <<< lws_service() RETURNED (result=%d)", loop_count, result);
        }
      }
      uint64_t service_end_ns = time_get_ns();

      if (loop_count <= 50) {
        char service_duration_str[32];
        time_pretty(service_end_ns - service_start_ns, -1, service_duration_str, sizeof(service_duration_str));
        log_debug_every(US_PER_SEC_INT, "[LOOP %d] lws_service() returned %d, duration %s", loop_count, result,
                        service_duration_str);
      }

      if (result < 0) {
        log_fatal("🔴 lws_service error: %d at loop %d", result, loop_count);
        break;
      }
    } else {
      // Sleep briefly between lws_service() calls to avoid busy-waiting
      // During handshake: 0us sleep (busy-wait for maximum responsiveness during TLS)
      //   lws_service is now non-blocking, so tight polling won't block
      // During transfer: 100us sleep (normal cadence)
      uint64_t sleep_us = ws_data->is_connected ? 100 : 0;
      if (loop_count <= 50) {
        log_debug_every(US_PER_SEC_INT, "[LOOP %d] Time since service: %lu us, interval: %lu us - sleeping %lu us",
                        loop_count, time_since_last_service / 1000, service_interval / 1000, sleep_us);
      }
      if (sleep_us > 0) {
        platform_sleep_us(sleep_us);
      }
      // else: no sleep during handshake - busy-wait with non-blocking lws_service calls
    }

    // Check if connection is still alive
    if (loop_count <= 50 && ws_data->is_connected) {
      log_debug_every(US_PER_SEC_INT, "[LOOP %d] After lws_service: is_connected=true, wsi=%p", loop_count,
                      (void *)ws_data->wsi);
    } else if (loop_count <= 50) {
      log_debug_every(US_PER_SEC_INT, "[LOOP %d] After lws_service: is_connected=false, wsi=%p", loop_count,
                      (void *)ws_data->wsi);
    }
  }

  // log_debug("WebSocket service thread exiting");  // Disabled: causes AddressSanitizer heap-use-after-free crash
  return NULL;
}

// =============================================================================
// libwebsockets Callbacks
// =============================================================================

/**
 * @brief Publish a completed client connection and wake state waiters.
 */
static void websocket_on_established(struct lws *wsi, websocket_transport_data_t *ws_data) {
  // Don't process if we're in shutdown mode
  if (atomic_load_bool(&ws_data->is_destroying)) {
    return;
  }
  int send_buffer = SOCKET_STREAM_SEND_BUFFER;
  if (socket_setsockopt(lws_get_socket_fd(wsi), SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) != 0) {
    mutex_lock(&ws_data->state_mutex);
    ws_data->connection_failed = true;
    mutex_unlock(&ws_data->state_mutex);
    lws_set_timeout(wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
    return;
  }
  uint64_t now_ns = time_get_ns();
  log_fatal("🟢🟢🟢 WebSocket CLIENT_ESTABLISHED! wsi=%p, ws_data=%p, timestamp=%llu, elapsed_from_start=%llu",
            (void *)wsi, (void *)ws_data, (unsigned long long)now_ns, (unsigned long long)(now_ns / 1000000000ULL));
  if (ws_data) {
    mutex_lock(&ws_data->state_mutex);
    log_fatal("    [ESTABLISHED] Setting is_connected=true (was %d)", ws_data->is_connected);
    ws_data->is_connected = true;
    cond_signal(&ws_data->state_cond);
    mutex_unlock(&ws_data->state_mutex);
    log_fatal("    [ESTABLISHED] State updated, wsi=%p ready for send/recv", (void *)wsi);
  }
}

/**
 * @brief Queue an incoming client fragment for the blocking receiver.
 */
static int websocket_on_receive(struct lws *wsi, websocket_transport_data_t *ws_data, void *in, size_t len) {
  // Received data from server - may be fragmented for large messages
  if (atomic_load_bool(&ws_data->is_destroying)) {
    return 0;
  }
  uint64_t now_ns = time_get_ns();
  if (!in || len == 0) {
    log_debug_every(US_PER_SEC_INT, "CLIENT_RECEIVE: in=%p, len=%zu - skipping", in, len);
    return 0;
  }

  // Detect first/final bits from WebSocket frame using the LWS API
  bool is_first = lws_is_first_fragment(wsi);
  bool is_final = lws_is_final_fragment(wsi);

  log_debug_every(US_PER_SEC_INT,
                  "🟡 LWS_CALLBACK_CLIENT_RECEIVE: %zu bytes (first=%d, final=%d), wsi=%p, timestamp=%llu", len,
                  is_first, is_final, (void *)wsi, (unsigned long long)now_ns);

  // Queue this fragment immediately with first/final flags.
  // Per LWS design, each fragment is processed individually by the callback.
  // We must NOT manually reassemble fragments - that breaks LWS's internal state machine.
  // Instead, queue each fragment with metadata, and let the receiver decide on reassembly.

  websocket_recv_msg_t msg;
  msg.data = buffer_alloc(NULL, len);
  if (!msg.data) {
    log_error("Failed to allocate buffer for fragment (%zu bytes)", len);
    return -1;
  }

  memcpy(msg.data, in, len);
  msg.len = len;
  msg.first = is_first;
  msg.final = is_final;

  mutex_lock(&ws_data->recv_mutex);
  bool success = ringbuffer_write(ws_data->recv_queue, &msg);
  if (!success) {
    // Closing preserves packet boundaries when a fragment cannot be admitted.
    log_warn("WebSocket receive queue full - closing connection (len=%zu, first=%d, final=%d)", len, is_first,
             is_final);
    buffer_release(msg.data);
    mutex_unlock(&ws_data->recv_mutex);
    return -1;
  }

  // Signal waiting recv() call that a fragment is available
  cond_signal(&ws_data->recv_cond);
  mutex_unlock(&ws_data->recv_mutex);
  return 0;
}

/**
 * @brief Publish connection closure and wake blocked receivers.
 */
static void websocket_on_closed(struct lws *wsi, websocket_transport_data_t *ws_data,
                                enum lws_callback_reasons reason) {
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
}

/**
 * @brief Report a failed connection and wake connection and receive waiters.
 */
static void websocket_on_connection_error(struct lws *wsi, websocket_transport_data_t *ws_data,
                                          enum lws_callback_reasons reason, void *in) {
  uint64_t now_ns = time_get_ns();

  // Get detailed OpenSSL error if available
  unsigned long ssl_err = ERR_get_error();
  char ssl_err_str[256] = "no SSL error";
  if (ssl_err) {
    ERR_error_string_n(ssl_err, ssl_err_str, sizeof(ssl_err_str));
  }

  log_fatal(
      "🔴🔴🔴 WebSocket CONNECTION ERROR! reason=%d, error=%s, wsi=%p, ws_data=%p, ssl_err=%lu (%s), timestamp=%llu",
      reason, in ? (const char *)in : "unknown", (void *)wsi, (void *)ws_data, ssl_err, ssl_err_str,
      (unsigned long long)now_ns);
  if (ws_data) {
    mutex_lock(&ws_data->state_mutex);
    ws_data->is_connected = false;
    ws_data->connection_failed = true; // Signal service thread to exit
    cond_signal(&ws_data->state_cond); // Wake anyone waiting on connection
    mutex_unlock(&ws_data->state_mutex);

    // Wake any blocking recv() calls
    cond_broadcast(&ws_data->recv_cond);
  }
}

/**
 * @brief Drain client messages from the send queue on the service thread.
 */
void websocket_transport_write_pending(struct lws *wsi, websocket_transport_data_t *ws_data) {
  if (!ws_data || atomic_load_bool(&ws_data->is_destroying))
    return;
  /* lws suppresses writable callbacks until previously buffered output is
   * consumed. Keep one owner until this completion boundary, or connection
   * teardown; elapsed service-loop iterations are not a completion signal. */
  websocket_recv_msg_t msg = {0};
  mutex_lock(&ws_data->send_mutex);
  buffer_release(ws_data->inflight_buffer);
  ws_data->inflight_buffer = NULL;
  ws_data->pending_send_bytes -= ws_data->inflight_bytes;
  ws_data->inflight_bytes = 0;
  bool available = ringbuffer_read(ws_data->send_queue, &msg);
  if (available)
    ws_data->inflight_buffer = msg.data;
  ws_data->inflight_bytes = msg.len;
  mutex_unlock(&ws_data->send_mutex);
  if (!available)
    return;
  int written = lws_write(wsi, msg.data + LWS_PRE, msg.len, LWS_WRITE_BINARY);
  if (written < 0 || (size_t)written < msg.len) {
    mutex_lock(&ws_data->state_mutex);
    ws_data->is_connected = false;
    ws_data->connection_failed = true;
    cond_broadcast(&ws_data->state_cond);
    mutex_unlock(&ws_data->state_mutex);
    lws_set_timeout(wsi, PENDING_TIMEOUT_CLOSE_SEND, LWS_TO_KILL_ASYNC);
    return;
  }
  /* Exactly one write per callback. Request a completion callback even if
   * there is no next message, so the final owner is promptly released. */
  lws_callback_on_writable(wsi);
}

static void websocket_on_writable(struct lws *wsi, websocket_transport_data_t *ws_data) {
  websocket_transport_write_pending(wsi, ws_data);
}

/**
 * @brief libwebsockets callback - handles all WebSocket events
 *
 * This is the main callback function that libwebsockets uses to notify us
 * of events like connection, message arrival, closure, etc.
 */
static int websocket_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  websocket_transport_data_t *ws_data = wsi ? lws_get_opaque_user_data(wsi) : NULL;
  if (!ws_data) {
    ws_data = (websocket_transport_data_t *)user;
  }

  // Log all callback reasons for TLS investigation
  static uint64_t callback_count = 0;
  uint64_t now_ns = time_get_ns();
  if (reason == LWS_CALLBACK_PROTOCOL_INIT || reason == LWS_CALLBACK_CLIENT_ESTABLISHED ||
      reason == LWS_CALLBACK_CLIENT_WRITEABLE || reason == LWS_CALLBACK_CLIENT_RECEIVE ||
      reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR || reason == LWS_CALLBACK_CLOSED) {
    callback_count++;
    log_debug_every(US_PER_SEC_INT, ">>> CALLBACK #%lu: reason=%d (0x%x), wsi=%p, len=%zu, timestamp=%llu",
                    callback_count, reason, reason, (void *)wsi, len, (unsigned long long)now_ns);
  }

  // Validate ws_data exists before accessing it (prevents heap-buffer-overflow in TLS callbacks)
  // For unknown callbacks (especially TLS internal callbacks from OpenSSL), return early
  if (!ws_data) {
    return 0;
  }

  switch (reason) {
  case LWS_CALLBACK_CLIENT_ESTABLISHED:
    websocket_on_established(wsi, ws_data);
    break;

  case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH: {
    log_debug_every(US_PER_SEC_INT,
                    ">>> CALLBACK: LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH (wsi=%p) - WebSocket upgrade starting",
                    (void *)wsi);
    break;
  }

  case LWS_CALLBACK_PROTOCOL_INIT: {
    log_debug_every(US_PER_SEC_INT, ">>> CALLBACK: LWS_CALLBACK_PROTOCOL_INIT (wsi=%p, ws_data=%p)", (void *)wsi,
                    (void *)ws_data);
    if (atomic_load_bool(&ws_data->is_destroying)) {
      return 0;
    }
    break;
  }

  case LWS_CALLBACK_CLIENT_RECEIVE:
    return websocket_on_receive(wsi, ws_data, in, len);

  case LWS_CALLBACK_CLIENT_CLOSED:
  case LWS_CALLBACK_CLOSED:
    websocket_on_closed(wsi, ws_data, reason);
    break;

  case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
    websocket_on_connection_error(wsi, ws_data, reason, in);
    break;

  case LWS_CALLBACK_CLIENT_WRITEABLE:
    websocket_on_writable(wsi, ws_data);
    break;

  default:
    break;
  }

  return 0;
}

// =============================================================================
// WebSocket Transport Methods
// =============================================================================

/**
 * @brief Encrypt non-handshake packets when needed; return a borrowed payload or an owned buffer.
 */
static asciichat_error_t websocket_prepare_payload(acip_transport_t *transport, buffer_domain_t *domain,
                                                   const void *data, size_t len, const void **out_data, size_t *out_len,
                                                   uint8_t **out_encrypted_packet, size_t *out_encrypted_size) {
  *out_data = data;
  *out_len = len;
  *out_encrypted_packet = NULL;
  *out_encrypted_size = 0;
  socket_buffer_t slice = {data, len};
  acip_packet_plan_t plan;
  asciichat_error_t result = acip_packet_plan(transport->crypto_ctx, &slice, 1, &plan);
  if (result != ASCIICHAT_OK || !plan.encrypt)
    return result;
  size_t capacity = sizeof(packet_header_t) + plan.ciphertext_capacity;
  uint8_t *packet = buffer_alloc(domain, capacity);
  if (!packet)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate WebSocket ciphertext");
  packet_header_t header;
  size_t length = 0;
  result = acip_packet_encrypt(transport->crypto_ctx, &plan, domain, packet + sizeof(header), plan.ciphertext_capacity,
                               &header, &length);
  if (result != ASCIICHAT_OK) {
    buffer_release(packet);
    return result;
  }
  memcpy(packet, &header, sizeof(header));
  *out_data = packet;
  *out_len = sizeof(header) + length;
  *out_encrypted_packet = packet;
  *out_encrypted_size = *out_len;
  return ASCIICHAT_OK;
}

/**
 * @brief Copy a padded message into the appropriate send queue and wake its service thread.
 */
static asciichat_error_t websocket_queue_payload(websocket_transport_data_t *ws_data, buffer_domain_t *domain,
                                                 const void *send_data, size_t send_len) {
  // Both sides retain padded wire data until the owning service callback sends it.
  websocket_recv_msg_t msg = {.data = buffer_alloc(domain, LWS_PRE + send_len), .len = send_len,
                              .first = 1, .final = 1};
  if (!msg.data)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate WebSocket send buffer");
  memcpy(msg.data + LWS_PRE, send_data, send_len);
  mutex_lock(&ws_data->state_mutex);
  if (atomic_load_bool(&ws_data->is_destroying) || ws_data->connection_failed || !ws_data->context) {
    mutex_unlock(&ws_data->state_mutex);
    buffer_release(msg.data);
    return SET_ERRNO(ERROR_NETWORK, "WebSocket closed before send admission");
  }
  mutex_lock(&ws_data->send_mutex);
  bool admitted = ringbuffer_write(ws_data->send_queue, &msg);
  if (admitted)
    ws_data->pending_send_bytes += send_len;
  mutex_unlock(&ws_data->send_mutex);
  // Context detachment takes state_mutex too; cancellation cannot use a stale context.
  if (admitted)
    lws_cancel_service(ws_data->context);
  mutex_unlock(&ws_data->state_mutex);
  if (!admitted) {
    buffer_release(msg.data);
    return SET_ERRNO(ERROR_NETWORK, "WebSocket send queue full");
  }
  return ASCIICHAT_OK;
}

asciichat_error_t websocket_transport_send(acip_transport_t *transport, const void *data, size_t len) {
  // NOTE: Disabled START_TIMER/STOP_TIMER due to thread safety issues with timer hashtable
  // causing heap-use-after-free crashes in ASAN. The timer system has race conditions
  // when accessed from the service thread context. See: line 325 (similar issue).
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  // For server-side transports (owns_context=false), the connection is already established.
  // For client-side transports, packets may be queued before the asynchronous WebSocket
  // handshake completes. The service thread drains that queue after CLIENT_ESTABLISHED.
  if (ws_data->owns_context) {
    // Check connection state without blocking the service thread.
    mutex_lock(&ws_data->state_mutex);
    bool connected = ws_data->is_connected;
    bool connection_failed = ws_data->connection_failed;
    mutex_unlock(&ws_data->state_mutex);

    if (connection_failed) {
      log_error("Connection failed - cannot send");
      return SET_ERRNO(ERROR_NETWORK, "WebSocket connection failed");
    }

    if (!connected) {
      log_debug("Connection is opening; queueing packet for delivery after handshake");
    }

    log_dev_every(1000000, "websocket_send (client): is_connected=%d, wsi=%p, send_len=%zu", connected,
                  (void *)ws_data->wsi, len);
  } else {
    log_dev_every(4500 * US_PER_MS_INT,
                  "[WEBSOCKET_SEND_SERVER] Server transport send: wsi=%p, len=%zu (bypassing is_connected check)",
                  (void *)ws_data->wsi, len);
  }

  // Classify the original packet before encryption hides its type. Keep this
  // domain through every allocation, including the asynchronous queue owner.
  buffer_domain_t *domain = NULL;
  if (data && len >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, data, sizeof(header));
    packet_type_t type = NET_TO_HOST_U16(header.type);
    if (packet_is_handshake_type(type) || type == PACKET_TYPE_ERROR_MESSAGE)
      domain = buffer_control_domain();
  }
  const void *send_data = NULL;
  size_t send_len = 0;
  uint8_t *encrypted_packet = NULL;
  size_t encrypted_packet_size = 0;
  asciichat_error_t result = websocket_prepare_payload(transport, domain, data, len, &send_data, &send_len,
                                                       &encrypted_packet, &encrypted_packet_size);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  result = websocket_queue_payload(ws_data, domain, send_data, send_len);
  if (encrypted_packet) {
    buffer_release(encrypted_packet);
  }
  return result;
}

/**
 * @brief Wait for the asynchronous client upgrade before receiving; server transports skip this gate.
 */
static asciichat_error_t websocket_wait_for_connection(websocket_transport_data_t *ws_data) {
  // Client transports are returned before the asynchronous WebSocket upgrade
  // completes, so their first recv() must wait for the connection state. A
  // server transport is created from LWS_CALLBACK_ESTABLISHED and may already
  // have queued the client's first packet before its handler starts; applying
  // the client-only gate there can hide that packet after an early close.
  if (ws_data->owns_context) {
    mutex_lock(&ws_data->state_mutex);
    bool connected = ws_data->is_connected;
    bool connection_failed = ws_data->connection_failed;
    uint64_t start_time = time_get_ns();

    if (!connected && !connection_failed) {
      // Connection not yet established and hasn't failed - wait for it with timeout
      log_debug_every(US_PER_SEC_INT, "WEBSOCKET_RECV: Connection not yet established, waiting (timeout=5s)");
      // Wait up to 5 seconds for the state_cond to signal that is_connected changed
      int wait_result = cond_timedwait(&ws_data->state_cond, &ws_data->state_mutex, 5 * 1000000000ULL);

      uint64_t elapsed_ns = time_get_ns() - start_time;
      uint64_t elapsed_ms = elapsed_ns / 1000000ULL;

      log_debug_every(US_PER_SEC_INT, "WEBSOCKET_RECV: Wait returned (result=%d, elapsed=%llums)", wait_result,
                      (unsigned long long)elapsed_ms);

      // Re-check after wait
      connected = ws_data->is_connected;
      connection_failed = ws_data->connection_failed;

      log_debug_every(US_PER_SEC_INT, "WEBSOCKET_RECV: After wait: connected=%d, connection_failed=%d", connected,
                      connection_failed);
    }

    mutex_unlock(&ws_data->state_mutex);

    if (connection_failed && !connected) {
      log_error("🔴 WEBSOCKET_RECV: Connection failed during establishment");
      return SET_ERRNO(ERROR_NETWORK, "WebSocket connection failed");
    }

    if (!connected) {
      log_error("🔴 WEBSOCKET_RECV: Connection timeout (waited 5 seconds)");
      return SET_ERRNO(ERROR_NETWORK, "WebSocket connection timeout");
    }
  }

  return ASCIICHAT_OK;
}

/**
 * @brief Transfer one assembled packet, save trailing bytes, and release recv_mutex.
 */
static asciichat_error_t websocket_finish_packet_locked(websocket_transport_data_t *ws_data, uint8_t *assembled_buffer,
                                                        size_t assembled_size, size_t expected_size, void **buffer,
                                                        size_t *out_len, void **out_allocated_buffer) {
  // Check for leftover data after this packet
  size_t leftover_size = assembled_size - expected_size;
  if (leftover_size > 0) {
    // Save leftover data for next recv() call
    log_debug_every(US_PER_SEC_INT, "[WS_REASSEMBLE] Saving %zu bytes leftover for next recv() call", leftover_size);

    // Create buffer for leftover (or reuse if we can)
    uint8_t *leftover_buffer = buffer_alloc(NULL, leftover_size);
    if (leftover_buffer) {
      memcpy(leftover_buffer, assembled_buffer + expected_size, leftover_size);
      ws_data->partial_buffer = leftover_buffer;
      ws_data->partial_size = leftover_size;
      ws_data->partial_capacity = leftover_size;
    } else {
      log_error("[WS_REASSEMBLE] Failed to allocate leftover buffer (%zu bytes)", leftover_size);
      buffer_release(assembled_buffer);
      mutex_unlock(&ws_data->recv_mutex);
      return SET_ERRNO(ERROR_MEMORY, "Cannot preserve WebSocket packet boundary");
    }
  }

  // Capacity metadata lets the receiver own the original assembly even when
  // its backing allocation is larger than the logical packet.
  (void)buffer_set_size(assembled_buffer, expected_size);
  *buffer = assembled_buffer;
  *out_len = expected_size;
  *out_allocated_buffer = assembled_buffer;
  mutex_unlock(&ws_data->recv_mutex);
  return ASCIICHAT_OK;
}

/**
 * @brief Grow the assembly buffer and consume a fragment; the caller owns the assembly on error.
 */
static asciichat_error_t websocket_append_fragment(websocket_recv_msg_t frag, uint8_t **assembly_buffer,
                                                   size_t *assembly_size, size_t *assembly_capacity) {
  uint8_t *assembled_buffer = *assembly_buffer;
  size_t assembled_size = *assembly_size;
  size_t assembled_capacity = *assembly_capacity;
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
      buffer_release(frag.data);
      return SET_ERRNO(ERROR_NETWORK, "WebSocket frame exceeds maximum size (4MB)");
    }

    uint8_t *new_buffer = buffer_alloc(NULL, new_capacity);
    if (!new_buffer) {
      log_error("[WS_REASSEMBLE] Failed to allocate reassembly buffer (%zu bytes)", new_capacity);
      buffer_release(frag.data);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate fragment reassembly buffer");
    }

    // Copy existing data to new buffer
    if (assembled_size > 0) {
      memcpy(new_buffer, assembled_buffer, assembled_size);
    }

    // Free old buffer
    if (assembled_buffer) {
      buffer_release(assembled_buffer);
    }

    assembled_buffer = new_buffer;
    assembled_capacity = new_capacity;
    *assembly_buffer = assembled_buffer;
    *assembly_capacity = assembled_capacity;
  }

  // Append fragment data with bounds checking
  if (frag.len > 0 && frag.data) {
    // Safety check: ensure we don't overflow the buffer
    if (assembled_size + frag.len > assembled_capacity) {
      log_error("[WS_REASSEMBLE] CRITICAL: Buffer overflow detected! assembled_size=%zu, frag.len=%zu, capacity=%zu",
                assembled_size, frag.len, assembled_capacity);
      buffer_release(frag.data);
      return SET_ERRNO(ERROR_MEMORY, "Fragment reassembly buffer overflow");
    }

    memcpy(assembled_buffer + assembled_size, frag.data, frag.len);
    assembled_size += frag.len;
  }

  // Free fragment data after copying (allocated in LWS callback with buffer_alloc)
  if (frag.data) {
    // Use the fragment length that was stored when we allocated it
    // This must match the size passed to buffer_alloc in the LWS callback
    buffer_release(frag.data);
    frag.data = NULL; // Prevent accidental double-free
  }

  *assembly_size = assembled_size;
  return ASCIICHAT_OK;
}

static asciichat_error_t websocket_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                        void **out_allocated_buffer) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  asciichat_error_t result = websocket_wait_for_connection(ws_data);
  if (result != ASCIICHAT_OK) {
    return result;
  }

  mutex_lock(&ws_data->recv_mutex);

  // Even if connection is closed, we should try to deliver any buffered data
  // Check if there's data in the queue
  bool has_queued_data = !ringbuffer_is_empty(ws_data->recv_queue);

  // Connection is established at this point, but might close during data transfer
  // Deliver buffered data if available, otherwise wait
  if (!has_queued_data && ws_data->partial_size == 0) {
    // Check connection state again
    mutex_lock(&ws_data->state_mutex);
    bool still_connected = ws_data->is_connected;
    mutex_unlock(&ws_data->state_mutex);

    if (!still_connected) {
      uint64_t now_ns = time_get_ns();
      log_fatal("🔴 WEBSOCKET_RECV: Connection closed! has_queued_data=%d, partial_size=%zu, wsi=%p, "
                "timestamp=%llu",
                has_queued_data, ws_data->partial_size, (void *)ws_data->wsi, (unsigned long long)now_ns);
      mutex_unlock(&ws_data->recv_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Connection closed");
    }
  }

  // Reassemble fragmented WebSocket messages with SHORT timeout
  // We queue each fragment from the LWS callback with first/final flags.
  // Key insight: if we wait too long for final fragment, connection times out.
  // Return partial messages quickly (500ms) to avoid blocking handler thread.
  //
  // Use persistent partial_buffer to handle packet boundaries
  // that don't align with WebSocket frame boundaries. This prevents data loss
  // when a single WebSocket message contains multiple ACIP packets.

  uint8_t *assembled_buffer = NULL;
  size_t assembled_size = 0;
  size_t assembled_capacity = 0;
  int fragment_count = 0;

  // Start with leftover data from previous recv() call if available
  if (ws_data->partial_size > 0) {
    log_debug_every(US_PER_SEC_INT, "[WS_REASSEMBLE] Starting with leftover data from previous packet: %zu bytes",
                    ws_data->partial_size);
    assembled_buffer = ws_data->partial_buffer;
    assembled_size = ws_data->partial_size;
    assembled_capacity = ws_data->partial_capacity;

    // Clear the partial buffer state so next call starts fresh if no more leftovers
    ws_data->partial_buffer = NULL;
    ws_data->partial_size = 0;
    ws_data->partial_capacity = 0;
  }

  uint64_t started = time_get_ns();
  uint64_t timeout = transport->receive_timeout_ns ? transport->receive_timeout_ns : NS_PER_SEC_INT;
  while (true) {
    // Wait for fragment if queue is empty
    while (ringbuffer_is_empty(ws_data->recv_queue)) {
      // Don't timeout waiting for fragments. If the connection is dead, libwebsockets will
      // set is_connected=false and we'll detect it below. Arbitrary timeouts just cause
      // unnecessary disconnections while data is actually being transmitted.

      if (time_get_ns() - started >= timeout) {
        ws_data->partial_buffer = assembled_buffer;
        ws_data->partial_size = assembled_size;
        ws_data->partial_capacity = assembled_capacity;
        mutex_unlock(&ws_data->recv_mutex);
        return SET_ERRNO(assembled_size ? ERROR_NETWORK_INCOMPLETE : ERROR_NETWORK_TIMEOUT,
                         "WebSocket receive deadline expired");
      }
      // Check connection state - but don't fail immediately if connection closes
      // while reassembling. Instead, return what we have so the handler can process it.
      // This prevents losing buffered data due to connection timeouts.
      mutex_lock(&ws_data->state_mutex);
      bool still_connected = ws_data->is_connected;
      mutex_unlock(&ws_data->state_mutex);

      if (!still_connected && assembled_size > 0) {
        // Connection closed but we have partial data - return it
        log_debug_every(US_PER_SEC_INT,
                        "[WS_REASSEMBLE] Connection closed mid-reassembly, returning %zu bytes received so far",
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
          buffer_release(assembled_buffer);
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
        buffer_release(assembled_buffer);
      }
      mutex_unlock(&ws_data->recv_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Failed to read fragment from queue");
    }

    fragment_count++;
    if (frag.len > 100 || fragment_count == 1) {
      log_dev_every(US_PER_SEC_INT, "[WS_REASSEMBLE] Fragment #%d: %zu bytes, first=%d, final=%d, assembled_so_far=%zu",
                    fragment_count, frag.len, frag.first, frag.final, assembled_size);
    } else {
      log_debug_every(US_PER_SEC_INT, "[WS_REASSEMBLE] Fragment #%d: %zu bytes, first=%d, final=%d", fragment_count,
                      frag.len, frag.first, frag.final);
    }

    // Sanity check: first fragment must have first=1, continuations must have first=0
    if (assembled_size == 0 && !frag.first) {
      log_error("[WS_REASSEMBLE] ERROR: Expected first=1 for first fragment, got first=%d", frag.first);
      buffer_release(frag.data);
      if (assembled_buffer) {
        buffer_release(assembled_buffer);
      }
      mutex_unlock(&ws_data->recv_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Protocol error: continuation fragment without first fragment");
    }

    result = websocket_append_fragment(frag, &assembled_buffer, &assembled_size, &assembled_capacity);
    if (result != ASCIICHAT_OK) {
      if (assembled_buffer) {
        buffer_release(assembled_buffer);
      }
      mutex_unlock(&ws_data->recv_mutex);
      return result;
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
          log_dev_every(1000000, "[WS_REASSEMBLE] Complete message: %zu bytes in %d fragments (payload=%u)",
                        expected_size, fragment_count, msg_payload_len);

          return websocket_finish_packet_locked(ws_data, assembled_buffer, assembled_size, expected_size, buffer,
                                                out_len, out_allocated_buffer);
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
      if (assembled_size >= 14) { // Minimum to read ACIP length field
        const uint8_t *data = (const uint8_t *)assembled_buffer;
        uint32_t msg_payload_len = (data[10] << 24) | (data[11] << 16) | (data[12] << 8) | data[13];
        const size_t HEADER_SIZE = 22;
        size_t expected_size = HEADER_SIZE + msg_payload_len;

        if (assembled_size >= expected_size) {
          // We have a complete ACIP packet
          log_dev_every(1000000, "[WS_REASSEMBLE] Complete ACIP packet: %zu bytes in %d fragments", expected_size,
                        fragment_count);

          return websocket_finish_packet_locked(ws_data, assembled_buffer, assembled_size, expected_size, buffer,
                                                out_len, out_allocated_buffer);
        } else {
          // ACIP packet is incomplete even though WebSocket frame is final
          // This shouldn't happen - WebSocket is delivering corrupt data
          log_error("[WS_REASSEMBLE] ERROR: WebSocket final fragment but incomplete ACIP packet (have %zu, need %zu)",
                    assembled_size, expected_size);
          buffer_release(assembled_buffer);
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

asciichat_error_t acip_websocket_transport_flush(acip_transport_t *transport, uint64_t timeout_ns) {
  if (!transport || acip_transport_get_type(transport) != ACIP_TRANSPORT_WEBSOCKET)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Expected WebSocket transport");
  websocket_transport_data_t *ws_data = transport->impl_data;
  uint64_t started = time_get_ns();
  while (acip_transport_is_connected(transport)) {
    mutex_lock(&ws_data->send_mutex);
    bool drained = ringbuffer_is_empty(ws_data->send_queue) && !ws_data->inflight_buffer;
    mutex_unlock(&ws_data->send_mutex);
    if (drained)
      return ASCIICHAT_OK;
    if (time_get_ns() - started >= timeout_ns)
      return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "WebSocket send queue did not drain");
    platform_sleep_us(1000);
  }
  return SET_ERRNO(ERROR_NETWORK, "WebSocket closed before send queue drained");
}

static void websocket_interrupt(acip_transport_t *transport) {
  websocket_transport_data_t *ws_data = transport->impl_data;
  atomic_store_bool(&ws_data->is_destroying, true);
  mutex_lock(&ws_data->state_mutex);
  ws_data->is_connected = false;
  cond_broadcast(&ws_data->state_cond);
  if (ws_data->context)
    lws_cancel_service(ws_data->context);
  mutex_unlock(&ws_data->state_mutex);
  // Receivers release recv_mutex while waiting; acquire it to avoid lost wakes.
  mutex_lock(&ws_data->recv_mutex);
  cond_broadcast(&ws_data->recv_cond);
  mutex_unlock(&ws_data->recv_mutex);
}

static asciichat_error_t websocket_close(acip_transport_t *transport) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;
  websocket_interrupt(transport);
  // Client contexts are exclusive; server contexts close on their writable callback.
  if (ws_data->service_running) {
    asciichat_error_t result = asciichat_thread_join(&ws_data->service_thread, NULL);
    if (result != ASCIICHAT_OK)
      return result;
    ws_data->service_running = false;
  }
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

static bool websocket_has_pending_data(acip_transport_t *transport) {
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;
  if (!ws_data || !ws_data->recv_queue) {
    return false;
  }

  mutex_lock(&ws_data->recv_mutex);
  bool has_data = !ringbuffer_is_empty(ws_data->recv_queue);
  mutex_unlock(&ws_data->recv_mutex);
  return has_data;
}

// =============================================================================
// WebSocket Transport Destroy Implementation
// =============================================================================

/**
 * @brief Discard queued messages and partial data after the service thread and context have stopped.
 */
static void websocket_discard_buffers(websocket_transport_data_t *ws_data) {
  // Clear receive queue and free buffered messages
  if (ws_data->recv_queue) {
    mutex_lock(&ws_data->recv_mutex);

    websocket_recv_msg_t recv_msg;
    while (ringbuffer_read(ws_data->recv_queue, &recv_msg)) {
      if (recv_msg.data) {
        buffer_release(recv_msg.data);
      }
    }

    mutex_unlock(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->recv_queue);
    ws_data->recv_queue = NULL;

    if (ws_data->send_queue) {
      // Drain send queue before destroying to free allocated message data
      // Use buffer_release to match buffer_alloc used for send messages
      websocket_recv_msg_t send_msg;
      while (ringbuffer_read(ws_data->send_queue, &send_msg)) {
        if (send_msg.data) {
          buffer_release(send_msg.data);
        }
      }
      ringbuffer_destroy(ws_data->send_queue);
      ws_data->send_queue = NULL;
    }
  }

  // Free partial buffer (leftover data from previous recv())
  if (ws_data->partial_buffer) {
    buffer_release(ws_data->partial_buffer);
    ws_data->partial_buffer = NULL;
    ws_data->partial_size = 0;
    ws_data->partial_capacity = 0;
  }
}

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

  // Server transport callbacks retain their storage until CLOSED has detached it.
  while (atomic_load_bool(&ws_data->server_handler_owned) && !atomic_load_bool(&ws_data->server_detached))
    platform_sleep_ms(1);

  // Mark transport as destroying and broadcast condition variables
  // This signals all waiting threads to wake and exit naturally.
  log_debug("[WEBSOCKET_DESTROY] Marking transport as destroying");
  atomic_store_bool(&ws_data->is_destroying, true);

  // Broadcast all condition variables to wake waiting threads so they can check
  // the is_destroying flag and exit gracefully
  cond_broadcast(&ws_data->state_cond);
  cond_broadcast(&ws_data->recv_cond);

  // Cancel any pending libwebsockets service calls to interrupt blocking lws_service()
  // This prevents the service thread from getting stuck in lws_service()
  if (ws_data->context) {
    log_debug("[WEBSOCKET_DESTROY] Cancelling libwebsockets service");
    lws_cancel_service(ws_data->context);
  }

  // Stop service thread (client-side only)
  if (ws_data->service_running) {
    log_debug("Stopping WebSocket service thread");
    ws_data->service_running = false;
    asciichat_thread_join(&ws_data->service_thread, NULL);
    log_debug("WebSocket service thread stopped");
  }

  // Close WebSocket connection gracefully before destroying context
  // Skip lws_wsi_close() to avoid segfault in libwebsockets during cleanup
  // The context_destroy below will handle cleanup properly
  ws_data->wsi = NULL;

  // Destroy WebSocket context (only if we own it - client transports only)
  if (ws_data->context && ws_data->owns_context) {
    lws_context_destroy(ws_data->context);
    ws_data->context = NULL;
  }

  buffer_release(ws_data->inflight_buffer);
  websocket_discard_buffers(ws_data);

  // Destroy synchronization primitives
  cond_destroy(&ws_data->state_cond);
  mutex_destroy(&ws_data->state_mutex);
  cond_destroy(&ws_data->recv_cond);
  mutex_destroy(&ws_data->recv_mutex);
  mutex_destroy(&ws_data->send_mutex);

  // Deregister websocket implementation data
  NAMED_UNREGISTER(&ws_data->is_destroying);
  NAMED_UNREGISTER(&ws_data->server_handler_owned);
  NAMED_UNREGISTER(&ws_data->server_detached);
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

static size_t websocket_queued_bytes(acip_transport_t *transport) {
  websocket_transport_data_t *ws = transport->impl_data;
  mutex_lock(&ws->send_mutex);
  size_t bytes = ws->pending_send_bytes;
  mutex_unlock(&ws->send_mutex);
  return bytes;
}

static const acip_transport_methods_t websocket_methods = {
    .send = websocket_transport_send,
    .queued_bytes = websocket_queued_bytes,
    .reserved_bytes = SOCKET_STREAM_SEND_RESERVE,
    .recv = websocket_recv,
    .close = websocket_close,
    .interrupt = websocket_interrupt,
    .get_type = websocket_get_type,
    .get_socket = websocket_get_socket,
    .is_connected = websocket_is_connected,
    .has_pending_data = websocket_has_pending_data,
    .destroy_impl = websocket_destroy_impl,
};

// =============================================================================
// WebSocket Transport Creation
// =============================================================================

typedef struct {
  char host[256];
  char path[256];
  int port;
  bool use_ssl;
} websocket_endpoint_t;

/**
 * @brief Parse the client endpoint without changing URL validation or default ports.
 */
static asciichat_error_t websocket_parse_endpoint(const char *url, websocket_endpoint_t *endpoint) {
  // Parse URL to extract host, port, and path
  // Format: ws://host:port/path or wss://host:port/path
  const char *protocol_end = strstr(url, "://");
  if (!protocol_end) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid WebSocket URL format (missing ://)");
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
      return SET_ERRNO(ERROR_INVALID_PARAM, "Host name too long");
    }
    memcpy(host, host_start, host_len);
    host[host_len] = '\0';

    // Extract port
    char *endptr;
    errno = 0;
    long port_val = strtol(port_start + 1, &endptr, 10);
    if (*endptr != '\0' || errno != 0 || port_val <= 0 || port_val > 65535) {
      log_error("Invalid port number: %s", port_start + 1);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid port number");
    }
    port = (uint16_t)port_val;
  } else {
    // No port specified, use default
    size_t host_len = path_start ? (size_t)(path_start - host_start) : strlen(host_start);
    if (host_len >= sizeof(host)) {
      return SET_ERRNO(ERROR_INVALID_PARAM, "Host name too long");
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

  SAFE_STRNCPY(endpoint->host, host, sizeof(endpoint->host));
  SAFE_STRNCPY(endpoint->path, path, sizeof(endpoint->path));
  endpoint->port = port;
  endpoint->use_ssl = use_ssl;
  return ASCIICHAT_OK;
}

/**
 * @brief Create a client context with persistent protocols, buffering, and keep-alive policy.
 */
static struct lws_context *websocket_create_client_context(void) {
  // Create libwebsockets context
  // Protocol array must persist for lifetime of context - use static
  // Register "http" protocol for WebSocket connections - matches server-side protocol name
  // This ensures libwebsockets will invoke our websocket_callback during upgrade handshake and communication
  static struct lws_protocols client_protocols[] = {
      {
          "http", // Default HTTP protocol - handles WebSocket upgrade and ACIP communication
          websocket_callback,
          0,      // Per-session data - using connect_info.userdata instead
          524288, // RX buffer size
          0,      // ID
          NULL,   // User pointer (set from connect_info.userdata)
          524288  // TX packet size
      },
      {"acip", // Application subprotocol selected by the ACIP WebSocket client
       websocket_callback, 0, 524288, 0, NULL, 524288},
      {NULL, NULL, 0, 0, 0, NULL, 0} // Terminator
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
  // Initialize SSL globally for client (required even for clients to use OpenSSL)
  // The LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT flag works for both servers and clients
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

  // Enable libwebsockets logging through centralized logging system
  lws_log_init_client();

  return lws_create_context(&info);
}

struct acip_websocket_runtime {
  struct lws_context *context;
};

asciichat_error_t acip_websocket_runtime_create(acip_websocket_runtime_t **out) {
  if (!out || *out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "WebSocket runtime output must be empty");
  acip_websocket_runtime_t *runtime = SAFE_CALLOC(1, sizeof(*runtime), acip_websocket_runtime_t *);
  if (!runtime)
    return SET_ERRNO(ERROR_MEMORY, "Could not allocate WebSocket runtime");
  // libwebsockets 4.5 calls OPENSSL_cleanup when its last TLS context goes away.
  // An application reference prevents connection teardown from ending TLS lifetime.
  runtime->context = websocket_create_client_context();
  if (!runtime->context) {
    SAFE_FREE(runtime);
    return SET_ERRNO(ERROR_NETWORK, "Could not initialize WebSocket runtime");
  }
  *out = runtime;
  return ASCIICHAT_OK;
}

void acip_websocket_runtime_destroy(acip_websocket_runtime_t **runtime) {
  if (!runtime || !*runtime)
    return;
  lws_context_destroy((*runtime)->context);
  SAFE_FREE(*runtime);
  *runtime = NULL;
}

/**
 * @brief Allocate client transport storage and synchronization primitives with staged failure cleanup.
 */
static acip_transport_t *websocket_allocate_client_transport(const char *name) {
  // Allocate transport structure
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(acip_transport_t), acip_transport_t *);
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
  atomic_store_bool(&ws_data->is_destroying, false);

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

  transport->impl_data = ws_data;
  if (transport->methods && acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  return transport;
}

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

  websocket_endpoint_t endpoint;
  if (websocket_parse_endpoint(url, &endpoint) != ASCIICHAT_OK) {
    return NULL;
  }
  const char *host = endpoint.host;
  const char *path = endpoint.path;
  int port = endpoint.port;
  bool use_ssl = endpoint.use_ssl;

  acip_transport_t *transport = websocket_allocate_client_transport(name);
  if (!transport) {
    return NULL;
  }
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  ws_data->context = websocket_create_client_context();

  if (!ws_data->context) {
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

  // Initialize connection state flags BEFORE calling lws_client_connect_via_info
  // The callback might fire immediately, so all fields must be ready
  ws_data->is_connected = false;      // Will be set to true in LWS_CALLBACK_CLIENT_ESTABLISHED
  ws_data->connection_failed = false; // Set to true in LWS_CALLBACK_CLIENT_CONNECTION_ERROR
  ws_data->owns_context = true;       // Client transport owns the context

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
  // Request the ACIP subprotocol so server routes to acip_protocol handler
  // This ensures WebSocket upgrade includes Sec-WebSocket-Protocol: acip
  connect_info.local_protocol_name = "acip";
  connect_info.protocol = "acip";
  // Use SSL + skip server certificate hostname verification + allow self-signed certs (for development)
  connect_info.ssl_connection =
      use_ssl ? (LCCSCF_USE_SSL | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK | LCCSCF_ALLOW_SELFSIGNED) : 0;
  connect_info.opaque_user_data = ws_data;

  log_debug("Calling lws_client_connect_via_info...");
  ws_data->wsi = lws_client_connect_via_info(&connect_info);
  log_debug("lws_client_connect_via_info returned: %p", (void *)ws_data->wsi);
  if (!ws_data->wsi) {
    lws_context_destroy(ws_data->context);
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

  // Initialize transport
  transport->methods = &websocket_methods;
  transport->stats_peer = stats_runtime_peer_open("WebSocket");
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = ws_data;

  // Start service thread immediately - it will drive TLS handshake with frequent polling
  // The service thread checks is_destroying and context validity before calling lws_service,
  // so it's safe to start it right away without delays.
  ws_data->service_running = true;
  if (asciichat_thread_create(&ws_data->service_thread, "ws_service", websocket_service_thread, ws_data) != 0) {
    log_error("Failed to create WebSocket service thread");
    ws_data->service_running = false;
    lws_context_destroy(ws_data->context);
    cond_destroy(&ws_data->state_cond);
    mutex_destroy(&ws_data->state_mutex);
    cond_destroy(&ws_data->recv_cond);
    mutex_destroy(&ws_data->recv_mutex);
    ringbuffer_destroy(ws_data->send_queue);
    ringbuffer_destroy(ws_data->recv_queue);
    SAFE_FREE(ws_data);
    stats_runtime_peer_close(transport->stats_peer);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to create service thread");
    return NULL;
  }
  log_debug("WebSocket service thread started");

  // Do NOT block the main thread waiting for connection!
  // This prevents stdin/keyboard input from being processed and causes the client to hang.
  // The service thread will establish the connection asynchronously.
  // The protocol layer (recv) will detect if connection fails and handle it there.
  // Return immediately so the main thread can respond to keyboard input.
  log_debug("WebSocket transport created, service thread will establish connection asynchronously");

  // Register transport
  NAMED_REGISTER_TRANSPORT(transport, name, NULL);

  // Register websocket implementation data with transport as parent
  (void)NAMED_REGISTER_WEBSOCKET_IMPL(ws_data, "impl", (uintptr_t)(const void *)(transport));

  // Return transport immediately - connection will be established by service thread
  // If connection fails, recv() will detect is_connected=false and return error
  if (transport->methods && acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  return transport;
}

/**
 * @brief Allocate server transport storage and synchronization primitives with staged failure cleanup.
 */
static acip_transport_t *websocket_allocate_server_transport(const char *name) {
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

  // Initialize synchronization primitives with transport-aware names
  char recv_name[64], send_name[64], state_name[64];
  snprintf(recv_name, sizeof(recv_name), "recv_%s", name);
  snprintf(send_name, sizeof(send_name), "send_%s", name);
  snprintf(state_name, sizeof(state_name), "state_%s", name);

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

  transport->impl_data = ws_data;
  if (transport->methods && acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
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

  acip_transport_t *transport = websocket_allocate_server_transport(name);
  if (!transport) {
    return NULL;
  }
  websocket_transport_data_t *ws_data = (websocket_transport_data_t *)transport->impl_data;

  // Store connection info (server-side: no context ownership, connection already established)
  ws_data->wsi = wsi;

  ws_data->context = lws_get_context(wsi); // Get context from wsi (not owned)
  ws_data->owns_context = false;           // Server owns context, not transport
  ws_data->is_connected = true;            // Already connected (server-side)
  log_debug_every(US_PER_SEC_INT, "★★★ SERVER TRANSPORT CREATED: is_connected=true, wsi=%p, ws_data=%p", (void *)wsi,
                  (void *)ws_data);
  log_debug("Server transport created: is_connected=true, wsi=%p", (void *)wsi);

  // Initialize transport
  transport->methods = &websocket_methods;
  transport->stats_peer = stats_runtime_peer_open("WebSocket");
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = ws_data;

  log_debug_every(US_PER_SEC_INT, "Created WebSocket server transport (crypto: %s)",
                  crypto_ctx ? "enabled" : "disabled");

  // Register transport
  NAMED_REGISTER_TRANSPORT(transport, name, NULL);

  // Register websocket implementation data with transport as parent
  (void)NAMED_REGISTER_WEBSOCKET_IMPL(ws_data, "impl", (uintptr_t)(const void *)(transport));

  int send_buffer = SOCKET_STREAM_SEND_BUFFER;
  if (socket_setsockopt(lws_get_socket_fd(wsi), SOL_SOCKET, SO_SNDBUF, &send_buffer, sizeof(send_buffer)) != 0) {
    acip_transport_destroy(transport);
    SET_ERRNO_SYS(ERROR_NETWORK, "Cannot bound WebSocket send buffer");
    return NULL;
  }

  if (transport->methods && acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  return transport;
}
