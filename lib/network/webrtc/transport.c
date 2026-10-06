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
 * - Peer manager owns peer_conn and data_channel; transport borrows them
 * - Receive queue owns buffered message data
 * - recv() allocates message buffer, caller must free
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date January 2026
 */

#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/platform/mutex.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <string.h>

/**
 * @brief Maximum receive queue size (messages buffered before recv())
 *
 * Power of 2 for ringbuffer optimization. Completed ACIP packets are queued here.
 * On overflow, retain control packets and recent media so the connection can recover.
 */
#define WEBRTC_RECV_QUEUE_SIZE 512
// At 60 Hz, 96 queued packets can represent more than a second of stale
// media.  Start coalescing early so a congested peer stays close to live.
#define WEBRTC_RECV_QUEUE_HIGH_WATER 12

// Allow ICE connectivity checks to recover temporarily, then let the owning
// session tear down and renegotiate a peer that has remained disconnected.
#define WEBRTC_DISCONNECTED_GRACE_NS (30 * NS_PER_SEC_INT)

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
  webrtc_peer_connection_t *peer_conn; ///< Borrowed peer connection
  webrtc_data_channel_t *data_channel; ///< Borrowed data channel
  ringbuffer_t *recv_queue;            ///< Receive message queue
  uint8_t *partial;                    ///< Incomplete ACIP packet bytes
  size_t partial_len;
  size_t partial_capacity;
  mutex_t queue_mutex;            ///< Protect queue operations
  cond_t queue_cond;              ///< Signal when messages arrive
  bool is_connected;              ///< Connection state
  bool data_channel_closed;       ///< DataChannel close is terminal for this transport
  bool close_requested;           ///< Whether this transport has closed its borrowed peer handles
  uint64_t disconnected_since_ns; ///< Start of the current transient ICE disconnect
  mutex_t state_mutex;            ///< Protect state changes
  mutex_t send_mutex;             ///< Keep chunks from concurrent packets together
} webrtc_transport_data_t;

// =============================================================================
// DataChannel Callbacks
// =============================================================================

static bool queued_message_is_media(const webrtc_recv_msg_t *msg) {
  if (msg->len < sizeof(packet_header_t))
    return false;
  packet_header_t header;
  memcpy(&header, msg->data, sizeof(header));
  packet_type_t type = NET_TO_HOST_U16(header.type);
  return type == PACKET_TYPE_IMAGE_FRAME || type == PACKET_TYPE_ASCII_FRAME || type == PACKET_TYPE_AUDIO_BATCH ||
         type == PACKET_TYPE_AUDIO_OPUS_BATCH;
}

static bool queued_message_is_video(const webrtc_recv_msg_t *msg) {
  if (msg->len < sizeof(packet_header_t))
    return false;
  packet_header_t header;
  memcpy(&header, msg->data, sizeof(header));
  packet_type_t type = NET_TO_HOST_U16(header.type);
  return type == PACKET_TYPE_IMAGE_FRAME || type == PACKET_TYPE_ASCII_FRAME;
}

static void discard_stale_video(webrtc_transport_data_t *wrtc) {
  webrtc_recv_msg_t queued[WEBRTC_RECV_QUEUE_SIZE];
  size_t count = 0, video_count = 0;
  while (count < WEBRTC_RECV_QUEUE_SIZE && ringbuffer_read(wrtc->recv_queue, &queued[count])) {
    if (queued_message_is_video(&queued[count]))
      video_count++;
    count++;
  }
  // Video frames are independent snapshots. Drop only superseded video frames;
  // audio packets carry consecutive samples and must remain ordered so playback
  // does not develop gaps when the receive worker briefly falls behind.
  size_t video_to_discard = video_count > 1 ? video_count - 1 : 0;
  for (size_t i = 0; i < count; i++) {
    if (video_to_discard && queued_message_is_video(&queued[i])) {
      buffer_pool_free(NULL, queued[i].data, queued[i].len);
      video_to_discard--;
    } else {
      ringbuffer_write(wrtc->recv_queue, &queued[i]);
    }
  }
  log_warn_every(US_PER_SEC_INT, "WebRTC receive backlog: coalesced superseded video frames");
}

/**
 * @brief DataChannel message callback - push to receive queue
 */
