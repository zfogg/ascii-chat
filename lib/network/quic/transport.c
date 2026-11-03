#include "transport.h"
#include "datagram.h"

#include <ascii-chat/buffer.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/platform/cond.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/time.h>
#include <errno.h>
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
  cond_t received;
  cond_t security_changed;
  void (*wake)(void *);
  void *wake_context;
  void (*on_destroy)(acip_transport_t *, void *);
  void *destroy_context;
  received_packet_t *head, *tail;
  size_t bytes;
  asciichat_error_t failure;
  bool connected;
} quic_transport_t;

static const acip_transport_methods_t quic_methods;

asciichat_error_t acip_quic_transport_peer_address(acip_transport_t *transport, struct sockaddr_storage *address,
                                                   socklen_t *length) {
  if (!transport || transport->methods != &quic_methods || !address || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC peer address query");
  quic_transport_t *quic = transport->impl_data;
  if (quic->lane)
    mutex_lock(quic->lane);
  asciichat_error_t result = quic->connected ? acip_quic_connection_peer_address(quic->connection, address, length)
                                             : SET_ERRNO(quic->failure, "QUIC transport is closed");
  if (quic->lane)
    mutex_unlock(quic->lane);
  return result;
}

static void security_ready_notify(void *context) {
  quic_transport_t *quic = context;
  if (quic->lane)
    cond_broadcast(&quic->security_changed);
}

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
  if (quic->lane)
    cond_signal(&quic->received);
  return ASCIICHAT_OK;
}

