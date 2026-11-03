#pragma once

#include <ascii-chat/network/acip/transport.h>

/** Native raw ACIP-over-QUIC endpoint. This is separate from HTTP/3
 * WebTransport and requires the platform socket subsystem to be initialized. */
typedef struct acip_quic_endpoint acip_quic_endpoint_t;

typedef struct {
  const char *alpn;             /**< NULL selects "acip/1". */
  const char *certificate_file; /**< Required for listeners. */
  const char *private_key_file; /**< Required for listeners. */
  const char *ca_file;          /**< NULL selects system trust for clients. */
  bool enable_datagrams;
} acip_quic_config_t;

/** Bind a concrete IPv4 or IPv6 local address and start the endpoint worker.
 * Configuration strings are consumed during this call. The caller retains
 * address storage. A listener requires a matching certificate and key. */
asciichat_error_t acip_quic_endpoint_open(const struct sockaddr *bind_address, socklen_t bind_length, bool listener,
                                          const acip_quic_config_t *config, acip_quic_endpoint_t **out);
asciichat_error_t acip_quic_endpoint_address(acip_quic_endpoint_t *endpoint, struct sockaddr_storage *address,
                                             socklen_t *length);

/** Start a client connection to a resolved address. server_name is used for
 * certificate identity verification and is required even for an IP address.
 * The returned transport is owned by the caller and is initially
 * negotiating TLS. Check capabilities for TLS readiness before ACIP handshake.
 * No application media is enabled until ACIP authentication and readiness. */
asciichat_error_t acip_quic_connect(acip_quic_endpoint_t *endpoint, const struct sockaddr *remote,
                                    socklen_t remote_length, const char *server_name, acip_transport_t **out);
/** Nonblocking accept. Returns ERROR_NETWORK_TIMEOUT when none is available.
 * A returned transport is owned and may still be negotiating TLS. */
asciichat_error_t acip_quic_accept(acip_quic_endpoint_t *endpoint, acip_transport_t **out);

/** Start the authenticated transition after the existing ACIP handshake.
 * crypto is borrowed and must outlive the transport. After this call the
 * caller must not access or rekey it until the transport has been released. */
asciichat_error_t acip_quic_transport_begin_security(acip_transport_t *transport, crypto_context_t *crypto);
bool acip_quic_transport_security_ready(acip_transport_t *transport);
asciichat_error_t acip_quic_transport_allow_media(acip_transport_t *transport);

/** Stop all users, then release through the endpoint, or destroy the transport
 * directly using acip_transport_destroy. The endpoint must outlive it. */
asciichat_error_t acip_quic_release(acip_quic_endpoint_t *endpoint, acip_transport_t **transport);
/** Stop and join the worker and interrupt remaining transports. Unclaimed
 * accepts are discarded. Claimed transports must still be destroyed. */
asciichat_error_t acip_quic_endpoint_stop(acip_quic_endpoint_t *endpoint);
/** Stop must succeed and all claimed transports must be destroyed first.
 * Lifecycle calls and destruction must not run concurrently with other calls. */
asciichat_error_t acip_quic_endpoint_destroy(acip_quic_endpoint_t **endpoint);
