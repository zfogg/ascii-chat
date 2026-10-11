#include <ascii-chat/network/tcp/async.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/common.h>
#include <ascii-chat/network/packet/decoder.h>
#include <ascii-chat/buffer_pool.h>
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
  cond_t received;
  packet_decoder_t decoder;
  async_packet_t *head, *tail;
  size_t received_bytes, received_count;
  bool packet_received;
  atomic_t interrupted;
} async_tcp_t;

static asciichat_error_t async_sendv(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count) {
  if (!buffers || !count || count > SOCKET_IOV_MAX || buffers[0].len < sizeof(packet_header_t))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing async packet header");
  size_t size = 0;
  for (size_t i = 0; i < count; ++i) {
    if ((!buffers[i].data && buffers[i].len) || buffers[i].len > SOCKET_SEND_BUFFER_MAX - size)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Invalid async packet vectors");
    size += buffers[i].len;
  }
  async_tcp_t *tcp = transport->impl_data;
  mutex_lock(&tcp->mutex);
  if (tcp->closed) {
    mutex_unlock(&tcp->mutex);
    return SET_ERRNO(ERROR_NETWORK, "Async transport closed");
  }
  packet_header_t header;
  memcpy(&header, buffers[0].data, sizeof(header));
  bool encrypt = transport->crypto_ctx && transport->crypto_ctx->encrypt_data &&
                 crypto_is_ready(transport->crypto_ctx) && !packet_is_handshake_type(NET_TO_HOST_U16(header.type));
  asciichat_error_t result;
  if (!encrypt) {
    result = platform_event_stream_write(tcp->stream, buffers, count, NULL, NULL);
  } else if (size > CRYPTO_MAX_PLAINTEXT_SIZE) {
    result = SET_ERRNO(ERROR_NETWORK_SIZE, "Encrypted async packet too large");
  } else {
    uint8_t *plaintext = SAFE_MALLOC(size, uint8_t *);
    size_t offset = 0;
    for (size_t i = 0; i < count; ++i) {
      if (buffers[i].len)
        memcpy(plaintext + offset, buffers[i].data, buffers[i].len);
      offset += buffers[i].len;
    }
    size_t capacity = size + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
    uint8_t *ciphertext = SAFE_MALLOC(capacity, uint8_t *);
    size_t length = 0;
    crypto_result_t encrypted = crypto_encrypt(transport->crypto_ctx, plaintext, size, ciphertext, capacity, &length);
    SAFE_FREE(plaintext);
    if (encrypted == CRYPTO_OK) {
      packet_header_t outer = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                               .type = HOST_TO_NET_U16(PACKET_TYPE_ENCRYPTED),
                               .length = HOST_TO_NET_U32((uint32_t)length),
                               .crc32 = HOST_TO_NET_U32(asciichat_crc32(ciphertext, length)),
                               .client_id = 0};
      socket_buffer_t wire[] = {{&outer, sizeof(outer)}, {ciphertext, length}};
      result = platform_event_stream_write(tcp->stream, wire, 2, NULL, NULL);
    } else {
      result = SET_ERRNO(ERROR_CRYPTO, "Async packet encryption failed");
    }
    SAFE_FREE(ciphertext);
  }
  if (result != ASCIICHAT_OK) {
    // Encryption sequence may already have advanced. Never continue this stream.
    tcp->closed = true;
    platform_event_stream_request_close(tcp->stream, result);
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
  if (!tcp->loop || !data || !length || !allocation)
    return SET_ERRNO(ERROR_INVALID_STATE, "Async server receives through packet callbacks");
  *data = *allocation = NULL;
  *length = 0;
  uint64_t start = time_get_ns();
  uint64_t timeout = transport->receive_timeout_ns ? transport->receive_timeout_ns : NS_PER_SEC_INT;
  mutex_lock(&tcp->mutex);
  while (!tcp->head && !tcp->closed) {
    uint64_t elapsed = time_get_ns() - start;
    if (elapsed >= timeout) {
      mutex_unlock(&tcp->mutex);
      return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "Async receive deadline expired");
    }
    cond_timedwait(&tcp->received, &tcp->mutex, timeout - elapsed);
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
    buffer_pool_free(NULL, envelope.allocated_buffer, envelope.allocated_size);
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
  bool connected = !tcp->closed;
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
  bool pending = tcp->head != NULL || tcp->closed;
  mutex_unlock(&tcp->mutex);
  return pending;
}
static void async_destroy(acip_transport_t *transport) {
  async_tcp_t *tcp = transport->impl_data;
  if (tcp->loop) {
    platform_event_loop_stop(tcp->loop);
    if (tcp->thread_started)
      asciichat_thread_join(&tcp->thread, NULL);
    else
      platform_event_loop_run(tcp->loop);
    platform_event_loop_destroy(tcp->loop);
    packet_decoder_reset(&tcp->decoder);
    while (tcp->head) {
      async_packet_t *packet = tcp->head;
      tcp->head = packet->next;
      buffer_pool_free(NULL, packet->data, packet->size);
      SAFE_FREE(packet);
    }
    cond_destroy(&tcp->received);
  }
  mutex_destroy(&tcp->mutex);
}
static const acip_transport_methods_t async_methods = {.send = async_send,
                                                       .sendv = async_sendv,
                                                       .recv = async_recv,
                                                       .close = async_close,
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
  packet->data = buffer_pool_alloc(NULL, length);
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
  mutex_unlock(&tcp->mutex);
  platform_event_loop_stop(tcp->loop);
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
void acip_async_tcp_transport_interrupt(acip_transport_t *transport) {
  if (acip_async_tcp_transport_is(transport)) {
    async_tcp_t *tcp = transport->impl_data;
    atomic_store_bool(&tcp->interrupted, true);
  }
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
  if (platform_event_loop_create(&tcp->loop) != ASCIICHAT_OK) {
    cond_destroy(&tcp->received);
    acip_transport_destroy(transport);
    return NULL;
  }
  asciichat_error_t result = platform_event_stream_adopt(tcp->loop, socket, &tcp->stream);
  if (result == ASCIICHAT_OK)
    result = platform_event_stream_start(tcp->stream, client_read, client_closed, tcp);
  if (result == ASCIICHAT_OK)
    result = platform_event_loop_tick(tcp->loop, 50, client_tick, tcp);
  if (result == ASCIICHAT_OK)
    result = asciichat_thread_create(&tcp->thread, "tcp_io", client_loop, tcp);
  if (result != ASCIICHAT_OK) {
    acip_transport_destroy(transport);
    return NULL;
  }
  tcp->thread_started = true;
  return transport;
}
