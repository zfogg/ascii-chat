#include <ascii-chat/network/acip/backpressure.h>
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
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/webrtc/webrtc.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/ringbuffer.h>
#include <ascii-chat/buffer.h>
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
// Start coalescing well before the 512-message ring fills so a congested peer
// stays close to live.
#define WEBRTC_RECV_QUEUE_HIGH_WATER 12

// DataChannel sends are asynchronous. Bound replaceable video so reliable,
// ordered delivery cannot turn a slow peer into an ever-growing history of
// obsolete frames.
#define WEBRTC_SEND_VIDEO_HIGH_WATER (256 * 1024)

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
  acip_security_t *security;           ///< Owned per-peer security state
  void (*on_ready)(acip_transport_t *, void *);
  void *ready_user_data;
  acip_transport_t *transport; ///< Borrowed enclosing transport
  ringbuffer_t *recv_queue;    ///< Receive message queue
  uint8_t *partial;            ///< Incomplete ACIP packet bytes
  size_t partial_len;
  size_t partial_capacity;
  uint64_t partial_since_ns;
  mutex_t queue_mutex;            ///< Protect queue operations
  cond_t queue_cond;              ///< Signal when messages arrive
  bool is_connected;              ///< Connection state
  bool data_channel_closed;       ///< DataChannel close is terminal for this transport
  bool close_requested;           ///< Whether this transport has closed its borrowed peer handles
  uint64_t disconnected_since_ns; ///< Start of the current transient ICE disconnect
  mutex_t state_mutex;            ///< Protect state changes
  mutex_t send_mutex;             ///< Keep chunks from concurrent packets together
} webrtc_transport_data_t;

static void webrtc_abort(webrtc_transport_data_t *wrtc) {
  mutex_lock(&wrtc->state_mutex);
  wrtc->is_connected = false;
  wrtc->data_channel_closed = true;
  mutex_unlock(&wrtc->state_mutex);
  cond_broadcast(&wrtc->queue_cond);
  // Closing is safe in a callback; the owner deletes the channel after callbacks finish.
  webrtc_peer_connection_close(wrtc->peer_conn);
}

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

static bool queued_message_is_audio(const webrtc_recv_msg_t *msg) {
  if (msg->len < sizeof(packet_header_t))
    return false;
  packet_header_t header;
  memcpy(&header, msg->data, sizeof(header));
  packet_type_t type = NET_TO_HOST_U16(header.type);
  return type == PACKET_TYPE_AUDIO_BATCH || type == PACKET_TYPE_AUDIO_OPUS_BATCH;
}

