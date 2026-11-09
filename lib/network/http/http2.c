#include "http2.h"

#if __has_include(<nghttp2/nghttp2.h>)

#include <ascii-chat/common.h>
#include <nghttp2/nghttp2.h>
#include <string.h>

#define HTTP2_MAX_TEXT 8192u
#define HTTP2_MAX_HEADERS 64u
#define HTTP2_MAX_CONCURRENT_STREAMS 128u

typedef struct {
  int32_t stream_id;
  char *method;
  char *path;
  char *body;
  size_t body_length;
  size_t body_capacity;
  http2_header_t headers[HTTP2_MAX_HEADERS];
  char *header_names[HTTP2_MAX_HEADERS];
  char *header_values[HTTP2_MAX_HEADERS];
  size_t header_count;
  bool dispatched;
} request_state_t;

typedef struct {
  char *data;
  size_t length;
  size_t offset;
} response_state_t;

struct http2_connection {
  nghttp2_session *session;
  http2_request_fn on_request;
  void *user_data;
  size_t max_body_length;
  size_t max_response_body_length;
  const uint8_t *output;
  size_t output_length;
};

static request_state_t *get_request(nghttp2_session *session, int32_t stream_id) {
  return nghttp2_session_get_stream_user_data(session, stream_id);
}

static char *copy_text(const uint8_t *data, size_t length) {
  if (length > HTTP2_MAX_TEXT)
    return NULL;
  char *copy = SAFE_MALLOC(length + 1, char *);
  if (!copy)
    return NULL;
  memcpy(copy, data, length);
  copy[length] = 0;
  return copy;
}

static int on_begin_headers(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
  (void)user_data;
  if (frame->hd.type != NGHTTP2_HEADERS)
    return 0;
  request_state_t *request = SAFE_CALLOC(1, sizeof(*request), request_state_t *);
  if (!request)
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  request->stream_id = frame->hd.stream_id;
  nghttp2_session_set_stream_user_data(session, frame->hd.stream_id, request);
  return 0;
}

static int on_header(nghttp2_session *session, const nghttp2_frame *frame, const uint8_t *name, size_t name_length,
                     const uint8_t *value, size_t value_length, uint8_t flags, void *user_data) {
  (void)flags;
  (void)user_data;
  request_state_t *request = get_request(session, frame->hd.stream_id);
  if (!request)
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  if (name_length == 7 && memcmp(name, ":method", 7) == 0) {
    SAFE_FREE(request->method);
    request->method = copy_text(value, value_length);
  } else if (name_length == 5 && memcmp(name, ":path", 5) == 0) {
    SAFE_FREE(request->path);
    request->path = copy_text(value, value_length);
  } else if (request->header_count < HTTP2_MAX_HEADERS) {
    char *header_name = copy_text(name, name_length);
    char *header_value = copy_text(value, value_length);
    if (!header_name || !header_value) {
      SAFE_FREE(header_name);
      SAFE_FREE(header_value);
      return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
    }
    request->header_names[request->header_count] = header_name;
    request->header_values[request->header_count] = header_value;
    request->headers[request->header_count] = (http2_header_t){header_name, header_value};
    request->header_count++;
  }
  return (request->method || request->path) ? 0 : NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
}

static int on_data(nghttp2_session *session, uint8_t flags, int32_t stream_id, const uint8_t *data, size_t length,
                   void *user_data) {
  (void)flags;
  (void)user_data;
  request_state_t *request = get_request(session, stream_id);
  http2_connection_t *connection = user_data;
  if (!request || request->body_length > connection->max_body_length ||
      length > connection->max_body_length - request->body_length)
    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
  size_t required = request->body_length + length;
  if (request->body_capacity < required) {
    size_t capacity = request->body_capacity ? request->body_capacity : 1024;
    while (capacity < required) {
      if (capacity > SIZE_MAX / 2) {
        capacity = required;
        break;
      }
      capacity *= 2;
    }
    char *body = SAFE_REALLOC(request->body, capacity, char *);
    if (!body)
      return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
    request->body = body;
    request->body_capacity = capacity;
  }
  memcpy(request->body + request->body_length, data, length);
  request->body_length += length;
  return 0;
}

