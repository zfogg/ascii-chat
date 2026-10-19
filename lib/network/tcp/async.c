#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/common.h>
#include <ascii-chat/network/packet/decoder.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/util/time.h>

typedef struct async_packet {
  struct async_packet *next;
  void *data;
  size_t size;
} async_packet_t;

typedef struct {
  platform_event_stream_t *stream;
  mutex_t mutex;
  bool closed;
  platform_event_loop_t *loop;
  asciichat_thread_t thread;
  bool thread_started;
  bool queued_receive;
  acip_transport_t *destroy_on_close;
  cond_t received;
  packet_decoder_t decoder;
  async_packet_t *head, *tail;
  size_t received_bytes, received_count;
  bool packet_received;
  atomic_t interrupted;
} async_tcp_t;

static asciichat_error_t async_sendv(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  async_tcp_t *tcp = transport->impl_data;
  mutex_lock(&tcp->mutex);
  if (tcp->closed || atomic_load_bool(&tcp->interrupted)) {
    mutex_unlock(&tcp->mutex);
    return SET_ERRNO(ERROR_NETWORK, "Async transport closed");
  }
  acip_packet_plan_t plan;
  asciichat_error_t result = acip_packet_plan(transport->crypto_ctx, buffers, count, &plan);
  if (result != ASCIICHAT_OK) {
    mutex_unlock(&tcp->mutex);
    return result;
  }
  if (!plan.encrypt) {
    result = platform_event_stream_write(tcp->stream, buffers, count, NULL, NULL);
  } else {
    uint8_t *ciphertext = buffer_alloc(plan.domain, plan.ciphertext_capacity);
    if (!ciphertext) {
      mutex_unlock(&tcp->mutex);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate async ciphertext");
    }
    size_t length = 0;
    packet_header_t outer;
    result = acip_packet_encrypt(transport->crypto_ctx, &plan, plan.domain, ciphertext, plan.ciphertext_capacity,
                                 &outer, &length);
    if (result == ERROR_MEMORY) {
      buffer_release(ciphertext);
      mutex_unlock(&tcp->mutex);
      return result;
    }
    if (result == ASCIICHAT_OK) {
      socket_buffer_t wire[] = {{&outer, sizeof(outer)}, {ciphertext, length}};
      // The event stream copies the vectors before returning.
      result = platform_event_stream_write(tcp->stream, wire, 2, NULL, NULL);
    }
    buffer_release(ciphertext);
  }
  if (result != ASCIICHAT_OK) {
    // Encryption sequence may already have advanced. Never continue this stream.
    tcp->closed = true;
    platform_event_stream_request_close(tcp->stream, result);
    if (result != ERROR_CRYPTO)
      result = SET_ERRNO(ERROR_NETWORK, "Async packet transmission failed");
  }
  mutex_unlock(&tcp->mutex);
  return result;
}

static asciichat_error_t async_send(acip_transport_t *transport, const void *data, size_t length) {
  socket_buffer_t buffer = {data, length};
  return async_sendv(transport, &buffer, 1);
}