static void discard_stale_media(webrtc_transport_data_t *wrtc) {
  webrtc_recv_msg_t queued[WEBRTC_RECV_QUEUE_SIZE];
  size_t count = 0, video_count = 0, audio_count = 0;
  while (count < WEBRTC_RECV_QUEUE_SIZE && ringbuffer_read(wrtc->recv_queue, &queued[count])) {
    if (queued_message_is_video(&queued[count]))
      video_count++;
    else if (queued_message_is_audio(&queued[count]))
      audio_count++;
    count++;
  }
  // Video frames are snapshots, so retain the newest one. Preserve a short
  // recent audio history, but discard stale audio when it would otherwise fill
  // the queue and prevent new video packets from reaching the receive worker.
  // Derive the audio allowance from the existing queue high-water mark after
  // reserving room for control messages and the newest video frame.
  size_t control_count = count - video_count - audio_count;
  size_t retained_video_count = video_count > 0 ? 1 : 0;
  size_t non_audio_count = control_count + retained_video_count;
  size_t keep_audio_count =
      non_audio_count < WEBRTC_RECV_QUEUE_HIGH_WATER ? WEBRTC_RECV_QUEUE_HIGH_WATER - non_audio_count : 0;
  size_t video_to_discard = video_count - retained_video_count;
  size_t audio_to_discard = audio_count > keep_audio_count ? audio_count - keep_audio_count : 0;
  size_t dropped_video = 0;
  size_t dropped_audio = 0;
  for (size_t i = 0; i < count; i++) {
    if (video_to_discard && queued_message_is_video(&queued[i])) {
      buffer_release(queued[i].data);
      video_to_discard--;
      dropped_video++;
    } else if (audio_to_discard && queued_message_is_audio(&queued[i])) {
      buffer_release(queued[i].data);
      audio_to_discard--;
      dropped_audio++;
    } else {
      ringbuffer_write(wrtc->recv_queue, &queued[i]);
    }
  }
  if (dropped_video || dropped_audio) {
    log_warn_every(US_PER_SEC_INT,
                   "WebRTC receive backlog: dropped %zu stale video packets and %zu oldest audio packets",
                   dropped_video, dropped_audio);
  }
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
    webrtc_abort(wrtc);
    return;
  }
  if (!wrtc->partial_len)
    wrtc->partial_since_ns = time_get_ns();
  size_t required = wrtc->partial_len + len;
  if (required > wrtc->partial_capacity) {
    size_t capacity = wrtc->partial_capacity ? wrtc->partial_capacity : 16384;
    while (capacity < required) {
      capacity *= 2;
    }
    if (capacity > limit) {
      capacity = limit;
    }
    uint8_t *next = buffer_alloc(NULL, capacity);
    if (!next) {
      mutex_unlock(&wrtc->queue_mutex);
      webrtc_abort(wrtc);
      return;
    }
    if (wrtc->partial_len)
      memcpy(next, wrtc->partial, wrtc->partial_len);
    buffer_release(wrtc->partial);
    wrtc->partial = next;
    wrtc->partial_capacity = capacity;
  }
  memcpy(wrtc->partial + wrtc->partial_len, data, len);
  wrtc->partial_len += len;
  size_t offset = 0;
  bool notify_ready = false;
  while (wrtc->partial_len - offset >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, wrtc->partial + offset, sizeof(header));
    size_t payload_len = NET_TO_HOST_U32(header.length);
    if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC || payload_len > limit - sizeof(header)) {
      mutex_unlock(&wrtc->queue_mutex);
      log_error("Invalid ACIP packet on WebRTC DataChannel");
      webrtc_abort(wrtc);
      return;
    }
    size_t packet_len = sizeof(header) + payload_len;
    if (wrtc->partial_len - offset < packet_len)
      break;
    webrtc_recv_msg_t msg = {.data = buffer_alloc(NULL, packet_len), .len = packet_len};
    if (!msg.data)
      break;
    memcpy(msg.data, wrtc->partial + offset, packet_len);
    if (asciichat_crc32(msg.data + sizeof(header), payload_len) != NET_TO_HOST_U32(header.crc32)) {
      buffer_release(msg.data);
      mutex_unlock(&wrtc->queue_mutex);
      log_error("WebRTC packet checksum failed");
      webrtc_abort(wrtc);
      return;
    }
    if (wrtc->security && !wrtc->security->ready) {
      asciichat_error_t result = acip_security_receive(wrtc->security, wrtc->transport, NET_TO_HOST_U16(header.type),
                                                       msg.data + sizeof(header), payload_len);
      buffer_release(msg.data);
      if (result != ASCIICHAT_OK) {
        mutex_unlock(&wrtc->queue_mutex);
        asciichat_error_context_t error;
        log_error("WebRTC peer handshake failed: %s",
                  HAS_ERRNO(&error) ? error.context_message : asciichat_error_string(result));
        webrtc_abort(wrtc);
        return;
      }
      if (wrtc->security->ready && wrtc->on_ready) {
        notify_ready = true;
      }
      offset += packet_len;
      continue;
    }
    packet_envelope_t envelope = {.type = NET_TO_HOST_U16(header.type),
                                  .data = msg.data + sizeof(header),
                                  .len = payload_len,
                                  .allocated_buffer = msg.data,
                                  .allocated_size = msg.len};
    crypto_context_t *crypto = wrtc->transport->crypto_ctx;
    asciichat_error_t result = ASCIICHAT_OK;
    if (asciichat_crc32(envelope.data, envelope.len) != NET_TO_HOST_U32(header.crc32)) {
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid WebRTC packet checksum");
    } else if (envelope.type == PACKET_TYPE_ENCRYPTED) {
      if (!crypto || !crypto->encrypt_data || !crypto_is_ready(crypto))
        result = SET_ERRNO(ERROR_CRYPTO, "Unexpected encrypted WebRTC packet");
      else
        result = packet_decrypt_envelope(&envelope, crypto);
      if (result == ASCIICHAT_OK) {
        packet_header_t inner;
        memcpy(&inner, envelope.allocated_buffer, sizeof(inner));
        if (NET_TO_HOST_U64(inner.magic) != PACKET_MAGIC ||
            asciichat_crc32(envelope.data, envelope.len) != NET_TO_HOST_U32(inner.crc32) ||
            envelope.type == PACKET_TYPE_ENCRYPTED || packet_is_handshake_type(envelope.type))
          result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid decrypted WebRTC packet");
      }
    } else if (crypto && crypto->encrypt_data && crypto_is_ready(crypto) && !packet_is_handshake_type(envelope.type)) {
      result = SET_ERRNO(ERROR_CRYPTO, "Plaintext application packet on encrypted WebRTC connection");
    }
    if (result != ASCIICHAT_OK) {
      buffer_release(envelope.allocated_buffer);
      mutex_unlock(&wrtc->queue_mutex);
      log_error("WebRTC packet rejected: %s", asciichat_error_string(result));
      webrtc_abort(wrtc);
      return;
    }
    msg.data = envelope.allocated_buffer;
    msg.len = sizeof(packet_header_t) + envelope.len;
    if (ringbuffer_is_full(wrtc->recv_queue)) {
      discard_stale_media(wrtc);
    }
    if (!ringbuffer_write(wrtc->recv_queue, &msg)) {
      bool media = queued_message_is_media(&msg);
      if (media) {
        log_warn_every(US_PER_SEC_INT, "WebRTC receive queue saturated; dropping newest media packet");
      }
      buffer_release(msg.data);
      if (media) {
        offset += packet_len;
        continue;
      }
      mutex_unlock(&wrtc->queue_mutex);
      log_error("WebRTC receive queue exceeded");
      webrtc_abort(wrtc);
      return;
    }
    // Keep video presentation current and prevent stale audio from filling the
    // bounded queue ahead of newer media and control packets.
    if (ringbuffer_size(wrtc->recv_queue) > WEBRTC_RECV_QUEUE_HIGH_WATER) {
      discard_stale_media(wrtc);
    }
    offset += packet_len;
  }
  wrtc->partial_len -= offset;
  if (offset)
    wrtc->partial_since_ns = wrtc->partial_len ? time_get_ns() : 0;
  if (offset && wrtc->partial_len)
    memmove(wrtc->partial, wrtc->partial + offset, wrtc->partial_len);
  cond_signal(&wrtc->queue_cond);
  mutex_unlock(&wrtc->queue_mutex);
  // Application callbacks may immediately receive from the transport.
  if (notify_ready)
    wrtc->on_ready(wrtc->transport, wrtc->ready_user_data);
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

  // The DataChannel open callback can precede the peer connection's CONNECTED
  // callback. An open channel is already usable during that event ordering.
  if (state == WEBRTC_STATE_CONNECTED || state == WEBRTC_STATE_CONNECTING) {
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

  if (wrtc->security && !wrtc->security->ready) {
    packet_header_t header;
    if (len < sizeof(header))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Incomplete WebRTC header");
    memcpy(&header, data, sizeof(header));
    if (NET_TO_HOST_U16(header.type) != PACKET_TYPE_PROTOCOL_VERSION &&
        !packet_is_handshake_type(NET_TO_HOST_U16(header.type)))
      return SET_ERRNO(ERROR_INVALID_STATE, "WebRTC peer security is not ready");
  }
  mutex_lock(&wrtc->send_mutex);
  uint64_t send_start_ns = time_get_ns();
  size_t buffered_before_send = 0;
  asciichat_error_t buffered_result = webrtc_datachannel_get_buffered_amount(wrtc->data_channel, &buffered_before_send);
  if (buffered_result != ASCIICHAT_OK) {
    mutex_unlock(&wrtc->send_mutex);
    return buffered_result;
  }

  size_t max_message_size = 16384;
  asciichat_errno_scope_t size_scope = asciichat_errno_scope_begin();
  asciichat_error_t max_size_result = webrtc_datachannel_get_max_message_size(wrtc->data_channel, &max_message_size);
  if (max_size_result != ASCIICHAT_OK || max_message_size == 0) {
    max_message_size = 16384;
  } else {
    ASSERT_NO_ERRNO_SINCE(size_scope);
  }
  asciichat_errno_scope_end(size_scope, ASCIICHAT_ERRNO_DISMISSED);
  log_debug_every(60 * US_PER_SEC_INT, "WebRTC DataChannel negotiated max message size: %zu bytes", max_message_size);

  socket_buffer_t input = {.data = data, .len = len};
  acip_packet_plan_t plan;
  crypto_context_t *send_crypto = wrtc->security && !wrtc->security->ready ? NULL : transport->crypto_ctx;
  asciichat_error_t result = acip_packet_plan(send_crypto, &input, 1, &plan);
  uint8_t *encrypted = NULL;
  if (result == ASCIICHAT_OK && plan.encrypt) {
    encrypted = buffer_alloc(plan.domain, sizeof(packet_header_t) + plan.ciphertext_capacity);
    if (!encrypted) {
      result = SET_ERRNO(ERROR_MEMORY, "Cannot allocate WebRTC encrypted packet");
    } else {
      packet_header_t header;
      size_t ciphertext_length = 0;
      result = acip_packet_encrypt(transport->crypto_ctx, &plan, plan.domain, encrypted + sizeof(header),
                                   plan.ciphertext_capacity, &header, &ciphertext_length);
      if (result == ASCIICHAT_OK) {
        memcpy(encrypted, &header, sizeof(header));
        data = encrypted;
        len = sizeof(header) + ciphertext_length;
      }
    }
  }
  if (result != ASCIICHAT_OK) {
    buffer_release(encrypted);
    mutex_unlock(&wrtc->send_mutex);
    return result;
  }
  for (size_t offset = 0; offset < len;) {
    size_t chunk = len - offset;
    if (chunk > max_message_size)
      chunk = max_message_size;
    result = webrtc_datachannel_send(wrtc->data_channel, (const uint8_t *)data + offset, chunk);
    if (result != ASCIICHAT_OK)
      break;
    offset += chunk;
  }
  buffer_release(encrypted);
  mutex_unlock(&wrtc->send_mutex);

  uint64_t send_duration_ns = time_elapsed_ns(send_start_ns, time_get_ns());
  if (send_duration_ns > 20 * NS_PER_MS_INT) {
    log_warn_every(US_PER_SEC_INT, "WebRTC DataChannel send blocked %.1fms for %zu-byte packet (buffered=%zu)",
                   (double)send_duration_ns / 1e6, len, buffered_before_send);
  }

  if (result != ASCIICHAT_OK) {
    // A partial packet or consumed nonce cannot be retried on this stream.
    mutex_lock(&wrtc->state_mutex);
    wrtc->is_connected = false;
    wrtc->data_channel_closed = true;
    mutex_unlock(&wrtc->state_mutex);
    return SET_ERRNO(ERROR_NETWORK, "Failed to send on WebRTC DataChannel");
  }

  return ASCIICHAT_OK;
}