static void webrtc_on_message(webrtc_data_channel_t *channel, const uint8_t *data, size_t len, void *user_data) {
  (void)channel; // Unused
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)user_data;

  if (!wrtc || !data || len == 0) {
    return;
  }

  // Ordered DataChannels may split a large ACIP packet across messages.
  const size_t limit = 8 * 1024 * 1024;
  mutex_lock(&wrtc->queue_mutex);
  if (len > limit || wrtc->partial_len > limit - len) {
    mutex_unlock(&wrtc->queue_mutex);
    log_error("WebRTC receive buffer exceeded");
    webrtc_datachannel_close(channel);
    return;
  }
  size_t required = wrtc->partial_len + len;
  if (required > wrtc->partial_capacity) {
    size_t capacity = wrtc->partial_capacity ? wrtc->partial_capacity : 16384;
    while (capacity < required) {
      capacity *= 2;
    }
    if (capacity > limit) {
      capacity = limit;
    }
    wrtc->partial = SAFE_REALLOC(wrtc->partial, capacity, uint8_t *);
    wrtc->partial_capacity = capacity;
  }
  memcpy(wrtc->partial + wrtc->partial_len, data, len);
  wrtc->partial_len += len;
  size_t offset = 0;
  while (wrtc->partial_len - offset >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, wrtc->partial + offset, sizeof(header));
    size_t payload_len = NET_TO_HOST_U32(header.length);
    if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC || payload_len > limit - sizeof(header)) {
      mutex_unlock(&wrtc->queue_mutex);
      log_error("Invalid ACIP packet on WebRTC DataChannel");
      webrtc_datachannel_close(channel);
      return;
    }
    size_t packet_len = sizeof(header) + payload_len;
    if (wrtc->partial_len - offset < packet_len)
      break;
    webrtc_recv_msg_t msg = {.data = buffer_pool_alloc(NULL, packet_len), .len = packet_len};
    if (!msg.data)
      break;
    memcpy(msg.data, wrtc->partial + offset, packet_len);
    if (ringbuffer_is_full(wrtc->recv_queue)) {
      discard_stale_video(wrtc);
    }
    if (!ringbuffer_write(wrtc->recv_queue, &msg)) {
      bool media = queued_message_is_media(&msg);
      if (media) {
        log_warn_every(US_PER_SEC_INT, "WebRTC receive queue saturated; dropping newest media packet");
      }
      buffer_pool_free(NULL, msg.data, msg.len);
      if (media) {
        offset += packet_len;
        continue;
      }
      mutex_unlock(&wrtc->queue_mutex);
      log_error("WebRTC receive queue exceeded");
      webrtc_datachannel_close(channel);
      return;
    }
    // Keep video presentation current while preserving every queued audio
    // packet and control message.
    if (ringbuffer_size(wrtc->recv_queue) > WEBRTC_RECV_QUEUE_HIGH_WATER) {
      discard_stale_video(wrtc);
    }
    offset += packet_len;
  }
  wrtc->partial_len -= offset;
  if (offset && wrtc->partial_len)
    memmove(wrtc->partial, wrtc->partial + offset, wrtc->partial_len);
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
  wrtc->data_channel_closed = false;
  wrtc->disconnected_since_ns = 0;
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

  // A DataChannel error can be reported while ICE is recovering. The peer
  // connection state determines whether the transport is terminal.
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
  wrtc->data_channel_closed = true;
  mutex_unlock(&wrtc->state_mutex);

  // Wake any blocking recv() calls
  cond_broadcast(&wrtc->queue_cond);
}

// =============================================================================
// WebRTC Transport Methods
// =============================================================================

static bool webrtc_transport_is_connected_impl(webrtc_transport_data_t *wrtc) {
  if (!wrtc) {
    return false;
  }

  if (!wrtc->peer_conn || !wrtc->data_channel) {
    return false;
  }

  mutex_lock(&wrtc->state_mutex);
  bool close_requested = wrtc->close_requested;
  bool data_channel_closed = wrtc->data_channel_closed;
  bool connected = wrtc->is_connected;
  mutex_unlock(&wrtc->state_mutex);
  webrtc_state_t state = webrtc_get_state(wrtc->peer_conn);
  if (close_requested || data_channel_closed) {
    return false;
  }

  if (state == WEBRTC_STATE_CONNECTED) {
    if (!connected || !webrtc_datachannel_is_open(wrtc->data_channel)) {
      return false;
    }
    mutex_lock(&wrtc->state_mutex);
    wrtc->disconnected_since_ns = 0;
    mutex_unlock(&wrtc->state_mutex);
    return true;
  }

  if (state != WEBRTC_STATE_DISCONNECTED) {
    return false;
  }

  // ICE can briefly enter DISCONNECTED while connectivity checks recover.
  // Retain the DataChannel for a bounded window so a temporary network change
  // does not restart the whole session; a stuck peer must still be cleaned up.
  uint64_t now_ns = time_get_ns();
  mutex_lock(&wrtc->state_mutex);
  if (wrtc->disconnected_since_ns == 0) {
    wrtc->disconnected_since_ns = now_ns;
  }
  bool within_grace = now_ns - wrtc->disconnected_since_ns <= WEBRTC_DISCONNECTED_GRACE_NS;
  mutex_unlock(&wrtc->state_mutex);
  return within_grace;
}

