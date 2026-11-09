#include <ascii-chat/network/http/server.h>
#include <ascii-chat/network/acip/quic.h>
#include "server_internal.h"
#include "http2.h"
#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/platform/string.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/network/rate_limit/rate_limit.h>
#include <ascii-chat/util/time.h>
#include <uv.h>
#include <string.h>

#define HTTP_READ_CAPACITY (1024u * 1024u)
#define HTTP_HEADER_CAPACITY 64u

static const char *http_protocol_name(http_protocol_t protocol) {
  switch (protocol) {
  case HTTP_PROTOCOL_10:
    return "1.0";
  case HTTP_PROTOCOL_11:
    return "1.1";
  case HTTP_PROTOCOL_2:
    return "2";
  case HTTP_PROTOCOL_3:
    return "3";
  default:
    return "unknown";
  }
}

typedef struct http_client http_client_t;
static asciichat_error_t http2_request_cb(http2_connection_t *connection, const http2_request_view_t *view,
                                          void *user_data);
static asciichat_error_t http2_flush(http_client_t *client);

struct http_server {
  http_server_config_t config;
  uv_loop_t loop;
  uv_tcp_t listener;
  uv_async_t stop_async;
  uv_async_t wake_async;
  bool running;
  bool adopted_socket;
  bool thread_started;
  asciichat_thread_t thread;
  size_t connections;
  atomic_t inflight_requests;
  http_client_t *clients;
  rate_limiter_t *rate_limiter;
  rate_limit_config_t request_rate_limit;
  mutex_t response_mutex;
  http_response_t *retained_responses;
};
struct http_request {
  http_request_info_t info;
  http_header_t headers[HTTP_HEADER_CAPACITY];
  char *storage;
  size_t storage_length;
  size_t body_offset;
  size_t body_length;
  bool cancelled;
  bool inflight_accounted;
};
struct http_response {
  uv_stream_t *stream;
  http_client_t *client;
  int32_t http2_stream_id;
  http_protocol_t protocol;
  unsigned status;
  http_header_t headers[HTTP_HEADER_CAPACITY];
  size_t header_count;
  char *body;
  size_t body_length;
  size_t max_body_length;
  bool finished;
  bool deferred;
  bool retained;
  bool cancelled;
  http_server_t *server;
  http_response_t *next_retained;
  http3_submit_response_fn http3_submit;
  void *http3_context;
};
struct http_client {
  uv_tcp_t tcp;
  http_server_t *server;
  char *buffer;
  size_t length;
  http_client_t *next;
  http_client_t *previous;
  http2_connection_t *http2;
  bool http2_mode;
  bool admitted;
  char peer_address[64];
};
static void stop_async_cb(uv_async_t *async);
static void wake_async_cb(uv_async_t *async) {
  http_server_t *s = async->data;
  if (s->config.on_wakeup)
    s->config.on_wakeup(s->config.user_data);
}
static size_t http_max_inflight(const http_server_t *server) {
  if (server->config.max_inflight_requests)
    return server->config.max_inflight_requests;
  return server->config.max_connections > SIZE_MAX / 4 ? SIZE_MAX : server->config.max_connections * 4;
}
static bool http_request_admit(http_server_t *server) {
  int current = atomic_fetch_add_int(&server->inflight_requests, 1);
  size_t max = http_max_inflight(server);
  if (current < 0 || (size_t)current >= max) {
    atomic_fetch_sub_int(&server->inflight_requests, 1);
    return false;
  }
  return true;
}
static void http_request_release(http_server_t *server) {
  if (server)
    atomic_fetch_sub_int(&server->inflight_requests, 1);
}

