#include "transport.h"

#include <ascii-chat/buffer.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

#define QUIC_RECEIVE_BUDGET (16U * 1024U * 1024U)

typedef struct received_packet {
  struct received_packet *next;
  uint8_t *data;
  size_t length, cost;
} received_packet_t;

typedef struct {
  acip_quic_connection_t *connection;
  mutex_t *lane;
  received_packet_t *head, *tail;
  size_t bytes;
  bool connected;
} quic_transport_t;

static void lane_lock(quic_transport_t *quic) {
  if (quic->lane)
    mutex_lock(quic->lane);
}

static void lane_unlock(quic_transport_t *quic) {
  if (quic->lane)
    mutex_unlock(quic->lane);
}

static asciichat_error_t queue_received_packet(acip_traffic_class_t traffic_class, const void *packet, size_t length,
                                               void *context) {
  (void)traffic_class;
  quic_transport_t *quic = context;
  if (!quic->connected || length > QUIC_RECEIVE_BUDGET - quic->bytes)
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC receive queue is full or closed");
  received_packet_t *item = SAFE_CALLOC(1, sizeof(*item), received_packet_t *);
  if (!item)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC receive queue entry");
  item->data = buffer_alloc(NULL, length);
  if (!item->data) {
    SAFE_FREE(item);
    return SET_ERRNO(ERROR_MEMORY, "Cannot copy QUIC received packet");
  }
  item->length = length;
  item->cost = buffer_capacity(item->data) + sizeof(*item);
  if (item->cost > QUIC_RECEIVE_BUDGET - quic->bytes) {
    buffer_release(item->data);
    SAFE_FREE(item);
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC receive queue is full");
  }
  memcpy(item->data, packet, length);
  if (quic->tail)
    quic->tail->next = item;
  else
    quic->head = item;
  quic->tail = item;
  quic->bytes += item->cost;
  return ASCIICHAT_OK;
}

static asciichat_error_t quic_sendv_ex(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count,
                                       const acip_send_options_t *options) {
  quic_transport_t *quic = transport->impl_data;
  if (!buffers || !count || count > SOCKET_IOV_MAX || !options)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport send arguments");
  if (options->delivery != ACIP_DELIVERY_RELIABLE || options->deadline_ns)
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "QUIC datagrams and deadlines are not available");
  size_t length = 0;
  for (size_t i = 0; i < count; ++i) {
    if ((!buffers[i].data && buffers[i].len) || buffers[i].len > SOCKET_SEND_BUFFER_MAX - length)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Invalid QUIC send vectors");
    length += buffers[i].len;
  }
  if (length < sizeof(packet_header_t) || length - sizeof(packet_header_t) > MAX_PACKET_SIZE)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Invalid QUIC packet size");
  uint8_t *packet = buffer_alloc(NULL, length);
  if (!packet)
    return SET_ERRNO(ERROR_MEMORY, "Cannot assemble QUIC packet");
  size_t offset = 0;
  for (size_t i = 0; i < count; ++i) {
    if (buffers[i].len)
      memcpy(packet + offset, buffers[i].data, buffers[i].len);
    offset += buffers[i].len;
  }
  lane_lock(quic);
  asciichat_error_t result = quic->connected
                                 ? acip_quic_connection_submit(quic->connection, options->traffic_class, packet, length)
                                 : SET_ERRNO(ERROR_NETWORK, "QUIC transport is closed");
  lane_unlock(quic);
  buffer_release(packet);
  return result;
}

static asciichat_error_t quic_send(acip_transport_t *transport, const void *data, size_t length) {
  if (!data || length < sizeof(packet_header_t))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC ACIP packet header");
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  acip_send_options_t options = {.traffic_class = acip_packet_traffic_class(NET_TO_HOST_U16(header.type)),
                                 .delivery = ACIP_DELIVERY_RELIABLE};
  socket_buffer_t slice = {data, length};
  return quic_sendv_ex(transport, &slice, 1, &options);
}

