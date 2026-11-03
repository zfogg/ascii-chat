#include "connection.h"
#include "datagram.h"
#include "http3.h"

#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <sodium.h>
#include <string.h>

#define ACIP_QUIC_CONNECTION_ID_LENGTH 16
#define ACIP_QUIC_INITIAL_STREAM_WINDOW (5U * 1024U * 1024U)
#define ACIP_QUIC_INITIAL_CONNECTION_WINDOW (16U * 1024U * 1024U)
#define ACIP_QUIC_IDLE_TIMEOUT_NS (30ULL * 1000000000ULL)
#define ACIP_QUIC_FLOW_COUNT 3
#define ACIP_QUIC_DATAGRAM_FRAME_LIMIT 1400U
#define ACIP_QUIC_DATAGRAM_OVERHEAD 128U
#define ACIP_HTTP3_PEER_UNI_STREAMS 6U

typedef struct send_chunk {
  struct send_chunk *next;
  uint8_t *data;
  size_t length, submitted, cost;
  uint64_t end_offset;
} send_chunk_t;

typedef struct {
  int64_t id;
  uint64_t queued_offset, acked_offset;
  send_chunk_t *head, *tail;
  bool initialized, fin_requested, fin_submitted;
} send_stream_t;

typedef struct {
  int64_t id;
  uint64_t received_offset;
  acip_quic_stream_reader_t reader;
  bool active, registered;
} receive_stream_t;

typedef struct {
  int64_t id;
  uint64_t offset;
  uint8_t prefix[16];
  size_t prefix_used, prefix_length;
  unsigned kind; /* 0 undecided, 1 HTTP/3, 2 WebTransport */
  bool active;
} http3_receive_stream_t;

typedef struct datagram_send {
  struct datagram_send *next;
  uint8_t *data;
  size_t length, cost;
  uint64_t id, deadline_ns;
  uint16_t unit, index, count;
} datagram_send_t;

struct acip_quic_connection {
  ngtcp2_conn *conn;
  ngtcp2_crypto_conn_ref reference;
  ngtcp2_crypto_ossl_ctx *crypto;
  SSL *ssl;
  acip_quic_tls_t *tls;
  acip_quic_security_t security;
  acip_http3_server_t *http3;
  ngtcp2_cid initial_dcid;
  bool has_initial_dcid;
  uint64_t last_time_ns;
  send_stream_t sends[ACIP_QUIC_FLOW_COUNT];
  receive_stream_t receives[ACIP_QUIC_FLOW_COUNT];
  http3_receive_stream_t http3_receives[ACIP_HTTP3_PEER_UNI_STREAMS];
  bool received_classes[ACIP_QUIC_FLOW_COUNT];
  size_t pending_bytes;
  acip_quic_stream_emit_fn emit;
  void *emit_context;
  void (*security_notify)(void *);
  void *security_notify_context;
  void (*keyframe_notify)(uint16_t, uint32_t, void *);
  void *keyframe_notify_context;
  asciichat_error_t callback_error;
  bool failed;
  bool tls_ready;
  bool packet_pending;
  bool packet_has_stream_data;
  acip_traffic_class_t packet_traffic_class;
  bool media_allowed;
  bool peer_ready_seen;
  bool peer_security_ready;
  uint8_t *early_security_packet;
  size_t early_security_length;
  bool datagrams_enabled;
  acip_quic_datagram_reader_t datagram_reader;
  datagram_send_t *datagram_head[ACIP_QUIC_FLOW_COUNT], *datagram_tail[ACIP_QUIC_FLOW_COUNT];
  uint64_t datagram_id[ACIP_QUIC_FLOW_COUNT];
  uint64_t last_visual_sequence;
  uint16_t visual_codec;
  bool visual_waiting_keyframe, visual_keyframe_requested;
};

static asciichat_error_t update_readiness(acip_quic_connection_t *connection);
static asciichat_error_t queue_wire_packet(acip_quic_connection_t *connection, acip_traffic_class_t traffic_class,
                                           const void *packet, size_t length);
static const uint8_t quic_ready_payload[] = {'A', 'C', 'Q', 'R', 1};
static const uint8_t quic_ack_payload[] = {'A', 'C', 'Q', 'A', 1};

static asciichat_error_t queue_keyframe_request(acip_quic_connection_t *connection, uint16_t type, uint32_t id) {
  uint8_t payload[6];
  uint16_t network_type = HOST_TO_NET_U16(type);
  uint32_t network_id = HOST_TO_NET_U32(id);
  memcpy(payload, &network_type, sizeof(network_type));
  memcpy(payload + sizeof(network_type), &network_id, sizeof(network_id));
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(PACKET_TYPE_QUIC_KEYFRAME_REQUEST),
                            .length = HOST_TO_NET_U32(sizeof(payload)),
                            .crc32 = HOST_TO_NET_U32(asciichat_crc32(payload, sizeof(payload)))};
  uint8_t packet[sizeof(header) + sizeof(payload)];
  memcpy(packet, &header, sizeof(header));
  memcpy(packet + sizeof(header), payload, sizeof(payload));
  uint8_t *wire = NULL;
  size_t wire_length = 0;
  asciichat_error_t result = acip_quic_security_seal(
      &connection->security, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, packet, sizeof(packet), &wire, &wire_length);
  if (result == ASCIICHAT_OK)
    result = queue_wire_packet(connection, ACIP_TRAFFIC_CONTROL, wire, wire_length);
  buffer_release(wire);
  return result;
}

static asciichat_error_t queue_security_barrier(acip_quic_connection_t *connection, const uint8_t *payload,
                                                size_t payload_length) {
  packet_header_t header = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                            .type = HOST_TO_NET_U16(PACKET_TYPE_QUIC_READY),
                            .length = HOST_TO_NET_U32((uint32_t)payload_length),
                            .crc32 = HOST_TO_NET_U32(asciichat_crc32(payload, payload_length))};
  uint8_t marker[sizeof(header) + sizeof(quic_ready_payload)];
  if (payload_length != sizeof(quic_ready_payload))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC readiness marker");
  memcpy(marker, &header, sizeof(header));
  memcpy(marker + sizeof(header), payload, payload_length);
  uint8_t *wire = NULL;
  size_t wire_length = 0;
  asciichat_error_t result = acip_quic_security_seal(
      &connection->security, ACIP_TRAFFIC_CONTROL, ACIP_DELIVERY_RELIABLE, marker, sizeof(marker), &wire, &wire_length);
  if (result == ASCIICHAT_OK)
    result = queue_wire_packet(connection, ACIP_TRAFFIC_CONTROL, wire, wire_length);
  buffer_release(wire);
  return result;
}

static asciichat_error_t open_security_packet(acip_quic_connection_t *connection, acip_traffic_class_t traffic_class,
                                              const void *packet, size_t size) {
  uint8_t *plaintext = NULL;
  size_t plaintext_length = 0;
  asciichat_error_t result = acip_quic_security_open(&connection->security, traffic_class, ACIP_DELIVERY_RELIABLE,
                                                     packet, size, &plaintext, &plaintext_length);
  if (result != ASCIICHAT_OK)
    return result;
  packet_header_t header;
  memcpy(&header, plaintext, sizeof(header));
  packet_type_t type = NET_TO_HOST_U16(header.type);
  if (type == PACKET_TYPE_QUIC_READY) {
    const uint8_t *payload = plaintext + sizeof(header);
    if (traffic_class != ACIP_TRAFFIC_CONTROL || plaintext_length != sizeof(header) + sizeof(quic_ready_payload)) {
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid QUIC readiness barrier");
    } else if (!connection->peer_ready_seen && memcmp(payload, quic_ready_payload, sizeof(quic_ready_payload)) == 0) {
      connection->peer_ready_seen = true;
      result = queue_security_barrier(connection, quic_ack_payload, sizeof(quic_ack_payload));
    } else if (connection->peer_ready_seen && !connection->peer_security_ready &&
               memcmp(payload, quic_ack_payload, sizeof(quic_ack_payload)) == 0) {
      connection->peer_security_ready = true;
      if (connection->security_notify)
        connection->security_notify(connection->security_notify_context);
    } else {
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected QUIC readiness barrier");
    }
  } else if (!connection->peer_ready_seen) {
    result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Missing authenticated QUIC readiness barrier");
  } else if (type == PACKET_TYPE_QUIC_KEYFRAME_REQUEST) {
    uint16_t requested_type;
    uint32_t stream_id;
    if (traffic_class != ACIP_TRAFFIC_CONTROL || plaintext_length != sizeof(header) + 6) {
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid QUIC keyframe request");
    } else {
      memcpy(&requested_type, plaintext + sizeof(header), sizeof(requested_type));
      memcpy(&stream_id, plaintext + sizeof(header) + sizeof(requested_type), sizeof(stream_id));
      requested_type = NET_TO_HOST_U16(requested_type);
      stream_id = NET_TO_HOST_U32(stream_id);
      if ((requested_type != PACKET_TYPE_IMAGE_FRAME_H264 && requested_type != PACKET_TYPE_IMAGE_FRAME_H265) ||
          stream_id != 0)
        result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unsupported QUIC keyframe request");
      else if (connection->keyframe_notify)
        connection->keyframe_notify(requested_type, stream_id, connection->keyframe_notify_context);
    }
  } else {
    result = connection->emit(traffic_class, plaintext, plaintext_length, connection->emit_context);
  }
  buffer_release(plaintext);
  return result;
}