static void client_freed(uv_handle_t *handle) {
  http_client_t *c = handle->data;
  mutex_lock(&c->server->response_mutex);
  for (http_response_t *response = c->server->retained_responses; response; response = response->next_retained) {
    if (response->client == c && !response->cancelled) {
      response->cancelled = true;
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_CANCELLED, 1);
    }
  }
  mutex_unlock(&c->server->response_mutex);
  if (c->previous)
    c->previous->next = c->next;
  else
    c->server->clients = c->next;
  if (c->next)
    c->next->previous = c->previous;
  if (c->admitted && c->server->connections)
    c->server->connections--;
  if (c->admitted)
    stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_CONNECTIONS_ACTIVE, c->server->connections);
  if (c->admitted && c->server->config.on_connection)
    c->server->config.on_connection("", false, c->server->config.user_data);
  http2_connection_destroy(&c->http2);
  SAFE_FREE(c->buffer);
  SAFE_FREE(c);
}
static void client_close(http_client_t *c) {
  if (!uv_is_closing((uv_handle_t *)&c->tcp))
    uv_close((uv_handle_t *)&c->tcp, client_freed);
}
static void client_peer_address(http_client_t *c) {
  struct sockaddr_storage address;
  int length = sizeof(address);
  if (uv_tcp_getpeername(&c->tcp, (struct sockaddr *)&address, &length) != 0) {
    safe_snprintf(c->peer_address, sizeof(c->peer_address), "unknown");
    return;
  }
  if (address.ss_family == AF_INET)
    uv_ip4_name((const struct sockaddr_in *)&address, c->peer_address, sizeof(c->peer_address));
  else if (address.ss_family == AF_INET6)
    uv_ip6_name((const struct sockaddr_in6 *)&address, c->peer_address, sizeof(c->peer_address));
  else
    safe_snprintf(c->peer_address, sizeof(c->peer_address), "unknown");
}
static void alloc_buffer(uv_handle_t *h, size_t n, uv_buf_t *b) {
  (void)h;
  b->base = SAFE_MALLOC(n, char *);
  b->len = (unsigned)n;
}
static bool parse_request(http_client_t *c, http_request_t *r) {
  char *end = strstr(c->buffer, "\r\n\r\n");
  if (!end)
    return false;
  size_t head = (size_t)(end - c->buffer) + 4;
  size_t body = 0;
  for (char *scan = strstr(c->buffer, "\r\n") + 2; scan < end;) {
    char *next = strstr(scan, "\r\n");
    if (!next)
      return false;
    if (platform_strncasecmp(scan, "Content-Length:", 15) == 0)
      body = (size_t)strtoull(scan + 15, NULL, 10);
    scan = next + 2;
  }
  if (body > c->server->config.max_request_body_bytes || c->length < head + body)
    return false;
  char *line = c->buffer;
  char *nl = strstr(line, "\r\n");
  if (!nl)
    return false;
  *nl = 0;
  char *a = strchr(line, ' '), *b = a ? strchr(a + 1, ' ') : NULL;
  if (!a || !b)
    return false;
  *a = 0;
  *b = 0;
  r->storage = c->buffer;
  r->storage_length = c->length;
  r->info.method = line;
  r->info.path = a + 1;
  if (strcmp(b + 1, "HTTP/1.0") == 0)
    r->info.protocol = HTTP_PROTOCOL_10;
  else if (strcmp(b + 1, "HTTP/1.1") == 0)
    r->info.protocol = HTTP_PROTOCOL_11;
  else
    return false;
  char *q = strchr(r->info.path, '?');
  if (q) {
    *q = 0;
    r->info.query = q + 1;
  }
  char *p = nl + 2;
  r->info.header_count = 0;
  while (p < end && *p) {
    char *next = strstr(p, "\r\n");
    if (!next)
      return false;
    *next = 0;
    char *colon = strchr(p, ':');
    if (colon && r->info.header_count < HTTP_HEADER_CAPACITY) {
      *colon = 0;
      while (colon[1] == ' ')
        colon++;
      r->headers[r->info.header_count] = (http_header_t){p, colon + 1};
      r->info.header_count++;
    }
    p = next + 2;
  }
  r->body_offset = head;
  r->body_length = body;
  r->info.headers = r->headers;
  r->info.peer_address = c->peer_address;
  r->info.user_data = c->server->config.user_data;
  r->info.user_data = c->server->config.user_data;
  return true;
}
typedef struct {
  char *bytes;
  size_t length;
  uv_stream_t *stream;
  bool close_on_done;
} http_write_t;

