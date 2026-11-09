#pragma once

/**
 * @file network/http/server.h
 * @brief Backend-neutral HTTP request/response contract.
 *
 * The public API deliberately contains no libuv, TLS, HTTP/2, HTTP/3, or
 * libwebsockets types.  Backends translate their protocol into this contract;
 * handlers can therefore be shared by MCP, API, discovery, and WebTransport.
 */

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct http_server http_server_t;
typedef struct http_request http_request_t;
typedef struct http_response http_response_t;

typedef enum {
  HTTP_PROTOCOL_10 = 0,
  HTTP_PROTOCOL_11,
  HTTP_PROTOCOL_2,
  HTTP_PROTOCOL_3,
} http_protocol_t;

typedef struct {
  const char *name;
  const char *value;
} http_header_t;

typedef struct {
  const char *method;
  const char *path;
  const char *query;
  http_protocol_t protocol;
  const http_header_t *headers;
  size_t header_count;
  const char *peer_address;
  void *user_data;
} http_request_info_t;

typedef void (*http_request_fn)(http_request_t *request, http_response_t *response, void *user_data);
typedef void (*http_connection_fn)(const char *peer_address, bool connected, void *user_data);
typedef void (*http_error_fn)(asciichat_error_t error, const char *message, void *user_data);
/** Invoked on the HTTP event-loop thread after http_server_wakeup(). */
typedef void (*http_wakeup_fn)(void *user_data);

typedef struct {
  const char *bind_address;
  uint16_t port;
  bool enable_http1;
  bool enable_http2;
  bool enable_http3;
  const char *tls_certificate;
  const char *tls_private_key;
  /** Optional pre-opened QUIC HTTP/3 endpoint; ownership remains with caller. */
  void *http3_endpoint;
  size_t max_connections;
  /** Maximum concurrently dispatched requests; zero derives a safe bound from max_connections. */
  size_t max_inflight_requests;
  size_t max_request_body_bytes;
  /** Maximum buffered response body; zero uses max_request_body_bytes. */
  size_t max_response_body_bytes;
  /** Maximum requests per peer during the configured rate window; zero uses the safe default. */
  size_t max_requests_per_peer;
  /** Request rate window in seconds; zero uses the safe default. */
  uint32_t request_rate_window_seconds;
  http_request_fn on_request;
  http_connection_fn on_connection;
  http_error_fn on_error;
  http_wakeup_fn on_wakeup;
  void *user_data;
} http_server_config_t;

asciichat_error_t http_server_init(http_server_t **server, const http_server_config_t *config);
asciichat_error_t http_server_start(http_server_t *server);
/** Adopt an already-listening native socket before starting the server. */
asciichat_error_t http_server_adopt_socket(http_server_t *server, intptr_t socket_handle);
/** Wake the private event loop from a worker thread. */
asciichat_error_t http_server_wakeup(http_server_t *server);
void http_server_stop(http_server_t *server);
void http_server_destroy(http_server_t **server);
/** Run the server loop when embedding the server without a background thread. */
asciichat_error_t http_server_run(http_server_t *server);

const http_request_info_t *http_request_info(const http_request_t *request);
const void *http_request_body(const http_request_t *request, size_t *length);
bool http_request_is_cancelled(const http_request_t *request);
asciichat_error_t http_request_cancel(http_request_t *request);
bool http_response_is_cancelled(const http_response_t *response);

asciichat_error_t http_response_set_status(http_response_t *response, unsigned status);
/** Retain a response for completion after the request callback returns. */
asciichat_error_t http_response_retain(http_response_t *response, http_response_t **retained);
/** Release a response retained with http_response_retain(). */
void http_response_release(http_response_t **response);
asciichat_error_t http_response_set_header(http_response_t *response, const char *name, const char *value);
asciichat_error_t http_response_write(http_response_t *response, const void *data, size_t length);
/** Return the number of bytes buffered for the response body. */
size_t http_response_body_length(const http_response_t *response);
asciichat_error_t http_response_finish(http_response_t *response);
