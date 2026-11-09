#include "http3.h"
#include "../http/server_internal.h"

#include <ascii-chat/common.h>
#include <string.h>

#define H3_MAX_HEADER_BYTES 16384U
#define H3_MAX_ORIGIN 512U
#define H3_MAX_PATH 512U
#define H3_SETTINGS_BUFFER 96U
#define H3_MAX_HEADERS 64U
#define H3_MAX_TEXT 2048U

/**
 * @brief For acip http3 server, this is the live record that ties `http`, `control`, `request`, `settings_length`
 * together. The owning code creates it, updates it while work is active, and releases its handles and pointed-to
 * storage before freeing the record.
 */
struct acip_http3_server {
  nghttp3_conn *http;
  int64_t control, request;
  char origin[H3_MAX_ORIGIN], path[H3_MAX_PATH];
  uint8_t settings[H3_SETTINGS_BUFFER];
  size_t settings_length, settings_written, settings_acked, header_bytes;
  int64_t pending_stream;
  size_t pending_bytes;
  bool pending;
  bool settings_received, headers_done, responded, accepted, malformed;
  bool method, scheme, authority, protocol, request_path, request_origin;
  http_server_t *http_server;
  char method_text[32], request_target[H3_MAX_TEXT];
  char header_names[H3_MAX_HEADERS][H3_MAX_TEXT], header_values[H3_MAX_HEADERS][H3_MAX_TEXT];
  http_header_t headers[H3_MAX_HEADERS];
  size_t header_count;
  uint8_t *response_body;
  size_t response_length, response_offset;
};

static bool equal_bytes(nghttp3_vec bytes, const char *value) {
  size_t length = strlen(value);
  return bytes.len == length && memcmp(bytes.base, value, length) == 0;
}

static uint8_t *put_varint(uint8_t *p, uint64_t value) {
  size_t length = value < (1ULL << 6) ? 1 : value < (1ULL << 14) ? 2 : value < (1ULL << 30) ? 4 : 8;
  for (size_t i = 0; i < length; ++i)
    p[length - i - 1] = (uint8_t)(value >> (8 * i));
  p[0] |= length == 1 ? 0 : length == 2 ? 0x40 : length == 4 ? 0x80 : 0xc0;
  return p + length;
}

static void make_settings(acip_http3_server_t *server) {
  uint8_t body[H3_SETTINGS_BUFFER];
  uint8_t *p = body;
  /* QPACK is static-only. Advertise HTTP datagrams, extended CONNECT, and
   * browser WebTransport draft settings absent from nghttp3's settings API. */
  static const uint64_t entries[][2] = {{1, 0},        {6, H3_MAX_HEADER_BYTES}, {7, 0},         {8, 1}, {0x33, 1},
                                        {0xffd277, 1}, {0x2b603742, 1},          {0xc671706a, 1}};
  for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i) {
    p = put_varint(p, entries[i][0]);
    p = put_varint(p, entries[i][1]);
  }
  uint8_t *out = server->settings;
  out = put_varint(out, 0); /* HTTP/3 control stream */
  out = put_varint(out, 4); /* SETTINGS frame */
  out = put_varint(out, (uint64_t)(p - body));
  memcpy(out, body, (size_t)(p - body));
  server->settings_length = (size_t)(out - server->settings) + (size_t)(p - body);
}

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

static nghttp3_ssize read_response_body(nghttp3_conn *conn, int64_t stream_id, nghttp3_vec *vec, size_t count,
                                        uint32_t *flags, void *context, void *stream_context) {
  (void)conn;
  (void)stream_id;
  (void)stream_context;
  acip_http3_server_t *server = context;
  size_t remaining = server->response_length - server->response_offset;
  size_t written = 0;
  for (size_t i = 0; i < count && written < remaining; ++i) {
    size_t length = vec[i].len < remaining - written ? vec[i].len : remaining - written;
    memcpy(vec[i].base, server->response_body + server->response_offset + written, length);
    vec[i].len = length;
    written += length;
  }
  server->response_offset += written;
  if (server->response_offset == server->response_length) {
    *flags |= NGHTTP3_DATA_FLAG_EOF;
    SAFE_FREE(server->response_body);
  }
  return (nghttp3_ssize)written;
}