static asciichat_error_t webrtc_recv(acip_transport_t *transport, void **buffer, size_t *out_len,
                                     void **out_allocated_buffer) {
  webrtc_transport_data_t *wrtc = (webrtc_transport_data_t *)transport->impl_data;
  uint64_t started = time_get_ns();
  uint64_t timeout = transport->receive_timeout_ns ? transport->receive_timeout_ns : NS_PER_SEC_INT;

  mutex_lock(&wrtc->queue_mutex);

  // Block until message arrives or connection closes
  while (ringbuffer_is_empty(wrtc->recv_queue)) {
    uint64_t elapsed = time_get_ns() - started;
    if (elapsed >= timeout) {
      bool partial = wrtc->partial_len != 0;
      mutex_unlock(&wrtc->queue_mutex);
      return SET_ERRNO(partial ? ERROR_NETWORK_INCOMPLETE : ERROR_NETWORK_TIMEOUT, "WebRTC receive deadline expired");
    }
    if (!webrtc_transport_is_connected_impl(wrtc)) {
      mutex_unlock(&wrtc->queue_mutex);
      return SET_ERRNO(ERROR_NETWORK, "Connection closed while waiting for data");
    }

    // Wait for message arrival or connection close
    // Peer-connection state changes do not necessarily produce a DataChannel
    // callback. Periodically recheck the peer state so a lost connection can
    // leave this blocking receive path and let its owner clean up.
    cond_timedwait(&wrtc->queue_cond, &wrtc->queue_mutex, MIN(timeout - elapsed, 250 * NS_PER_MS_INT));
    if (ringbuffer_is_empty(wrtc->recv_queue) && wrtc->partial_len) {
      bool expired = time_elapsed_ns(wrtc->partial_since_ns, time_get_ns()) >= 5 * NS_PER_SEC_INT;
      mutex_unlock(&wrtc->queue_mutex);
      if (expired) {
        mutex_lock(&wrtc->state_mutex);
        wrtc->is_connected = false;
        wrtc->data_channel_closed = true;
        mutex_unlock(&wrtc->state_mutex);
        return SET_ERRNO(ERROR_NETWORK, "Incomplete WebRTC packet exceeded receive deadline");
      }
      return SET_ERRNO(ERROR_NETWORK_INCOMPLETE, "Waiting for remaining WebRTC packet bytes");
    }
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
        buffer_release(msg.data);
        msg.data = NULL;
      }
    }

    mutex_unlock(&wrtc->queue_mutex);
    ringbuffer_destroy(wrtc->recv_queue);
    wrtc->recv_queue = NULL;
  }

  buffer_release(wrtc->partial);
  wrtc->partial = NULL;
  wrtc->partial_len = 0;
  wrtc->partial_capacity = 0;

  if (wrtc->security) {
    acip_security_cleanup(wrtc->security);
    SAFE_FREE(wrtc->security);
  }

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

