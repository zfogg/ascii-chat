/**
 * @file network/acip/transport_webrtc.c
 * @brief WebRTC DataChannel transport implementation for ACIP protocol
 *
 * Implements the acip_transport_t interface for WebRTC DataChannels.
 * Enables P2P ACIP packet transport over libdatachannel connections.
 *
 * ARCHITECTURE:
 * =============
 * - Star topology: Session creator (server) connects to N clients
 * - Each connection uses a dedicated DataChannel for ACIP packets
 * - Async DataChannel callbacks bridge to sync recv() via ringbuffer
 * - Thread-safe receive queue handles async message arrival
 *
 * MESSAGE FLOW:
 * =============
 * 1. send(): Synchronous write via webrtc_datachannel_send()
 * 2. DataChannel callback: Async write to receive ringbuffer
 * 3. recv(): Blocking read from receive ringbuffer
 *
 * MEMORY OWNERSHIP:
 * =================
 * - Transport OWNS peer_conn and data_channel (closes on destroy)
 * - Receive queue owns buffered message data
 * - recv() allocates message buffer, caller must free
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include "network/acip/transport.h"
#include "network/webrtc/webrtc.h"
#include "log/logging.h"
#include "ringbuffer.h"
#include "buffer_pool.h"
#include "platform/mutex.h"
#include "platform/cond.h"
#include <string.h>

/**
 * @brief Maximum receive queue size (messages buffered before recv())
 *
 * Power of 2 for ringbuffer optimization. 64 messages = ~2-3 seconds
 * of video frames at 30 FPS, enough for network jitter and processing delays.
 */
#define WEBRTC_RECV_QUEUE_SIZE 64

/**
 * @brief Receive queue element (variable-length message)
 */
typedef struct {
  uint8_t *data; ///< Message data (allocated, caller must free)
  size_t len;    ///< Message length in bytes
} webrtc_recv_msg_t;

/**
 * @brief WebRTC transport implementation data
 */
typedef struct {
  webrtc_peer_connection_t *peer_conn; ///< Peer connection (owned)
  webrtc_data_channel_t *data_channel; ///< Data channel (owned)
  ringbuffer_t *recv_queue;            ///< Receive message queue
  mutex_t queue_mutex;                 ///< Protect queue operations
  cond_t queue_cond;                   ///< Signal when messages arrive
  bool is_connected;                   ///< Connection state
  mutex_t state_mutex;                 ///< Protect state changes
} webrtc_transport_data_t;

// =============================================================================
// DataChannel Callbacks
// =============================================================================

/**
 * @brief DataChannel message callback - push to receive queue
 */
static void webrtc_on_message(webrtc_data_channel_t *channel, const uint8_t *data, size_t len, void *user_data) {
  (void)channel; // Unused
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)user_data;

  if (!wrtc || !data || len == 0) {
    return;
  }

  // Allocate message buffer using buffer pool (will be freed by acip_client_receive_and_dispatch)
  webrtc_recv_msg_t msg;
  msg.data = buffer_pool_alloc(NULL, len);
  if (!msg.data) {
    return;
  }

  // Copy data
  memcpy(msg.data, data, len);
  msg.len = len;

  // Push to receive queue
  mutex_lock(&wrtc->queue_mutex);

  bool success = ringbuffer_write(wrtc->recv_queue, &msg);
  if (!success) {
    // Queue full - drop oldest message to make room
    webrtc_recv_msg_t dropped_msg;
    if (ringbuffer_read(wrtc->recv_queue, &dropped_msg)) {
      buffer_pool_free(NULL, dropped_msg.data, dropped_msg.len);
    }

    // Try again
    success = ringbuffer_write(wrtc->recv_queue, &msg);
    if (!success) {
      buffer_pool_free(NULL, msg.data, len);
      mutex_unlock(&wrtc->queue_mutex);
      return;
    }
  }

  // Signal waiting recv() call
  cond_signal(&wrtc->queue_cond);
  mutex_unlock(&wrtc->queue_mutex);
}

/**
 * @brief DataChannel open callback
 */
static void webrtc_on_open(webrtc_data_channel_t *channel, void *user_data) {
  (void)channel;
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)user_data;

  if (!wrtc) {
    return;
  }

  mutex_lock(&wrtc->state_mutex);
  wrtc->is_connected = true;
  mutex_unlock(&wrtc->state_mutex);

  log_info("WebRTC DataChannel opened, transport ready");
}

/**
 * @brief DataChannel error callback
 */
static void webrtc_on_error(webrtc_data_channel_t *channel, const char *error_msg, void *user_data) {
  (void)channel;
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)user_data;

  log_error("WebRTC DataChannel error: %s", error_msg ? error_msg : "unknown error");

  if (!wrtc) {
    return;
  }

  mutex_lock(&wrtc->state_mutex);
  wrtc->is_connected = false;
  mutex_unlock(&wrtc->state_mutex);

  // Wake any blocking recv() calls
  cond_broadcast(&wrtc->queue_cond);
}

