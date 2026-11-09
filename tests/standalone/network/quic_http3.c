#include <ascii-chat/common.h>
#include <network/quic/http3.h>
#include <ascii-chat/network/acip/acds_protocol.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <string.h>

#define CHECK(expression)                                                                                              \
  do {                                                                                                                 \
    if (!(expression))                                                                                                 \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expression);                                             \
  } while (0)

static nghttp3_ssize keep_open(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t count, uint32_t *flags,
                               void *context, void *stream_context) {
  (void)conn;
  (void)stream_id;
  (void)vec;
  (void)count;
  (void)flags;
  (void)context;
  (void)stream_context;
  return NGHTTP3_ERR_WOULDBLOCK;
}

typedef struct {
  int status;
  bool fin;
  bool settings;
} client_state_t;
static int recv_header(nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name, nghttp3_rcbuf *value,
                       uint8_t flags, void *context, void *stream_context) {
  (void)conn;
  (void)stream_id;
  (void)token;
  (void)flags;
  (void)stream_context;
  client_state_t *state = context;
  nghttp3_vec n = nghttp3_rcbuf_get_buf(name), v = nghttp3_rcbuf_get_buf(value);
  if (n.len == 7 && memcmp(n.base, ":status", 7) == 0 && v.len == 3)
    state->status = (v.base[0] - '0') * 100 + (v.base[1] - '0') * 10 + v.base[2] - '0';
  return 0;
}
static int end_headers(nghttp3_conn *conn, int64_t stream_id, int fin, void *context, void *stream_context) {
  (void)conn;
  (void)stream_id;
  (void)stream_context;
  ((client_state_t *)context)->fin = fin != 0;
  return 0;
}
static int recv_settings(nghttp3_conn *conn, const nghttp3_proto_settings *settings, void *context) {
  (void)conn;
  ((client_state_t *)context)->settings = settings->enable_connect_protocol && settings->h3_datagram;
  return 0;
}

