#include "http2.h"

#if __has_include(<nghttp2/nghttp2.h>)

#include <ascii-chat/common.h>
#include <nghttp2/nghttp2.h>
#include <string.h>

struct http2_connection {
  nghttp2_session *session;
  http2_frame_fn on_request;
  void *user_data;
  const uint8_t *output;
  size_t output_length;
};

static int on_begin_headers(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
  (void)session;
  (void)frame;
  (void)user_data;
  return 0;
}

static int on_frame_recv(nghttp2_session *session, const nghttp2_frame *frame, void *user_data) {
  http2_connection_t *connection = user_data;
  if (frame->hd.type == NGHTTP2_HEADERS && (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) &&
      (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
    if (connection->on_request(connection, NULL, 0, connection->user_data) != ASCIICHAT_OK)
      return NGHTTP2_ERR_CALLBACK_FAILURE;
  }
  return 0;
}

static int send_callback(nghttp2_session *session, const uint8_t *data, size_t length, int flags, void *user_data) {
  (void)session;
  (void)data;
  (void)length;
  (void)flags;
  (void)user_data;
  return 0;
}

asciichat_error_t http2_connection_create(http2_connection_t **out, http2_frame_fn on_request, void *user_data) {
  if (!out || !on_request)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing HTTP/2 connection callback");
  http2_connection_t *connection = SAFE_CALLOC(1, sizeof(*connection), http2_connection_t *);
  nghttp2_session_callbacks *callbacks = NULL;
  if (nghttp2_session_callbacks_new(&callbacks) != 0 ||
          nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, on_begin_headers),
      nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, on_frame_recv),
      nghttp2_session_callbacks_set_send_callback(callbacks, send_callback),
      nghttp2_session_server_new(&connection->session, callbacks, connection) != 0) {
    if (callbacks)
      nghttp2_session_callbacks_del(callbacks);
    SAFE_FREE(connection);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Cannot initialize HTTP/2 session");
  }
  nghttp2_session_callbacks_del(callbacks);
  connection->on_request = on_request;
  connection->user_data = user_data;
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
  connection->output = nghttp2_session_mem_send(connection->session, &connection->output_length);
  *data = connection->output;
  *length = connection->output_length;
  return ASCIICHAT_OK;
}
#endif
