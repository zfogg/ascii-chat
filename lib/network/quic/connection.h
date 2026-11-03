#pragma once

#include "stream.h"
#include "tls.h"
#include "security.h"

#include <ascii-chat/platform/socket.h>
#include <ngtcp2/ngtcp2.h>

/* Private serialized connection engine. An endpoint owns and drives each
 * instance from one lane; it must not call these operations concurrently.
 * Times are nanoseconds on the time_get_ns() monotonic clock and cannot move
 * backwards. The TLS context and callback reference outlive the connection. */
typedef struct acip_quic_connection acip_quic_connection_t;

/* Produce a best-effort CONNECTION_CLOSE before an owned connection is
 * discarded. A zero-length result means that no close can be sent. */
asciichat_error_t acip_quic_connection_write_close(acip_quic_connection_t *connection, ngtcp2_path_storage *path,
                                                   void *packet, size_t capacity, size_t *length, uint64_t now_ns);

asciichat_error_t acip_quic_connection_client_create(acip_quic_tls_t *tls, const char *server_name,
                                                     const ngtcp2_path *path, uint64_t now_ns,
                                                     acip_quic_connection_t **out);
/** Creates a server connection for an acceptable QUIC v1 Initial packet.
 * The caller must subsequently pass that same packet to receive(). */
asciichat_error_t acip_quic_connection_server_create(acip_quic_tls_t *tls, const ngtcp2_path *path, const void *initial,
                                                     size_t initial_length, uint64_t now_ns,
                                                     acip_quic_connection_t **out);
/* As above, after validating the Retry token and recovering its original DCID. */
asciichat_error_t acip_quic_connection_server_create_validated(acip_quic_tls_t *tls, const ngtcp2_path *path,
                                                               const void *initial, size_t initial_length,
                                                               const ngtcp2_cid *original_dcid, uint64_t now_ns,
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
/** Reports the class of newly submitted stream or datagram data in the pending packet.
 * Retransmissions from other streams may also be present in that packet. */
bool acip_quic_connection_pending_packet_class(const acip_quic_connection_t *connection,
                                               acip_traffic_class_t *traffic_class);
uint64_t acip_quic_connection_expiry(const acip_quic_connection_t *connection);
asciichat_error_t acip_quic_connection_handle_expiry(acip_quic_connection_t *connection, uint64_t now_ns);
bool acip_quic_connection_tls_ready(const acip_quic_connection_t *connection);
/* A WebTransport listener is admitted only after its CONNECT is authorized. */
bool acip_quic_connection_webtransport_ready(const acip_quic_connection_t *connection);
asciichat_error_t acip_quic_connection_peer_address(const acip_quic_connection_t *connection,
                                                    struct sockaddr_storage *address, socklen_t *length);
/* Match an incoming destination CID, including the server's original Initial
 * CID. Called only on the connection lane. */
asciichat_error_t acip_quic_connection_matches_cid(const acip_quic_connection_t *connection, const uint8_t *cid,
                                                   size_t length, bool *matches);
/** Install a serialized receive callback. It borrows complete wire packets;
 * the callback must copy what it retains and must not reenter the connection. */
asciichat_error_t acip_quic_connection_set_receiver(acip_quic_connection_t *connection, acip_quic_stream_emit_fn emit,
                                                    void *context);
/** Detach the current receiver on the connection lane before freeing its context. */
asciichat_error_t acip_quic_connection_clear_receiver(acip_quic_connection_t *connection, void *context);
/** The notifier runs on the serialized lane when the readiness exchange completes. */
void acip_quic_connection_set_security_notifier(acip_quic_connection_t *connection, void (*notify)(void *),
                                                void *context);
/* Notifier runs on the serialized lane and must not acquire the outbound queue lock. */
void acip_quic_connection_set_keyframe_notifier(acip_quic_connection_t *connection,
                                                void (*notify)(uint16_t, uint32_t, void *), void *context);
/** Permit outbound media after the authenticated readiness exchange. The
 * owner must separately coordinate later application control barriers. */
asciichat_error_t acip_quic_connection_allow_media(acip_quic_connection_t *connection);
/** Enable authenticated ACIP framing after the local ACIP handshake. This
 * queues a readiness marker and handles the peer's marker and acknowledgement.
 * The crypto context is borrowed until connection destruction. The endpoint
 * must serialize every operation on it, including rekeying, with this engine. */
asciichat_error_t acip_quic_connection_enable_security(acip_quic_connection_t *connection, crypto_context_t *crypto);
/** True after the peer's authenticated READY and acknowledgement are received. */
bool acip_quic_connection_security_ready(const acip_quic_connection_t *connection);
/** Copy one complete ACIP wire packet to its reliable stream. Admission is
 * bounded; ERROR_BUFFER_FULL leaves the queue unchanged. The connection owns
 * submitted bytes until ngtcp2 acknowledges them or the connection is freed. */
asciichat_error_t acip_quic_connection_submit(acip_quic_connection_t *connection, acip_traffic_class_t traffic_class,
                                              const void *packet, size_t length);
/* Maximum ACQD payload for the current path, or zero when unavailable. */
size_t acip_quic_connection_datagram_payload(const acip_quic_connection_t *connection);
asciichat_error_t acip_quic_connection_validate_datagram(const acip_quic_connection_t *connection,
                                                         acip_traffic_class_t traffic_class, uint64_t deadline_ns);
asciichat_error_t acip_quic_connection_submit_datagram(acip_quic_connection_t *connection,
                                                       acip_traffic_class_t traffic_class, const void *packet,
                                                       size_t length, uint64_t deadline_ns);
size_t acip_quic_connection_pending_bytes(const acip_quic_connection_t *connection);
/* Queue FIN after all previously admitted data for an open stream. No further
 * packets may be submitted to that class. ngtcp2 retransmits the FIN on loss. */
asciichat_error_t acip_quic_connection_finish_stream(acip_quic_connection_t *connection,
                                                     acip_traffic_class_t traffic_class);
bool acip_quic_connection_received_fin(const acip_quic_connection_t *connection, acip_traffic_class_t traffic_class);
void acip_quic_connection_destroy(acip_quic_connection_t **connection);

/* Client-only immediate migration after handshake confirmation. The new local
 * socket must already be bound; its owner keeps it alive for the connection. */
asciichat_error_t acip_quic_connection_migrate(acip_quic_connection_t *connection, const struct sockaddr *local,
                                               socklen_t length, uint64_t now_ns);