static asciichat_error_t async_recv(acip_transport_t *transport, void **data, size_t *length, void **allocation) {
  async_tcp_t *tcp = transport->impl_data;
  if (!tcp->queued_receive || !data || !length || !allocation)
    return SET_ERRNO(ERROR_INVALID_STATE, "Async server receives through packet callbacks");
  *data = *allocation = NULL;
  *length = 0;
  uint64_t start = time_get_ns();
  uint64_t timeout = transport->receive_timeout_ns ? transport->receive_timeout_ns : NS_PER_SEC_INT;
  mutex_lock(&tcp->mutex);
  while (!tcp->head && !tcp->closed) {
    if (atomic_load_bool(&tcp->interrupted)) {
      platform_event_stream_request_close(tcp->stream, ERROR_NETWORK);
      mutex_unlock(&tcp->mutex);
      return SET_ERRNO(ERROR_NETWORK, "Async receive interrupted");
    }
    uint64_t elapsed = time_get_ns() - start;
    if (elapsed >= timeout) {
      mutex_unlock(&tcp->mutex);
      return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "Async receive deadline expired");
    }
    uint64_t wait = timeout - elapsed;
    if (wait > 50 * NS_PER_MS_INT)
      wait = 50 * NS_PER_MS_INT;
    cond_timedwait(&tcp->received, &tcp->mutex, wait);
  }
  if (!tcp->head) {
    mutex_unlock(&tcp->mutex);
    return SET_ERRNO(ERROR_NETWORK, "Async peer disconnected");
  }
  async_packet_t *packet = tcp->head;
  tcp->head = packet->next;
  if (!tcp->head)
    tcp->tail = NULL;
  tcp->received_bytes -= packet->size;
  --tcp->received_count;
  mutex_unlock(&tcp->mutex);
  packet_header_t header;
  memcpy(&header, packet->data, sizeof(header));
  packet_envelope_t envelope = {.type = NET_TO_HOST_U16(header.type),
                                .data = (uint8_t *)packet->data + sizeof(header),
                                .len = packet->size - sizeof(header),
                                .allocated_buffer = packet->data,
                                .allocated_size = packet->size};
  SAFE_FREE(packet);
  asciichat_error_t result = ASCIICHAT_OK;
  mutex_lock(&tcp->mutex);
  if (envelope.type == PACKET_TYPE_ENCRYPTED) {
    result = packet_decrypt_envelope(&envelope, transport->crypto_ctx);
    if (result == ASCIICHAT_OK) {
      memcpy(&header, envelope.allocated_buffer, sizeof(header));
      if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC ||
          (envelope.len && asciichat_crc32(envelope.data, envelope.len) != NET_TO_HOST_U32(header.crc32)))
        result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid decrypted async packet");
    }
  } else if (transport->crypto_ctx && transport->crypto_ctx->encrypt_data && !packet_is_handshake_type(envelope.type)) {
    result = SET_ERRNO(ERROR_CRYPTO, "Unexpected plaintext packet");
  }
  mutex_unlock(&tcp->mutex);
  if (result != ASCIICHAT_OK) {
    buffer_release(envelope.allocated_buffer);
    mutex_lock(&tcp->mutex);
    platform_event_stream_request_close(tcp->stream, result);
    mutex_unlock(&tcp->mutex);
    return result;
  }
  *data = *allocation = envelope.allocated_buffer;
  *length = envelope.len + sizeof(header);
  return ASCIICHAT_OK;
}

static asciichat_error_t async_close(acip_transport_t *transport) {
  async_tcp_t *tcp = transport->impl_data;
  mutex_lock(&tcp->mutex);
  tcp->closed = true;
  platform_event_stream_request_close(tcp->stream, ASCIICHAT_OK);
  mutex_unlock(&tcp->mutex);
  return ASCIICHAT_OK;
}

void acip_async_tcp_transport_detach(acip_transport_t *transport) {
  async_tcp_t *tcp = transport->impl_data;
  mutex_lock(&tcp->mutex);
  tcp->closed = true;
  tcp->stream = NULL;
  mutex_unlock(&tcp->mutex);
}