static int on_frame_recv(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
  http2_connection_t *connection = user_data;
  if (!(frame->hd.flags & NGHTTP2_FLAG_END_STREAM) ||
      (frame->hd.type != NGHTTP2_HEADERS && frame->hd.type != NGHTTP2_DATA))
    return 0;
  request_state_t *request = get_request(session, frame->hd.stream_id);
  if (!request || request->dispatched || !request->method || !request->path)
    return NGHTTP2_ERR_PROTO;
  request->dispatched = true;
  http2_request_view_t view = {.method = request->method,
                               .path = request->path,
                               .body = request->body,
                               .body_length = request->body_length,
                               .stream_id = request->stream_id,
                               .headers = request->headers,
                               .header_count = request->header_count};
  char *query = strchr(request->path, '?');
  if (query) {
    *query = 0;
    view.query = query + 1;
  }
  return connection->on_request(connection, &view, connection->user_data) == ASCIICHAT_OK
             ? 0
             : NGHTTP2_ERR_CALLBACK_FAILURE;
}

static int on_stream_close(nghttp2_session *session, int32_t stream_id, uint32_t error_code, void *user_data) {
  (void)error_code;
  (void)user_data;
  request_state_t *request = get_request(session, stream_id);
  if (!request)
    return 0;
  SAFE_FREE(request->method);
  SAFE_FREE(request->path);
  SAFE_FREE(request->body);
  for (size_t i = 0; i < request->header_count; ++i) {
    SAFE_FREE(request->header_names[i]);
    SAFE_FREE(request->header_values[i]);
  }
  SAFE_FREE(request);
  return 0;
}

static ssize_t read_response(nghttp2_session *session, int32_t stream_id, uint8_t *buffer, size_t length,
                             uint32_t *flags, nghttp2_data_source *source, void *user_data) {
  (void)session;
  (void)stream_id;
  (void)user_data;
  response_state_t *response = source->ptr;
  size_t remaining = response->length - response->offset;
  size_t copied = remaining < length ? remaining : length;
  if (copied)
    memcpy(buffer, response->data + response->offset, copied);
  response->offset += copied;
  if (response->offset == response->length) {
    *flags |= NGHTTP2_DATA_FLAG_EOF;
    SAFE_FREE(response->data);
    SAFE_FREE(response);
  }
  return (ssize_t)copied;
}

asciichat_error_t http2_connection_create(http2_connection_t **out, http2_request_fn on_request, void *user_data,
                                          size_t max_request_body_length, size_t max_response_body_length) {
  if (!out || !on_request || max_request_body_length == 0 || max_response_body_length == 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing HTTP/2 request callback");
  http2_connection_t *connection = SAFE_CALLOC(1, sizeof(*connection), http2_connection_t *);
  nghttp2_session_callbacks *callbacks = NULL;
  nghttp2_option *options = NULL;
  if (nghttp2_session_callbacks_new(&callbacks) != 0 || nghttp2_option_new(&options) != 0) {
    nghttp2_session_callbacks_del(callbacks);
    nghttp2_option_del(options);
    SAFE_FREE(connection);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot initialize HTTP/2 callbacks");
  }
  nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers);
  nghttp2_session_callbacks_set_on_header_callback(callbacks, on_header);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, on_data);
  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv);
  nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, on_stream_close);
  if (nghttp2_session_server_new2(&connection->session, callbacks, connection, options) != 0) {
    nghttp2_session_callbacks_del(callbacks);
    nghttp2_option_del(options);
    SAFE_FREE(connection);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot initialize HTTP/2 session");
  }
  nghttp2_session_callbacks_del(callbacks);
  nghttp2_option_del(options);
  nghttp2_settings_entry settings = {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, HTTP2_MAX_CONCURRENT_STREAMS};
  if (nghttp2_submit_settings(connection->session, NGHTTP2_FLAG_NONE, &settings, 1) != 0) {
    nghttp2_session_del(connection->session);
    SAFE_FREE(connection);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot configure HTTP/2 stream limit");
  }
  connection->on_request = on_request;
  connection->user_data = user_data;
  connection->max_body_length = max_request_body_length;
  connection->max_response_body_length = max_response_body_length;
  *out = connection;
  return ASCIICHAT_OK;
}

