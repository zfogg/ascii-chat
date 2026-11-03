#pragma once

#include "stream.h"
#include "tls.h"

#include <ascii-chat/platform/socket.h>
#include <ngtcp2/ngtcp2.h>

/* Private serialized connection engine. An endpoint owns and drives each
 * instance from one lane; it must not call these operations concurrently.
 * Times are nanoseconds on the time_get_ns() monotonic clock and cannot move
 * backwards. The TLS context and callback reference outlive the connection. */
typedef struct acip_quic_connection acip_quic_connection_t;

asciichat_error_t acip_quic_connection_client_create(acip_quic_tls_t *tls, const char *server_name,
                                                     const ngtcp2_path *path, uint64_t now_ns,
                                                     acip_quic_connection_t **out);
/** Creates a server connection for an acceptable QUIC v1 Initial packet.
 * The caller must subsequently pass that same packet to receive(). */
asciichat_error_t acip_quic_connection_server_create(acip_quic_tls_t *tls, const ngtcp2_path *path, const void *initial,
                                                     size_t initial_length, uint64_t now_ns,
                                                     acip_quic_connection_t **out);
asciichat_error_t acip_quic_connection_receive(acip_quic_connection_t *connection, const ngtcp2_path *path,
                                               const void *packet, size_t length, uint64_t now_ns);
/** Writes a single UDP payload, or sets length to zero if no packet is ready.
 * The returned path identifies the local and remote addresses for that packet.
 * The endpoint must retain an unsent payload across socket backpressure and
 * call packet_sent after successful transmission, before writing another. */
asciichat_error_t acip_quic_connection_write(acip_quic_connection_t *connection, ngtcp2_path_storage *path,
                                             void *packet, size_t capacity, size_t *length, uint64_t now_ns);
asciichat_error_t acip_quic_connection_packet_sent(acip_quic_connection_t *connection, uint64_t now_ns);
/** Reports the class of newly submitted stream data in the pending packet.
 * Retransmissions from other streams may also be present in that packet. */
bool acip_quic_connection_pending_packet_class(const acip_quic_connection_t *connection,
                                               acip_traffic_class_t *traffic_class);
uint64_t acip_quic_connection_expiry(const acip_quic_connection_t *connection);
asciichat_error_t acip_quic_connection_handle_expiry(acip_quic_connection_t *connection, uint64_t now_ns);
bool acip_quic_connection_tls_ready(const acip_quic_connection_t *connection);
/** Install a serialized receive callback. It borrows complete wire packets;
 * the callback must copy what it retains and must not reenter the connection. */
asciichat_error_t acip_quic_connection_set_receiver(acip_quic_connection_t *connection, acip_quic_stream_emit_fn emit,
                                                    void *context);
/** Permit media only after the application has completed and acknowledged the
 * ACIP handshake. The owner must separately coordinate later control barriers. */
asciichat_error_t acip_quic_connection_allow_media(acip_quic_connection_t *connection);
/** Copy one complete ACIP wire packet to its reliable stream. Admission is
 * bounded; ERROR_BUFFER_FULL leaves the queue unchanged. The connection owns
 * submitted bytes until ngtcp2 acknowledges them or the connection is freed. */
asciichat_error_t acip_quic_connection_submit(acip_quic_connection_t *connection, acip_traffic_class_t traffic_class,
                                              const void *packet, size_t length);
size_t acip_quic_connection_pending_bytes(const acip_quic_connection_t *connection);
void acip_quic_connection_destroy(acip_quic_connection_t **connection);