static void pump_server(acip_http3_server_t *server, nghttp3_conn *client) {
  for (unsigned i = 0; i < 30; ++i) {
    int64_t stream = -1;
    bool fin = false;
    nghttp3_vec vectors[8];
    size_t count = 0, bytes = 0;
    CHECK(acip_http3_server_write(server, &stream, &fin, vectors, 8, &count) == ASCIICHAT_OK);
    if (stream < 0)
      return;
    for (size_t j = 0; j < count; ++j) {
      nghttp3_ssize read =
          nghttp3_conn_read_stream2(client, stream, vectors[j].base, vectors[j].len, fin && j + 1 == count, 0);
      CHECK(read >= 0 && (size_t)read == vectors[j].len);
      bytes += vectors[j].len;
    }
    CHECK(acip_http3_server_wrote(server, stream, bytes) == ASCIICHAT_OK);
    CHECK(acip_http3_server_ack(server, stream, bytes) == ASCIICHAT_OK);
  }
  FATAL(ERROR_ASSERTION_FAILED, "HTTP/3 server output did not quiesce");
}
static void pump_client(nghttp3_conn *client, acip_http3_server_t *server) {
  for (unsigned i = 0; i < 30; ++i) {
    int64_t stream = -1;
    int fin = 0;
    nghttp3_vec vectors[8];
    nghttp3_ssize count = nghttp3_conn_writev_stream(client, &stream, &fin, vectors, 8);
    CHECK(count >= 0);
    if (stream < 0)
      return;
    size_t bytes = 0;
    for (nghttp3_ssize j = 0; j < count; ++j) {
      size_t consumed = 0;
      CHECK(acip_http3_server_read(server, stream, vectors[j].base, vectors[j].len, fin && j + 1 == count, 0,
                                   &consumed) == ASCIICHAT_OK);
      CHECK(consumed == vectors[j].len);
      bytes += vectors[j].len;
    }
    CHECK(nghttp3_conn_add_write_offset(client, stream, bytes) == 0);
    CHECK(nghttp3_conn_add_ack_offset(client, stream, bytes) == 0);
  }
  FATAL(ERROR_ASSERTION_FAILED, "HTTP/3 client output did not quiesce");
}
static void pump_client_delayed_settings(nghttp3_conn *client, acip_http3_server_t *server) {
  uint8_t delayed[512];
  size_t delayed_length = 0;
  for (unsigned i = 0; i < 30; ++i) {
    int64_t stream = -1;
    int fin = 0;
    nghttp3_vec vectors[8];
    nghttp3_ssize count = nghttp3_conn_writev_stream(client, &stream, &fin, vectors, 8);
    CHECK(count >= 0);
    if (stream < 0)
      break;
    size_t bytes = 0;
    for (nghttp3_ssize j = 0; j < count; ++j) {
      if (stream == 2) {
        CHECK(vectors[j].len <= sizeof(delayed) - delayed_length);
        memcpy(delayed + delayed_length, vectors[j].base, vectors[j].len);
        delayed_length += vectors[j].len;
      } else {
        size_t consumed = 0;
        CHECK(acip_http3_server_read(server, stream, vectors[j].base, vectors[j].len, fin && j + 1 == count, 0,
                                     &consumed) == ASCIICHAT_OK);
        CHECK(consumed == vectors[j].len);
      }
      bytes += vectors[j].len;
    }
    CHECK(nghttp3_conn_add_write_offset(client, stream, bytes) == 0);
    CHECK(nghttp3_conn_add_ack_offset(client, stream, bytes) == 0);
  }
  CHECK(delayed_length > 0 && !acip_http3_server_accepted(server, NULL));
  size_t consumed = 0;
  CHECK(acip_http3_server_read(server, 2, delayed, delayed_length, false, 0, &consumed) == ASCIICHAT_OK);
  CHECK(consumed == delayed_length);
}
#define NV(name, value) {(const uint8_t *)(name), (const uint8_t *)(value), sizeof(name) - 1, sizeof(value) - 1, 0}
static void exchange(unsigned scenario) {
  bool allowed = scenario == 0 || scenario == 5;
  acip_http3_server_t *server = NULL;
  CHECK(acip_http3_server_create("https://client.example", "/acip", 3, 7, 11, NULL, &server) == ASCIICHAT_OK);
  client_state_t capture = {0};
  nghttp3_callbacks callbacks = {
      .recv_header = recv_header, .end_headers = end_headers, .recv_settings2 = recv_settings};
  nghttp3_settings settings;
  nghttp3_settings_default(&settings);
  settings.h3_datagram = 1;
  settings.qpack_max_dtable_capacity = 0;
  settings.qpack_encoder_max_dtable_capacity = 0;
  nghttp3_conn *client = NULL;
  CHECK(nghttp3_conn_client_new(&client, &callbacks, &settings, NULL, &capture) == 0);
  CHECK(nghttp3_conn_bind_control_stream(client, 2) == 0);
  CHECK(nghttp3_conn_bind_qpack_streams(client, 6, 10) == 0);
  pump_server(server, client);
  CHECK(capture.settings);
  if (scenario != 5)
    pump_client(client, server);
  const nghttp3_nv headers[] = {NV(":method", "CONNECT"),           NV(":scheme", "https"),
                                NV(":authority", "server.example"), NV(":path", "/acip"),
                                NV(":protocol", "webtransport"),    NV("origin", "https://client.example")};
  nghttp3_nv request[sizeof(headers) / sizeof(headers[0])];
  memcpy(request, headers, sizeof(request));
  static const char other[] = "https://untrusted.example";
  if (scenario == 1) {
    request[5].value = (const uint8_t *)other;
    request[5].valuelen = sizeof(other) - 1;
  }
  static const char wrong_path[] = "/other";
  if (scenario == 2) {
    request[3].value = (const uint8_t *)wrong_path;
    request[3].valuelen = sizeof(wrong_path) - 1;
  }
  nghttp3_data_reader body = {.read_data = keep_open};
  size_t request_count = sizeof(request) / sizeof(request[0]) - (scenario == 3 ? 1 : 0);
  CHECK(nghttp3_conn_submit_request(client, 0, request, request_count, scenario == 4 ? NULL : &body, NULL) == 0);
  if (scenario == 5)
    pump_client_delayed_settings(client, server);
  else
    pump_client(client, server);
  pump_server(server, client);
  int64_t session_id = -1;
  CHECK(acip_http3_server_accepted(server, &session_id) == allowed);
  CHECK(capture.status == (allowed ? 200 : 403));
  if (allowed)
    CHECK(session_id == 0 && !capture.fin);
  nghttp3_conn_del(client);
  acip_http3_server_destroy(&server);
}
static uint64_t read_varint(const uint8_t *data, size_t length, size_t *offset) {
  CHECK(*offset < length);
  size_t width = (size_t)1 << (data[*offset] >> 6);
  CHECK(width <= length - *offset);
  uint64_t value = data[*offset] & 0x3f;
  for (size_t i = 1; i < width; ++i)
    value = (value << 8) | data[*offset + i];
  *offset += width;
  return value;
}
static void test_output_contract(void) {
  acip_http3_server_t *server = NULL;
  CHECK(acip_http3_server_create("https://client.example", "/acip", 3, 7, 11, NULL, &server) == ASCIICHAT_OK);
  nghttp3_vec vectors[2];
  int64_t stream;
  bool fin;
  size_t count;
  CHECK(acip_http3_server_write(server, &stream, &fin, vectors, 2, &count) == ASCIICHAT_OK);
  CHECK(stream == 3 && !fin && count == 1 && vectors[0].len > 4);
  size_t length = vectors[0].len, first = length / 2;
  size_t offset = 0;
  CHECK(read_varint(vectors[0].base, length, &offset) == 0);
  CHECK(read_varint(vectors[0].base, length, &offset) == 4);
  uint64_t settings_size = read_varint(vectors[0].base, length, &offset);
  CHECK(settings_size == length - offset);
  bool draft02 = false, draft07 = false, datagrams = false;
  while (offset < length) {
    uint64_t id = read_varint(vectors[0].base, length, &offset);
    uint64_t value = read_varint(vectors[0].base, length, &offset);
    if (id == 0x2b603742)
      draft02 = value == 1;
    if (id == 0xc671706a)
      draft07 = value == 1;
    if (id == 0x33)
      datagrams = value == 1;
  }
  CHECK(draft02 && draft07 && datagrams);
  CHECK(acip_http3_server_write(server, &stream, &fin, vectors, 2, &count) == ERROR_INVALID_STATE);
  CHECK(acip_http3_server_wrote(server, 7, first) == ERROR_INVALID_PARAM);
  CHECK(acip_http3_server_wrote(server, 3, length + 1) == ERROR_INVALID_PARAM);
  CHECK(acip_http3_server_wrote(server, 3, first) == ASCIICHAT_OK);
  CHECK(acip_http3_server_ack(server, 3, first + 1) == ERROR_INVALID_PARAM);
  CHECK(acip_http3_server_ack(server, 3, first) == ASCIICHAT_OK);
  CHECK(acip_http3_server_write(server, &stream, &fin, vectors, 2, &count) == ASCIICHAT_OK);
  CHECK(stream == 3 && vectors[0].len == length - first);
  CHECK(acip_http3_server_wrote(server, 3, length - first) == ASCIICHAT_OK);
  CHECK(acip_http3_server_ack(server, 3, length - first) == ASCIICHAT_OK);
  acip_http3_server_destroy(&server);
}
static void test_discovered_endpoint(void) {
  const char url[] = "https://example.test:4433/acip";
  uint8_t response[sizeof(acip_session_joined_t) + 10 + sizeof(url) - 1] = {0};
  acip_session_joined_t joined = {.success = 1, .session_type = SESSION_TYPE_DIRECT_TCP};
  memcpy(response, &joined, sizeof(joined));
  uint8_t *extension = response + sizeof(joined);
  memcpy(extension, "ACQT", 4);
  extension[4] = 1;
  extension[6] = 0x6a;
  extension[7] = 0x58;
  extension[9] = sizeof(url) - 1;
  memcpy(extension + 10, url, sizeof(url) - 1);
  acds_protocol_t protocol = {.state = ACDS_JOINING, .require_webrtc = true};
  CHECK(acds_protocol_receive(&protocol, PACKET_TYPE_ACIP_SESSION_JOINED, response, sizeof(response)) == ASCIICHAT_OK);
  CHECK(protocol.quic_port == 27224 && strcmp(protocol.webtransport_url, url) == 0);
  protocol = (acds_protocol_t){.state = ACDS_JOINING};
  CHECK(acds_protocol_receive(&protocol, PACKET_TYPE_ACIP_SESSION_JOINED, response, sizeof(joined)) == ASCIICHAT_OK);
  CHECK(!protocol.quic_port && !protocol.webtransport_url[0]);
  for (size_t length = sizeof(joined) + 1; length < sizeof(response); ++length) {
    protocol = (acds_protocol_t){.state = ACDS_JOINING};
    CHECK(acds_protocol_receive(&protocol, PACKET_TYPE_ACIP_SESSION_JOINED, response, length) ==
          ERROR_NETWORK_PROTOCOL);
  }
  extension[4] = 2;
  protocol = (acds_protocol_t){.state = ACDS_JOINING};
  CHECK(acds_protocol_receive(&protocol, PACKET_TYPE_ACIP_SESSION_JOINED, response, sizeof(response)) ==
        ERROR_NETWORK_PROTOCOL);
  extension[4] = 1;
  extension[10] = 'w';
  protocol = (acds_protocol_t){.state = ACDS_JOINING};
  CHECK(acds_protocol_receive(&protocol, PACKET_TYPE_ACIP_SESSION_JOINED, response, sizeof(response)) ==
        ERROR_NETWORK_PROTOCOL);
}

