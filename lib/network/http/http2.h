#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/http/server.h>
#include <stddef.h>
#include <stdint.h>

typedef struct http2_connection http2_connection_t;
typedef struct {
  const char *method;
  const char *path;
  const char *query;
  const void *body;
  size_t body_length;
  int32_t stream_id;
  const struct http2_header *headers;
  size_t header_count;
} http2_request_view_t;
typedef struct http2_header {
  const char *name;
  const char *value;
} http2_header_t;
typedef asciichat_error_t (*http2_request_fn)(http2_connection_t *connection, const http2_request_view_t *request,
                                              void *user_data);

asciichat_error_t http2_connection_create(http2_connection_t **connection, http2_request_fn on_request, void *user_data,
                                          size_t max_request_body_length, size_t max_response_body_length);
void http2_connection_destroy(http2_connection_t **connection);
asciichat_error_t http2_connection_receive(http2_connection_t *connection, const void *data, size_t length);
asciichat_error_t http2_connection_next_output(http2_connection_t *connection, const uint8_t **data, size_t *length);
asciichat_error_t http2_connection_submit_response(http2_connection_t *connection, int32_t stream_id, unsigned status,
                                                   const http_header_t *headers, size_t header_count, const void *body,
                                                   size_t body_length);