static asciichat_error_t webrtc_send(acip_transport_t *transport, const void *data, size_t len) {
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;

  if (!webrtc_transport_is_connected_impl(wrtc)) {
    return SET_ERRNO(ERROR_NETWORK, "WebRTC transport not connected");
  }

  mutex_lock(&wrtc->send_mutex);
  uint64_t send_start_ns = time_get_ns();
  size_t buffered_before_send = 0;
  if (len >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, data, sizeof(header));
    if (NET_TO_HOST_U64(header.magic) == PACKET_MAGIC) {
      uint16_t packet_type = NET_TO_HOST_U16(header.type);
      if (packet_type == PACKET_TYPE_IMAGE_FRAME || packet_type == PACKET_TYPE_ASCII_FRAME) {
        asciichat_error_t buffered_result =
            webrtc_datachannel_get_buffered_amount(wrtc->data_channel, &buffered_before_send);
        if (buffered_result != ASCIICHAT_OK) {
          buffered_before_send = 0;
          CLEAR_ERRNO();
        }
      }
    }
  }

  size_t max_message_size = 16384;
  asciichat_error_t max_size_result =
      webrtc_datachannel_get_max_message_size(wrtc->data_channel, &max_message_size);
  if (max_size_result != ASCIICHAT_OK || max_message_size == 0) {
    max_message_size = 16384;
    CLEAR_ERRNO();
  }
  log_info_every(60 * NS_PER_SEC_INT, "WebRTC DataChannel negotiated max message size: %zu bytes",
                 max_message_size);

  asciichat_error_t result = ASCIICHAT_OK;
  for (size_t offset = 0; offset < len;) {
    size_t chunk = len - offset;
    if (chunk > max_message_size)
      chunk = max_message_size;
    result = webrtc_datachannel_send(wrtc->data_channel, (const uint8_t *)data + offset, chunk);
    if (result != ASCIICHAT_OK)
      break;
    offset += chunk;
  }
  mutex_unlock(&wrtc->send_mutex);

  uint64_t send_duration_ns = time_elapsed_ns(send_start_ns, time_get_ns());
  if (send_duration_ns > 20 * NS_PER_MS_INT) {
    log_warn_every(US_PER_SEC_INT, "WebRTC DataChannel send blocked %.1fms for %zu-byte packet (buffered=%zu)",
                   (double)send_duration_ns / 1e6, len, buffered_before_send);
  }

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
    if (!webrtc_transport_is_connected_impl(wrtc)) {
      mutex_unlock(&wrtc->queue_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Connection closed while waiting for data");
    }

    // Wait for message arrival or connection close
    // Peer-connection state changes do not necessarily produce a DataChannel
    // callback. Periodically recheck the peer state so a lost connection can
    // leave this blocking receive path and let its owner clean up.
    cond_timedwait(&wrtc->queue_cond, &wrtc->queue_mutex, 250 * NS_PER_MS_INT);
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
  if (wrtc->close_requested) {
    mutex_unlock(&wrtc->state_mutex);
    return ASCIICHAT_OK; // Already closed
  }

  wrtc->close_requested = true;
  wrtc->is_connected = false;
  mutex_unlock(&wrtc->state_mutex);

  // Close the peer connection before deleting its DataChannel. libdatachannel
  // tears down channel state as part of rtcClose(); deleting the channel first
  // leaves rtcClose() trying to access an ID that no longer exists.
  if (wrtc->peer_conn) {
    webrtc_peer_connection_close(wrtc->peer_conn);
  }

  // Delete the channel after the peer has stopped using it. Its owner will
  // later destroy the wrapper; we only invalidate the native channel here so
  // queued callbacks cannot retain this transport as user_data.
  if (wrtc->data_channel) {
    webrtc_datachannel_close(wrtc->data_channel);
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
  return webrtc_transport_is_connected_impl(wrtc);
}

// =============================================================================
// WebRTC Transport Destroy Implementation
// =============================================================================

/**
 * @brief Destroy WebRTC transport and free all resources
 *
 * This is called by the generic acip_transport_destroy() after calling close().
 * Frees transport-owned buffers and synchronization primitives. The peer
 * manager owns the peer connection and DataChannel wrappers.
 *
 * @param transport Transport to destroy (impl_data will be freed by caller)
 */
static void webrtc_destroy_impl(acip_transport_t *transport) {
  if (!transport || !transport->impl_data) {
    return;
  }

  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;

  // acip_transport_destroy() only invokes close() for transports that still
  // report connected. A WebRTC peer can enter DISCONNECTED before destruction,
  // leaving this transport's callbacks registered on the DataChannel. Deleting
  // the channel here is the libdatachannel synchronization point: it waits for
  // scheduled callbacks and prevents new ones before this callback userdata,
  // queue, and mutexes are released.
  (void)webrtc_close(transport);

  // Clear receive queue and free buffered messages
  if (wrtc->recv_queue) {
    mutex_lock(&wrtc->queue_mutex);

    webrtc_recv_msg_t msg;
    while (ringbuffer_read(wrtc->recv_queue, &msg)) {
      if (msg.data) {
        buffer_pool_free(NULL, msg.data, msg.len);
        msg.data = NULL;
      }
    }

    mutex_unlock(&wrtc->queue_mutex);
    ringbuffer_destroy(wrtc->recv_queue);
    wrtc->recv_queue = NULL;
  }

  SAFE_FREE(wrtc->partial);
  wrtc->partial = NULL;
  wrtc->partial_len = 0;
  wrtc->partial_capacity = 0;

  // Destroy synchronization primitives
  mutex_destroy(&wrtc->state_mutex);
  mutex_destroy(&wrtc->send_mutex);
  cond_destroy(&wrtc->queue_cond);
  mutex_destroy(&wrtc->queue_mutex);

  log_debug("Destroyed WebRTC transport resources");
}

// =============================================================================
// WebRTC Transport Method Table
// =============================================================================

static bool webrtc_has_pending_data(acip_transport_t *transport) {
  if (!transport || !transport->impl_data)
    return false;
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;
  mutex_lock(&wrtc->queue_mutex);
  bool pending = !ringbuffer_is_empty(wrtc->recv_queue);
  mutex_unlock(&wrtc->queue_mutex);
  return pending;
}

static const acip_transport_methods_t webrtc_methods = {
    .send = webrtc_send,
    .recv = webrtc_recv,
    .close = webrtc_close,
    .get_type = webrtc_get_type,
    .get_socket = webrtc_get_socket,
    .is_connected = webrtc_is_connected,
    .has_pending_data = webrtc_has_pending_data,
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
  webrtc_transport_data_t *wrtc_data = SAFE_CALLOC(1, sizeof(webrtc_transport_data_t), webrtc_transport_data_t *);
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
  if (mutex_init(&wrtc_data->queue_mutex, "webrtc_queue") != 0) {
    ringbuffer_destroy(wrtc_data->recv_queue);
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize queue mutex");
    return NULL;
  }

  if (cond_init(&wrtc_data->queue_cond, "queue") != 0) {
    mutex_destroy(&wrtc_data->queue_mutex);
    ringbuffer_destroy(wrtc_data->recv_queue);
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize queue condition variable");
    return NULL;
  }

  if (mutex_init(&wrtc_data->state_mutex, "webrtc_state") != 0) {
    cond_destroy(&wrtc_data->queue_cond);
    mutex_destroy(&wrtc_data->queue_mutex);
    ringbuffer_destroy(wrtc_data->recv_queue);
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize state mutex");
    return NULL;
  }

  if (mutex_init(&wrtc_data->send_mutex, "webrtc_send") != 0) {
    mutex_destroy(&wrtc_data->state_mutex);
    cond_destroy(&wrtc_data->queue_cond);
    mutex_destroy(&wrtc_data->queue_mutex);
    ringbuffer_destroy(wrtc_data->recv_queue);
    SAFE_FREE(wrtc_data);
    SAFE_FREE(transport);
    SET_ERRNO(ERROR_INTERNAL, "Failed to initialize send mutex");
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
    mutex_destroy(&wrtc_data->send_mutex);
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

  // Register WebRTC transport implementation data with transport as parent
  NAMED_REGISTER(wrtc_data, "impl", "webrtc_impl", "0x%tx", (uintptr_t)(const void *)(transport));

  return transport;
}