static void test_transport_metadata_codec(void) {
  uint8_t bytes[ACDS_TRANSPORT_METADATA_MAX + 1];
  char url[512];
  uint16_t port = 99;
  size_t length = 0;
  CHECK(acds_transport_metadata_decode(NULL, 0, &port, url, sizeof(url)) == ASCIICHAT_OK && !port && !url[0]);
  CHECK(acds_transport_metadata_encode(30001, "https://example.test:443/acip", bytes, sizeof(bytes), &length) ==
        ASCIICHAT_OK);
  CHECK(acds_transport_metadata_decode(bytes, length, &port, url, sizeof(url)) == ASCIICHAT_OK && port == 30001 &&
        !strcmp(url, "https://example.test:443/acip"));
  CHECK(acds_transport_metadata_decode(bytes, length + 1, &port, url, sizeof(url)) == ERROR_NETWORK_PROTOCOL && !port &&
        !url[0]);
  bytes[4] = 2;
  CHECK(acds_transport_metadata_decode(bytes, length, &port, url, sizeof(url)) == ERROR_NETWORK_PROTOCOL);
  bytes[4] = 1;
  bytes[5] = 1;
  CHECK(acds_transport_metadata_decode(bytes, length, &port, url, sizeof(url)) == ERROR_NETWORK_PROTOCOL);
  bytes[5] = 0;
  bytes[12] = 0;
  CHECK(acds_transport_metadata_decode(bytes, length, &port, url, sizeof(url)) == ERROR_NETWORK_PROTOCOL);
  CHECK(acds_transport_metadata_encode(1, "http://example.test/acip", bytes, sizeof(bytes), &length) ==
        ERROR_INVALID_PARAM);
  CHECK(acds_transport_metadata_encode(1, "https://example.test/acip", bytes, 10, &length) == ERROR_INVALID_PARAM);
}

int main(void) {
  log_init(NULL, LOG_ERROR, false, false);
  errno_suppress(true);
  acip_http3_server_t *invalid = NULL;
  CHECK(acip_http3_server_create("http://example", "/acip", 3, 7, 11, NULL, &invalid) == ERROR_INVALID_PARAM);
  CHECK(!invalid);
  test_output_contract();
  test_discovered_endpoint();
  test_transport_metadata_codec();
  for (unsigned scenario = 0; scenario < 6; ++scenario)
    exchange(scenario);
  log_destroy();
  return 0;
}