static void webrtc_interrupt(acip_transport_t *transport) {
  webrtc_transport_data_t *wrtc = transport->impl_data;
  /* Publish terminal state before waking readers. The owner retains peer handles
   * and callback contexts until all media workers have joined. */
  mutex_lock(&wrtc->queue_mutex);
  mutex_lock(&wrtc->state_mutex);
  wrtc->is_connected = false;
  wrtc->data_channel_closed = true;
  mutex_unlock(&wrtc->state_mutex);
  cond_broadcast(&wrtc->queue_cond);
  mutex_unlock(&wrtc->queue_mutex);
}

static size_t webrtc_queued_bytes(acip_transport_t *transport) {
  webrtc_transport_data_t *wrtc = transport->impl_data;
  size_t bytes = 0;
  asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
  asciichat_error_t result = webrtc_datachannel_get_buffered_amount(wrtc->data_channel, &bytes);
  asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  // Let the sender report a closed channel and fail the outbound queue.
  return result == ASCIICHAT_OK ? bytes : 0;
}

static const acip_transport_methods_t webrtc_methods = {
    .send = webrtc_send,
    .queued_bytes = webrtc_queued_bytes,
    .reserved_bytes = ACIP_OUTBOUND_SCTP_RESERVE,
    .recv = webrtc_recv,
    .close = webrtc_close,
    .interrupt = webrtc_interrupt,
    .get_type = webrtc_get_type,
    .get_socket = webrtc_get_socket,
    .is_connected = webrtc_is_connected,
    .has_pending_data = webrtc_has_pending_data,
    .destroy_impl = webrtc_destroy_impl,
};