static bool async_connected(acip_transport_t *transport) {
  async_tcp_t *tcp = transport->impl_data;
  mutex_lock(&tcp->mutex);
  bool connected = !tcp->closed && !atomic_load_bool(&tcp->interrupted);
  mutex_unlock(&tcp->mutex);
  return connected;
}
static acip_transport_type_t async_type(acip_transport_t *transport) {
  (void)transport;
  return ACIP_TRANSPORT_TCP;
}
static socket_t async_socket(acip_transport_t *transport) {
  (void)transport;
  return INVALID_SOCKET_VALUE;
}
static bool async_pending(acip_transport_t *transport) {
  async_tcp_t *tcp = transport->impl_data;
  mutex_lock(&tcp->mutex);
  bool pending = tcp->head != NULL || tcp->closed || atomic_load_bool(&tcp->interrupted);
  mutex_unlock(&tcp->mutex);
  return pending;
}
static void async_destroy(acip_transport_t *transport) {
  async_tcp_t *tcp = transport->impl_data;
  if (tcp->queued_receive && !tcp->loop) {
    mutex_lock(&tcp->mutex);
    if (tcp->stream)
      platform_event_stream_request_close(tcp->stream, ASCIICHAT_OK);
    while (tcp->stream)
      cond_wait(&tcp->received, &tcp->mutex);
    mutex_unlock(&tcp->mutex);
  }
  if (tcp->loop) {
    platform_event_loop_stop(tcp->loop);
    if (tcp->thread_started)
      asciichat_thread_join(&tcp->thread, NULL);
    else
      platform_event_loop_run(tcp->loop);
    platform_event_loop_destroy(tcp->loop);
  }
  if (tcp->queued_receive) {
    packet_decoder_reset(&tcp->decoder);
    while (tcp->head) {
      async_packet_t *packet = tcp->head;
      tcp->head = packet->next;
      buffer_release(packet->data);
      SAFE_FREE(packet);
    }
    cond_destroy(&tcp->received);
  }
  mutex_destroy(&tcp->mutex);
}
static void async_interrupt(acip_transport_t *transport) {
  async_tcp_t *tcp = transport->impl_data;
  atomic_store_bool(&tcp->interrupted, true);
}

static size_t async_queued_bytes(acip_transport_t *transport) {
  return acip_async_tcp_transport_queued(transport);
}

static const acip_transport_methods_t async_methods = {.send = async_send,
                                                       .reserved_bytes = SOCKET_STREAM_SEND_RESERVE,
                                                       .queued_bytes = async_queued_bytes,
                                                       .sendv = async_sendv,
                                                       .recv = async_recv,
                                                       .close = async_close,
                                                       .interrupt = async_interrupt,
                                                       .get_type = async_type,
                                                       .get_socket = async_socket,
                                                       .is_connected = async_connected,
                                                       .has_pending_data = async_pending,
                                                       .destroy_impl = async_destroy};

bool acip_async_tcp_transport_is(const acip_transport_t *transport) {
  return transport && transport->methods == &async_methods;
}

size_t acip_async_tcp_transport_queued(acip_transport_t *transport) {
  async_tcp_t *tcp = transport->impl_data;
  mutex_lock(&tcp->mutex);
  size_t queued = platform_event_stream_queued_bytes(tcp->stream);
  mutex_unlock(&tcp->mutex);
  return queued;
}