static asciichat_error_t http3_submit_response(void *context, unsigned status, const http_header_t *headers,
                                               size_t header_count, const void *body, size_t body_length) {
  acip_http3_server_t *server = context;
  if (!server || server->request < 0 || status < 100 || status > 599 || header_count > H3_MAX_HEADERS ||
      body_length > H3_MAX_HEADER_BYTES * 64U || (!body && body_length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 response");
  char status_text[4];
  safe_snprintf(status_text, sizeof(status_text), "%u", status);
  nghttp3_nv fields[H3_MAX_HEADERS + 1] = {
      {(uint8_t *)":status", (uint8_t *)status_text, sizeof(":status") - 1, strlen(status_text), NGHTTP3_NV_FLAG_NONE}};
  for (size_t i = 0; i < header_count; ++i) {
    if (!headers[i].name || !headers[i].value)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 response header");
    fields[i + 1] = (nghttp3_nv){(uint8_t *)headers[i].name, (uint8_t *)headers[i].value, strlen(headers[i].name),
                                 strlen(headers[i].value), NGHTTP3_NV_FLAG_NONE};
  }
  SAFE_FREE(server->response_body);
  server->response_length = body_length;
  server->response_offset = 0;
  if (body_length) {
    server->response_body = SAFE_MALLOC(body_length, uint8_t *);
    if (!server->response_body)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP/3 response body");
    memcpy(server->response_body, body, body_length);
  }
  nghttp3_data_reader reader = {.read_data = read_response_body};
  int result = nghttp3_conn_submit_response(server->http, server->request, fields, header_count + 1,
                                            body_length ? &reader : NULL);
  if (result != 0) {
    SAFE_FREE(server->response_body);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot submit HTTP/3 response: %s", nghttp3_strerror(result));
  }
  server->responded = true;
  return ASCIICHAT_OK;
}

static int maybe_respond(acip_http3_server_t *server) {
  if (!server->headers_done || !server->settings_received || server->responded)
    return 0;
  if (server->http_server)
    return 0;
  bool accepted = !server->malformed && server->method && server->scheme && server->authority && server->protocol &&
                  server->request_path && server->request_origin;
  static const uint8_t status_name[] = ":status", ok[] = "200", forbidden[] = "403";
  static const uint8_t draft_name[] = "sec-webtransport-http3-draft", draft_value[] = "draft02";
  nghttp3_nv fields[] = {
      {status_name, accepted ? ok : forbidden, sizeof(status_name) - 1, 3, NGHTTP3_NV_FLAG_NONE},
      {draft_name, draft_value, sizeof(draft_name) - 1, sizeof(draft_value) - 1, NGHTTP3_NV_FLAG_NONE}};
  const nghttp3_data_reader reader = {.read_data = keep_open};
  int result =
      nghttp3_conn_submit_response(server->http, server->request, fields, accepted ? 2 : 1, accepted ? &reader : NULL);
  if (result != 0)
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  server->responded = true;
  server->accepted = accepted;
  return 0;
}

static int dispatch_generic_request(acip_http3_server_t *server) {
  if (!server->http_server || !server->headers_done || !server->settings_received || server->responded)
    return 0;
  asciichat_error_t result =
      http_server_dispatch_http3(server->http_server, server->method_text, server->request_target, server->headers,
                                 server->header_count, NULL, 0, NULL, http3_submit_response, server);
  return result == ASCIICHAT_OK ? 0 : NGHTTP3_ERR_CALLBACK_FAILURE;
}

static int begin_headers(nghttp3_conn *conn, int64_t stream_id, void *context, void *stream_context) {
  (void)conn;
  (void)stream_context;
  acip_http3_server_t *server = context;
  if (server->request != -1 || stream_id < 0 || (stream_id & 3) != 0)
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  server->request = stream_id;
  return 0;
}

static int receive_header(nghttp3_conn *conn, int64_t stream_id, int32_t token, nghttp3_rcbuf *name,
                          nghttp3_rcbuf *value, uint8_t flags, void *context, void *stream_context) {
  (void)conn;
  (void)token;
  (void)flags;
  (void)stream_context;
  acip_http3_server_t *server = context;
  nghttp3_vec key = nghttp3_rcbuf_get_buf(name), val = nghttp3_rcbuf_get_buf(value);
  if (stream_id != server->request || key.len > H3_MAX_HEADER_BYTES - server->header_bytes ||
      val.len > H3_MAX_HEADER_BYTES - server->header_bytes - key.len)
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  server->header_bytes += key.len + val.len;
  if (key.len >= H3_MAX_TEXT || val.len >= H3_MAX_TEXT || server->header_count >= H3_MAX_HEADERS)
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  memcpy(server->header_names[server->header_count], key.base, key.len);
  server->header_names[server->header_count][key.len] = 0;
  memcpy(server->header_values[server->header_count], val.base, val.len);
  server->header_values[server->header_count][val.len] = 0;
  server->headers[server->header_count] =
      (http_header_t){server->header_names[server->header_count], server->header_values[server->header_count]};
  server->header_count++;
  if (equal_bytes(key, ":method")) {
    if (val.len >= sizeof(server->method_text))
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    memcpy(server->method_text, val.base, val.len);
    server->method_text[val.len] = 0;
  } else if (equal_bytes(key, ":path")) {
    if (val.len >= sizeof(server->request_target))
      return NGHTTP3_ERR_CALLBACK_FAILURE;
    memcpy(server->request_target, val.base, val.len);
    server->request_target[val.len] = 0;
  }
  bool *field = NULL, valid = false;
  if (equal_bytes(key, ":method")) {
    field = &server->method;
    valid = server->http_server ? val.len > 0 : equal_bytes(val, "CONNECT");
  } else if (equal_bytes(key, ":scheme")) {
    field = &server->scheme;
    valid = equal_bytes(val, "https");
  } else if (equal_bytes(key, ":authority")) {
    field = &server->authority;
    valid = val.len > 0;
  } else if (equal_bytes(key, ":protocol")) {
    field = &server->protocol;
    valid = equal_bytes(val, "webtransport");
  } else if (equal_bytes(key, ":path")) {
    field = &server->request_path;
    valid = server->http_server ? val.len > 0 : equal_bytes(val, server->path);
  } else if (equal_bytes(key, "origin")) {
    field = &server->request_origin;
    valid = server->http_server ? true : equal_bytes(val, server->origin);
  }
  if (field) {
    if (*field || !valid)
      server->malformed = true;
    *field = true;
  }
  return 0;
}

static int end_headers(nghttp3_conn *conn, int64_t stream_id, int fin, void *context, void *stream_context) {
  (void)conn;
  (void)stream_context;
  acip_http3_server_t *server = context;
  if (stream_id != server->request || server->headers_done)
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  server->headers_done = true;
  if (server->http_server && fin)
    return dispatch_generic_request(server);
  server->malformed |= fin != 0;
  return maybe_respond(server);
}

static int receive_settings(nghttp3_conn *conn, const nghttp3_proto_settings *settings, void *context) {
  (void)conn;
  acip_http3_server_t *server = context;
  if (!server->http_server && !settings->h3_datagram)
    return NGHTTP3_ERR_CALLBACK_FAILURE;
  server->settings_received = true;
  if (server->http_server)
    return dispatch_generic_request(server);
  return maybe_respond(server);
}

asciichat_error_t acip_http3_server_create(const char *origin, const char *path, int64_t control, int64_t encoder,
                                           int64_t decoder, http_server_t *http_server, acip_http3_server_t **out) {
  if (!out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing HTTP/3 server output");
  *out = NULL;
  if ((!http_server && (!origin || !path)) ||
      (origin && strnlen(origin, sizeof(((acip_http3_server_t *)0)->origin)) >= H3_MAX_ORIGIN) ||
      (path && strnlen(path, sizeof(((acip_http3_server_t *)0)->path)) >= H3_MAX_PATH) ||
      (!http_server && (strncmp(origin, "https://", sizeof("https://") - 1) || path[0] != '/')) || control < 0 ||
      encoder < 0 || decoder < 0 || control == encoder || control == decoder || encoder == decoder ||
      (control & 3) != 3 || (encoder & 3) != 3 || (decoder & 3) != 3)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 WebTransport configuration");
  acip_http3_server_t *server = SAFE_CALLOC(1, sizeof(*server), acip_http3_server_t *);
  if (!server)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP/3 server");
  if (origin)
    memcpy(server->origin, origin, strlen(origin) + (size_t)1);
  if (path)
    memcpy(server->path, path, strlen(path) + (size_t)1);
  server->control = control;
  server->http_server = http_server;
  server->request = -1;
  make_settings(server);
  nghttp3_callbacks callbacks = {.begin_headers = begin_headers,
                                 .recv_header = receive_header,
                                 .end_headers = end_headers,
                                 .recv_settings2 = receive_settings};
  nghttp3_settings settings;
  nghttp3_settings_default(&settings);
  settings.enable_connect_protocol = 1;
  settings.h3_datagram = 1;
  settings.qpack_max_dtable_capacity = 0;
  settings.qpack_encoder_max_dtable_capacity = 0;
  settings.qpack_blocked_streams = 0;
  settings.max_field_section_size = H3_MAX_HEADER_BYTES;
  int result = nghttp3_conn_server_new(&server->http, &callbacks, &settings, NULL, server);
  if (!result)
    result = nghttp3_conn_bind_control_stream(server->http, control);
  if (!result)
    result = nghttp3_conn_bind_qpack_streams(server->http, encoder, decoder);
  if (result) {
    acip_http3_server_destroy(&server);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot initialize HTTP/3: %s", nghttp3_strerror(result));
  }
  nghttp3_conn_set_max_client_streams_bidi(server->http, 1);
  *out = server;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_http3_server_read(acip_http3_server_t *server, int64_t stream_id, const uint8_t *data,
                                         size_t length, bool fin, uint64_t now_ns, size_t *consumed) {
  if (!server || !consumed || (!data && length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 input");
  *consumed = 0;
  nghttp3_ssize result = nghttp3_conn_read_stream2(server->http, stream_id, data, length, fin, now_ns);
  if (result < 0)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "HTTP/3 receive failed: %s", nghttp3_strerror((int)result));
  *consumed = (size_t)result;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_http3_server_write(acip_http3_server_t *server, int64_t *stream_id, bool *fin,
                                          nghttp3_vec *vectors, size_t capacity, size_t *count) {
  if (!server || !stream_id || !fin || !vectors || !capacity || !count)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 output");
  if (server->pending)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP/3 output has not been submitted");
  *stream_id = -1;
  *fin = false;
  *count = 0;
  if (server->settings_written < server->settings_length) {
    *stream_id = server->control;
    vectors[0].base = server->settings + server->settings_written;
    vectors[0].len = server->settings_length - server->settings_written;
    *count = 1;
    server->pending = true;
    server->pending_stream = *stream_id;
    server->pending_bytes = vectors[0].len;
    return ASCIICHAT_OK;
  }
  for (;;) {
    int is_fin = 0;
    nghttp3_ssize result = nghttp3_conn_writev_stream(server->http, stream_id, &is_fin, vectors, capacity);
    if (result < 0)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "HTTP/3 output failed: %s", nghttp3_strerror((int)result));
    if (*stream_id != server->control) {
      *fin = is_fin != 0;
      *count = (size_t)result;
      if (*stream_id >= 0) {
        server->pending = true;
        server->pending_stream = *stream_id;
        server->pending_bytes = 0;
        for (nghttp3_ssize i = 0; i < result; ++i) {
          if (vectors[i].len > SIZE_MAX - server->pending_bytes)
            return SET_ERRNO(ERROR_NETWORK_SIZE, "HTTP/3 output size overflow");
          server->pending_bytes += vectors[i].len;
        }
      }
      return ASCIICHAT_OK;
    }
    size_t skipped = 0;
    for (nghttp3_ssize i = 0; i < result; ++i)
      skipped += vectors[i].len;
    if (is_fin || !skipped || nghttp3_conn_add_write_offset(server->http, *stream_id, skipped) ||
        nghttp3_conn_add_ack_offset(server->http, *stream_id, skipped))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot replace HTTP/3 control settings");
  }
}

asciichat_error_t acip_http3_server_wrote(acip_http3_server_t *server, int64_t stream_id, size_t length) {
  if (!server || !server->pending || stream_id != server->pending_stream || length > server->pending_bytes)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 submitted output");
  if (stream_id == server->control) {
    server->settings_written += length;
    server->pending = false;
    return ASCIICHAT_OK;
  }
  int result = nghttp3_conn_add_write_offset(server->http, stream_id, length);
  if (result)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot advance HTTP/3 output: %s", nghttp3_strerror(result));
  server->pending = false;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_http3_server_ack(acip_http3_server_t *server, int64_t stream_id, uint64_t length) {
  if (!server)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 server");
  if (stream_id == server->control) {
    if (length > server->settings_written - server->settings_acked)
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 settings acknowledgement");
    server->settings_acked += (size_t)length;
    return ASCIICHAT_OK;
  }
  int result = nghttp3_conn_add_ack_offset(server->http, stream_id, length);
  return result ? SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot acknowledge HTTP/3 output: %s", nghttp3_strerror(result))
                : ASCIICHAT_OK;
}

bool acip_http3_server_accepted(const acip_http3_server_t *server, int64_t *session_id) {
  if (!server || !server->accepted)
    return false;
  if (session_id)
    *session_id = server->request;
  return true;
}

void acip_http3_server_destroy(acip_http3_server_t **server) {
  if (!server || !*server)
    return;
  nghttp3_conn_del((*server)->http);
  SAFE_FREE(*server);
}