void http2_connection_destroy(http2_connection_t **connection) {
  if (!connection || !*connection)
    return;
  nghttp2_session_del((*connection)->session);
  SAFE_FREE(*connection);
  *connection = NULL;
}

asciichat_error_t http2_connection_receive(http2_connection_t *connection, const void *data, size_t length) {
  if (!connection || (!data && length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/2 input");
  if (nghttp2_session_mem_recv(connection->session, data, length) < 0)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid HTTP/2 frame sequence");
  return ASCIICHAT_OK;
}

asciichat_error_t http2_connection_next_output(http2_connection_t *connection, const uint8_t **data, size_t *length) {
  if (!connection || !data || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/2 output request");
  ssize_t output_length = nghttp2_session_mem_send(connection->session, &connection->output);
  if (output_length < 0)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot serialize HTTP/2 output");
  connection->output_length = (size_t)output_length;
  *data = connection->output;
  *length = connection->output_length;
  return ASCIICHAT_OK;
}

asciichat_error_t http2_connection_submit_response(http2_connection_t *connection, int32_t stream_id, unsigned status,
                                                   const http_header_t *headers, size_t header_count, const void *body,
                                                   size_t body_length) {
  if (!connection || status < 100 || status > 599 || body_length > connection->max_response_body_length ||
      (!body && body_length))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/2 response");
  if (header_count > HTTP2_MAX_HEADERS || (header_count && !headers))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/2 response headers");
  char *status_text = SAFE_MALLOC(4, char *);
  if (!status_text)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP/2 response status");
  safe_snprintf(status_text, 4, "%u", status);
  response_state_t *response = SAFE_CALLOC(1, sizeof(*response), response_state_t *);
  if (!response) {
    SAFE_FREE(status_text);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP/2 response state");
  }
  response->length = body_length;
  if (body_length) {
    response->data = SAFE_MALLOC(body_length, char *);
    if (!response->data) {
      SAFE_FREE(response);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP/2 response body");
    }
    memcpy(response->data, body, body_length);
  }
  nghttp2_nv *fields = SAFE_CALLOC(header_count + 1, sizeof(*fields), nghttp2_nv *);
  if (!fields) {
    SAFE_FREE(status_text);
    SAFE_FREE(response->data);
    SAFE_FREE(response);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP/2 response headers");
  }
  fields[0] = (nghttp2_nv){(uint8_t *)":status", (uint8_t *)status_text, 7, strlen(status_text), NGHTTP2_NV_FLAG_NONE};
  for (size_t i = 0; i < header_count; ++i) {
    if (!headers[i].name || !headers[i].value) {
      SAFE_FREE(fields);
      SAFE_FREE(status_text);
      SAFE_FREE(response->data);
      SAFE_FREE(response);
      return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/2 response header");
    }
    fields[i + 1] = (nghttp2_nv){(uint8_t *)headers[i].name, (uint8_t *)headers[i].value, strlen(headers[i].name),
                                 strlen(headers[i].value), NGHTTP2_NV_FLAG_NONE};
  }
  nghttp2_data_provider provider = {.source = {.ptr = response}, .read_callback = read_response};
  int result =
      nghttp2_submit_response(connection->session, stream_id, fields, header_count + 1, body_length ? &provider : NULL);
  SAFE_FREE(fields);
  SAFE_FREE(status_text);
  if (!body_length)
    SAFE_FREE(response);
  if (result != 0) {
    if (body_length) {
      SAFE_FREE(response->data);
      SAFE_FREE(response);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot submit HTTP/2 response");
  }
  return ASCIICHAT_OK;
}
#endif
