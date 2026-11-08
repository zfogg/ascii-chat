#pragma once

#include <ascii-chat/network/acip/transport.h>

/** QUIC endpoint for native ACIP and optional HTTP/3 WebTransport. A listener
 * can negotiate both wire protocols by ALPN on one UDP socket. Requires the
 * platform socket subsystem to be initialized. */
typedef struct acip_quic_endpoint acip_quic_endpoint_t;

typedef struct {
  const char *alpn;                /**< NULL selects "acip/1". */
  const char *certificate_file;    /**< Required for listeners. */
  const char *private_key_file;    /**< Required for listeners. */
  const char *ca_file;             /**< NULL selects system trust for clients. */
  const char *webtransport_origin; /**< Exact HTTPS origin; enables the server HTTP/3 listener. */
  const char *webtransport_path;   /**< CONNECT path, required for WebTransport. */
  bool enable_datagrams;
  bool accept_native;           /**< Also accept acip/1 on a server WebTransport listener. */
  bool prefer_visual_datagrams; /**< Prefer negotiated visual datagrams. */
  bool prefer_audio_datagrams;  /**< Send audio as datagrams when negotiated; otherwise use reliable streams. */
} acip_quic_config_t;

/** Bind a concrete IPv4 or IPv6 local address and start the endpoint worker.
 * Configuration strings are consumed during this call. The caller retains
 * address storage. A listener requires a matching certificate and key. */
asciichat_error_t quic_endpoint_open(const struct sockaddr *bind_address, socklen_t bind_length, bool listener,
                                          const acip_quic_config_t *config, acip_quic_endpoint_t **out);
asciichat_error_t quic_endpoint_address(acip_quic_endpoint_t *endpoint, struct sockaddr_storage *address,
                                             socklen_t *length);

/** Start a client connection to a resolved address. server_name is used for
 * certificate identity verification and is required even for an IP address.
 * The returned transport is owned by the caller and is initially
 * negotiating TLS. Check capabilities for TLS readiness before ACIP handshake.
 * No application media is enabled until ACIP authentication and readiness. */
asciichat_error_t quic_connect(acip_quic_endpoint_t *endpoint, const struct sockaddr *remote,
                                    socklen_t remote_length, const char *server_name, acip_transport_t **out);
/** Nonblocking accept. Returns ERROR_NETWORK_TIMEOUT when none is available.
 * A returned transport is owned and may still be negotiating TLS. */
asciichat_error_t quic_accept(acip_quic_endpoint_t *endpoint, acip_transport_t **out);
/** Copy the current peer address. It may change after a validated migration. */
asciichat_error_t quic_transport_peer_address(acip_transport_t *transport, struct sockaddr_storage *address,
                                                   socklen_t *length);

/** Start the authenticated transition after the existing ACIP handshake.
 * crypto is borrowed and must outlive the transport. After this call the
 * caller must not access or rekey it until the transport has been released. */
asciichat_error_t quic_transport_begin_security(acip_transport_t *transport, crypto_context_t *crypto);
bool quic_transport_security_ready(acip_transport_t *transport);
/** Wait at most timeout_ns for the authenticated readiness exchange. */
asciichat_error_t quic_transport_wait_security(acip_transport_t *transport, uint64_t timeout_ns);
asciichat_error_t quic_transport_allow_media(acip_transport_t *transport);

/** Stop all users, then release through the endpoint, or destroy the transport
 * directly using acip_transport_destroy. The endpoint must outlive it. */
asciichat_error_t quic_release(acip_quic_endpoint_t *endpoint, acip_transport_t **transport);
/** Stop and join the worker and interrupt remaining transports. Unclaimed
 * accepts are discarded. Claimed transports must still be destroyed. */
asciichat_error_t quic_endpoint_stop(acip_quic_endpoint_t *endpoint);
/** Stop must succeed and all claimed transports must be destroyed first.
 * Lifecycle calls and destruction must not run concurrently with other calls. */
asciichat_error_t quic_endpoint_destroy(acip_quic_endpoint_t **endpoint);

/** Move a single-connection client endpoint to a new concrete local address.
 * Port zero requests a new ephemeral port. TLS, ACIP keys, and stream state
 * survive; ngtcp2 validates the new path after the immediate switch. Migration
 * requires handshake confirmation, a spare peer CID, and peer permission.
 * Pause transport users first, as for endpoint lifecycle calls. This call stops
 * and restarts the worker to avoid closing a socket under its readiness wait.
 * A rejected migration retains the original socket and live connection.
 * The new address must have the same family as the existing remote address. */
asciichat_error_t quic_endpoint_migrate(acip_quic_endpoint_t *endpoint, const struct sockaddr *local_address,
                                             socklen_t length);
