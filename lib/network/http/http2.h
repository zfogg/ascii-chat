#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stddef.h>
#include <stdint.h>

typedef struct http2_connection http2_connection_t;
typedef asciichat_error_t (*http2_frame_fn)(http2_connection_t *connection, const uint8_t *data, size_t length,
                                            void *user_data);

asciichat_error_t http2_connection_create(http2_connection_t **connection, http2_frame_fn on_request, void *user_data);
void http2_connection_destroy(http2_connection_t **connection);
asciichat_error_t http2_connection_receive(http2_connection_t *connection, const void *data, size_t length);
asciichat_error_t http2_connection_next_output(http2_connection_t *connection, const uint8_t **data, size_t *length);
