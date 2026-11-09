#include <ascii-chat/network/http/server.h>
#include "http2.h"
#include <ascii-chat/common.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/network/rate_limit/rate_limit.h>
#include <ascii-chat/util/time.h>
#include <uv.h>
#include <string.h>

#define HTTP_READ_CAPACITY (1024u * 1024u)
#define HTTP_HEADER_CAPACITY 64u

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
  bool thread_started;
  asciichat_thread_t thread;
  size_t connections;
  http_client_t *clients;
  rate_limiter_t *rate_limiter;
  rate_limit_config_t request_rate_limit;
};
struct http_request {
  http_request_info_t info;
  http_header_t headers[HTTP_HEADER_CAPACITY];
  char *storage;
  size_t storage_length;
  size_t body_offset;
  size_t body_length;
  bool cancelled;
};
struct http_response {
  uv_stream_t *stream;
  http_client_t *client;
  int32_t http2_stream_id;
  unsigned status;
  http_header_t headers[HTTP_HEADER_CAPACITY];
  size_t header_count;
  char *body;
  size_t body_length;
  bool finished;
  bool deferred;
  bool retained;
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
  char peer_address[64];
};
static void stop_async_cb(uv_async_t *async);
static void wake_async_cb(uv_async_t *async) {
  (void)async;
}

static void client_freed(uv_handle_t *handle) {
  http_client_t *c = handle->data;
  if (c->previous)
    c->previous->next = c->next;
  else
    c->server->clients = c->next;
  if (c->next)
    c->next->previous = c->previous;
  if (c->server->connections)
    c->server->connections--;
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_CONNECTIONS_ACTIVE, c->server->connections);
  if (c->server->config.on_connection)
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
    if (_strnicmp(scan, "Content-Length:", 15) == 0)
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
  r->info.protocol = HTTP_PROTOCOL_11;
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
  r->info.protocol = HTTP_PROTOCOL_11;
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
  if (!r || !retained || r->finished || r->retained)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP response cannot be retained");
  http_response_t *copy = SAFE_CALLOC(1, sizeof(*copy), http_response_t *);
  if (!copy)
    return SET_ERRNO(ERROR_MEMORY, "Cannot retain HTTP response");
  copy->stream = r->stream;
  copy->client = r->client;
  copy->http2_stream_id = r->http2_stream_id;
  copy->status = r->status;
  copy->header_count = r->header_count;
  memcpy(copy->headers, r->headers, r->header_count * sizeof(r->headers[0]));
  copy->retained = true;
  r->deferred = true;
  r->retained = true;
  *retained = copy;
  return ASCIICHAT_OK;
}
void http_response_release(http_response_t **response) {
  if (!response || !*response)
    return;
  if ((*response)->retained) {
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
  if (r->client && r->client->http2_mode) {
    asciichat_error_t result = http2_connection_submit_response(r->client->http2, r->http2_stream_id, r->status,
                                                                r->headers, r->header_count, r->body, r->body_length);
    if (result != ASCIICHAT_OK)
      return result;
    return http2_flush(r->client);
  }
  size_t cap = 256 + r->body_length;
  for (size_t i = 0; i < r->header_count; i++)
    cap += strlen(r->headers[i].name) + strlen(r->headers[i].value) + 4;
  char *out = SAFE_MALLOC(cap, char *);
  if (!out)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP response");
  int n = safe_snprintf(out, cap, "HTTP/1.1 %u %s\r\nContent-Length: %zu\r\nConnection: close\r\n", r->status,
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
  w->data = write;
  uv_buf_t b = uv_buf_init(out, n);
  return uv_write(w, r->stream, &b, 1, write_done) == 0 ? ASCIICHAT_OK
                                                        : SET_ERRNO(ERROR_NETWORK, "Cannot write HTTP response");
}
const http_request_info_t *http_request_info(const http_request_t *r) {
  return r ? &r->info : NULL;
}
const void *http_request_body(const http_request_t *r, size_t *n) {
  if (n)
    *n = r ? r->body_length : 0;
  return r && r->body_length ? r->storage + r->body_offset : NULL;
}
bool http_request_is_cancelled(const http_request_t *r) {
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
  http_request_t request = {0};
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
    http_response_t limited = {
        .stream = (uv_stream_t *)&client->tcp, .client = client, .http2_stream_id = view->stream_id, .status = 429};
    return http_response_finish(&limited);
  }
  rate_limiter_record(client->server->rate_limiter, client->peer_address, RATE_EVENT_HTTP_REQUEST);
  http_response_t response = {
      .stream = (uv_stream_t *)&client->tcp, .client = client, .http2_stream_id = view->stream_id, .status = 200};
  client->server->config.on_request(&request, &response, client->server->config.user_data);
  if (!response.finished && !response.deferred)
    return http_response_finish(&response);
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
    if (http2_connection_create(&c->http2, http2_request_cb, c) != ASCIICHAT_OK ||
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
    http_response_t limited = {.stream = s, .status = 429};
    http_response_finish(&limited);
    uv_read_stop(s);
    return;
  }
  rate_limiter_record(c->server->rate_limiter, c->peer_address, RATE_EVENT_HTTP_REQUEST);
  http_response_t resp = {.stream = s, .status = 200};
  uint64_t request_start = time_get_ns();
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REQUESTS, 1);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 1);
  c->server->config.on_request(&r, &resp, c->server->config.user_data);
  if (!resp.finished && !resp.deferred)
    http_response_finish(&resp);
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
  c->buffer = SAFE_CALLOC(HTTP_READ_CAPACITY, 1, char *);
  uv_tcp_init(&s->loop, &c->tcp);
  c->tcp.data = c;
  if (uv_accept(l, (uv_stream_t *)&c->tcp) == 0) {
    c->next = s->clients;
    if (s->clients)
      s->clients->previous = c;
    s->clients = c;
    client_peer_address(c);
    s->connections++;
    stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_CONNECTIONS_ACTIVE, s->connections);
    if (s->config.on_connection)
      s->config.on_connection("", true, s->config.user_data);
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
  if (cfg->enable_http3 || cfg->tls_certificate || cfg->tls_private_key)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Requested HTTP/3 or TLS backend is not enabled");
  http_server_t *s = SAFE_CALLOC(1, sizeof(*s), http_server_t *);
  s->rate_limiter = rate_limiter_create_memory();
  if (!s->rate_limiter) {
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
  SAFE_FREE(s);
  *p = NULL;
}