// =============================================================================
// WebRTC Transport Creation
// =============================================================================

static acip_transport_t *webrtc_transport_create(webrtc_peer_connection_t *peer_conn,
                                                 webrtc_data_channel_t *data_channel, crypto_context_t *crypto_ctx,
                                                 acip_security_t *security,
                                                 void (*on_ready)(acip_transport_t *, void *), void *user_data) {
  if (!peer_conn || !data_channel) {
    SET_ERRNO(ERROR_INVALID_PARAM, "peer_conn and data_channel are required");
    return NULL;
  }

  // Allocate transport structure
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(acip_transport_t), acip_transport_t *);
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
  // Installing the receive callback may synchronously drain queued messages.
  // The channel is already open: initialize send readiness before that can
  // advance the handshake and send its protocol response.
  wrtc_data->is_connected = true;
  webrtc_datachannel_set_open_state(data_channel, true);

  transport->methods = &webrtc_methods;
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = wrtc_data;
  wrtc_data->transport = transport;
  wrtc_data->security = security;
  wrtc_data->on_ready = on_ready;
  wrtc_data->ready_user_data = user_data;

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

  // Initialize transport
  transport->methods = &webrtc_methods;
  transport->stats_peer = stats_runtime_peer_open("WebRTC");
  transport->crypto_ctx = crypto_ctx;
  transport->impl_data = wrtc_data;

  log_info("Created WebRTC transport (crypto: %s)", crypto_ctx ? "enabled" : "disabled");

  // Register WebRTC transport implementation data with transport as parent
  NAMED_REGISTER(wrtc_data, "impl", "webrtc_impl", "0x%tx", (uintptr_t)(const void *)(transport));

  if (acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  return transport;
}

acip_transport_t *acip_webrtc_transport_create(webrtc_peer_connection_t *peer_conn, webrtc_data_channel_t *data_channel,
                                               crypto_context_t *crypto_ctx) {
  return webrtc_transport_create(peer_conn, data_channel, crypto_ctx, NULL, NULL, NULL);
}

acip_transport_t *acip_webrtc_transport_create_secure(webrtc_peer_connection_t *peer_conn,
                                                      webrtc_data_channel_t *data_channel, acip_security_t *security,
                                                      void (*on_ready)(acip_transport_t *, void *), void *user_data) {
  if (!security) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Missing WebRTC security state");
    return NULL;
  }
  return webrtc_transport_create(peer_conn, data_channel, &security->handshake.crypto_ctx, security, on_ready,
                                 user_data);
}