/**
 * @brief DataChannel close callback
 */
static void webrtc_on_close(webrtc_data_channel_t *channel, void *user_data) {
  (void)channel;
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)user_data;

  log_info("WebRTC DataChannel closed");

  if (!wrtc) {
    return;
  }

  mutex_lock(&wrtc->state_mutex);
  wrtc->is_connected = false;
  mutex_unlock(&wrtc->state_mutex);

  // Wake any blocking recv() calls
  cond_broadcast(&wrtc->queue_cond);
}

// =============================================================================
// WebRTC Transport Methods
// =============================================================================

static asciichat_error_t webrtc_send(acip_transport_t *transport, const void *data, size_t len) {
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;

  mutex_lock(&wrtc->state_mutex);
  bool connected = wrtc->is_connected;
  mutex_unlock(&wrtc->state_mutex);

  if (!connected) {
    return SET_ERRNO(ERROR_NETWORK, "WebRTC transport not connected");
  }

  // Send via DataChannel
  asciichat_error_t result = webrtc_datachannel_send(wrtc->data_channel, data, len);

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send on WebRTC DataChannel");
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t webrtc_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                     void **out_allocated_buffer) {
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;

  mutex_lock(&wrtc->queue_mutex);

  // Block until message arrives or connection closes
  while (ringbuffer_is_empty(wrtc->recv_queue)) {
    mutex_lock(&wrtc->state_mutex);
    bool connected = wrtc->is_connected;
    mutex_unlock(&wrtc->state_mutex);

    if (!connected) {
      mutex_unlock(&wrtc->queue_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Connection closed while waiting for data");
    }

    // Wait for message arrival or connection close
    cond_wait(&wrtc->queue_cond, &wrtc->queue_mutex);
  }

  // Read message from queue
  webrtc_recv_msg_t msg;
  bool success = ringbuffer_read(wrtc->recv_queue, &msg);
  mutex_unlock(&wrtc->queue_mutex);

  if (!success) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to read from receive queue");
  }

  // Return message to caller (caller owns the buffer)
  *buffer = msg.data;
  *out_len = msg.len;
  *out_allocated_buffer = msg.data;

  return ASCIICHAT_OK;
}

static asciichat_error_t webrtc_close(acip_transport_t *transport) {
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;

  mutex_lock(&wrtc->state_mutex);

  if (!wrtc->is_connected) {
    mutex_unlock(&wrtc->state_mutex);
    return ASCIICHAT_OK; // Already closed
  }

  wrtc->is_connected = false;
  mutex_unlock(&wrtc->state_mutex);

  // Close DataChannel
  if (wrtc->data_channel) {
    webrtc_datachannel_close(wrtc->data_channel);
  }

  // Close peer connection
  if (wrtc->peer_conn) {
    webrtc_peer_connection_close(wrtc->peer_conn);
  }

  // Wake any blocking recv() calls
  cond_broadcast(&wrtc->queue_cond);

  log_debug("WebRTC transport closed");
  return ASCIICHAT_OK;
}

static acip_transport_type_t webrtc_get_type(acip_transport_t *transport) {
  (void)transport;
  return ACIP_TRANSPORT_WEBRTC;
}

static socket_t webrtc_get_socket(acip_transport_t *transport) {
  (void)transport;
  return INVALID_SOCKET_VALUE; // WebRTC has no underlying socket
}

static bool webrtc_is_connected(acip_transport_t *transport) {
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;

  mutex_lock(&wrtc->state_mutex);
  bool connected = wrtc->is_connected;
  mutex_unlock(&wrtc->state_mutex);

  return connected;
}

// =============================================================================
// WebRTC Transport Destroy Implementation
// =============================================================================

/**
 * @brief Destroy WebRTC transport and free all resources
 *
 * This is called by the generic acip_transport_destroy() after calling close().
 * Frees WebRTC-specific resources including peer connection, data channel,
 * receive queue, and synchronization primitives.
 *
 * @param transport Transport to destroy (impl_data will be freed by caller)
 */
static void webrtc_destroy_impl(acip_transport_t *transport) {
  if (!transport || !transport->impl_data) {
    return;
  }

  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;

  // Destroy peer connection and data channel
  if (wrtc->data_channel) {
    webrtc_datachannel_destroy(wrtc->data_channel);
    wrtc->data_channel = NULL;
  }

  if (wrtc->peer_conn) {
    webrtc_peer_connection_destroy(wrtc->peer_conn);
    wrtc->peer_conn = NULL;
  }

  // Clear receive queue and free buffered messages
  if (wrtc->recv_queue) {
    mutex_lock(&wrtc->queue_mutex);

    webrtc_recv_msg_t msg;
    while (ringbuffer_read(wrtc->recv_queue, &msg)) {
      if (msg.data) {
        SAFE_FREE(msg.data);
      }
    }

    mutex_unlock(&wrtc->queue_mutex);
    ringbuffer_destroy(wrtc->recv_queue);
    wrtc->recv_queue = NULL;
  }

  // Destroy synchronization primitives
  mutex_destroy(&wrtc->state_mutex);
  cond_destroy(&wrtc->queue_cond);
  mutex_destroy(&wrtc->queue_mutex);

  log_debug("Destroyed WebRTC transport resources");
}