static asciichat_error_t quic_sendv_ex(acip_transport_t *transport, const socket_buffer_t *buffers, size_t count,
                                       const acip_send_options_t *options) {
  quic_transport_t *quic = transport->impl_data;
  if (!buffers || !count || count > SOCKET_IOV_MAX || !options)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport send arguments");
  if (options->delivery == ACIP_DELIVERY_RELIABLE && options->deadline_ns)
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "QUIC reliable deadlines are not available");
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
  asciichat_error_t result =
      !quic->connected ? SET_ERRNO(quic->failure, "QUIC transport is closed")
      : options->delivery == ACIP_DELIVERY_DATAGRAM
          ? acip_quic_connection_submit_datagram(quic->connection, options->traffic_class, packet, length,
                                                 options->deadline_ns)
          : acip_quic_connection_submit(quic->connection, options->traffic_class, packet, length);
  if (result == ASCIICHAT_OK && quic->wake)
    quic->wake(quic->wake_context);
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
  if (quic->lane && quic->connected && !quic->head) {
    uint64_t timeout = transport->receive_timeout_ns ? transport->receive_timeout_ns : NS_PER_SEC_INT;
    uint64_t start = time_get_ns();
    while (quic->connected && !quic->head) {
      uint64_t elapsed = time_get_ns() - start;
      if (elapsed >= timeout)
        break;
      uint64_t remaining = timeout - elapsed;
      int wait_result =
          cond_timedwait(&quic->received, quic->lane, remaining < NS_PER_SEC_INT ? remaining : NS_PER_SEC_INT);
      if (wait_result != 0 && wait_result != ETIMEDOUT) {
        lane_unlock(quic);
        return SET_ERRNO(ERROR_NETWORK, "QUIC receive wait failed");
      }
    }
  }
  if (!quic->connected) {
    asciichat_error_t failure = quic->failure;
    lane_unlock(quic);
    return SET_ERRNO(failure, "QUIC transport is closed");
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

static asciichat_error_t quic_close_locked(quic_transport_t *quic, asciichat_error_t reason) {
  if (!quic->connected) {
    return ASCIICHAT_OK;
  }
  quic->connected = false;
  quic->failure = reason;
  quic->wake = NULL;
  quic->wake_context = NULL;
  if (quic->lane) {
    cond_broadcast(&quic->received);
    cond_broadcast(&quic->security_changed);
  }
  asciichat_error_t result = acip_quic_connection_clear_receiver(quic->connection, quic);
  quic->connection = NULL;
  while (quic->head) {
    received_packet_t *item = quic->head;
    quic->head = item->next;
    buffer_release(item->data);
    SAFE_FREE(item);
  }
  quic->tail = NULL;
  quic->bytes = 0;
  return result;
}

static asciichat_error_t quic_close(acip_transport_t *transport) {
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  asciichat_error_t result = quic_close_locked(quic, ERROR_NETWORK);
  lane_unlock(quic);
  return result;
}

asciichat_error_t acip_quic_transport_fail(acip_transport_t *transport, asciichat_error_t reason) {
  if (!transport || transport->methods != &quic_methods || reason == ASCIICHAT_OK)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport failure");
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  asciichat_error_t result = quic_close_locked(quic, reason);
  lane_unlock(quic);
  return result;
}

asciichat_error_t acip_quic_transport_fail_on_lane(acip_transport_t *transport, asciichat_error_t reason) {
  if (!transport || transport->methods != &quic_methods || reason == ASCIICHAT_OK)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport failure");
  return quic_close_locked(transport->impl_data, reason);
}

asciichat_error_t acip_quic_transport_destroy_unclaimed_on_lane(acip_transport_t **transport,
                                                                asciichat_error_t reason) {
  if (!transport || !*transport || (*transport)->methods != &quic_methods || (*transport)->outbound ||
      (*transport)->stats_peer || reason == ASCIICHAT_OK)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid unclaimed QUIC transport");
  quic_transport_t *quic = (*transport)->impl_data;
  asciichat_error_t result = quic_close_locked(quic, reason);
  if (result != ASCIICHAT_OK)
    return result;
  if (quic->lane)
    cond_destroy(&quic->received);
  if (quic->lane)
    cond_destroy(&quic->security_changed);
  SAFE_FREE((*transport)->impl_data);
  SAFE_FREE(*transport);
  *transport = NULL;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_transport_set_waker_on_lane(acip_transport_t *transport, void (*wake)(void *),
                                                        void *context) {
  if (!transport || transport->methods != &quic_methods)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport waker");
  quic_transport_t *quic = transport->impl_data;
  if (!quic->connected)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC transport is closed");
  quic->wake = wake;
  quic->wake_context = wake ? context : NULL;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_transport_set_waker(acip_transport_t *transport, void (*wake)(void *), void *context) {
  if (!transport || transport->methods != &quic_methods)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport waker");
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  asciichat_error_t result = acip_quic_transport_set_waker_on_lane(transport, wake, context);
  lane_unlock(quic);
  return result;
}

asciichat_error_t acip_quic_transport_begin_security(acip_transport_t *transport, crypto_context_t *crypto) {
  if (!transport || transport->methods != &quic_methods || !crypto)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport security context");
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  asciichat_error_t result = quic->connected ? acip_quic_connection_enable_security(quic->connection, crypto)
                                             : SET_ERRNO(quic->failure, "QUIC transport is closed");
  if (result == ASCIICHAT_OK) {
    transport->crypto_ctx = crypto;
    if (quic->wake)
      quic->wake(quic->wake_context);
  }
  lane_unlock(quic);
  return result;
}

bool acip_quic_transport_security_ready(acip_transport_t *transport) {
  if (!transport || transport->methods != &quic_methods)
    return false;
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  bool ready = quic->connected && acip_quic_connection_security_ready(quic->connection);
  lane_unlock(quic);
  return ready;
}

asciichat_error_t acip_quic_transport_wait_security(acip_transport_t *transport, uint64_t timeout_ns) {
  if (!transport || transport->methods != &quic_methods)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport");
  quic_transport_t *quic = transport->impl_data;
  if (!quic->lane)
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "QUIC security wait requires a serialized endpoint");
  uint64_t start = time_get_ns();
  lane_lock(quic);
  while (quic->connected && !acip_quic_connection_security_ready(quic->connection)) {
    uint64_t elapsed = time_get_ns() - start;
    if (elapsed >= timeout_ns) {
      lane_unlock(quic);
      return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "QUIC security readiness timed out");
    }
    int rc = cond_timedwait(&quic->security_changed, quic->lane, timeout_ns - elapsed);
    if (rc != 0 && rc != ETIMEDOUT) {
      lane_unlock(quic);
      return SET_ERRNO(ERROR_THREAD, "QUIC security readiness wait failed");
    }
  }
  asciichat_error_t result = quic->connected ? ASCIICHAT_OK : SET_ERRNO(quic->failure, "QUIC transport is closed");
  lane_unlock(quic);
  return result;
}

asciichat_error_t acip_quic_transport_allow_media(acip_transport_t *transport) {
  if (!transport || transport->methods != &quic_methods)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC transport");
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  asciichat_error_t result = quic->connected ? acip_quic_connection_allow_media(quic->connection)
                                             : SET_ERRNO(quic->failure, "QUIC transport is closed");
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
  size_t bytes = quic->connection ? acip_quic_connection_pending_bytes(quic->connection) : 0;
  lane_unlock(quic);
  return bytes;
}
static asciichat_error_t quic_capabilities(acip_transport_t *transport, acip_transport_capabilities_t *out) {
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  if (!quic->connected) {
    asciichat_error_t result = SET_ERRNO(quic->failure, "QUIC transport is closed");
    lane_unlock(quic);
    return result;
  }
  if (!acip_quic_connection_tls_ready(quic->connection)) {
    lane_unlock(quic);
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC capabilities are not negotiated yet");
  }
  size_t payload = acip_quic_connection_datagram_payload(quic->connection);
  *out =
      (acip_transport_capabilities_t){.independent_streams = true,
                                      .datagrams = payload != 0,
                                      .max_datagram_payload = payload ? payload + ACIP_QUIC_DATAGRAM_HEADER_SIZE : 0};
  lane_unlock(quic);
  return ASCIICHAT_OK;
}

static asciichat_error_t quic_validate_options(acip_transport_t *transport, const acip_send_options_t *options) {
  if (options->delivery != ACIP_DELIVERY_DATAGRAM)
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "QUIC reliable deadlines are not available");
  if (options->deadline_ns && options->deadline_ns <= time_get_ns())
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "QUIC datagram deadline expired");
  quic_transport_t *quic = transport->impl_data;
  lane_lock(quic);
  asciichat_error_t result =
      quic->connected
          ? acip_quic_connection_validate_datagram(quic->connection, options->traffic_class, options->deadline_ns)
          : SET_ERRNO(quic->failure, "QUIC transport is closed");
  lane_unlock(quic);
  return result;
}
static void quic_destroy_impl(acip_transport_t *transport) {
  quic_transport_t *quic = transport->impl_data;
  if (quic->on_destroy)
    quic->on_destroy(transport, quic->destroy_context);
  if (quic->lane)
    cond_destroy(&quic->received);
  if (quic->lane)
    cond_destroy(&quic->security_changed);
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
                                                      .queued_bytes = quic_queued,
                                                      .get_capabilities = quic_capabilities,
                                                      .validate_send_options = quic_validate_options,
                                                      .destroy_impl = quic_destroy_impl};

