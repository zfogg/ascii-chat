#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/network/http/server/server.h>
#include <nghttp3/nghttp3.h>
#include <stdbool.h>
#include <stdint.h>

/* Serialized HTTP/3 CONNECT session. The QUIC owner supplies three local
 * unidirectional stream IDs and retains vectors until they are acknowledged. */
typedef struct quic_http3_server quic_http3_server_t;

asciichat_error_t quic_http3_server_create(const char *origin, const char *path, int64_t control_stream,
                                           int64_t encoder_stream, int64_t decoder_stream, http_server_t *http_server,
                                           const char *peer_address, quic_http3_server_t **out);
asciichat_error_t quic_http3_server_read(quic_http3_server_t *server, int64_t stream_id, const uint8_t *data,
                                         size_t length, bool fin, uint64_t now_ns, size_t *consumed);
/* Vectors remain owned by the codec until acknowledged. The caller must call
 * wrote() with exactly the bytes accepted before the next write(). */
asciichat_error_t quic_http3_server_write(quic_http3_server_t *server, int64_t *stream_id, bool *fin,
                                          nghttp3_vec *vectors, size_t capacity, size_t *count);
asciichat_error_t quic_http3_server_wrote(quic_http3_server_t *server, int64_t stream_id, size_t length);
asciichat_error_t quic_http3_server_ack(quic_http3_server_t *server, int64_t stream_id, uint64_t length);
bool quic_http3_server_accepted(const quic_http3_server_t *server, int64_t *session_id);
void quic_http3_server_destroy(quic_http3_server_t **server);
