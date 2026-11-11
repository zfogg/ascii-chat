#include <ascii-chat/network/http/server/server.h>
#include <ascii-chat/network/quic/quic.h>
#include "server_internal.h"
#include "http2.h"
#include <ascii-chat/common.h>
#include <libplatform/atomic.h>
#include <libplatform/thread.h>
#include <libplatform/util/string.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/network/rate_limit/rate_limit.h>
#include <ascii-chat/util/time.h>
#include <uv.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string.h>

#define HTTP_READ_CAPACITY (1024u * 1024u)
#define HTTP_HEADER_MAX_BYTES (64u * 1024u)
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

typedef struct http_server_client http_server_client_t;
typedef struct http_write {
  char *bytes;
  size_t length;
  uv_stream_t *stream;
  http_server_client_t *client;
  bool close_on_done;
} http_write_t;
static void write_done(uv_write_t *req, int status);
static void connection_cb(uv_stream_t *listener, int status);
static asciichat_error_t http2_request_cb(http2_connection_t *connection, const http2_request_view_t *view,
                                          void *user_data);
static asciichat_error_t http2_flush(http_server_client_t *client);

struct http_server {
  http_server_config_t config;
  uv_loop_t loop;
  uv_tcp_t listener;
  uv_tcp_t listener6;
  uv_async_t stop_async;
  uv_async_t wake_async;
  bool running;
  bool adopted_socket;
  bool thread_started;
  bool foreground_running;
  bool http3_attached;
  bool listener_initialized;
  bool listener6_initialized;
  bool stop_async_initialized;
  bool wake_async_initialized;
  platform_thread_t thread;
  size_t connections;
  atomic_t inflight_requests;
  http_server_client_t *clients;
  rate_limiter_t *rate_limiter;
  rate_limit_config_t request_rate_limit;
  mutex_t response_mutex;
  http_response_t *retained_responses;
  SSL_CTX *tls_context;
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
  http_server_client_t *client;
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
  bool inflight_accounted;
  http_server_t *server;
  http_response_t *next_retained;
  http3_submit_response_fn http3_submit;
  void *http3_context;
};