static void write_done(uv_write_t *req, int status) {
  http_write_t *write = req->data;
  if (write->close_on_done && !uv_is_closing((uv_handle_t *)write->stream))
    uv_close((uv_handle_t *)write->stream, client_freed);
  SAFE_FREE(write->bytes);
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_BYTES_SENT, write->length);
  SAFE_FREE(write);
  SAFE_FREE(req);
  (void)status;
}
static asciichat_error_t http2_flush(http_client_t *client) {
  const uint8_t *data = NULL;
  size_t length = 0;
  asciichat_error_t result = http2_connection_next_output(client->http2, &data, &length);
  if (result != ASCIICHAT_OK || !length)
    return result;
  uv_write_t *request = SAFE_CALLOC(1, sizeof(*request), uv_write_t *);
  http_write_t *write = SAFE_CALLOC(1, sizeof(*write), http_write_t *);
  if (!request || !write) {
    SAFE_FREE(request);
    SAFE_FREE(write);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP/2 write state");
  }
  write->bytes = SAFE_MALLOC(length, char *);
  if (!write->bytes) {
    SAFE_FREE(request);
    SAFE_FREE(write);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP/2 output");
  }
  memcpy(write->bytes, data, length);
  write->length = length;
  write->stream = (uv_stream_t *)&client->tcp;
  write->close_on_done = false;
  request->data = write;
  uv_buf_t buffer = uv_buf_init(write->bytes, (unsigned)length);
  if (uv_write(request, write->stream, &buffer, 1, write_done) != 0) {
    SAFE_FREE(write->bytes);
    SAFE_FREE(write);
    SAFE_FREE(request);
    return SET_ERRNO(ERROR_NETWORK, "Cannot write HTTP/2 response");
  }
  return ASCIICHAT_OK;
}
static const char *reason(unsigned s) {
  switch (s) {
  case 200:
    return "OK";
  case 204:
    return "No Content";
  case 400:
    return "Bad Request";
  case 404:
    return "Not Found";
  case 413:
    return "Payload Too Large";
  case 429:
    return "Too Many Requests";
  case 500:
    return "Internal Server Error";
  default:
    return "";
  }
}
asciichat_error_t http_response_set_status(http_response_t *r, unsigned s) {
  if (!r || s < 100 || s > 599)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP status");
  r->status = s;
  return ASCIICHAT_OK;
}
asciichat_error_t http_response_retain(http_response_t *r, http_response_t **retained) {
  if (!r || !retained || !r->server || r->finished || r->retained)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP response cannot be retained");
  http_response_t *copy = SAFE_CALLOC(1, sizeof(*copy), http_response_t *);
  if (!copy)
    return SET_ERRNO(ERROR_MEMORY, "Cannot retain HTTP response");
  copy->stream = r->stream;
  copy->client = r->client;
  copy->http2_stream_id = r->http2_stream_id;
  copy->protocol = r->protocol;
  copy->max_body_length = r->max_body_length;
  copy->http3_submit = r->http3_submit;
  copy->http3_context = r->http3_context;
  copy->server = r->server;
  copy->inflight_accounted = r->inflight_accounted;
  copy->status = r->status;
  copy->header_count = r->header_count;
  memcpy(copy->headers, r->headers, r->header_count * sizeof(r->headers[0]));
  if (r->body_length) {
    copy->body = SAFE_MALLOC(r->body_length, char *);
    if (!copy->body) {
      SAFE_FREE(copy);
      return SET_ERRNO(ERROR_MEMORY, "Cannot retain HTTP response body");
    }
    memcpy(copy->body, r->body, r->body_length);
    copy->body_length = r->body_length;
  }
  copy->retained = true;
  mutex_lock(&r->server->response_mutex);
  copy->next_retained = r->server->retained_responses;
  r->server->retained_responses = copy;
  mutex_unlock(&r->server->response_mutex);
  r->deferred = true;
  r->retained = true;
  r->inflight_accounted = false;
  *retained = copy;
  return ASCIICHAT_OK;
}
void http_response_release(http_response_t **response) {
  if (!response || !*response)
    return;
  if ((*response)->retained) {
    http_server_t *server = (*response)->server;
    if ((*response)->inflight_accounted) {
      http_request_release(server);
      (*response)->inflight_accounted = false;
    }
    if (server) {
      mutex_lock(&server->response_mutex);
      http_response_t **cursor = &server->retained_responses;
      while (*cursor && *cursor != *response)
        cursor = &(*cursor)->next_retained;
      if (*cursor == *response)
        *cursor = (*response)->next_retained;
      mutex_unlock(&server->response_mutex);
    }
    SAFE_FREE((*response)->body);
    SAFE_FREE(*response);
  }
  *response = NULL;
}
asciichat_error_t http_response_set_header(http_response_t *r, const char *n, const char *v) {
  if (!r || !n || !v || r->header_count >= HTTP_HEADER_CAPACITY)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP response header");
  r->headers[r->header_count++] = (http_header_t){n, v};
  return ASCIICHAT_OK;
}
asciichat_error_t http_response_write(http_response_t *r, const void *d, size_t n) {
  if (!r || (!d && n) || r->finished)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP response is not writable");
  if (n > r->max_body_length || r->body_length > r->max_body_length - n)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "HTTP response body exceeds configured limit");
  char *p = SAFE_REALLOC(r->body, r->body_length + n + 1, char *);
  if (!p)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP response body");
  r->body = p;
  memcpy(p + r->body_length, d, n);
  r->body_length += n;
  p[r->body_length] = 0;
  return ASCIICHAT_OK;
}
asciichat_error_t http_response_finish(http_response_t *r) {
  if (!r || r->finished)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP response already finished");
  r->finished = true;
  asciichat_error_t result;
  if (r->http3_submit)
    result = r->http3_submit(r->http3_context, r->status, r->headers, r->header_count, r->body, r->body_length);
  else if (r->client && r->client->http2_mode) {
    result = http2_connection_submit_response(r->client->http2, r->http2_stream_id, r->status, r->headers,
                                              r->header_count, r->body, r->body_length);
    if (result == ASCIICHAT_OK)
      result = http2_flush(r->client);
  } else {
    size_t cap = 256 + r->body_length;
    for (size_t i = 0; i < r->header_count; i++)
      cap += strlen(r->headers[i].name) + strlen(r->headers[i].value) + 4;
    char *out = SAFE_MALLOC(cap, char *);
    if (!out)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP response");
    const char *version = r->protocol == HTTP_PROTOCOL_10 ? "HTTP/1.0" : "HTTP/1.1";
    int n = safe_snprintf(out, cap, "%s %u %s\r\nContent-Length: %zu\r\nConnection: close\r\n", version, r->status,
                          reason(r->status), r->body_length);
    for (size_t i = 0; i < r->header_count; i++)
      n += safe_snprintf(out + n, cap - (size_t)n, "%s: %s\r\n", r->headers[i].name, r->headers[i].value);
    n += safe_snprintf(out + n, cap - (size_t)n, "\r\n");
    if (r->body_length)
      memcpy(out + n, r->body, r->body_length);
    n += (int)r->body_length;
    uv_write_t *w = SAFE_CALLOC(1, sizeof(*w), uv_write_t *);
    http_write_t *write = SAFE_CALLOC(1, sizeof(*write), http_write_t *);
    if (!write) {
      SAFE_FREE(out);
      SAFE_FREE(w);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP write state");
    }
    write->bytes = out;
    write->length = (size_t)n;
    write->stream = r->stream;
    write->close_on_done = true;
    w->data = write;
    uv_buf_t b = uv_buf_init(out, n);
    result = uv_write(w, r->stream, &b, 1, write_done) == 0 ? ASCIICHAT_OK
                                                            : SET_ERRNO(ERROR_NETWORK, "Cannot write HTTP response");
  }
  if (r->inflight_accounted && !r->deferred) {
    http_request_release(r->server);
    r->inflight_accounted = false;
  }
  return result;
}

