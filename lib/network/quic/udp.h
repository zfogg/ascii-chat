#pragma once

#include "connection.h"

/* Private, single-lane UDP driver. The caller owns TLS contexts and must keep
 * them alive until after endpoint destruction. Accepted and connected handles
 * are borrowed from the endpoint and become invalid when removed or destroyed.
 * The accept callback runs on the pump lane before the Initial is processed;
 * it may install the packet receiver but must not reenter the endpoint. */
typedef struct acip_quic_udp_endpoint acip_quic_udp_endpoint_t;
typedef asciichat_error_t (*acip_quic_udp_accept_fn)(acip_quic_connection_t *, void *);

asciichat_error_t acip_quic_udp_endpoint_create(const struct sockaddr *bind_address, socklen_t bind_length,
                                                acip_quic_tls_t *server_tls, acip_quic_udp_accept_fn accept,
                                                void *context, acip_quic_udp_endpoint_t **out);
asciichat_error_t acip_quic_udp_endpoint_address(const acip_quic_udp_endpoint_t *endpoint,
                                                 struct sockaddr_storage *address, socklen_t *length);
asciichat_error_t acip_quic_udp_connect(acip_quic_udp_endpoint_t *endpoint, acip_quic_tls_t *client_tls,
                                        const char *server_name, const struct sockaddr *remote, socklen_t remote_length,
                                        uint64_t now_ns, acip_quic_connection_t **out);
/* Nonblocking bounded work. A per-connection failure is returned with *failed
 * set to its borrowed handle; the caller may remove it and continue the other
 * connections. Socket-level failures leave *failed NULL. */
asciichat_error_t acip_quic_udp_pump(acip_quic_udp_endpoint_t *endpoint, uint64_t now_ns,
                                     acip_quic_connection_t **failed);
/* After pumping, obtain the earliest timer and whether a socket-blocked
 * datagram needs write readiness. Call on the serialized endpoint lane. */
asciichat_error_t acip_quic_udp_wait_parameters(const acip_quic_udp_endpoint_t *endpoint, uint64_t now_ns,
                                                uint64_t maximum_wait_ns, uint64_t *wait_ns, bool *want_write);
/* Wait outside the endpoint lane. The owner must keep the endpoint/socket
 * alive until the wait and all wake calls have returned. A wake is coalescible
 * and must be followed by a pump; it carries no application data. */
asciichat_error_t acip_quic_udp_wait(acip_quic_udp_endpoint_t *endpoint, uint64_t wait_ns, bool want_write,
                                     bool *ready);
asciichat_error_t acip_quic_udp_wake(acip_quic_udp_endpoint_t *endpoint);
asciichat_error_t acip_quic_udp_remove(acip_quic_udp_endpoint_t *endpoint, acip_quic_connection_t *connection);
void acip_quic_udp_endpoint_destroy(acip_quic_udp_endpoint_t **endpoint);