struct http_server_client {
  uv_tcp_t tcp;
  http_server_t *server;
  char *buffer;
  size_t capacity;
  size_t length;
  http_server_client_t *next;
  http_server_client_t *previous;
  http2_connection_t *http2;
  bool http2_mode;
  bool admitted;
  bool request_too_large;
  bool chunked_initialized;
  bool chunk_trailers;
  size_t chunk_size;
  size_t chunk_decoded_length;
  size_t chunk_decoded_capacity;
  uint8_t *chunk_decoded;
  char peer_address[64];
  SSL *tls;
  size_t pending_writes;
  bool close_after_write;
};
static size_t http_max_response_body(const http_server_t *server) {
  return server->config.max_response_body_bytes ? server->config.max_response_body_bytes
                                                : server->config.max_request_body_bytes;
}
size_t http_server_max_response_body(const http_server_t *server) {
  return server ? http_max_response_body(server) : 0;
}
void http_server_invalidate_http3_context(http_server_t *server, void *context) {
  if (!server || !context)
    return;
  mutex_lock(&server->response_mutex);
  for (http_response_t *response = server->retained_responses; response; response = response->next_retained) {
    if (response->http3_context == context) {
      response->http3_submit = NULL;
      response->http3_context = NULL;
      response->cancelled = true;
    }
  }
  mutex_unlock(&server->response_mutex);
}
static void stop_async_cb(uv_async_t *async);
static void http_server_detach_http3(http_server_t *s) {
  if (s->http3_attached) {
    (void)quic_endpoint_set_http_server((quic_endpoint_t *)s->config.http3_endpoint, NULL);
    s->http3_attached = false;
  }
}
static void http_server_rollback_start(http_server_t *s) {
  http_server_detach_http3(s);
  s->running = false;
  if (s->stop_async_initialized)
    stop_async_cb(&s->stop_async);
  while (uv_run(&s->loop, UV_RUN_NOWAIT) != 0)
    ;
}
static asciichat_error_t http_server_start_listener(http_server_t *s) {
  if (s->config.enable_http3) {
    asciichat_error_t result = quic_endpoint_set_http_server((quic_endpoint_t *)s->config.http3_endpoint, s);
    if (result != ASCIICHAT_OK)
      return SET_ERRNO(ERROR_NETWORK, "Cannot attach shared HTTP/3 handler");
    s->http3_attached = true;
  }
  bool tcp_enabled = s->config.enable_http1 || s->config.enable_http2;
  if (tcp_enabled && !s->adopted_socket) {
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
    if (rc || uv_tcp_bind(&s->listener, (const struct sockaddr *)&address, 0) != 0) {
      http_server_rollback_start(s);
      return SET_ERRNO(ERROR_NETWORK, "Cannot bind HTTP listener");
    }
    if (s->config.bind_address6 && *s->config.bind_address6 && !strchr(bind_address, ':')) {
      struct sockaddr_in6 ipv6;
      if (uv_ip6_addr(s->config.bind_address6, s->config.port, &ipv6) != 0 ||
          uv_tcp_bind(&s->listener6, (const struct sockaddr *)&ipv6, UV_TCP_IPV6ONLY) != 0) {
        http_server_rollback_start(s);
        return SET_ERRNO(ERROR_NETWORK, "Cannot bind IPv6 HTTP listener");
      }
    }
  }
  if (tcp_enabled && uv_listen((uv_stream_t *)&s->listener, (int)s->config.max_connections, connection_cb) != 0) {
    http_server_rollback_start(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot listen for HTTP connections");
  }
  if (tcp_enabled && s->listener6_initialized && s->config.bind_address6 && *s->config.bind_address6 &&
      !strchr(s->config.bind_address ? s->config.bind_address : "0.0.0.0", ':') &&
      uv_listen((uv_stream_t *)&s->listener6, (int)s->config.max_connections, connection_cb) != 0) {
    http_server_rollback_start(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot listen on IPv6 HTTP listener");
  }
  return ASCIICHAT_OK;
}
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
  http_server_client_t *c = handle->data;
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
    c->server->config.on_connection(c->peer_address, false, c->server->config.user_data);
  http2_connection_destroy(&c->http2);
  SSL_free(c->tls);
  SAFE_FREE(c->buffer);
  SAFE_FREE(c);
}

static int http_select_alpn(SSL *ssl, const unsigned char **out, unsigned char *out_length,
                            const unsigned char *offered, unsigned int offered_length, void *arg) {
  http_server_t *server = arg;
  unsigned char protocols[sizeof("h2") + sizeof("http/1.1")];
  unsigned int length = 0;
  if (server->config.enable_http2) {
    protocols[length++] = 2;
    memcpy(protocols + length, "h2", 2);
    length += 2;
  }
  if (server->config.enable_http1) {
    protocols[length++] = 8;
    memcpy(protocols + length, "http/1.1", 8);
    length += 8;
  }
  if (!length || SSL_select_next_proto((unsigned char **)out, out_length, protocols, length, offered, offered_length) !=
                     OPENSSL_NPN_NEGOTIATED)
    return SSL_TLSEXT_ERR_NOACK;
  (void)ssl;
  return SSL_TLSEXT_ERR_OK;
}

static int tls_password_cb(char *buf, int size, int rwflag, void *userdata) {
  (void)rwflag;
  const char *password = userdata;
  if (!password || size <= 0)
    return 0;
  size_t length = strlen(password);
  if (length >= (size_t)size)
    return 0;
  memcpy(buf, password, length);
  return (int)length;
}

static bool tls_flush(http_server_client_t *c) {
  char buffer[16 * 1024];
  while (c->tls && BIO_pending(SSL_get_wbio(c->tls)) > 0) {
    int n = BIO_read(SSL_get_wbio(c->tls), buffer, sizeof(buffer));
    if (n <= 0)
      return false;
    char *copy = SAFE_MALLOC((size_t)n, char *);
    uv_write_t *request = SAFE_CALLOC(1, sizeof(*request), uv_write_t *);
    http_write_t *write = SAFE_CALLOC(1, sizeof(*write), http_write_t *);
    if (!copy || !request || !write) {
      SAFE_FREE(copy);
      SAFE_FREE(request);
      SAFE_FREE(write);
      return false;
    }
    memcpy(copy, buffer, (size_t)n);
    write->bytes = copy;
    write->length = (size_t)n;
    write->stream = (uv_stream_t *)&c->tcp;
    write->client = c;
    request->data = write;
    uv_buf_t b = uv_buf_init(copy, n);
    if (uv_write(request, write->stream, &b, 1, write_done) != 0) {
      SAFE_FREE(copy);
      SAFE_FREE(request);
      SAFE_FREE(write);
      return false;
    }
    c->pending_writes++;
  }
  return true;
}

static bool tls_handshake(http_server_client_t *c) {
  int result = SSL_accept(c->tls);
  if (result != 1) {
    int error = SSL_get_error(c->tls, result);
    if (error != SSL_ERROR_WANT_READ && error != SSL_ERROR_WANT_WRITE)
      return false;
  }
  return tls_flush(c);
}
static void client_close(http_server_client_t *c) {
  if (!uv_is_closing((uv_handle_t *)&c->tcp))
    uv_close((uv_handle_t *)&c->tcp, client_freed);
}
static void client_peer_address(http_server_client_t *c) {
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
static bool decode_chunked_body(http_server_client_t *c, size_t offset, size_t *body_length) {
  size_t limit = c->server->config.max_request_body_bytes;
  if (!c->chunked_initialized) {
    c->chunked_initialized = true;
    c->chunk_size = 0;
    c->chunk_trailers = false;
    c->chunk_decoded_length = 0;
    c->chunk_decoded_capacity = 0;
    c->chunk_decoded = NULL;
  }
  for (;;) {
    if (c->chunk_size) {
      size_t available = c->length - offset;
      size_t take = available < c->chunk_size ? available : c->chunk_size;
      if (take) {
        if (c->chunk_decoded_length > limit - take) {
          c->request_too_large = true;
          goto invalid;
        }
        size_t required = c->chunk_decoded_length + take;
        if (required > c->chunk_decoded_capacity) {
          size_t capacity = c->chunk_decoded_capacity ? c->chunk_decoded_capacity : 4096;
          while (capacity < required) {
            if (capacity > limit / 2) {
              capacity = limit;
              break;
            }
            capacity *= 2;
          }
          uint8_t *decoded = SAFE_REALLOC(c->chunk_decoded, capacity, uint8_t *);
          if (!decoded)
            goto invalid;
          c->chunk_decoded = decoded;
          c->chunk_decoded_capacity = capacity;
        }
        memcpy(c->chunk_decoded + c->chunk_decoded_length, c->buffer + offset, take);
        c->chunk_decoded_length += take;
        c->chunk_size -= take;
        memmove(c->buffer + offset, c->buffer + offset + take, available - take);
        c->length -= take;
        c->buffer[c->length] = 0;
      }
      if (c->chunk_size)
        return false;
      if (c->length - offset < sizeof("\r\n") - 1)
        return false;
      if (memcmp(c->buffer + offset, "\r\n", sizeof("\r\n") - 1) != 0)
        goto invalid;
      memmove(c->buffer + offset, c->buffer + offset + 2, c->length - offset - 2);
      c->length -= 2;
      c->buffer[c->length] = 0;
    }
    char *line_end = strstr(c->buffer + offset, "\r\n");
    if (!line_end)
      return false;
    if (line_end == c->buffer + offset) {
      *body_length = c->chunk_decoded_length;
      if (*body_length)
        memcpy(c->buffer + offset, c->chunk_decoded, *body_length);
      c->length = offset + *body_length;
      c->buffer[c->length] = 0;
      SAFE_FREE(c->chunk_decoded);
      c->chunked_initialized = false;
      return true;
    }
    if (c->chunk_trailers) {
      size_t line_length = (size_t)(line_end - (c->buffer + offset)) + 2;
      memmove(c->buffer + offset, c->buffer + offset + line_length, c->length - offset - line_length);
      c->length -= line_length;
      c->buffer[c->length] = 0;
      continue;
    }
    char *number_end = NULL;
    unsigned long long chunk = strtoull(c->buffer + offset, &number_end, 16);
    if (number_end == c->buffer + offset || chunk > SIZE_MAX || (number_end != line_end && *number_end != ';'))
      goto invalid;
    size_t line_length = (size_t)(line_end - (c->buffer + offset)) + 2;
    memmove(c->buffer + offset, c->buffer + offset + line_length, c->length - offset - line_length);
    c->length -= line_length;
    c->buffer[c->length] = 0;
    if (chunk > limit || c->chunk_decoded_length > limit - (size_t)chunk) {
      c->request_too_large = true;
      goto invalid;
    }
    if (chunk == 0) {
      c->chunk_trailers = true;
      continue;
    }
    c->chunk_size = (size_t)chunk;
  }
invalid:
  SAFE_FREE(c->chunk_decoded);
  c->chunked_initialized = false;
  return false;
}
static bool parse_request(http_server_client_t *c, http_request_t *r) {
  char *end = strstr(c->buffer, "\r\n\r\n");
  if (!end)
    return false;
  size_t head = (size_t)(end - c->buffer) + 4;
  size_t body = 0;
  bool chunked = false;
  bool transfer_encoding = false;
  for (char *scan = strstr(c->buffer, "\r\n") + 2; scan < end;) {
    char *next = strstr(scan, "\r\n");
    if (!next)
      return false;
    if (platform_strncasecmp(scan, "Content-Length:", 15) == 0)
      body = (size_t)strtoull(scan + 15, NULL, 10);
    if (platform_strncasecmp(scan, "Transfer-Encoding:", 18) == 0) {
      transfer_encoding = true;
      const char *value = scan + 18;
      while (*value && *value != '\r' && *value != '\n') {
        while (*value == ' ' || *value == '\t' || *value == ',')
          value++;
        const char *token = value;
        while (*value && *value != ',' && *value != ';' && *value != '\r' && *value != '\n' && *value != ' ' &&
               *value != '\t')
          value++;
        size_t token_length = (size_t)(value - token);
        if (token_length == sizeof("chunked") - 1 && platform_strncasecmp(token, "chunked", token_length) == 0)
          chunked = true;
        else if (token_length)
          return false;
        while (*value == ' ' || *value == '\t' || *value == ';')
          value++;
      }
    }
    scan = next + 2;
  }
  if (transfer_encoding && !chunked)
    return false;
  if (chunked) {
    if (!decode_chunked_body(c, head, &body))
      return false;
  } else if (body > c->server->config.max_request_body_bytes || c->length < head + body)
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

static void write_done(uv_write_t *req, int status) {
  http_write_t *write = req->data;
  if (write->client && write->client->pending_writes)
    write->client->pending_writes--;
  if ((write->close_on_done || (write->client && write->client->close_after_write && !write->client->pending_writes)) &&
      !uv_is_closing((uv_handle_t *)write->stream))
    uv_close((uv_handle_t *)write->stream, client_freed);
  if (!status && write->client && write->client->http2)
    (void)http2_flush(write->client);
  SAFE_FREE(write->bytes);
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_BYTES_SENT, write->length);
  SAFE_FREE(write);
  SAFE_FREE(req);
  (void)status;
}
static asciichat_error_t client_write_plain(http_server_client_t *client, const char *data, size_t length,
                                            bool close_on_done) {
  if (!client->tls) {
    uv_write_t *request = SAFE_CALLOC(1, sizeof(*request), uv_write_t *);
    http_write_t *write = SAFE_CALLOC(1, sizeof(*write), http_write_t *);
    char *copy = SAFE_MALLOC(length, char *);
    if (!request || !write || !copy) {
      SAFE_FREE(request);
      SAFE_FREE(write);
      SAFE_FREE(copy);
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP write state");
    }
    memcpy(copy, data, length);
    write->bytes = copy;
    write->length = length;
    write->stream = (uv_stream_t *)&client->tcp;
    write->client = client;
    write->close_on_done = close_on_done;
    request->data = write;
    uv_buf_t b = uv_buf_init(copy, (unsigned)length);
    if (uv_write(request, write->stream, &b, 1, write_done) != 0) {
      SAFE_FREE(copy);
      SAFE_FREE(write);
      SAFE_FREE(request);
      return SET_ERRNO(ERROR_NETWORK, "Cannot write HTTP response");
    }
    client->pending_writes++;
    return ASCIICHAT_OK;
  }
  size_t offset = 0;
  while (offset < length) {
    int n = SSL_write(client->tls, data + offset, (int)(length - offset));
    if (n <= 0) {
      int error = SSL_get_error(client->tls, n);
      if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
        if (!tls_flush(client))
          break;
        continue;
      }
      return SET_ERRNO(ERROR_NETWORK, "Cannot encrypt HTTP response");
    }
    offset += (size_t)n;
    if (!tls_flush(client))
      return SET_ERRNO(ERROR_NETWORK, "Cannot write encrypted HTTP response");
  }
  if (close_on_done)
    client->close_after_write = true;
  return offset == length ? ASCIICHAT_OK : SET_ERRNO(ERROR_NETWORK, "Cannot write encrypted HTTP response");
}
static asciichat_error_t http2_flush(http_server_client_t *client) {
  const uint8_t *data = NULL;
  size_t length = 0;
  asciichat_error_t result = http2_connection_next_output(client->http2, &data, &length);
  if (result != ASCIICHAT_OK || !length)
    return result;
  return client_write_plain(client, (const char *)data, length, false);
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
  if (!r || !n || !v)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid HTTP response header");
  for (size_t i = 0; i < r->header_count; ++i) {
    if (platform_strcasecmp(r->headers[i].name, n) == 0) {
      r->headers[i] = (http_header_t){n, v};
      return ASCIICHAT_OK;
    }
  }
  if (r->header_count >= HTTP_HEADER_CAPACITY)
    return SET_ERRNO(ERROR_INVALID_PARAM, "HTTP response header limit exceeded");
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
size_t http_response_body_length(const http_response_t *r) {
  return r ? r->body_length : 0;
}
asciichat_error_t http_response_finish(http_response_t *r) {
  if (!r || r->finished)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP response already finished");
  r->finished = true;
  if (r->status >= 400)
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_ERRORS, 1);
  if (r->cancelled) {
    if (r->inflight_accounted && !r->deferred) {
      http_request_release(r->server);
      r->inflight_accounted = false;
    }
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP response was cancelled");
  }
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
    if (!out) {
      if (r->inflight_accounted && !r->deferred) {
        http_request_release(r->server);
        r->inflight_accounted = false;
      }
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate HTTP response");
    }
    const char *version = r->protocol == HTTP_PROTOCOL_10 ? "HTTP/1.0" : "HTTP/1.1";
    int n = safe_snprintf(out, cap, "%s %u %s\r\nContent-Length: %zu\r\nConnection: close\r\n", version, r->status,
                          reason(r->status), r->body_length);
    for (size_t i = 0; i < r->header_count; i++)
      n += safe_snprintf(out + n, cap - (size_t)n, "%s: %s\r\n", r->headers[i].name, r->headers[i].value);
    n += safe_snprintf(out + n, cap - (size_t)n, "\r\n");
    if (r->body_length)
      memcpy(out + n, r->body, r->body_length);
    n += (int)r->body_length;
    result = client_write_plain(r->client, out, (size_t)n, true);
    SAFE_FREE(out);
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
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_BYTES_RECEIVED, body_length);
  const char *peer = peer_address ? peer_address : "unknown";
  bool allowed = false;
  if (rate_limiter_check(server->rate_limiter, peer, RATE_EVENT_HTTP_REQUEST, &server->request_rate_limit, &allowed) !=
          ASCIICHAT_OK ||
      !allowed) {
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
    http_response_t limited = {.server = server,
                               .protocol = HTTP_PROTOCOL_3,
                               .status = 429,
                               .max_body_length = http_max_response_body(server),
                               .http3_submit = submit,
                               .http3_context = submit_context};
    return http_response_finish(&limited);
  }
  rate_limiter_record(server->rate_limiter, peer, RATE_EVENT_HTTP_REQUEST);
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REQUESTS, 1);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 1);
  uint64_t request_start = time_get_ns();
  if (!http_request_admit(server)) {
    http_response_t limited = {.server = server,
                               .protocol = HTTP_PROTOCOL_3,
                               .status = 429,
                               .max_body_length = http_max_response_body(server),
                               .http3_submit = submit,
                               .http3_context = submit_context};
    return http_response_finish(&limited);
  }
  http_response_t response = {.server = server,
                              .protocol = HTTP_PROTOCOL_3,
                              .status = 200,
                              .max_body_length = http_max_response_body(server),
                              .inflight_accounted = true,
                              .http3_submit = submit,
                              .http3_context = submit_context};
  ASCII_OTEL_SCOPE(HTTP);
  ASCII_OTEL_ATTR_STRING("http.method", method);
  ASCII_OTEL_ATTR_STRING("http.route", request.info.path);
  ASCII_OTEL_ATTR_STRING("http.protocol", "3");
  ASCII_OTEL_ATTR_STRING("http.backend", "quic-ngtcp2-nghttp3");
  if (http_server_authorize(&server->config.basic_auth, &request.info, &response))
    server->config.on_request(&request, &response, server->config.user_data);
  if (!response.finished && !response.deferred) {
    asciichat_error_t result = http_response_finish(&response);
    ASCII_OTEL_ATTR_INT("http.status", response.status);
    ASCII_OTEL_ATTR_INT("http.response_bytes", (int64_t)response.body_length);
    stats_duration_record(stats_runtime_scope(), STATS_DURATION_HTTP_REQUEST, time_get_ns() - request_start);
    stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 0);
    if (result == ASCIICHAT_OK && response.protocol == HTTP_PROTOCOL_3)
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_BYTES_SENT, response.body_length);
    SAFE_FREE(response.body);
    return result;
  }
  stats_duration_record(stats_runtime_scope(), STATS_DURATION_HTTP_REQUEST, time_get_ns() - request_start);
  stats_gauge_set(stats_runtime_scope(), STATS_GAUGE_HTTP_STREAMS_ACTIVE, 0);
  SAFE_FREE(response.body);
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
  if (!r->cancelled)
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_CANCELLED, 1);
  r->cancelled = true;
  return ASCIICHAT_OK;
}
static asciichat_error_t http2_request_cb(http2_connection_t *connection, const http2_request_view_t *view,
                                          void *user_data) {
  http_server_client_t *client = user_data;
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
                               .max_body_length = http_max_response_body(client->server),
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
                               .max_body_length = http_max_response_body(client->server),
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
                              .max_body_length = http_max_response_body(client->server),
                              .inflight_accounted = true,
                              .status = 200};
  if (http_server_authorize(&client->server->config.basic_auth, &request.info, &response))
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
/**
 * Consume one libuv read, identify the negotiated HTTP protocol, and dispatch
 * complete HTTP/1.x or HTTP/2 requests while enforcing the receive bound.
 */