static bool initial_handshake_packet(packet_type_t type) {
  return type == PACKET_TYPE_ERROR_MESSAGE || type == PACKET_TYPE_PROTOCOL_VERSION ||
         type == PACKET_TYPE_CRYPTO_CLIENT_HELLO ||
         (type >= PACKET_TYPE_CRYPTO_CAPABILITIES && type < PACKET_TYPE_CRYPTO_NO_ENCRYPTION);
}

static void free_chunk(acip_quic_connection_t *connection, send_chunk_t *chunk) {
  connection->pending_bytes -= chunk->cost;
  buffer_release(chunk->data);
  SAFE_FREE(chunk);
}

static void free_unqueued_chunk(send_chunk_t *chunk) {
  if (chunk) {
    buffer_release(chunk->data);
    SAFE_FREE(chunk);
  }
}

static send_chunk_t *copy_chunk(const void *data, size_t length) {
  send_chunk_t *chunk = SAFE_CALLOC(1, sizeof(*chunk), send_chunk_t *);
  if (!chunk)
    return NULL;
  chunk->data = buffer_alloc(NULL, length);
  if (!chunk->data) {
    SAFE_FREE(chunk);
    return NULL;
  }
  memcpy(chunk->data, data, length);
  chunk->length = length;
  chunk->cost = buffer_capacity(chunk->data) + sizeof(*chunk);
  return chunk;
}

static void append_chunk(acip_quic_connection_t *connection, send_stream_t *stream, send_chunk_t *chunk) {
  stream->queued_offset += chunk->length;
  chunk->end_offset = stream->queued_offset;
  if (stream->tail)
    stream->tail->next = chunk;
  else
    stream->head = chunk;
  stream->tail = chunk;
  connection->pending_bytes += chunk->cost;
}

static int acknowledged_stream_data(ngtcp2_conn *conn, int64_t stream_id, uint64_t offset, uint64_t length,
                                    void *user_data, void *stream_user_data) {
  (void)conn;
  (void)stream_user_data;
  acip_quic_connection_t *connection = user_data;
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i) {
    send_stream_t *stream = &connection->sends[i];
    if (stream->id != stream_id)
      continue;
    if (offset != stream->acked_offset || offset > stream->queued_offset || length > stream->queued_offset - offset)
      return NGTCP2_ERR_CALLBACK_FAILURE;
    stream->acked_offset = offset + length;
    while (stream->head && stream->head->end_offset <= stream->acked_offset) {
      send_chunk_t *chunk = stream->head;
      if (chunk->submitted != chunk->length)
        return NGTCP2_ERR_CALLBACK_FAILURE;
      stream->head = chunk->next;
      if (!stream->head)
        stream->tail = NULL;
      free_chunk(connection, chunk);
    }
    return 0;
  }
  if (connection->http3 && acip_http3_server_ack(connection->http3, stream_id, length) == ASCIICHAT_OK)
    return 0;
  return NGTCP2_ERR_CALLBACK_FAILURE;
}

typedef struct {
  acip_quic_connection_t *connection;
  receive_stream_t *stream;
} receive_context_t;

static asciichat_error_t register_receive_class(receive_context_t *context) {
  receive_stream_t *stream = context->stream;
  if (stream->registered)
    return ASCIICHAT_OK;
  acip_traffic_class_t traffic_class = stream->reader.traffic_class;
  if ((unsigned)traffic_class >= ACIP_QUIC_FLOW_COUNT || context->connection->received_classes[traffic_class])
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Duplicate QUIC traffic stream");
  context->connection->received_classes[traffic_class] = true;
  stream->registered = true;
  return ASCIICHAT_OK;
}

static asciichat_error_t emit_received_packet(acip_traffic_class_t traffic_class, const void *packet, size_t size,
                                              void *arg) {
  receive_context_t *context = arg;
  asciichat_error_t result = register_receive_class(context);
  if (result != ASCIICHAT_OK)
    return result;
  if (!context->connection->emit || (traffic_class != ACIP_TRAFFIC_CONTROL &&
                                     (!context->connection->security.crypto || !context->connection->peer_ready_seen)))
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC packet received before application readiness");
  if (!context->connection->security.crypto) {
    packet_header_t header;
    memcpy(&header, packet, sizeof(header));
    packet_type_t type = NET_TO_HOST_U16(header.type);
    if (traffic_class == ACIP_TRAFFIC_CONTROL && type == PACKET_TYPE_ENCRYPTED) {
      if (context->connection->early_security_packet || size > 256)
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid early QUIC security barrier");
      context->connection->early_security_packet = buffer_alloc(NULL, size);
      if (!context->connection->early_security_packet)
        return SET_ERRNO(ERROR_MEMORY, "Cannot buffer early QUIC security barrier");
      memcpy(context->connection->early_security_packet, packet, size);
      context->connection->early_security_length = size;
      return ASCIICHAT_OK;
    }
    if (!initial_handshake_packet(type))
      return SET_ERRNO(ERROR_CRYPTO, "Non-handshake ACIP packet received before authentication");
    return context->connection->emit(traffic_class, packet, size, context->connection->emit_context);
  }
  return open_security_packet(context->connection, traffic_class, packet, size);
}

