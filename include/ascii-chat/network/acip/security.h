#pragma once

#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/crypto/rekey.h>
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/acip/transport.h>

/** Per-peer, event-driven negotiation. The caller owns and configures handshake.
 * @brief acip security is intentionally a small, concrete record. Fields such as `handshake`, `mode`, `forbid_encryption`, `require_encryption` give callers the identity and current value they need. It is normally borrowed from, or owned by, the surrounding subsystem—not independently managed.
 * Callers serialize receive/start and keep the transport alive until cleanup.
 * Media must wait for ready, including authentication-only connections.
 */
typedef struct acip_security {
  crypto_handshake_context_t handshake;
  uint8_t mode;
  bool forbid_encryption;
  bool require_encryption;
  bool version_received;
  bool parameters_received;
  bool ready;
} acip_security_t;

asciichat_error_t acip_security_start(acip_security_t *security, acip_transport_t *transport);
asciichat_error_t acip_security_receive(acip_security_t *security, acip_transport_t *transport, packet_type_t type,
                                        const void *payload, size_t length);

/** Initialize a peer from native CLI options; cleanup releases owned credentials. */
asciichat_error_t acip_security_init_options(acip_security_t *security, bool server, bool transport_secure,
                                             const char *password);
void acip_security_cleanup(acip_security_t *security);

/** Transfer security ownership on success. Ready fires only after negotiation. */
acip_transport_t *acip_webrtc_transport_create_secure(struct webrtc_peer_connection *peer,
                                                      struct webrtc_data_channel *channel, acip_security_t *security,
                                                      void (*on_ready)(acip_transport_t *, void *), void *user_data);
/** Borrow security already owned by an authenticated peer transport. */
acip_security_t *acip_webrtc_transport_security(acip_transport_t *transport);

bool acip_security_rekey_packet(uint16_t type);
asciichat_error_t acip_security_rekey_encode(unsigned flow, crypto_rekey_message_t, const void *, size_t, void *,
                                             size_t, size_t *);
asciichat_error_t acip_security_rekey_receive(crypto_rekey_session_t *, acip_traffic_class_t, acip_delivery_t,
                                              const void *, size_t, uint64_t now_ns, bool *consumed);

asciichat_error_t acip_security_rekey_seal(crypto_record_t *, unsigned, crypto_rekey_message_t, const void *, size_t,
                                           uint8_t **, size_t *);

asciichat_error_t acip_security_activate(acip_transport_t *);
asciichat_error_t acip_security_flush(acip_transport_t *);
asciichat_error_t acip_security_request_rekey(acip_transport_t *);
asciichat_error_t acip_security_decrypt(crypto_context_t *, unsigned, unsigned, const void *, size_t, uint8_t **,
                                        size_t *, uint64_t *, bool *consumed);

asciichat_error_t acip_security_seal(crypto_context_t *, acip_traffic_class_t, acip_delivery_t, const void *, size_t,
                                     uint8_t **, size_t *);
asciichat_error_t acip_security_open(crypto_context_t *, acip_traffic_class_t, acip_delivery_t, const void *, size_t,
                                     uint8_t **, size_t *, uint64_t *, bool *);

asciichat_error_t acip_security_next_control(crypto_context_t *, acip_traffic_class_t *, uint8_t **, size_t *);