static asciichat_error_t quic_recv(acip_transport_t *transport, void **buffer, size_t *length, void **allocation) {
  if (!buffer || !length || !allocation)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC receive output");
  *buffer = NULL;
  *length = 0;
  *allocation = NULL;
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  if (!quic->connected) {
    lane_unlock(quic);
    return SET_ERRNO(ERROR_NETWORK, "QUIC transport is closed");
  }
  received_packet_t *item = quic->head;
  if (!item) {
    lane_unlock(quic);
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "No QUIC packet is ready");
  }
  quic->head = item->next;
  if (!quic->head)
    quic->tail = NULL;
  quic->bytes -= item->cost;
  *buffer = *allocation = item->data;
  *length = item->length;
  lane_unlock(quic);
  SAFE_FREE(item);
  return ASCIICHAT_OK;
}

static asciichat_error_t quic_close(acip_transport_t *transport) {
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  if (!quic->connected) {
    lane_unlock(quic);
    return ASCIICHAT_OK;
  }
  quic->connected = false;
  asciichat_error_t result = acip_quic_connection_clear_receiver(quic->connection, quic);
  while (quic->head) {
    received_packet_t *item = quic->head;
    quic->head = item->next;
    buffer_release(item->data);
    SAFE_FREE(item);
  }
  quic->tail = NULL;
  quic->bytes = 0;
  lane_unlock(quic);
  return result;
}

static void quic_interrupt(acip_transport_t *transport) {
  (void)quic_close(transport);
}
static acip_transport_type_t quic_type(acip_transport_t *transport) {
  (void)transport;
  return ACIP_TRANSPORT_QUIC;
}
static socket_t quic_socket(acip_transport_t *transport) {
  (void)transport;
  return INVALID_SOCKET_VALUE;
}
static bool quic_connected(acip_transport_t *transport) {
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  bool connected = quic->connected;
  lane_unlock(quic);
  return connected;
}
static bool quic_pending(acip_transport_t *transport) {
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  bool pending = quic->head != NULL;
  lane_unlock(quic);
  return pending;
}
static size_t quic_queued(acip_transport_t *transport) {
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  size_t bytes = acip_quic_connection_pending_bytes(quic->connection);
  lane_unlock(quic);
  return bytes;
}
static const acip_transport_methods_t quic_methods = {.send = quic_send,
                                                      .sendv_ex = quic_sendv_ex,
                                                      .recv = quic_recv,
                                                      .close = quic_close,
                                                      .interrupt = quic_interrupt,
                                                      .get_type = quic_type,
                                                      .get_socket = quic_socket,
                                                      .is_connected = quic_connected,
                                                      .has_pending_data = quic_pending,
                                                      .queued_bytes = quic_queued};

static asciichat_error_t attach_transport(acip_quic_connection_t *connection, crypto_context_t *crypto, mutex_t *lane,
                                          acip_transport_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC transport output");
  *out = NULL;
  if (!connection)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC connection");
  acip_transport_t *transport = SAFE_CALLOC(1, sizeof(*transport), acip_transport_t *);
  quic_transport_t *quic = SAFE_CALLOC(1, sizeof(*quic), quic_transport_t *);
  if (!transport || !quic) {
    SAFE_FREE(transport);
    SAFE_FREE(quic);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC transport");
  }
  quic->connection = connection;
  quic->lane = lane;
  quic->connected = true;
  transport->methods = &quic_methods;
  transport->impl_data = quic;
  transport->crypto_ctx = crypto;
  lane_lock(quic);
  asciichat_error_t result = acip_quic_connection_set_receiver(connection, queue_received_packet, quic);
  lane_unlock(quic);
  if (result != ASCIICHAT_OK) {
    SAFE_FREE(quic);
    SAFE_FREE(transport);
    return result;
  }
  *out = transport;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_transport_attach(acip_quic_connection_t *connection, crypto_context_t *crypto,
                                             acip_transport_t **out) {
  return attach_transport(connection, crypto, NULL, out);
}

asciichat_error_t acip_quic_transport_attach_serialized(acip_quic_connection_t *connection, crypto_context_t *crypto,
                                                        mutex_t *lane, acip_transport_t **out) {
  if (!lane) {
    if (out)
      *out = NULL;
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC lane mutex");
  }
  return attach_transport(connection, crypto, lane, out);
}