static int receive_acip_stream(ngtcp2_conn *conn, acip_quic_connection_t *connection, uint32_t flags, int64_t stream_id,
                               uint64_t offset, const uint8_t *data, size_t length) {
  receive_stream_t *stream = NULL;
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i) {
    if (connection->receives[i].active && connection->receives[i].id == stream_id) {
      stream = &connection->receives[i];
      break;
    }
  }
  if (!stream) {
    for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i) {
      if (!connection->receives[i].active) {
        stream = &connection->receives[i];
        stream->active = true;
        stream->id = stream_id;
        break;
      }
    }
  }
  if (!stream || offset != stream->received_offset || length > UINT64_MAX - offset) {
    connection->callback_error = ERROR_NETWORK_PROTOCOL;
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  receive_context_t context = {.connection = connection, .stream = stream};
  asciichat_error_t result = acip_quic_stream_read(&stream->reader, data, length, emit_received_packet, &context);
  if (result == ASCIICHAT_OK && stream->reader.preamble_used == ACIP_QUIC_STREAM_PREAMBLE_SIZE)
    result = register_receive_class(&context);
  if (result == ASCIICHAT_OK && (flags & NGTCP2_STREAM_DATA_FLAG_FIN))
    result = acip_quic_stream_finish(&stream->reader);
  if (result != ASCIICHAT_OK) {
    connection->callback_error = result;
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  stream->received_offset += length;
  if (ngtcp2_conn_extend_max_stream_offset(conn, stream_id, length) != 0) {
    connection->callback_error = ERROR_NETWORK_PROTOCOL;
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  ngtcp2_conn_extend_max_offset(conn, length);
  return 0;
}

static bool read_quic_varint(const uint8_t *data, size_t length, uint64_t *value, size_t *used) {
  if (!length)
    return false;
  size_t width = (size_t)1 << (data[0] >> 6);
  if (length < width)
    return false;
  uint64_t result = data[0] & 0x3f;
  for (size_t i = 1; i < width; ++i)
    result = (result << 8) | data[i];
  *value = result;
  *used = width;
  return true;
}

static size_t write_quic_varint(uint8_t *out, uint64_t value) {
  size_t width = value < (1ULL << 6) ? 1 : value < (1ULL << 14) ? 2 : value < (1ULL << 30) ? 4 : 8;
  for (size_t i = 0; i < width; ++i)
    out[width - i - 1] = (uint8_t)(value >> (8 * i));
  out[0] |= width == 1 ? 0 : width == 2 ? 0x40 : width == 4 ? 0x80 : 0xc0;
  return width;
}

static int receive_http3(ngtcp2_conn *conn, acip_quic_connection_t *connection, uint32_t flags, int64_t stream_id,
                         const uint8_t *data, size_t length) {
  size_t consumed = 0;
  asciichat_error_t result =
      acip_http3_server_read(connection->http3, stream_id, data, length, (flags & NGTCP2_STREAM_DATA_FLAG_FIN) != 0,
                             connection->last_time_ns, &consumed);
  if (result != ASCIICHAT_OK || ngtcp2_conn_extend_max_stream_offset(conn, stream_id, consumed) != 0) {
    connection->callback_error = result != ASCIICHAT_OK ? result : ERROR_NETWORK_PROTOCOL;
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  ngtcp2_conn_extend_max_offset(conn, consumed);
  int64_t session_id = -1;
  if ((flags & NGTCP2_STREAM_DATA_FLAG_FIN) && acip_http3_server_accepted(connection->http3, &session_id) &&
      stream_id == session_id) {
    connection->callback_error = ERROR_NETWORK;
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  return 0;
}

static int receive_webtransport_uni(ngtcp2_conn *conn, acip_quic_connection_t *connection, uint32_t flags,
                                    int64_t stream_id, uint64_t offset, const uint8_t *data, size_t length) {
  http3_receive_stream_t *stream = NULL;
  for (size_t i = 0; i < ACIP_HTTP3_PEER_UNI_STREAMS; ++i) {
    if (connection->http3_receives[i].active && connection->http3_receives[i].id == stream_id) {
      stream = &connection->http3_receives[i];
      break;
    }
  }
  if (!stream) {
    for (size_t i = 0; i < ACIP_HTTP3_PEER_UNI_STREAMS; ++i) {
      if (!connection->http3_receives[i].active) {
        stream = &connection->http3_receives[i];
        stream->active = true;
        stream->id = stream_id;
        break;
      }
    }
  }
  if (!stream || offset != stream->offset || length > UINT64_MAX - offset)
    goto invalid;
  stream->offset += length;
  size_t input = 0;
  while (!stream->kind && input < length) {
    if (stream->prefix_used == sizeof(stream->prefix))
      goto invalid;
    stream->prefix[stream->prefix_used++] = data[input++];
    uint64_t type = 0, session = 0;
    size_t type_length = 0, session_length = 0;
    if (!read_quic_varint(stream->prefix, stream->prefix_used, &type, &type_length))
      continue;
    if (type != 0x54) {
      stream->kind = 1;
      break;
    }
    if (!read_quic_varint(stream->prefix + type_length, stream->prefix_used - type_length, &session, &session_length))
      continue;
    int64_t accepted = -1;
    if (!acip_http3_server_accepted(connection->http3, &accepted) || session != (uint64_t)accepted)
      goto invalid;
    stream->kind = 2;
    stream->prefix_length = type_length + session_length;
    if (ngtcp2_conn_extend_max_stream_offset(conn, stream_id, stream->prefix_length) != 0)
      goto invalid;
    ngtcp2_conn_extend_max_offset(conn, stream->prefix_length);
  }
  if (!stream->kind) {
    if (flags & NGTCP2_STREAM_DATA_FLAG_FIN)
      goto invalid;
    return 0;
  }
  if (stream->kind == 1) {
    if (stream->prefix_used) {
      uint32_t prefix_flags = input == length ? flags : flags & ~NGTCP2_STREAM_DATA_FLAG_FIN;
      if (receive_http3(conn, connection, prefix_flags, stream_id, stream->prefix, stream->prefix_used) != 0)
        return NGTCP2_ERR_CALLBACK_FAILURE;
      stream->prefix_used = 0;
    }
    if (input < length)
      return receive_http3(conn, connection, flags, stream_id, data + input, length - input);
    if (!length && (flags & NGTCP2_STREAM_DATA_FLAG_FIN))
      return receive_http3(conn, connection, flags, stream_id, NULL, 0);
    return 0;
  }
  return receive_acip_stream(conn, connection, flags, stream_id, offset + input - stream->prefix_length,
                             data ? data + input : NULL, length - input);
invalid:
  connection->callback_error = ERROR_NETWORK_PROTOCOL;
  return NGTCP2_ERR_CALLBACK_FAILURE;
}

static int receive_stream_data(ngtcp2_conn *conn, uint32_t flags, int64_t stream_id, uint64_t offset,
                               const uint8_t *data, size_t length, void *user_data, void *stream_user_data) {
  (void)stream_user_data;
  acip_quic_connection_t *connection = user_data;
  asciichat_error_t readiness = update_readiness(connection);
  if (connection->tls->webtransport) {
    if ((flags & NGTCP2_STREAM_DATA_FLAG_0RTT) || ngtcp2_conn_is_local_stream2(conn, stream_id) ||
        readiness != ASCIICHAT_OK || !connection->tls_ready || !connection->http3) {
      connection->callback_error = readiness != ASCIICHAT_OK ? readiness : ERROR_NETWORK_PROTOCOL;
      return NGTCP2_ERR_CALLBACK_FAILURE;
    }
    if (ngtcp2_is_bidi_stream(stream_id))
      return receive_http3(conn, connection, flags, stream_id, data, length);
    return receive_webtransport_uni(conn, connection, flags, stream_id, offset, data, length);
  }
  if ((flags & NGTCP2_STREAM_DATA_FLAG_0RTT) || ngtcp2_is_bidi_stream(stream_id) ||
      ngtcp2_conn_is_local_stream2(conn, stream_id) || readiness != ASCIICHAT_OK || !connection->tls_ready) {
    connection->callback_error = readiness != ASCIICHAT_OK ? readiness : ERROR_NETWORK_PROTOCOL;
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
  return receive_acip_stream(conn, connection, flags, stream_id, offset, data, length);
}

static int reset_stream(ngtcp2_conn *conn, int64_t stream_id, uint64_t final_size, uint64_t app_error_code,
                        void *user_data, void *stream_user_data) {
  (void)conn;
  (void)stream_id;
  (void)final_size;
  (void)app_error_code;
  (void)stream_user_data;
  ((acip_quic_connection_t *)user_data)->callback_error = ERROR_NETWORK_PROTOCOL;
  return NGTCP2_ERR_CALLBACK_FAILURE;
}

static int receive_stop_sending(ngtcp2_conn *conn, int64_t stream_id, uint64_t app_error_code, void *user_data,
                                void *stream_user_data) {
  (void)conn;
  (void)stream_id;
  (void)app_error_code;
  (void)stream_user_data;
  ((acip_quic_connection_t *)user_data)->callback_error = ERROR_NETWORK_PROTOCOL;
  return NGTCP2_ERR_CALLBACK_FAILURE;
}

static int receive_datagram(ngtcp2_conn *conn, uint32_t flags, const uint8_t *data, size_t length, void *user_data) {
  (void)conn;
  acip_quic_connection_t *connection = user_data;
  if ((flags & NGTCP2_DATAGRAM_FLAG_0RTT) || !connection->datagrams_enabled || !connection->security.crypto ||
      !connection->peer_ready_seen || !connection->emit)
    return 0;
  if (connection->tls->webtransport) {
    int64_t session_id = -1;
    uint64_t quarter = 0;
    size_t prefix_length = 0;
    if (!acip_http3_server_accepted(connection->http3, &session_id) ||
        !read_quic_varint(data, length, &quarter, &prefix_length) || quarter != (uint64_t)session_id / 4)
      return 0;
    data += prefix_length;
    length -= prefix_length;
  }
  uint8_t *wire = NULL, *plain = NULL;
  size_t wire_length = 0, plain_length = 0;
  uint64_t sequence = 0;
  acip_traffic_class_t flow = ACIP_TRAFFIC_CONTROL;
  asciichat_error_t result = acip_quic_datagram_read(&connection->datagram_reader, data, length,
                                                     connection->last_time_ns, &flow, &wire, &wire_length);
  if (result == ASCIICHAT_OK && wire)
    result = acip_quic_security_open_sequence(&connection->security, flow, ACIP_DELIVERY_DATAGRAM, wire, wire_length,
                                              &plain, &plain_length, &sequence);
  if (result == ASCIICHAT_OK && plain && flow == ACIP_TRAFFIC_VISUAL) {
    if (sequence <= connection->last_visual_sequence)
      goto done;
    bool gap = connection->last_visual_sequence && sequence != connection->last_visual_sequence + 1;
    connection->last_visual_sequence = sequence;
    packet_header_t header;
    memcpy(&header, plain, sizeof(header));
    uint16_t type = NET_TO_HOST_U16(header.type);
    uint32_t id = NET_TO_HOST_U32(header.client_id);
    bool encoded = type == PACKET_TYPE_IMAGE_FRAME_H264 || type == PACKET_TYPE_IMAGE_FRAME_H265;
    if (encoded) {
      bool keyframe = plain_length > sizeof(header) && (plain[sizeof(header)] & 1) != 0;
      if (gap || connection->visual_codec != type)
        connection->visual_waiting_keyframe = true;
      connection->visual_codec = type;
      if (keyframe) {
        connection->visual_waiting_keyframe = false;
        connection->visual_keyframe_requested = false;
      }
      if (connection->visual_waiting_keyframe) {
        if (!connection->visual_keyframe_requested && connection->peer_security_ready && id == 0) {
          asciichat_errno_scope_t scope = errno_scope_begin();
          if (queue_keyframe_request(connection, type, id) == ASCIICHAT_OK)
            connection->visual_keyframe_requested = true;
          errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
        }
        goto done;
      }
      if (connection->emit(flow, plain, plain_length, connection->emit_context) != ASCIICHAT_OK) {
        connection->visual_waiting_keyframe = true;
        connection->visual_keyframe_requested = false;
      }
    } else {
      connection->visual_codec = 0;
      connection->visual_waiting_keyframe = false;
      connection->visual_keyframe_requested = false;
      (void)connection->emit(flow, plain, plain_length, connection->emit_context);
    }
  } else if (result == ASCIICHAT_OK && plain) {
    (void)connection->emit(flow, plain, plain_length, connection->emit_context);
  }
done:
  buffer_release(plain);
  buffer_release(wire);
  return 0;
}

static ngtcp2_conn *get_connection(ngtcp2_crypto_conn_ref *reference) {
  return ((acip_quic_connection_t *)reference->user_data)->conn;
}

static void random_bytes(uint8_t *output, size_t length, const ngtcp2_rand_ctx *context) {
  (void)context;
  randombytes_buf(output, length);
}

static int new_connection_id(ngtcp2_conn *conn, ngtcp2_cid *cid, ngtcp2_stateless_reset_token *token, size_t length,
                             void *user_data) {
  (void)conn;
  (void)user_data;
  if (length > sizeof(cid->data))
    return NGTCP2_ERR_CALLBACK_FAILURE;
  randombytes_buf(cid->data, length);
  cid->datalen = length;
  randombytes_buf(token->data, sizeof(token->data));
  return 0;
}

static int path_validation(ngtcp2_conn *conn, uint32_t flags, const ngtcp2_path *path, const ngtcp2_path *fallback_path,
                           ngtcp2_path_validation_result result, void *user_data) {
  (void)conn;
  (void)flags;
  (void)fallback_path;
  (void)user_data;
  if (result == NGTCP2_PATH_VALIDATION_RESULT_SUCCESS && path && path->remote.addr) {
    unsigned port = 0;
    if (path->remote.addr->sa_family == AF_INET && path->remote.addrlen >= sizeof(struct sockaddr_in))
      port = NET_TO_HOST_U16(((const struct sockaddr_in *)path->remote.addr)->sin_port);
    else if (path->remote.addr->sa_family == AF_INET6 && path->remote.addrlen >= sizeof(struct sockaddr_in6))
      port = NET_TO_HOST_U16(((const struct sockaddr_in6 *)path->remote.addr)->sin6_port);
    log_info("QUIC peer path validated: port=%u", port);
  }
  return 0;
}

static ngtcp2_callbacks connection_callbacks(bool server) {
  ngtcp2_callbacks callbacks = {0};
  callbacks.client_initial = server ? NULL : ngtcp2_crypto_client_initial_cb;
  callbacks.recv_client_initial = server ? ngtcp2_crypto_recv_client_initial_cb : NULL;
  callbacks.recv_crypto_data = ngtcp2_crypto_recv_crypto_data_cb;
  callbacks.encrypt = ngtcp2_crypto_encrypt_cb;
  callbacks.decrypt = ngtcp2_crypto_decrypt_cb;
  callbacks.hp_mask = ngtcp2_crypto_hp_mask_cb;
  callbacks.recv_stream_data = receive_stream_data;
  callbacks.acked_stream_data_offset = acknowledged_stream_data;
  callbacks.stream_reset = reset_stream;
  callbacks.recv_stop_sending = receive_stop_sending;
  callbacks.recv_datagram = receive_datagram;
  callbacks.recv_retry = server ? NULL : ngtcp2_crypto_recv_retry_cb;
  callbacks.rand = random_bytes;
  callbacks.update_key = ngtcp2_crypto_update_key_cb;
  callbacks.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
  callbacks.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
  callbacks.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
  callbacks.get_new_connection_id2 = new_connection_id;
  callbacks.get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb;
  callbacks.path_validation = path_validation;
  return callbacks;
}

static bool valid_address(const ngtcp2_addr *address) {
  if (!address || !address->addr)
    return false;
  size_t length = (size_t)address->addrlen;
  if (length < sizeof(struct sockaddr) || length > sizeof(struct sockaddr_storage))
    return false;
  if (address->addr->sa_family == AF_INET)
    return length >= sizeof(struct sockaddr_in);
  if (address->addr->sa_family == AF_INET6)
    return length >= sizeof(struct sockaddr_in6);
  return false;
}

static bool valid_path(const ngtcp2_path *path) {
  return path && valid_address(&path->local) && valid_address(&path->remote);
}

static asciichat_error_t connection_create(acip_quic_tls_t *tls, const char *server_name, const ngtcp2_path *path,
                                           const ngtcp2_pkt_hd *initial, const ngtcp2_cid *original_dcid,
                                           uint64_t now_ns, acip_quic_connection_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC connection output");
  *out = NULL;
  if (!tls || !valid_path(path) || now_ns == UINT64_MAX || (tls->server != (initial != NULL)) ||
      (!tls->server && (!server_name || !*server_name)))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC connection configuration");
  if (sodium_init() < 0)
    return SET_ERRNO(ERROR_CRYPTO, "Cannot initialize QUIC random source");

  acip_quic_connection_t *connection = SAFE_CALLOC(1, sizeof(*connection), acip_quic_connection_t *);
  if (!connection)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC connection");
  connection->tls = tls;
  connection->datagrams_enabled = tls->enable_datagrams;
  if (initial) {
    connection->initial_dcid = initial->dcid;
    connection->has_initial_dcid = true;
  }
  connection->last_time_ns = now_ns;
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i)
    connection->sends[i].id = -1;
  connection->reference.get_conn = get_connection;
  connection->reference.user_data = connection;

  ngtcp2_cid dcid = {0}, scid = {0};
  scid.datalen = ACIP_QUIC_CONNECTION_ID_LENGTH;
  randombytes_buf(scid.data, scid.datalen);
  if (initial)
    dcid = initial->scid;
  else {
    dcid.datalen = ACIP_QUIC_CONNECTION_ID_LENGTH;
    randombytes_buf(dcid.data, dcid.datalen);
  }
  ngtcp2_settings settings;
  ngtcp2_settings_default(&settings);
  settings.initial_ts = now_ns;
  if (original_dcid) {
    settings.token = initial->token;
    settings.tokenlen = initial->tokenlen;
    settings.token_type = NGTCP2_TOKEN_TYPE_RETRY;
  }
  ngtcp2_transport_params params;
  ngtcp2_transport_params_default(&params);
  params.initial_max_streams_uni = tls->webtransport ? 6 : 3;
  params.initial_max_streams_bidi = tls->webtransport ? 1 : 0;
  params.initial_max_stream_data_bidi_local = tls->webtransport ? ACIP_QUIC_INITIAL_STREAM_WINDOW : 0;
  params.initial_max_stream_data_bidi_remote = tls->webtransport ? ACIP_QUIC_INITIAL_STREAM_WINDOW : 0;
  params.initial_max_stream_data_uni = ACIP_QUIC_INITIAL_STREAM_WINDOW;
  params.initial_max_data = ACIP_QUIC_INITIAL_CONNECTION_WINDOW;
  params.max_idle_timeout = ACIP_QUIC_IDLE_TIMEOUT_NS;
  if (connection->datagrams_enabled)
    params.max_datagram_frame_size = 65535;
  if (initial) {
    params.original_dcid = original_dcid ? *original_dcid : initial->dcid;
    params.original_dcid_present = 1;
    if (original_dcid) {
      params.retry_scid = initial->dcid;
      params.retry_scid_present = 1;
    }
  }
  ngtcp2_callbacks callbacks = connection_callbacks(tls->server);
  int rc = initial ? ngtcp2_conn_server_new(&connection->conn, &dcid, &scid, path, initial->version, &callbacks,
                                            &settings, &params, NULL, connection)
                   : ngtcp2_conn_client_new(&connection->conn, &dcid, &scid, path, NGTCP2_PROTO_VER_V1, &callbacks,
                                            &settings, &params, NULL, connection);
  if (rc != 0) {
    asciichat_error_t result = SET_ERRNO(ERROR_NETWORK, "Cannot create QUIC connection: %s", ngtcp2_strerror(rc));
    acip_quic_connection_destroy(&connection);
    return result;
  }
  asciichat_error_t result = acip_quic_tls_session_create(tls, server_name, &connection->reference, &connection->ssl);
  if (result != ASCIICHAT_OK) {
    acip_quic_connection_destroy(&connection);
    return result;
  }
  rc = ngtcp2_crypto_ossl_ctx_new(&connection->crypto, connection->ssl);
  if (rc != 0) {
    result = SET_ERRNO(ERROR_CRYPTO, "Cannot create QUIC OpenSSL connection context: %d", rc);
    acip_quic_connection_destroy(&connection);
    return result;
  }
  ngtcp2_conn_set_tls_native_handle(connection->conn, connection->crypto);
  *out = connection;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_client_create(acip_quic_tls_t *tls, const char *server_name,
                                                     const ngtcp2_path *path, uint64_t now_ns,
                                                     acip_quic_connection_t **out) {
  return connection_create(tls, server_name, path, NULL, NULL, now_ns, out);
}

asciichat_error_t acip_quic_connection_server_create(acip_quic_tls_t *tls, const ngtcp2_path *path, const void *initial,
                                                     size_t initial_length, uint64_t now_ns,
                                                     acip_quic_connection_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC connection output");
  *out = NULL;
  if (!initial || !initial_length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC Initial packet");
  ngtcp2_pkt_hd header;
  int rc = ngtcp2_accept(&header, initial, initial_length);
  if (rc != 0 || header.version != NGTCP2_PROTO_VER_V1)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid or unsupported QUIC Initial packet");
  return connection_create(tls, NULL, path, &header, NULL, now_ns, out);
}

asciichat_error_t acip_quic_connection_server_create_validated(acip_quic_tls_t *tls, const ngtcp2_path *path,
                                                               const void *initial, size_t initial_length,
                                                               const ngtcp2_cid *original_dcid, uint64_t now_ns,
                                                               acip_quic_connection_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC connection output");
  *out = NULL;
  if (!initial || !initial_length || !original_dcid || !original_dcid->datalen ||
      original_dcid->datalen > NGTCP2_MAX_CIDLEN)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing validated QUIC Initial parameters");
  ngtcp2_pkt_hd header;
  int rc = ngtcp2_accept(&header, initial, initial_length);
  if (rc != 0 || header.version != NGTCP2_PROTO_VER_V1 || !header.tokenlen)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid validated QUIC Initial packet");
  return connection_create(tls, NULL, path, &header, original_dcid, now_ns, out);
}

static asciichat_error_t update_readiness(acip_quic_connection_t *connection) {
  if (!connection->tls_ready && ngtcp2_conn_get_handshake_completed2(connection->conn)) {
    asciichat_error_t result = acip_quic_tls_verify_ready(connection->tls, connection->ssl);
    if (result != ASCIICHAT_OK) {
      connection->failed = true;
      return result;
    }
    connection->tls_ready = true;
  }
  if (connection->tls_ready && connection->tls->webtransport && !connection->http3) {
    int64_t control, encoder, decoder;
    int rc = ngtcp2_conn_open_uni_stream(connection->conn, &control, NULL);
    if (!rc)
      rc = ngtcp2_conn_open_uni_stream(connection->conn, &encoder, NULL);
    if (!rc)
      rc = ngtcp2_conn_open_uni_stream(connection->conn, &decoder, NULL);
    if (rc) {
      connection->failed = true;
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot open HTTP/3 control streams: %s", ngtcp2_strerror(rc));
    }
    asciichat_error_t result =
        acip_http3_server_create(connection->tls->webtransport_origin, connection->tls->webtransport_path, control,
                                 encoder, decoder, &connection->http3);
    if (result != ASCIICHAT_OK) {
      connection->failed = true;
      return result;
    }
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t update_time(acip_quic_connection_t *connection, uint64_t now_ns) {
  if (now_ns < connection->last_time_ns || now_ns == UINT64_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC monotonic time moved backwards or is invalid");
  connection->last_time_ns = now_ns;
  acip_quic_datagram_expire(&connection->datagram_reader, now_ns);
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i) {
    datagram_send_t **link = &connection->datagram_head[i], *tail = NULL;
    while (*link) {
      datagram_send_t *item = *link;
      if (item->deadline_ns && item->deadline_ns <= now_ns) {
        *link = item->next;
        connection->pending_bytes -= item->cost;
        buffer_release(item->data);
        SAFE_FREE(item);
      } else {
        tail = item;
        link = &item->next;
      }
    }
    connection->datagram_tail[i] = tail;
  }
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_matches_cid(const acip_quic_connection_t *connection, const uint8_t *cid,
                                                   size_t length, bool *matches) {
  if (!connection || !connection->conn || !cid || !matches)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC connection ID lookup");
  *matches = false;
  if (!length || length > NGTCP2_MAX_CIDLEN)
    return ASCIICHAT_OK;
  if (connection->has_initial_dcid && length == connection->initial_dcid.datalen &&
      memcmp(cid, connection->initial_dcid.data, length) == 0) {
    *matches = true;
    return ASCIICHAT_OK;
  }
  size_t count = ngtcp2_conn_get_scid2(connection->conn, NULL);
  if (!count)
    return ASCIICHAT_OK;
  if (count > SIZE_MAX / sizeof(ngtcp2_cid))
    return SET_ERRNO(ERROR_MEMORY, "QUIC connection ID count overflow");
  ngtcp2_cid *ids = SAFE_CALLOC(count, sizeof(*ids), ngtcp2_cid *);
  if (!ids)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC connection ID list");
  size_t actual = ngtcp2_conn_get_scid2(connection->conn, ids);
  for (size_t i = 0; i < actual; ++i) {
    if (ids[i].datalen == length && memcmp(ids[i].data, cid, length) == 0) {
      *matches = true;
      break;
    }
  }
  SAFE_FREE(ids);
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_set_receiver(acip_quic_connection_t *connection, acip_quic_stream_emit_fn emit,
                                                    void *context) {
  if (!connection || !connection->conn || connection->failed || !emit || connection->emit)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC receive callback");
  connection->emit = emit;
  connection->emit_context = context;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_clear_receiver(acip_quic_connection_t *connection, void *context) {
  if (!connection || !connection->emit || connection->emit_context != context)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC receive callback context");
  connection->emit = NULL;
  connection->emit_context = NULL;
  connection->security_notify = NULL;
  connection->security_notify_context = NULL;
  connection->keyframe_notify = NULL;
  connection->keyframe_notify_context = NULL;
  return ASCIICHAT_OK;
}

void acip_quic_connection_set_keyframe_notifier(acip_quic_connection_t *connection,
                                                void (*notify)(uint16_t, uint32_t, void *), void *context) {
  if (connection) {
    connection->keyframe_notify = notify;
    connection->keyframe_notify_context = notify ? context : NULL;
  }
}

void acip_quic_connection_set_security_notifier(acip_quic_connection_t *connection, void (*notify)(void *),
                                                void *context) {
  if (connection) {
    connection->security_notify = notify;
    connection->security_notify_context = notify ? context : NULL;
  }
}

asciichat_error_t acip_quic_connection_allow_media(acip_quic_connection_t *connection) {
  if (!connection || !connection->conn || connection->failed || !connection->tls_ready ||
      !connection->security.crypto || !connection->peer_security_ready)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC and authenticated ACIP encryption are not ready for media");
  connection->media_allowed = true;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_enable_security(acip_quic_connection_t *connection, crypto_context_t *crypto) {
  if (!connection || !connection->conn || connection->failed || !connection->tls_ready || !connection->emit)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC is not ready for the ACIP security transition");
  asciichat_error_t result = acip_quic_security_init(&connection->security, crypto, connection->tls->server);
  if (result != ASCIICHAT_OK)
    return result;
  result = queue_security_barrier(connection, quic_ready_payload, sizeof(quic_ready_payload));
  if (result == ASCIICHAT_OK && connection->early_security_packet)
    result = open_security_packet(connection, ACIP_TRAFFIC_CONTROL, connection->early_security_packet,
                                  connection->early_security_length);
  buffer_release(connection->early_security_packet);
  connection->early_security_packet = NULL;
  connection->early_security_length = 0;
  if (result != ASCIICHAT_OK)
    connection->failed = true;
  return result;
}

bool acip_quic_connection_security_ready(const acip_quic_connection_t *connection) {
  return connection && connection->security.crypto && connection->peer_security_ready && !connection->failed;
}

size_t acip_quic_connection_datagram_payload(const acip_quic_connection_t *connection) {
  if (!connection || !connection->conn || connection->failed || !connection->tls_ready ||
      !connection->datagrams_enabled)
    return 0;
  const ngtcp2_transport_params *remote = ngtcp2_conn_get_remote_transport_params2(connection->conn);
  if (!remote || remote->max_datagram_frame_size <= ACIP_QUIC_DATAGRAM_HEADER_SIZE + 9)
    return 0;
  size_t path = ngtcp2_conn_get_path_max_tx_udp_payload_size2(connection->conn);
  if (path <= ACIP_QUIC_DATAGRAM_OVERHEAD + ACIP_QUIC_DATAGRAM_HEADER_SIZE)
    return 0;
  size_t frame = path - ACIP_QUIC_DATAGRAM_OVERHEAD;
  if (frame > ACIP_QUIC_DATAGRAM_FRAME_LIMIT)
    frame = ACIP_QUIC_DATAGRAM_FRAME_LIMIT;
  if (frame > remote->max_datagram_frame_size - 9)
    frame = (size_t)(remote->max_datagram_frame_size - 9);
  if (connection->tls->webtransport) {
    int64_t session_id = -1;
    uint8_t prefix[8];
    if (!connection->http3 || !acip_http3_server_accepted(connection->http3, &session_id))
      return 0;
    size_t size = write_quic_varint(prefix, (uint64_t)session_id / 4);
    if (frame <= size)
      return 0;
    frame -= size;
  }
  return frame > ACIP_QUIC_DATAGRAM_HEADER_SIZE ? frame - ACIP_QUIC_DATAGRAM_HEADER_SIZE : 0;
}

asciichat_error_t acip_quic_connection_validate_datagram(const acip_quic_connection_t *connection,
                                                         acip_traffic_class_t flow, uint64_t deadline_ns) {
  if (!connection || (flow != ACIP_TRAFFIC_AUDIO && flow != ACIP_TRAFFIC_VISUAL))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC datagram flow");
  if (!acip_quic_connection_datagram_payload(connection))
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "QUIC datagrams were not negotiated");
  if (!connection->security.crypto || !connection->peer_security_ready || !connection->media_allowed)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC media security is not ready");
  if (deadline_ns && deadline_ns <= connection->last_time_ns)
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "QUIC datagram deadline expired");
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_submit_datagram(acip_quic_connection_t *connection, acip_traffic_class_t flow,
                                                       const void *packet, size_t length, uint64_t deadline_ns) {
  asciichat_error_t validation = acip_quic_connection_validate_datagram(connection, flow, deadline_ns);
  if (validation != ASCIICHAT_OK)
    return validation;
  if (flow == ACIP_TRAFFIC_VISUAL && packet && length >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, packet, sizeof(header));
    uint16_t type = NET_TO_HOST_U16(header.type);
    if ((type == PACKET_TYPE_IMAGE_FRAME_H264 || type == PACKET_TYPE_IMAGE_FRAME_H265) && header.client_id != 0)
      return SET_ERRNO(ERROR_NOT_SUPPORTED, "QUIC encoded datagrams require stream ID zero");
  }
  size_t unit = acip_quic_connection_datagram_payload(connection);
  if (connection->datagram_id[flow] == UINT64_MAX)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "QUIC datagram ID exhausted");
  size_t reserve = flow == ACIP_TRAFFIC_VISUAL ? ACIP_OUTBOUND_CONTROL_RESERVE + ACIP_OUTBOUND_AUDIO_RESERVE
                                               : ACIP_OUTBOUND_CONTROL_RESERVE;
  size_t limit = ACIP_OUTBOUND_BUDGET - reserve;
  if (connection->pending_bytes > limit || length > limit - connection->pending_bytes)
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC datagram queue is full");
  uint8_t *wire = NULL;
  size_t wire_length = 0;
  asciichat_error_t result =
      acip_quic_security_seal(&connection->security, flow, ACIP_DELIVERY_DATAGRAM, packet, length, &wire, &wire_length);
  if (result != ASCIICHAT_OK)
    return result;
  size_t count = (wire_length + unit - 1) / unit;
  size_t cost = buffer_capacity(wire) + sizeof(datagram_send_t);
  if (count > ACIP_QUIC_DATAGRAM_MAX_FRAGMENTS || cost > limit - connection->pending_bytes) {
    buffer_release(wire);
    return SET_ERRNO(count > ACIP_QUIC_DATAGRAM_MAX_FRAGMENTS ? ERROR_NETWORK_SIZE : ERROR_BUFFER_FULL,
                     "QUIC datagram exceeds fragmentation or queue limit");
  }
  datagram_send_t *item = SAFE_CALLOC(1, sizeof(*item), datagram_send_t *);
  if (!item) {
    buffer_release(wire);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC datagram queue entry");
  }
  item->data = wire;
  item->length = wire_length;
  item->cost = cost;
  item->id = ++connection->datagram_id[flow];
  item->deadline_ns = deadline_ns;
  item->unit = (uint16_t)unit;
  item->count = (uint16_t)count;
  if (connection->datagram_tail[flow])
    connection->datagram_tail[flow]->next = item;
  else
    connection->datagram_head[flow] = item;
  connection->datagram_tail[flow] = item;
  connection->pending_bytes += cost;
  return ASCIICHAT_OK;
}

static void pop_datagram(acip_quic_connection_t *connection, acip_traffic_class_t flow) {
  datagram_send_t *item = connection->datagram_head[flow];
  connection->datagram_head[flow] = item->next;
  if (!item->next)
    connection->datagram_tail[flow] = NULL;
  connection->pending_bytes -= item->cost;
  buffer_release(item->data);
  SAFE_FREE(item);
}

static asciichat_error_t queue_wire_packet(acip_quic_connection_t *connection, acip_traffic_class_t traffic_class,
                                           const void *packet, size_t length) {
  if (!connection || !connection->conn || connection->failed || (unsigned)traffic_class >= ACIP_QUIC_FLOW_COUNT ||
      !packet || length < sizeof(packet_header_t) || length - sizeof(packet_header_t) > MAX_PACKET_SIZE)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC outbound packet");
  if (!connection->tls_ready || (traffic_class != ACIP_TRAFFIC_CONTROL && !connection->media_allowed))
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC connection is not ready for this traffic");
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  uint16_t type = NET_TO_HOST_U16(header.type);
  if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC || NET_TO_HOST_U32(header.length) != length - sizeof(header) ||
      (type != PACKET_TYPE_ENCRYPTED && acip_packet_traffic_class(type) != traffic_class))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid QUIC outbound ACIP packet or traffic class");

  send_stream_t *stream = &connection->sends[traffic_class];
  if (stream->fin_requested)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC traffic stream is closing");
  uint8_t preamble[ACIP_QUIC_STREAM_PREAMBLE_SIZE + 16];
  size_t prefix_length = 0;
  if (!stream->initialized) {
    if (connection->tls->webtransport) {
      int64_t session_id = -1;
      if (!acip_http3_server_accepted(connection->http3, &session_id))
        return SET_ERRNO(ERROR_INVALID_STATE, "WebTransport session is not established");
      prefix_length += write_quic_varint(preamble + prefix_length, 0x54);
      prefix_length += write_quic_varint(preamble + prefix_length, (uint64_t)session_id);
    }
    asciichat_error_t preamble_result = acip_quic_stream_preamble(traffic_class, preamble + prefix_length);
    if (preamble_result != ASCIICHAT_OK)
      return preamble_result;
    prefix_length += ACIP_QUIC_STREAM_PREAMBLE_SIZE;
  }
  if (stream->queued_offset > NGTCP2_MAX_VARINT - length - prefix_length)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "QUIC stream offset exhausted");
  size_t reserve = traffic_class == ACIP_TRAFFIC_VISUAL  ? ACIP_OUTBOUND_CONTROL_RESERVE + ACIP_OUTBOUND_AUDIO_RESERVE
                   : traffic_class == ACIP_TRAFFIC_AUDIO ? ACIP_OUTBOUND_CONTROL_RESERVE
                                                         : 0;
  size_t limit = ACIP_OUTBOUND_BUDGET - reserve;
  if (connection->pending_bytes > limit || length > limit - connection->pending_bytes)
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC outbound queue is full");

  send_chunk_t *prefix = NULL;
  if (prefix_length) {
    prefix = copy_chunk(preamble, prefix_length);
    if (!prefix)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC stream preamble");
  }
  send_chunk_t *chunk = copy_chunk(packet, length);
  if (!chunk) {
    free_unqueued_chunk(prefix);
    return SET_ERRNO(ERROR_MEMORY, "Cannot copy QUIC outbound packet");
  }
  size_t prefix_cost = prefix ? prefix->cost : 0;
  if (prefix_cost > limit - connection->pending_bytes ||
      chunk->cost > limit - connection->pending_bytes - prefix_cost) {
    free_unqueued_chunk(prefix);
    free_unqueued_chunk(chunk);
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC outbound queue is full");
  }
  if (prefix)
    append_chunk(connection, stream, prefix);
  append_chunk(connection, stream, chunk);
  stream->initialized = true;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_submit(acip_quic_connection_t *connection, acip_traffic_class_t traffic_class,
                                              const void *packet, size_t length) {
  if (connection && !connection->security.crypto && packet && length >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, packet, sizeof(header));
    packet_type_t type = NET_TO_HOST_U16(header.type);
    if (traffic_class == ACIP_TRAFFIC_CONTROL && !initial_handshake_packet(type))
      return SET_ERRNO(ERROR_CRYPTO, "Non-handshake ACIP packet sent before authentication");
  }
  if (!connection || !connection->security.crypto)
    return queue_wire_packet(connection, traffic_class, packet, length);
  if (!connection->peer_security_ready)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC peer has not acknowledged ACIP security readiness");
  if (packet && length >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, packet, sizeof(header));
    if (NET_TO_HOST_U16(header.type) == PACKET_TYPE_QUIC_READY ||
        NET_TO_HOST_U16(header.type) == PACKET_TYPE_QUIC_KEYFRAME_REQUEST)
      return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC readiness marker is reserved");
  }
  if (!connection->conn || connection->failed || !connection->tls_ready ||
      (unsigned)traffic_class >= ACIP_QUIC_FLOW_COUNT ||
      (traffic_class != ACIP_TRAFFIC_CONTROL && !connection->media_allowed))
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC connection is not ready for this traffic");
  if (connection->sends[traffic_class].fin_requested)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC traffic stream is closing");
  uint8_t *wire = NULL;
  size_t wire_length = 0;
  asciichat_error_t result = acip_quic_security_seal(&connection->security, traffic_class, ACIP_DELIVERY_RELIABLE,
                                                     packet, length, &wire, &wire_length);
  if (result == ASCIICHAT_OK)
    result = queue_wire_packet(connection, traffic_class, wire, wire_length);
  buffer_release(wire);
  return result;
}

size_t acip_quic_connection_pending_bytes(const acip_quic_connection_t *connection) {
  return connection ? connection->pending_bytes : 0;
}

asciichat_error_t acip_quic_connection_finish_stream(acip_quic_connection_t *connection,
                                                     acip_traffic_class_t traffic_class) {
  if (!connection || !connection->conn || connection->failed || (unsigned)traffic_class >= ACIP_QUIC_FLOW_COUNT)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC traffic stream");
  send_stream_t *stream = &connection->sends[traffic_class];
  if (!stream->initialized || stream->fin_requested)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC traffic stream is not open or is already closing");
  stream->fin_requested = true;
  return ASCIICHAT_OK;
}

bool acip_quic_connection_received_fin(const acip_quic_connection_t *connection, acip_traffic_class_t traffic_class) {
  if (!connection || (unsigned)traffic_class >= ACIP_QUIC_FLOW_COUNT)
    return false;
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i)
    if (connection->receives[i].registered && connection->receives[i].reader.traffic_class == traffic_class)
      return connection->receives[i].reader.finished;
  return false;
}

asciichat_error_t acip_quic_connection_receive(acip_quic_connection_t *connection, const ngtcp2_path *path,
                                               const void *packet, size_t length, uint64_t now_ns) {
  if (!connection || !connection->conn || connection->failed || !valid_path(path) || !packet || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC packet input");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  ngtcp2_pkt_info info = {0};
  connection->callback_error = ASCIICHAT_OK;
  int rc = ngtcp2_conn_read_pkt(connection->conn, path, &info, packet, length, now_ns);
  if (rc != 0) {
    connection->failed = true;
    asciichat_error_t code = connection->callback_error != ASCIICHAT_OK ? connection->callback_error
                             : rc == NGTCP2_ERR_CRYPTO                  ? ERROR_CRYPTO
                             : rc == NGTCP2_ERR_DRAINING                ? ERROR_NETWORK
                                                                        : ERROR_NETWORK_PROTOCOL;
    return SET_ERRNO(code, "QUIC receive failed: %s", ngtcp2_strerror(rc));
  }
  return update_readiness(connection);
}

static ngtcp2_ssize write_pending(acip_quic_connection_t *connection, ngtcp2_path *path, ngtcp2_pkt_info *info,
                                  uint8_t *packet, size_t capacity, uint64_t now_ns) {
  connection->packet_has_stream_data = false;
  if (connection->http3) {
    int64_t stream_id = -1;
    bool fin = false;
    nghttp3_vec h3vec[8];
    size_t count = 0;
    if (acip_http3_server_write(connection->http3, &stream_id, &fin, h3vec, 8, &count) != ASCIICHAT_OK)
      return NGTCP2_ERR_CALLBACK_FAILURE;
    if (stream_id >= 0) {
      ngtcp2_vec vectors[8];
      for (size_t i = 0; i < count; ++i)
        vectors[i] = (ngtcp2_vec){.base = h3vec[i].base, .len = h3vec[i].len};
      ngtcp2_ssize consumed = -1;
      ngtcp2_ssize written = ngtcp2_conn_writev_stream(
          connection->conn, path, info, packet, capacity, &consumed,
          fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : NGTCP2_WRITE_STREAM_FLAG_NONE, stream_id, vectors, count, now_ns);
      if (consumed < 0) {
        if (acip_http3_server_wrote(connection->http3, stream_id, 0) != ASCIICHAT_OK)
          return NGTCP2_ERR_CALLBACK_FAILURE;
        if (written != NGTCP2_ERR_STREAM_DATA_BLOCKED)
          return written;
        /* Other streams and QUIC acknowledgements can still make progress
         * while this HTTP/3 stream waits for flow-control credit. */
      } else {
        if (acip_http3_server_wrote(connection->http3, stream_id, (size_t)consumed) != ASCIICHAT_OK)
          return NGTCP2_ERR_CALLBACK_FAILURE;
        return written;
      }
    }
  }
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i) {
    acip_traffic_class_t flow = (acip_traffic_class_t)i;
    while (connection->datagram_head[i] &&
           ((connection->datagram_head[i]->deadline_ns && connection->datagram_head[i]->deadline_ns <= now_ns) ||
            connection->datagram_head[i]->unit > acip_quic_connection_datagram_payload(connection)))
      pop_datagram(connection, flow);
    datagram_send_t *item = connection->datagram_head[i];
    if (item) {
      uint8_t fragment[ACIP_QUIC_DATAGRAM_FRAME_LIMIT];
      size_t fragment_length = 0, prefix_length = 0;
      if (connection->tls->webtransport) {
        int64_t session_id = -1;
        if (!acip_http3_server_accepted(connection->http3, &session_id))
          return NGTCP2_ERR_CALLBACK_FAILURE;
        prefix_length = write_quic_varint(fragment, (uint64_t)session_id / 4);
      }
      if (acip_quic_datagram_fragment(flow, item->id, item->data, item->length, item->unit, item->index,
                                      fragment + prefix_length, sizeof(fragment) - prefix_length,
                                      &fragment_length) != ASCIICHAT_OK)
        return NGTCP2_ERR_CALLBACK_FAILURE;
      fragment_length += prefix_length;
      int accepted = 0;
      ngtcp2_ssize written =
          ngtcp2_conn_write_datagram(connection->conn, path, info, packet, capacity, &accepted,
                                     NGTCP2_WRITE_DATAGRAM_FLAG_NONE, 0, fragment, fragment_length, now_ns);
      if (accepted) {
        connection->packet_has_stream_data = true;
        connection->packet_traffic_class = flow;
        if (++item->index == item->count)
          pop_datagram(connection, flow);
      }
      return written;
    }
    send_stream_t *stream = &connection->sends[i];
    send_chunk_t *chunk = stream->head;
    while (chunk && chunk->submitted == chunk->length)
      chunk = chunk->next;
    bool send_fin = !chunk && stream->fin_requested && !stream->fin_submitted;
    if (!chunk && !send_fin)
      continue;
    if (stream->id < 0) {
      int rc = ngtcp2_conn_open_uni_stream(connection->conn, &stream->id, NULL);
      if (rc == NGTCP2_ERR_STREAM_ID_BLOCKED)
        continue;
      if (rc != 0)
        return rc;
    }
    ngtcp2_vec vec = {0};
    if (chunk)
      vec = (ngtcp2_vec){.base = chunk->data + chunk->submitted, .len = chunk->length - chunk->submitted};
    ngtcp2_ssize consumed = -1;
    ngtcp2_ssize written =
        ngtcp2_conn_writev_stream(connection->conn, path, info, packet, capacity, &consumed,
                                  send_fin ? NGTCP2_WRITE_STREAM_FLAG_FIN : NGTCP2_WRITE_STREAM_FLAG_NONE, stream->id,
                                  send_fin ? NULL : &vec, send_fin ? 0 : 1, now_ns);
    if (written == NGTCP2_ERR_STREAM_DATA_BLOCKED)
      continue;
    if (written < 0)
      return written;
    if (send_fin && consumed == 0) {
      if (!written)
        return NGTCP2_ERR_CALLBACK_FAILURE;
      stream->fin_submitted = true;
      connection->packet_has_stream_data = true;
      connection->packet_traffic_class = (acip_traffic_class_t)i;
    }
    if (consumed > 0) {
      if (send_fin || (size_t)consumed > vec.len || !written)
        return NGTCP2_ERR_CALLBACK_FAILURE;
      chunk->submitted += (size_t)consumed;
      connection->packet_has_stream_data = true;
      connection->packet_traffic_class = (acip_traffic_class_t)i;
    }
    return written;
  }
  return ngtcp2_conn_write_pkt(connection->conn, path, info, packet, capacity, now_ns);
}

bool acip_quic_connection_pending_packet_class(const acip_quic_connection_t *connection,
                                               acip_traffic_class_t *traffic_class) {
  if (!connection || !connection->packet_pending || !connection->packet_has_stream_data || !traffic_class)
    return false;
  *traffic_class = connection->packet_traffic_class;
  return true;
}

asciichat_error_t acip_quic_connection_write_close(acip_quic_connection_t *connection, ngtcp2_path_storage *path,
                                                   void *packet, size_t capacity, size_t *length, uint64_t now_ns) {
  if (!connection || !connection->conn || !path || !packet || capacity < 1200 || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC close packet output");
  *length = 0;
  if (connection->failed || ngtcp2_conn_in_closing_period2(connection->conn) ||
      ngtcp2_conn_in_draining_period2(connection->conn))
    return ASCIICHAT_OK;
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  ngtcp2_path_storage_zero(path);
  ngtcp2_pkt_info info = {0};
  ngtcp2_ccerr close_error;
  ngtcp2_ccerr_default(&close_error);
  ngtcp2_ssize written =
      ngtcp2_conn_write_connection_close(connection->conn, &path->path, &info, packet, capacity, &close_error, now_ns);
  if (written < 0)
    return SET_ERRNO(ERROR_NETWORK, "Cannot write QUIC close packet: %s", ngtcp2_strerror((int)written));
  *length = (size_t)written;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_write(acip_quic_connection_t *connection, ngtcp2_path_storage *path,
                                             void *packet, size_t capacity, size_t *length, uint64_t now_ns) {
  if (!length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC packet length output");
  *length = 0;
  if (!connection || !connection->conn || connection->failed || !path || !packet || capacity < 1200)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC packet output");
  if (connection->packet_pending)
    return SET_ERRNO(ERROR_INVALID_STATE, "Previous QUIC packet has not been transmitted");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  ngtcp2_path_storage_zero(path);
  ngtcp2_pkt_info info = {0};
  ngtcp2_ssize written = write_pending(connection, &path->path, &info, packet, capacity, now_ns);
  if (written < 0) {
    connection->failed = true;
    return SET_ERRNO(written == NGTCP2_ERR_CRYPTO ? ERROR_CRYPTO : ERROR_NETWORK, "QUIC write failed: %s",
                     ngtcp2_strerror((int)written));
  }
  *length = (size_t)written;
  connection->packet_pending = written > 0;
  return update_readiness(connection);
}

asciichat_error_t acip_quic_connection_packet_sent(acip_quic_connection_t *connection, uint64_t now_ns) {
  if (!connection || !connection->conn || connection->failed || !connection->packet_pending)
    return SET_ERRNO(ERROR_INVALID_STATE, "No QUIC packet awaiting transmission");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  ngtcp2_conn_update_pkt_tx_time(connection->conn, now_ns);
  connection->packet_pending = false;
  return ASCIICHAT_OK;
}

uint64_t acip_quic_connection_expiry(const acip_quic_connection_t *connection) {
  if (!connection || !connection->conn || connection->failed)
    return UINT64_MAX;
  uint64_t expiry = ngtcp2_conn_get_expiry(connection->conn);
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i)
    for (const datagram_send_t *item = connection->datagram_head[i]; item; item = item->next)
      if (item->deadline_ns && item->deadline_ns < expiry)
        expiry = item->deadline_ns;
  for (size_t i = 0; i < ACIP_QUIC_DATAGRAM_SLOTS; ++i) {
    const acip_quic_datagram_slot_t *slot = &connection->datagram_reader.slots[i];
    if (slot->data && slot->started_ns <= UINT64_MAX - ACIP_QUIC_DATAGRAM_EXPIRY_NS &&
        slot->started_ns + ACIP_QUIC_DATAGRAM_EXPIRY_NS < expiry)
      expiry = slot->started_ns + ACIP_QUIC_DATAGRAM_EXPIRY_NS;
  }
  return expiry;
}

asciichat_error_t acip_quic_connection_handle_expiry(acip_quic_connection_t *connection, uint64_t now_ns) {
  if (!connection || !connection->conn || connection->failed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC timeout state");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  int rc = ngtcp2_conn_get_expiry(connection->conn) <= now_ns ? ngtcp2_conn_handle_expiry(connection->conn, now_ns) : 0;
  if (rc != 0) {
    connection->failed = true;
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "QUIC timer failed: %s", ngtcp2_strerror(rc));
  }
  return update_readiness(connection);
}

bool acip_quic_connection_webtransport_ready(const acip_quic_connection_t *connection) {
  return connection && connection->tls->webtransport && connection->http3 &&
         acip_http3_server_accepted(connection->http3, NULL) && !connection->failed;
}

bool acip_quic_connection_tls_ready(const acip_quic_connection_t *connection) {
  return connection && !connection->failed && connection->tls_ready;
}

asciichat_error_t acip_quic_connection_peer_address(const acip_quic_connection_t *connection,
                                                    struct sockaddr_storage *address, socklen_t *length) {
  if (!connection || !connection->conn || connection->failed || !address || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC peer address query");
  const ngtcp2_path *path = ngtcp2_conn_get_path2(connection->conn);
  if (!path || !valid_address(&path->remote))
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC peer address is unavailable");
  memset(address, 0, sizeof(*address));
  memcpy(address, path->remote.addr, (size_t)path->remote.addrlen);
  *length = path->remote.addrlen;
  return ASCIICHAT_OK;
}

void acip_quic_connection_destroy(acip_quic_connection_t **connection) {
  if (!connection || !*connection)
    return;
  ngtcp2_conn_del((*connection)->conn);
  (*connection)->conn = NULL;
  acip_http3_server_destroy(&(*connection)->http3);
  buffer_release((*connection)->early_security_packet);
  acip_quic_datagram_reset(&(*connection)->datagram_reader);
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i) {
    while ((*connection)->datagram_head[i])
      pop_datagram(*connection, (acip_traffic_class_t)i);
    send_chunk_t *chunk = (*connection)->sends[i].head;
    while (chunk) {
      send_chunk_t *next = chunk->next;
      free_chunk(*connection, chunk);
      chunk = next;
    }
    acip_quic_stream_reset(&(*connection)->receives[i].reader);
  }
  acip_quic_tls_session_destroy(&(*connection)->ssl);
  ngtcp2_crypto_ossl_ctx_del((*connection)->crypto);
  acip_quic_security_cleanup(&(*connection)->security);
  SAFE_FREE(*connection);
}

asciichat_error_t acip_quic_connection_migrate(acip_quic_connection_t *connection, const struct sockaddr *local,
                                               socklen_t length, uint64_t now_ns) {
  if (!connection || !connection->conn || connection->failed || connection->tls->server || !local ||
      (size_t)length > sizeof(ngtcp2_sockaddr_union))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC client migration");
  const ngtcp2_path *current = ngtcp2_conn_get_path2(connection->conn);
  ngtcp2_path path = *current;
  path.local.addr = (struct sockaddr *)local;
  path.local.addrlen = length;
  if (!valid_address(&path.local) || local->sa_family != current->remote.addr->sa_family)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC migration requires a concrete address in the peer address family");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  int rc = ngtcp2_conn_initiate_immediate_migration(connection->conn, &path, now_ns);
  if (rc == NGTCP2_ERR_INVALID_STATE)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC handshake is unconfirmed or peer disabled active migration");
  if (rc == NGTCP2_ERR_CONN_ID_BLOCKED)
    return SET_ERRNO(ERROR_BUFFER_FULL, "QUIC migration requires an unused peer connection ID");
  if (rc != 0)
    return SET_ERRNO(ERROR_NETWORK, "Cannot initiate QUIC migration: %s", ngtcp2_strerror(rc));
  return ASCIICHAT_OK;
}