asciichat_error_t http_server_dispatch_http3(http_server_t *server, const char *method, const char *path,
                                             const http_header_t *headers, size_t header_count, const void *body,
                                             size_t body_length, const char *peer_address,
                                             http3_submit_response_fn submit, void *submit_context) {
  if (!server || !method || !path || (header_count && !headers) || (!body && body_length) || !submit ||
      body_length > server->config.max_request_body_bytes)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP/3 request");
  char path_copy[2048];
  if (strlen(path) >= sizeof(path_copy))
    return SET_ERRNO(ERROR_NETWORK_SIZE, "HTTP/3 request path is too long");
  platform_strncpy(path_copy, sizeof(path_copy), path, sizeof(path_copy) - 1);
  char *query = strchr(path_copy, '?');
  if (query)
    *query++ = 0;
  http_request_t request = {0};
  request.info = (http_request_info_t){.method = method,
                                       .path = path_copy,
                                       .query = query,
                                       .protocol = HTTP_PROTOCOL_3,
                                       .headers = headers,
                                       .header_count = header_count,
                                       .peer_address = peer_address,
                                       .user_data = server->config.user_data};
  request.storage = (char *)body;
  request.body_length = body_length;
  const char *peer = peer_address ? peer_address : "unknown";
  bool allowed = false;
  if (rate_limiter_check(server->rate_limiter, peer, RATE_EVENT_HTTP_REQUEST, &server->request_rate_limit, &allowed) !=
          ASCIICHAT_OK ||
      !allowed) {
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
    http_response_t limited = {.server = server,
                               .status = 429,
                               .max_body_length = server->config.max_request_body_bytes,
                               .http3_submit = submit,
                               .http3_context = submit_context};
    return http_response_finish(&limited);
  }
  rate_limiter_record(server->rate_limiter, peer, RATE_EVENT_HTTP_REQUEST);
  if (!http_request_admit(server)) {
    http_response_t limited = {.server = server,
                               .status = 429,
                               .max_body_length = server->config.max_request_body_bytes,
                               .http3_submit = submit,
                               .http3_context = submit_context};
    return http_response_finish(&limited);
  }
  http_response_t response = {.server = server,
                              .status = 200,
                              .max_body_length = server->config.max_request_body_bytes,
                              .inflight_accounted = true,
                              .http3_submit = submit,
                              .http3_context = submit_context};
  ASCII_OTEL_SCOPE(HTTP);
  ASCII_OTEL_ATTR_STRING("http.method", method);
  ASCII_OTEL_ATTR_STRING("http.route", request.info.path);
  ASCII_OTEL_ATTR_STRING("http.protocol", "3");
  ASCII_OTEL_ATTR_STRING("http.backend", "quic-ngtcp2-nghttp3");
  server->config.on_request(&request, &response, server->config.user_data);
  if (!response.finished && !response.deferred) {
    asciichat_error_t result = http_response_finish(&response);
    ASCII_OTEL_ATTR_INT("http.status", response.status);
    ASCII_OTEL_ATTR_INT("http.response_bytes", (int64_t)response.body_length);
    SAFE_FREE(response.body);
    return result;
  }
  return ASCIICHAT_OK;
}
const http_request_info_t *http_request_info(const http_request_t *r) {
  return r ? &r->info : NULL;
}
const void *http_request_body(const http_request_t *r, size_t *n) {
  if (n)
    *n = r ? r->body_length : 0;
  return r && r->body_length ? r->storage + r->body_offset : NULL;
}