// =============================================================================
// WebRTC Transport Method Table
// =============================================================================

static const acip_transport_methods_t webrtc_methods = {
    .send = webrtc_send,
    .recv = webrtc_recv,
    .close = webrtc_close,
    .get_type = webrtc_get_type,
    .get_socket = webrtc_get_socket,
    .is_connected = webrtc_is_connected,
    .destroy_impl = webrtc_destroy_impl,
};

// =============================================================================
// WebRTC Transport Creation
// =============================================================================

acip_transport_t *acip_webrtc_transport_create(webrtc_peer_connection_t *peer_conn, webrtc_data_channel_t *data_channel,
                                               crypto_context_t *crypto_ctx) {
  if (!peer_conn || !data_channel) {
    SET_ERRNO(ERROR_INVALID_PARAM, "peer_conn and data_channel are required");
    return NULL;
  }

  // Allocate transport structure
  acip_transport_t *transport = SAFE_MALLOC(sizeof(acip_transport_t), acip_transport_t *);
  if (!transport) {
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate WebRTC transport");
    return NULL;
  }

  // Allocate WebRTC-specific data
  webrtc_transport_data_t *wrtc_data = SAFE_MALLOC(sizeof(webrtc_transport_data_t), webrtc_transport_data_t *);
  if (!wrtc_data) {
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to allocate WebRTC transport data");
    return NULL;
  }

  // Create receive queue
  wrtc_data->recv_queue = ringbuffer_create(sizeof(webrtc_recv_msg_t), WEBRTC_RECV_QUEUE_SIZE);
  if (!wrtc_data->recv_queue) {
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_MEMORY, "Failed to create receive queue");
    return NULL;
  }

  // Initialize synchronization primitives
  if (mutex_init(&wrtc_data->queue_mutex) != 0) {
    ringbuffer_destroy(wrtc_data->recv_queue);
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize queue mutex");
    return NULL;
  }

  if (cond_init(&wrtc_data->queue_cond) != 0) {
    mutex_destroy(&wrtc_data->queue_mutex);
    ringbuffer_destroy(wrtc_data->recv_queue);
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize queue condition variable");
    return NULL;
  }

  if (mutex_init(&wrtc_data->state_mutex) != 0) {
    cond_destroy(&wrtc_data->queue_cond);
    mutex_destroy(&wrtc_data->queue_mutex);
    ringbuffer_destroy(wrtc_data->recv_queue);
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize state mutex");
    return NULL;
  }

  // Initialize WebRTC data
  wrtc_data->peer_conn = peer_conn;
  wrtc_data->data_channel = data_channel;
  wrtc_data->is_connected = false; // Will be set to true in on_open callback

  // Register DataChannel callbacks
  webrtc_datachannel_callbacks_t callbacks = {
      .on_open = webrtc_on_open,
      .on_close = webrtc_on_close,
      .on_error = webrtc_on_error,
      .on_message = webrtc_on_message,
      .user_data = wrtc_data,
  };

  asciichat_error_t result = webrtc_datachannel_set_callbacks(data_channel, &callbacks);
  if (result != ASCIICHAT_OK) {
    mutex_destroy(&wrtc_data->state_mutex);
    cond_destroy(&wrtc_data->queue_cond);
    mutex_destroy(&wrtc_data->queue_mutex);
    ringbuffer_destroy(wrtc_data->recv_queue);
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to set DataChannel callbacks");
    return NULL;
  }

  // IMPORTANT: The transport is always created from peer_manager's on_datachannel_open callback,
  // which means the DataChannel is ALREADY OPEN when we get here. However, by setting our own
  // callbacks above (webrtc_datachannel_set_callbacks), we replaced the callbacks that would
  // have set dc->is_open=true. So we need to manually mark both the transport AND the DataChannel
  // as open/connected now.
  //
  // We cannot rely on webrtc_on_open being called later because:
  // 1. The DataChannel is already open
  // 2. libdatachannel won't fire the open event again
  // 3. Setting callbacks after open doesn't trigger a retroactive open event

  // Mark DataChannel as open (needed for webrtc_datachannel_send() check)
  webrtc_datachannel_set_open_state(data_channel, true);

  // Mark transport as connected
  mutex_lock(&wrtc_data->state_mutex);
  wrtc_data->is_connected = true;
  mutex_unlock(&wrtc_data->state_mutex);
  log_debug("Transport and DataChannel marked as connected/open (already open from peer_manager callback)");

  // Initialize transport
  transport->methods = &webrtc_methods;
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = wrtc_data;

  log_info("Created WebRTC transport (crypto: %s)", crypto_ctx ? "enabled" : "disabled");

  return transport;
}