static asciichat_error_t attach_transport(acip_quic_connection_t *connection, crypto_context_t *crypto, mutex_t *lane,
                                          bool lane_held, void (*wake)(void *), void *wake_context,
                                          void (*on_destroy)(acip_transport_t *, void *), void *destroy_context,
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
  quic->wake = wake;
  quic->wake_context = wake ? wake_context : NULL;
  quic->on_destroy = on_destroy;
  quic->destroy_context = destroy_context;
  quic->connected = true;
  if (lane && cond_init(&quic->received, "quic_receive") != 0) {
    SAFE_FREE(quic);
    SAFE_FREE(transport);
    return SET_ERRNO(ERROR_MEMORY, "Cannot initialize QUIC receive wait");
  }
  if (lane && cond_init(&quic->security_changed, "quic_security") != 0) {
    cond_destroy(&quic->received);
    SAFE_FREE(quic);
    SAFE_FREE(transport);
    return SET_ERRNO(ERROR_MEMORY, "Cannot initialize QUIC security wait");
  }
  transport->methods = &quic_methods;
  transport->impl_data = quic;
  transport->crypto_ctx = crypto;
  if (!lane_held)
    lane_lock(quic);
  asciichat_error_t result = acip_quic_connection_set_receiver(connection, queue_received_packet, quic);
  if (result == ASCIICHAT_OK)
    acip_quic_connection_set_security_notifier(connection, security_ready_notify, quic);
  if (!lane_held)
    lane_unlock(quic);
  if (result != ASCIICHAT_OK) {
    if (lane) {
      cond_destroy(&quic->received);
      cond_destroy(&quic->security_changed);
    }
    SAFE_FREE(quic);
    SAFE_FREE(transport);
    return result;
  }
  *out = transport;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_transport_attach(acip_quic_connection_t *connection, crypto_context_t *crypto,
                                             acip_transport_t **out) {
  return attach_transport(connection, crypto, NULL, false, NULL, NULL, NULL, NULL, out);
}

asciichat_error_t acip_quic_transport_attach_serialized(acip_quic_connection_t *connection, crypto_context_t *crypto,
                                                        mutex_t *lane, acip_transport_t **out) {
  if (!lane) {
    if (out)
      *out = NULL;
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC lane mutex");
  }
  return attach_transport(connection, crypto, lane, false, NULL, NULL, NULL, NULL, out);
}

asciichat_error_t acip_quic_transport_attach_on_lane(acip_quic_connection_t *connection, crypto_context_t *crypto,
                                                     mutex_t *lane, acip_transport_t **out) {
  if (!lane) {
    if (out)
      *out = NULL;
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC lane mutex");
  }
  return attach_transport(connection, crypto, lane, true, NULL, NULL, NULL, NULL, out);
}

asciichat_error_t acip_quic_transport_attach_on_lane_with_waker(acip_quic_connection_t *connection, mutex_t *lane,
                                                                void (*wake)(void *), void *context,
                                                                void (*on_destroy)(acip_transport_t *, void *),
                                                                void *destroy_context, acip_transport_t **out) {
  if (!lane) {
    if (out)
      *out = NULL;
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC lane mutex");
  }
  return attach_transport(connection, NULL, lane, true, wake, context, on_destroy, destroy_context, out);
}