acip_transport_t *acip_async_tcp_transport_create(platform_event_stream_t *stream, crypto_context_t *crypto) {
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  async_tcp_t *tcp = SAFE_CALLOC(1, sizeof(*tcp), async_tcp_t *);
  if (mutex_init(&tcp->mutex, "async_tcp_send") != ASCIICHAT_OK) {
    SAFE_FREE(tcp);
    SAFE_FREE(transport);
    return NULL;
  }
  tcp->stream = stream;
  transport->methods = &async_methods;
  transport->impl_data = tcp;
  transport->crypto_ctx = crypto;
  transport->stats_peer = stats_runtime_peer_open("TCP-async");
  if (acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  return transport;
}

static asciichat_error_t client_packet(const void *data, size_t length, void *arg) {
  async_tcp_t *tcp = arg;
  mutex_lock(&tcp->mutex);
  if (tcp->received_count >= 4096 || length > 16 * 1024 * 1024 - tcp->received_bytes) {
    mutex_unlock(&tcp->mutex);
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Async client receive queue full");
  }
  async_packet_t *packet = SAFE_CALLOC(1, sizeof(*packet), async_packet_t *);
  packet->size = length;
  packet->data = buffer_alloc(NULL, length);
  if (!packet->data) {
    SAFE_FREE(packet);
    mutex_unlock(&tcp->mutex);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate async receive packet");
  }
  memcpy(packet->data, data, length);
  if (tcp->tail)
    tcp->tail->next = packet;
  else
    tcp->head = packet;
  tcp->tail = packet;
  tcp->received_bytes += length;
  ++tcp->received_count;
  tcp->packet_received = true;
  cond_signal(&tcp->received);
  mutex_unlock(&tcp->mutex);
  return ASCIICHAT_OK;
}
static void client_read(platform_event_stream_t *stream, const void *data, size_t length, void *arg) {
  async_tcp_t *tcp = arg;
  bool partial = packet_decoder_pending(&tcp->decoder);
  tcp->packet_received = false;
  asciichat_error_t result = packet_decoder_feed(&tcp->decoder, data, length, client_packet, tcp);
  if (result != ASCIICHAT_OK)
    platform_event_stream_close(stream, result);
  else if (!packet_decoder_pending(&tcp->decoder))
    platform_event_stream_deadline(stream, 0);
  else if (!partial || tcp->packet_received)
    platform_event_stream_deadline(stream, 30000);
}
static void client_closed(void *arg, asciichat_error_t reason) {
  (void)reason;
  async_tcp_t *tcp = arg;
  mutex_lock(&tcp->mutex);
  tcp->closed = true;
  tcp->stream = NULL;
  cond_broadcast(&tcp->received);
  platform_event_loop_t *loop = tcp->loop;
  acip_transport_t *destroy_on_close = tcp->destroy_on_close;
  mutex_unlock(&tcp->mutex);
  if (loop)
    platform_event_loop_stop(loop);
  if (destroy_on_close)
    acip_transport_destroy(destroy_on_close);
}
static void client_tick(void *arg) {
  async_tcp_t *tcp = arg;
  if (atomic_load_bool(&tcp->interrupted))
    platform_event_stream_close(tcp->stream, ERROR_NETWORK);
}
static void *client_loop(void *arg) {
  async_tcp_t *tcp = arg;
  platform_event_loop_run(tcp->loop);
  return NULL;
}
acip_transport_t *acip_async_tcp_client_create(socket_t *socket, crypto_context_t *crypto) {
  acip_transport_t *transport = acip_async_tcp_transport_create(NULL, crypto);
  if (!transport)
    return NULL;
  async_tcp_t *tcp = transport->impl_data;
  if (cond_init(&tcp->received, "async_tcp_receive") != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  tcp->queued_receive = true;
  if (platform_event_loop_create(&tcp->loop) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  asciichat_error_t result = platform_event_stream_adopt(tcp->loop, socket, &tcp->stream);
  if (result == ASCIICHAT_OK) {
    result = platform_event_stream_start(tcp->stream, client_read, client_closed, tcp);
  }
  if (result == ASCIICHAT_OK)
    result = platform_event_loop_tick(tcp->loop, 50, client_tick, tcp);
  if (result == ASCIICHAT_OK)
    result = asciichat_thread_create(&tcp->thread, "tcp_io", client_loop, tcp);
  if (result != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  tcp->thread_started = true;
  if (acip_outbound_create(transport) != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  return transport;
}

acip_transport_t *acip_async_tcp_accepted_create(platform_event_stream_t *stream) {
  acip_transport_t *transport = acip_async_tcp_transport_create(stream, NULL);
  if (!transport)
    return NULL;
  async_tcp_t *tcp = transport->impl_data;
  if (cond_init(&tcp->received, "accepted_receive") != ASCIICHAT_OK) {
    tcp->stream = NULL;
    tcp->closed = true;
    acip_transport_destroy(transport);
    return NULL;
  }
  tcp->queued_receive = true;
  asciichat_error_t result = platform_event_stream_start(stream, client_read, client_closed, tcp);
  if (result != ASCIICHAT_OK) {
    // The stream retains its callbacks on startup failure; release their state after close completes.
    tcp->destroy_on_close = transport;
    platform_event_stream_close(stream, result);
    return NULL;
  }
  return transport;
}
