#pragma once

#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/network/acip/transport.h>

/** Per-peer, event-driven negotiation. The caller owns and configures handshake.
 * Callers serialize receive/start and keep the transport alive until cleanup.
 * Media must wait for ready, including authentication-only connections.
 */
typedef struct acip_security {
  crypto_handshake_context_t handshake;
  uint8_t mode;
  bool forbid_encryption;
  bool version_received;
  bool parameters_received;
  bool ready;
} acip_security_t;

asciichat_error_t acip_security_start(acip_security_t *security, acip_transport_t *transport);
asciichat_error_t acip_security_receive(acip_security_t *security, acip_transport_t *transport, packet_type_t type,
                                        const void *payload, size_t length);

/** Initialize a peer from native CLI options; cleanup releases owned credentials. */
asciichat_error_t acip_security_init_options(acip_security_t *security, bool server);
void acip_security_cleanup(acip_security_t *security);

/** Transfer security ownership on success. Ready fires only after negotiation. */
acip_transport_t *acip_webrtc_transport_create_secure(struct webrtc_peer_connection *peer,
                                                      struct webrtc_data_channel *channel, acip_security_t *security,
                                                      void (*on_ready)(acip_transport_t *, void *), void *user_data);
