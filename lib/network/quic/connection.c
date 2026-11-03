#include "connection.h"

#include <ascii-chat/buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/network/acip/backpressure.h>
#include <ascii-chat/util/endian.h>
#include <ngtcp2/ngtcp2_crypto_ossl.h>
#include <sodium.h>
#include <string.h>

#define ACIP_QUIC_CONNECTION_ID_LENGTH 16
#define ACIP_QUIC_INITIAL_STREAM_WINDOW (5U * 1024U * 1024U)
#define ACIP_QUIC_INITIAL_CONNECTION_WINDOW (16U * 1024U * 1024U)
#define ACIP_QUIC_IDLE_TIMEOUT_NS (30ULL * 1000000000ULL)
#define ACIP_QUIC_FLOW_COUNT 3

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
  bool initialized;
} send_stream_t;

typedef struct {
  int64_t id;
  uint64_t received_offset;
  acip_quic_stream_reader_t reader;
  bool active, registered;
} receive_stream_t;

struct acip_quic_connection {
  ngtcp2_conn *conn;
  ngtcp2_crypto_conn_ref reference;
  ngtcp2_crypto_ossl_ctx *crypto;
  SSL *ssl;
  acip_quic_tls_t *tls;
  uint64_t last_time_ns;
  send_stream_t sends[ACIP_QUIC_FLOW_COUNT];
  receive_stream_t receives[ACIP_QUIC_FLOW_COUNT];
  bool received_classes[ACIP_QUIC_FLOW_COUNT];
  size_t pending_bytes;
  acip_quic_stream_emit_fn emit;
  void *emit_context;
  asciichat_error_t callback_error;
  bool failed;
  bool tls_ready;
  bool packet_pending;
  bool packet_has_stream_data;
  acip_traffic_class_t packet_traffic_class;
  bool media_allowed;
};

static asciichat_error_t update_readiness(acip_quic_connection_t *connection);

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
  if (!context->connection->emit || (traffic_class != ACIP_TRAFFIC_CONTROL && !context->connection->media_allowed))
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC packet received before application readiness");
  return context->connection->emit(traffic_class, packet, size, context->connection->emit_context);
}