static void read_cb(uv_stream_t *s, ssize_t n, const uv_buf_t *b) {
  http_server_client_t *c = s->data;
  char *base = (char *)b->base;
  if (n <= 0) {
    SAFE_FREE(base);
    client_close(c);
    return;
  }
  if (c->tls) {
    if (BIO_write(SSL_get_rbio(c->tls), base, (int)n) != n || !tls_handshake(c)) {
      SAFE_FREE(base);
      client_close(c);
      return;
    }
    if (!SSL_is_init_finished(c->tls)) {
      SAFE_FREE(base);
      return;
    }
    char *plain_buffer = SAFE_MALLOC(b->len, char *);
    if (!plain_buffer) {
      SAFE_FREE(base);
      client_close(c);
      return;
    }
    int plain = SSL_read(c->tls, plain_buffer, (int)b->len);
    SAFE_FREE(base);
    if (plain <= 0) {
      client_close(c);
      return;
    }
    base = plain_buffer;
    n = plain;
  }
  size_t required = c->length + (size_t)n + 1;
  if (required > c->capacity) {
    size_t limit = c->server->config.max_request_body_bytes > SIZE_MAX - HTTP_READ_CAPACITY
                       ? SIZE_MAX
                       : c->server->config.max_request_body_bytes + HTTP_READ_CAPACITY;
    if (required > limit) {
      SAFE_FREE(base);
      client_close(c);
      return;
    }
    size_t capacity = c->capacity;
    while (capacity < required) {
      if (capacity > limit / 2) {
        capacity = limit;
        break;
      }
      capacity *= 2;
    }
    char *expanded = SAFE_REALLOC(c->buffer, capacity, char *);
    if (!expanded) {
      SAFE_FREE(base);
      client_close(c);
      return;
    }
    c->buffer = expanded;
    c->capacity = capacity;
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
    if (http2_connection_create(&c->http2, http2_request_cb, c, c->server->config.max_request_body_bytes,
                                http_max_response_body(c->server)) != ASCIICHAT_OK ||
        http2_connection_receive(c->http2, c->buffer, c->length) != ASCIICHAT_OK) {
      client_close(c);
      return;
    }
    c->length = 0;
    if (http2_flush(c) != ASCIICHAT_OK)
      client_close(c);
    return;
  }
  if (!c->http2_mode && !strstr(c->buffer, "\r\n\r\n") && c->length > HTTP_HEADER_MAX_BYTES) {
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
  c->request_too_large = false;
  if (!parse_request(c, &r)) {
    if (c->request_too_large)
      client_close(c);
    return;
  }
  if (!c->server->config.enable_http1) {
    client_close(c);
    return;
  }
  bool allowed = false;
  if (rate_limiter_check(c->server->rate_limiter, c->peer_address, RATE_EVENT_HTTP_REQUEST,
                         &c->server->request_rate_limit, &allowed) != ASCIICHAT_OK ||
      !allowed) {
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
    http_response_t limited = {.server = c->server,
                               .stream = s,
                               .protocol = r.info.protocol,
                               .max_body_length = http_max_response_body(c->server),
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
                               .max_body_length = http_max_response_body(c->server),
                               .status = 429};
    http_response_finish(&limited);
    uv_read_stop(s);
    return;
  }
  http_response_t resp = {.server = c->server,
                          .stream = s,
                          .client = c,
                          .protocol = r.info.protocol,
                          .max_body_length = http_max_response_body(c->server),
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
  if (http_server_authorize(&c->server->config.basic_auth, &r.info, &resp))
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
  http_server_client_t *c = SAFE_CALLOC(1, sizeof(*c), http_server_client_t *);
  if (!c)
    return;
  c->server = s;
  if (uv_tcp_init(&s->loop, &c->tcp) != 0) {
    SAFE_FREE(c);
    return;
  }
  c->tcp.data = c;
  if (uv_accept(l, (uv_stream_t *)&c->tcp) == 0) {
    if (s->connections >= s->config.max_connections) {
      client_close(c);
      return;
    }
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
    c->capacity = HTTP_READ_CAPACITY;
    c->buffer = SAFE_CALLOC(c->capacity, 1, char *);
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
    if (s->tls_context) {
      c->tls = SSL_new(s->tls_context);
      if (!c->tls) {
        client_close(c);
        return;
      }
      BIO *read_bio = BIO_new(BIO_s_mem());
      BIO *write_bio = BIO_new(BIO_s_mem());
      if (!read_bio || !write_bio) {
        BIO_free(read_bio);
        BIO_free(write_bio);
        client_close(c);
        return;
      }
      SSL_set_bio(c->tls, read_bio, write_bio);
      SSL_set_accept_state(c->tls);
      if (!tls_handshake(c)) {
        client_close(c);
        return;
      }
    }
    if (uv_read_start((uv_stream_t *)&c->tcp, alloc_buffer, read_cb) != 0)
      client_close(c);
  } else
    client_close(c);
}
static void stop_async_cb(uv_async_t *async) {
  http_server_t *s = async->data;
  if (!uv_is_closing((uv_handle_t *)&s->listener))
    uv_close((uv_handle_t *)&s->listener, NULL);
  if (s->listener6_initialized && !uv_is_closing((uv_handle_t *)&s->listener6))
    uv_close((uv_handle_t *)&s->listener6, NULL);
  for (http_server_client_t *c = s->clients; c; c = c->next)
    client_close(c);
  uv_close((uv_handle_t *)async, NULL);
  if (!uv_is_closing((uv_handle_t *)&s->wake_async))
    uv_close((uv_handle_t *)&s->wake_async, NULL);
  uv_stop(&s->loop);
}
static void *http_loop_thread(void *arg) {
  http_server_t *s = arg;
  uv_run(&s->loop, UV_RUN_DEFAULT);
  return NULL;
}
asciichat_error_t http_server_init(http_server_t **out, const http_server_config_t *cfg) {
  if (!out || !cfg || !cfg->on_request || (!cfg->enable_http1 && !cfg->enable_http2 && !cfg->enable_http3))
    return SET_ERRNO(ERROR_INVALID_PARAM, "At least one HTTP protocol and a request handler are required");
  if (cfg->max_connections == 0 || cfg->max_request_body_bytes == 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "HTTP limits must be non-zero");
  if (cfg->basic_auth.enabled && (!cfg->basic_auth.username || !cfg->basic_auth.password ||
                                  !cfg->basic_auth.username[0] || !cfg->basic_auth.password[0]))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Basic authentication requires non-empty credentials");
  if ((cfg->enable_http3 && !cfg->http3_endpoint) || ((cfg->tls_cert_path == NULL) != (cfg->tls_key_path == NULL)))
    return SET_ERRNO(ERROR_INVALID_PARAM, "HTTP/3 requires a pre-opened endpoint and TLS requires certificate and key");
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
  if (cfg->tls_cert_path) {
    s->tls_context = SSL_CTX_new(TLS_server_method());
    if (!s->tls_context) {
      rate_limiter_destroy(s->rate_limiter);
      mutex_destroy(&s->response_mutex);
      SAFE_FREE(s);
      return SET_ERRNO(ERROR_NETWORK, "Cannot initialize TLS context");
    }
    SSL_CTX_set_app_data(s->tls_context, s);
    SSL_CTX_set_alpn_select_cb(s->tls_context, http_select_alpn, s);
    if (cfg->tls_key_password) {
      SSL_CTX_set_default_passwd_cb_userdata(s->tls_context, (void *)cfg->tls_key_password);
    }
    if (cfg->tls_key_password)
      SSL_CTX_set_default_passwd_cb(s->tls_context, tls_password_cb);
    if (SSL_CTX_use_certificate_chain_file(s->tls_context, cfg->tls_cert_path) != 1 ||
        SSL_CTX_use_PrivateKey_file(s->tls_context, cfg->tls_key_path, SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(s->tls_context) != 1) {
      SSL_CTX_free(s->tls_context);
      s->tls_context = NULL;
      rate_limiter_destroy(s->rate_limiter);
      mutex_destroy(&s->response_mutex);
      SAFE_FREE(s);
      return SET_ERRNO(ERROR_NETWORK, "Cannot load HTTP TLS certificate or key");
    }
  }
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
    SSL_CTX_free(s->tls_context);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot initialize HTTP event loop");
  }
  if (uv_tcp_init(&s->loop, &s->listener) != 0) {
    uv_loop_close(&s->loop);
    rate_limiter_destroy(s->rate_limiter);
    mutex_destroy(&s->response_mutex);
    SSL_CTX_free(s->tls_context);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot initialize HTTP listener");
  }
  s->listener_initialized = true;
  s->listener.data = s;
  if (uv_tcp_init(&s->loop, &s->listener6) != 0) {
    uv_close((uv_handle_t *)&s->listener, NULL);
    return SET_ERRNO(ERROR_NETWORK, "Cannot initialize IPv6 HTTP listener");
  }
  s->listener6_initialized = true;
  s->listener6.data = s;
  if (uv_async_init(&s->loop, &s->stop_async, stop_async_cb) != 0) {
    uv_close((uv_handle_t *)&s->listener, NULL);
    uv_run(&s->loop, UV_RUN_DEFAULT);
    uv_loop_close(&s->loop);
    rate_limiter_destroy(s->rate_limiter);
    mutex_destroy(&s->response_mutex);
    SSL_CTX_free(s->tls_context);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot initialize HTTP stop handle");
  }
  s->stop_async_initialized = true;
  s->stop_async.data = s;
  if (uv_async_init(&s->loop, &s->wake_async, wake_async_cb) != 0) {
    uv_close((uv_handle_t *)&s->stop_async, NULL);
    uv_close((uv_handle_t *)&s->listener, NULL);
    uv_run(&s->loop, UV_RUN_DEFAULT);
    uv_loop_close(&s->loop);
    mutex_destroy(&s->response_mutex);
    rate_limiter_destroy(s->rate_limiter);
    SSL_CTX_free(s->tls_context);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_NETWORK, "Cannot initialize HTTP wakeup handle");
  }
  s->wake_async_initialized = true;
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
  asciichat_error_t result = http_server_start_listener(s);
  if (result != ASCIICHAT_OK)
    return result;
  s->running = true;
  if (platform_thread_create(&s->thread, "http_io", http_loop_thread, s) != 0) {
    http_server_rollback_start(s);
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
  if (!s || s->running || s->thread_started)
    return SET_ERRNO(ERROR_INVALID_STATE, "HTTP server loop is already running in its worker thread");
  asciichat_error_t result = http_server_start_listener(s);
  if (result != ASCIICHAT_OK)
    return result;
  s->running = true;
  s->foreground_running = true;
  uv_run(&s->loop, UV_RUN_DEFAULT);
  s->foreground_running = false;
  s->running = false;
  http_server_detach_http3(s);
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
    http_server_detach_http3(s);
  } else if (s->foreground_running) {
    (void)uv_async_send(&s->stop_async);
  } else {
    stop_async_cb(&s->stop_async);
    while (uv_run(&s->loop, UV_RUN_NOWAIT) != 0)
      ;
    http_server_detach_http3(s);
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
  http_server_detach_http3(s);
  rate_limiter_destroy(s->rate_limiter);
  mutex_destroy(&s->response_mutex);
  SSL_CTX_free(s->tls_context);
  SAFE_FREE(s);
  *p = NULL;
}
