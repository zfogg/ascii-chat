#pragma once

#include <ascii-chat/asciichat_errno.h>
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
} http2_request_view_t;
typedef asciichat_error_t (*http2_request_fn)(http2_connection_t *connection, const http2_request_view_t *request,
                                              void *user_data);

asciichat_error_t http2_connection_create(http2_connection_t **connection, http2_request_fn on_request, void *user_data);
void http2_connection_destroy(http2_connection_t **connection);
asciichat_error_t http2_connection_receive(http2_connection_t *connection, const void *data, size_t length);
asciichat_error_t http2_connection_next_output(http2_connection_t *connection, const uint8_t **data, size_t *length);
asciichat_error_t http2_connection_submit_response(http2_connection_t *connection, int32_t stream_id, unsigned status,
                                                   const void *body, size_t body_length);