static int receive_stream_data(ngtcp2_conn *conn, uint32_t flags, int64_t stream_id, uint64_t offset,
                               const uint8_t *data, size_t length, void *user_data, void *stream_user_data) {
  (void)stream_user_data;
  acip_quic_connection_t *connection = user_data;
  asciichat_error_t readiness = update_readiness(connection);
  if ((flags & NGTCP2_STREAM_DATA_FLAG_0RTT) || ngtcp2_is_bidi_stream(stream_id) ||
      ngtcp2_conn_is_local_stream2(conn, stream_id) || readiness != ASCIICHAT_OK || !connection->tls_ready) {
    connection->callback_error = readiness != ASCIICHAT_OK ? readiness : ERROR_NETWORK_PROTOCOL;
    return NGTCP2_ERR_CALLBACK_FAILURE;
  }
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
  callbacks.recv_retry = server ? NULL : ngtcp2_crypto_recv_retry_cb;
  callbacks.rand = random_bytes;
  callbacks.update_key = ngtcp2_crypto_update_key_cb;
  callbacks.delete_crypto_aead_ctx = ngtcp2_crypto_delete_crypto_aead_ctx_cb;
  callbacks.delete_crypto_cipher_ctx = ngtcp2_crypto_delete_crypto_cipher_ctx_cb;
  callbacks.version_negotiation = ngtcp2_crypto_version_negotiation_cb;
  callbacks.get_new_connection_id2 = new_connection_id;
  callbacks.get_path_challenge_data2 = ngtcp2_crypto_get_path_challenge_data2_cb;
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
                                           const ngtcp2_pkt_hd *initial, uint64_t now_ns,
                                           acip_quic_connection_t **out) {
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
  ngtcp2_transport_params params;
  ngtcp2_transport_params_default(&params);
  params.initial_max_streams_uni = 3;
  params.initial_max_stream_data_uni = ACIP_QUIC_INITIAL_STREAM_WINDOW;
  params.initial_max_data = ACIP_QUIC_INITIAL_CONNECTION_WINDOW;
  params.max_idle_timeout = ACIP_QUIC_IDLE_TIMEOUT_NS;
  if (initial) {
    params.original_dcid = initial->dcid;
    params.original_dcid_present = 1;
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
  return connection_create(tls, server_name, path, NULL, now_ns, out);
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
  return connection_create(tls, NULL, path, &header, now_ns, out);
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
  return ASCIICHAT_OK;
}

static asciichat_error_t update_time(acip_quic_connection_t *connection, uint64_t now_ns) {
  if (now_ns < connection->last_time_ns || now_ns == UINT64_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "QUIC monotonic time moved backwards or is invalid");
  connection->last_time_ns = now_ns;
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

asciichat_error_t acip_quic_connection_allow_media(acip_quic_connection_t *connection) {
  if (!connection || !connection->conn || connection->failed || !connection->tls_ready)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC TLS is not ready for ACIP media");
  connection->media_allowed = true;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_connection_submit(acip_quic_connection_t *connection, acip_traffic_class_t traffic_class,
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
  size_t prefix_length = stream->initialized ? 0 : ACIP_QUIC_STREAM_PREAMBLE_SIZE;
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
    uint8_t preamble[ACIP_QUIC_STREAM_PREAMBLE_SIZE];
    asciichat_error_t result = acip_quic_stream_preamble(traffic_class, preamble);
    if (result != ASCIICHAT_OK)
      return result;
    prefix = copy_chunk(preamble, sizeof(preamble));
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

size_t acip_quic_connection_pending_bytes(const acip_quic_connection_t *connection) {
  return connection ? connection->pending_bytes : 0;
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
                                                                        : ERROR_NETWORK_PROTOCOL;
    return SET_ERRNO(code, "QUIC receive failed: %s", ngtcp2_strerror(rc));
  }
  return update_readiness(connection);
}

static ngtcp2_ssize write_pending(acip_quic_connection_t *connection, ngtcp2_path *path, ngtcp2_pkt_info *info,
                                  uint8_t *packet, size_t capacity, uint64_t now_ns) {
  connection->packet_has_stream_data = false;
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i) {
    send_stream_t *stream = &connection->sends[i];
    send_chunk_t *chunk = stream->head;
    while (chunk && chunk->submitted == chunk->length)
      chunk = chunk->next;
    if (!chunk)
      continue;
    if (stream->id < 0) {
      int rc = ngtcp2_conn_open_uni_stream(connection->conn, &stream->id, NULL);
      if (rc == NGTCP2_ERR_STREAM_ID_BLOCKED)
        continue;
      if (rc != 0)
        return rc;
    }
    ngtcp2_vec vec = {.base = chunk->data + chunk->submitted, .len = chunk->length - chunk->submitted};
    ngtcp2_ssize consumed = -1;
    ngtcp2_ssize written = ngtcp2_conn_writev_stream(connection->conn, path, info, packet, capacity, &consumed,
                                                     NGTCP2_WRITE_STREAM_FLAG_NONE, stream->id, &vec, 1, now_ns);
    if (written == NGTCP2_ERR_STREAM_DATA_BLOCKED)
      continue;
    if (written < 0)
      return written;
    if (consumed > 0) {
      if ((size_t)consumed > vec.len || !written)
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
  return connection && connection->conn && !connection->failed ? ngtcp2_conn_get_expiry(connection->conn) : UINT64_MAX;
}

asciichat_error_t acip_quic_connection_handle_expiry(acip_quic_connection_t *connection, uint64_t now_ns) {
  if (!connection || !connection->conn || connection->failed)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid QUIC timeout state");
  asciichat_error_t result = update_time(connection, now_ns);
  if (result != ASCIICHAT_OK)
    return result;
  int rc = ngtcp2_conn_handle_expiry(connection->conn, now_ns);
  if (rc != 0) {
    connection->failed = true;
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "QUIC timer failed: %s", ngtcp2_strerror(rc));
  }
  return update_readiness(connection);
}

bool acip_quic_connection_tls_ready(const acip_quic_connection_t *connection) {
  return connection && !connection->failed && connection->tls_ready;
}

void acip_quic_connection_destroy(acip_quic_connection_t **connection) {
  if (!connection || !*connection)
    return;
  ngtcp2_conn_del((*connection)->conn);
  (*connection)->conn = NULL;
  for (size_t i = 0; i < ACIP_QUIC_FLOW_COUNT; ++i) {
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
  SAFE_FREE(*connection);
}