size_t http_server_max_request_body(const http_server_t *server) {
  return server ? server->config.max_request_body_bytes : 0;
}
bool http_request_is_cancelled(const http_request_t *r) {
  return !r || r->cancelled;
}
bool http_response_is_cancelled(const http_response_t *r) {
  return !r || r->cancelled;
}
asciichat_error_t http_request_cancel(http_request_t *r) {
  if (!r)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing HTTP request");
  r->cancelled = true;
  return ASCIICHAT_OK;
}
static asciichat_error_t http2_request_cb(http2_connection_t *connection, const http2_request_view_t *view,
                                          void *user_data) {
  http_client_t *client = user_data;
  uint64_t request_start = time_get_ns();
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REQUESTS, 1);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 1);
  http_request_t request = {0};
  ASCII_OTEL_SCOPE(HTTP);
  ASCII_OTEL_ATTR_STRING("http.method", view->method);
  ASCII_OTEL_ATTR_STRING("http.route", view->path);
  ASCII_OTEL_ATTR_STRING("http.protocol", "2");
  ASCII_OTEL_ATTR_STRING("http.backend", "libuv-nghttp2");
  request.info.method = view->method;
  request.info.path = view->path;
  request.info.query = view->query;
  request.info.protocol = HTTP_PROTOCOL_2;
  request.info.header_count = view->header_count < HTTP_HEADER_CAPACITY ? view->header_count : HTTP_HEADER_CAPACITY;
  for (size_t i = 0; i < request.info.header_count; ++i)
    request.headers[i] = (http_header_t){view->headers[i].name, view->headers[i].value};
  request.info.headers = request.headers;
  request.info.peer_address = client->peer_address;
  request.info.user_data = client->server->config.user_data;
  request.storage = (char *)view->body;
  request.body_offset = 0;
  request.body_length = view->body_length;
  request.info.user_data = client->server->config.user_data;
  bool allowed = false;
  if (rate_limiter_check(client->server->rate_limiter, client->peer_address, RATE_EVENT_HTTP_REQUEST,
                         &client->server->request_rate_limit, &allowed) != ASCIICHAT_OK ||
      !allowed) {
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
    http_response_t limited = {.server = client->server,
                               .stream = (uv_stream_t *)&client->tcp,
                               .client = client,
                               .http2_stream_id = view->stream_id,
                               .protocol = HTTP_PROTOCOL_2,
                               .max_body_length = client->server->config.max_request_body_bytes,
                               .status = 429};
    asciichat_error_t result = http_response_finish(&limited);
    SAFE_FREE(limited.body);
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_HTTP_REQUEST, time_get_ns() - request_start);
    stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 0);
    return result;
  }
  rate_limiter_record(client->server->rate_limiter, client->peer_address, RATE_EVENT_HTTP_REQUEST);
  if (!http_request_admit(client->server)) {
    http_response_t limited = {.server = client->server,
                               .stream = (uv_stream_t *)&client->tcp,
                               .client = client,
                               .http2_stream_id = view->stream_id,
                               .protocol = HTTP_PROTOCOL_2,
                               .max_body_length = client->server->config.max_request_body_bytes,
                               .status = 429};
    asciichat_error_t result = http_response_finish(&limited);
    SAFE_FREE(limited.body);
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
    return result;
  }
  http_response_t response = {.server = client->server,
                              .stream = (uv_stream_t *)&client->tcp,
                              .client = client,
                              .http2_stream_id = view->stream_id,
                              .protocol = HTTP_PROTOCOL_2,
                              .max_body_length = client->server->config.max_request_body_bytes,
                              .inflight_accounted = true,
                              .status = 200};
  client->server->config.on_request(&request, &response, client->server->config.user_data);
  if (!response.finished && !response.deferred) {
    asciichat_error_t result = http_response_finish(&response);
    ASCII_OTEL_ATTR_INT("http.status", response.status);
    ASCII_OTEL_ATTR_INT("http.response_bytes", (int64_t)response.body_length);
    SAFE_FREE(response.body);
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_HTTP_REQUEST, time_get_ns() - request_start);
    stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 0);
    return result;
  }
  stats_duration_record(stats_runtime_scope(), STATS_DURATION_HTTP_REQUEST, time_get_ns() - request_start);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 0);
  (void)connection;
  return ASCIICHAT_OK;
}
static void read_cb(uv_stream_t *s, ssize_t n, const uv_buf_t *b) {
  http_client_t *c = s->data;
  char *base = (char *)b->base;
  if (n <= 0) {
    SAFE_FREE(base);
    client_close(c);
    return;
  }
  if (c->length + (size_t)n >= HTTP_READ_CAPACITY) {
    SAFE_FREE(base);
    client_close(c);
    return;
  }
  memcpy(c->buffer + c->length, base, (size_t)n);
  c->length += (size_t)n;
  c->buffer[c->length] = 0;
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_BYTES_RECEIVED, (size_t)n);
  SAFE_FREE(base);
  static const char h2_preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
  if (!c->http2_mode && c->server->config.enable_http2 && c->length >= sizeof(h2_preface) - 1 &&
      memcmp(c->buffer, h2_preface, sizeof(h2_preface) - 1) == 0) {
    c->http2_mode = true;
    if (http2_connection_create(&c->http2, http2_request_cb, c, c->server->config.max_request_body_bytes) !=
            ASCIICHAT_OK ||
        http2_connection_receive(c->http2, c->buffer + sizeof(h2_preface) - 1, c->length - (sizeof(h2_preface) - 1)) !=
            ASCIICHAT_OK) {
      client_close(c);
      return;
    }
    c->length = 0;
    if (http2_flush(c) != ASCIICHAT_OK)
      client_close(c);
    return;
  }
  if (c->http2_mode) {
    if (http2_connection_receive(c->http2, c->buffer, c->length) != ASCIICHAT_OK || http2_flush(c) != ASCIICHAT_OK)
      client_close(c);
    c->length = 0;
    return;
  }
  http_request_t r = {0};
  if (!parse_request(c, &r))
    return;
  bool allowed = false;
  if (rate_limiter_check(c->server->rate_limiter, c->peer_address, RATE_EVENT_HTTP_REQUEST,
                         &c->server->request_rate_limit, &allowed) != ASCIICHAT_OK ||
      !allowed) {
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
    http_response_t limited = {.server = c->server,
                               .stream = s,
                               .protocol = r.info.protocol,
                               .max_body_length = c->server->config.max_request_body_bytes,
                               .status = 429};
    http_response_finish(&limited);
    uv_read_stop(s);
    return;
  }
  rate_limiter_record(c->server->rate_limiter, c->peer_address, RATE_EVENT_HTTP_REQUEST);
  if (!http_request_admit(c->server)) {
    http_response_t limited = {.server = c->server,
                               .stream = s,
                               .protocol = r.info.protocol,
                               .max_body_length = c->server->config.max_request_body_bytes,
                               .status = 429};
    http_response_finish(&limited);
    uv_read_stop(s);
    return;
  }
  http_response_t resp = {.server = c->server,
                          .stream = s,
                          .protocol = r.info.protocol,
                          .max_body_length = c->server->config.max_request_body_bytes,
                          .inflight_accounted = true,
                          .status = 200};
  uint64_t request_start = time_get_ns();
  ASCII_OTEL_SCOPE(HTTP);
  ASCII_OTEL_ATTR_STRING("http.method", r.info.method);
  ASCII_OTEL_ATTR_STRING("http.route", r.info.path);
  ASCII_OTEL_ATTR_STRING("http.protocol", http_protocol_name(r.info.protocol));
  ASCII_OTEL_ATTR_STRING("http.backend", "libuv");
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REQUESTS, 1);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 1);
  c->server->config.on_request(&r, &resp, c->server->config.user_data);
  if (!resp.finished && !resp.deferred)
    http_response_finish(&resp);
  ASCII_OTEL_ATTR_INT("http.status", resp.status);
  ASCII_OTEL_ATTR_INT("http.response_bytes", (int64_t)resp.body_length);
  SAFE_FREE(resp.body);
  stats_duration_record(stats_runtime_scope(), STATS_DURATION_HTTP_REQUEST, time_get_ns() - request_start);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 0);
  uv_read_stop(s);
}
static void connection_cb(uv_stream_t *l, int status) {
  if (status < 0)
    return;
  http_server_t *s = l->data;
  if (s->connections >= s->config.max_connections)
    return;
  http_client_t *c = SAFE_CALLOC(1, sizeof(*c), http_client_t *);
  c->server = s;
  uv_tcp_init(&s->loop, &c->tcp);
  c->tcp.data = c;
  if (uv_accept(l, (uv_stream_t *)&c->tcp) == 0) {
    client_peer_address(c);
    bool allowed = false;
    if (rate_limiter_check(s->rate_limiter, c->peer_address, RATE_EVENT_CONNECTION,
                           &DEFAULT_RATE_LIMITS[RATE_EVENT_CONNECTION], &allowed) != ASCIICHAT_OK ||
        !allowed) {
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
      client_close(c);
      return;
    }
    rate_limiter_record(s->rate_limiter, c->peer_address, RATE_EVENT_CONNECTION);
    c->buffer = SAFE_CALLOC(HTTP_READ_CAPACITY, 1, char *);
    if (!c->buffer) {
      client_close(c);
      return;
    }
    c->next = s->clients;
    if (s->clients)
      s->clients->previous = c;
    s->clients = c;
    c->admitted = true;
    s->connections++;
    stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_CONNECTIONS_ACTIVE, s->connections);
    if (s->config.on_connection)
      s->config.on_connection(c->peer_address, true, s->config.user_data);
    uv_read_start((uv_stream_t *)&c->tcp, alloc_buffer, read_cb);
  } else
    client_close(c);
}
static void stop_async_cb(uv_async_t *async) {
  http_server_t *s = async->data;
  if (!uv_is_closing((uv_handle_t *)&s->listener))
    uv_close((uv_handle_t *)&s->listener, NULL);
  for (http_client_t *c = s->clients; c; c = c->next)
    client_close(c);
  uv_close((uv_handle_t *)async, NULL);
  if (!uv_is_closing((uv_handle_t *)&s->wake_async))
    uv_close((uv_handle_t *)&s->wake_async, NULL);
  uv_stop(&s->loop);
}
static void *http_loop_thread(void *arg) {
  http_server_t *s = arg;
  uv_run(&s->loop, UV_RUN_DEFAULT);
  uv_run(&s->loop, UV_RUN_DEFAULT);
  return NULL;
}
asciichat_error_t http_server_init(http_server_t **out, const http_server_config_t *cfg) {
  if (!out || !cfg || !cfg->on_request || (!cfg->enable_http1 && !cfg->enable_http2))
    return SET_ERRNO(ERROR_INVALID_PARAM, "At least one HTTP protocol and a request handler are required");
  if (cfg->max_connections == 0 || cfg->max_request_body_bytes == 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "HTTP limits must be non-zero");
  if ((cfg->enable_http3 && !cfg->http3_endpoint) || cfg->tls_certificate || cfg->tls_private_key)
    return SET_ERRNO(ERROR_INVALID_PARAM, "HTTP/3 requires a pre-opened endpoint and TLS is endpoint-owned");
  http_server_t *s = SAFE_CALLOC(1, sizeof(*s), http_server_t *);
  if (mutex_init(&s->response_mutex, "http_responses") != 0) {
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot initialize HTTP response synchronization");
  }
  s->rate_limiter = rate_limiter_create_memory();
  if (!s->rate_limiter) {
    mutex_destroy(&s->response_mutex);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_MEMORY, "Cannot initialize HTTP rate limiter");
  }
  s->config = *cfg;
  s->request_rate_limit.max_events =
      cfg->max_requests_per_peer
          ? (uint32_t)(cfg->max_requests_per_peer > UINT32_MAX ? UINT32_MAX : cfg->max_requests_per_peer)
          : DEFAULT_RATE_LIMITS[RATE_EVENT_HTTP_REQUEST].max_events;
  s->request_rate_limit.window_secs = cfg->request_rate_window_seconds
                                          ? cfg->request_rate_window_seconds
                                          : DEFAULT_RATE_LIMITS[RATE_EVENT_HTTP_REQUEST].window_secs;
  if (uv_loop_init(&s->loop) != 0) {
    rate_limiter_destroy(s->rate_limiter);
    mutex_destroy(&s->response_mutex);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot initialize HTTP event loop");
  }
  uv_tcp_init(&s->loop, &s->listener);
  s->listener.data = s;
  uv_async_init(&s->loop, &s->stop_async, stop_async_cb);
  s->stop_async.data = s;
  if (uv_async_init(&s->loop, &s->wake_async, wake_async_cb) != 0) {
    uv_close((uv_handle_t *)&s->stop_async, NULL);
    uv_close((uv_handle_t *)&s->listener, NULL);
    uv_run(&s->loop, UV_RUN_DEFAULT);
    uv_loop_close(&s->loop);
    mutex_destroy(&s->response_mutex);
    rate_limiter_destroy(s->rate_limiter);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot initialize HTTP wakeup handle");
  }
  s->wake_async.data = s;
  *out = s;
  return ASCIICHAT_OK;
}
asciichat_error_t http_server_wakeup(http_server_t *s) {
  if (!s || !s->running || uv_is_closing((uv_handle_t *)&s->wake_async))
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP server is not running");
  return uv_async_send(&s->wake_async) == 0 ? ASCIICHAT_OK : SET_ERRNO(ERROR_NETWORK, "Cannot wake HTTP event loop");
}
asciichat_error_t http_server_start(http_server_t *s) {
  if (!s || s->running)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP server is already running");
  if (s->config.enable_http3 &&
      quic_endpoint_set_http_server((acip_quic_endpoint_t *)s->config.http3_endpoint, s) != ASCIICHAT_OK)
    return SET_ERRNO(ERROR_NETWORK, "Cannot attach shared HTTP/3 handler");
  if (!s->adopted_socket) {
    const char *bind_address = s->config.bind_address ? s->config.bind_address : "0.0.0.0";
    struct sockaddr_storage address;
    int rc;
    if (strchr(bind_address, ':')) {
      struct sockaddr_in6 ipv6;
      rc = uv_ip6_addr(bind_address, s->config.port, &ipv6);
      memcpy(&address, &ipv6, sizeof(ipv6));
    } else {
      struct sockaddr_in ipv4;
      rc = uv_ip4_addr(bind_address, s->config.port, &ipv4);
      memcpy(&address, &ipv4, sizeof(ipv4));
    }
    if (rc || uv_tcp_bind(&s->listener, (const struct sockaddr *)&address, 0) != 0)
      return SET_ERRNO(ERROR_NETWORK, "Cannot bind HTTP listener");
  }
  if (uv_listen((uv_stream_t *)&s->listener, (int)s->config.max_connections, connection_cb) != 0)
    return SET_ERRNO(ERROR_NETWORK, "Cannot listen for HTTP connections");
  s->running = true;
  if (acthread_create(&s->thread, "http_io", http_loop_thread, s) != 0) {
    s->running = false;
    return SET_ERRNO(ERROR_THREAD, "Cannot create HTTP event-loop thread");
  }
  s->thread_started = true;
  return ASCIICHAT_OK;
}
asciichat_error_t http_server_adopt_socket(http_server_t *s, intptr_t socket_handle) {
  if (!s || s->running || s->adopted_socket || socket_handle < 0)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP listener cannot adopt this socket");
  if (uv_tcp_open(&s->listener, (uv_os_sock_t)socket_handle) != 0)
    return SET_ERRNO(ERROR_NETWORK, "Cannot adopt HTTP listener socket");
  s->adopted_socket = true;
  return ASCIICHAT_OK;
}
asciichat_error_t http_server_run(http_server_t *s) {
  if (!s || !s->running)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP server is not running");
  uv_run(&s->loop, UV_RUN_DEFAULT);
  return ASCIICHAT_OK;
}
void http_server_stop(http_server_t *s) {
  if (!s || !s->running)
    return;
  s->running = false;
  if (s->thread_started) {
    uv_async_send(&s->stop_async);
    thread_join(&s->thread, NULL);
    s->thread_started = false;
  } else {
    stop_async_cb(&s->stop_async);
    while (uv_run(&s->loop, UV_RUN_NOWAIT) != 0)
      ;
  }
}
void http_server_destroy(http_server_t **p) {
  if (!p || !*p)
    return;
  http_server_t *s = *p;
  http_server_stop(s);
  while (uv_run(&s->loop, UV_RUN_NOWAIT) != 0)
    ;
  uv_loop_close(&s->loop);
  rate_limiter_destroy(s->rate_limiter);
  mutex_destroy(&s->response_mutex);
  SAFE_FREE(s);
  *p = NULL;
}
