#pragma once

#include <ascii-chat/network/http/server.h>

typedef asciichat_error_t (*http3_submit_response_fn)(void *context, unsigned status, const http_header_t *headers,
                                                      size_t header_count, const void *body, size_t body_length);

size_t http_server_max_request_body(const http_server_t *server);
size_t http_server_max_response_body(const http_server_t *server);
void http_server_invalidate_http3_context(http_server_t *server, void *context);

asciichat_error_t http_server_dispatch_http3(http_server_t *server, const char *method, const char *path,
                                             const http_header_t *headers, size_t header_count, const void *body,
                                             size_t body_length, const char *peer_address,
                                             http3_submit_response_fn submit, void *submit_context);
