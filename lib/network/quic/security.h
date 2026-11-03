#pragma once

#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/network/acip/traffic.h>
#include <ascii-chat/asciichat_errno.h>

/* Private QUIC ACIP encryption framing. All operations, including access to
 * the borrowed crypto context and rekeying, belong to one serialized lane.
 * Zero-initialize once and retain exactly one state per context and key epoch.
 * Construct a fresh state only after an acknowledged key-epoch transition. */
typedef struct {
  crypto_context_t *crypto;
  uint64_t key_epoch;
  uint64_t send_sequence[3][2];
  uint64_t receive_high[3][2];
  uint64_t datagram_seen[3];
  bool server;
} acip_quic_security_t;

asciichat_error_t acip_quic_security_init(acip_quic_security_t *state, crypto_context_t *crypto, bool server);
/* Returns a buffer-owned encrypted ACIP packet. Caller releases *out with
 * buffer_release. Encrypted metadata binds direction, class, and delivery. */
asciichat_error_t acip_quic_security_seal(acip_quic_security_t *state, acip_traffic_class_t traffic_class,
                                          acip_delivery_t delivery, const void *packet, size_t length, uint8_t **out,
                                          size_t *out_length);
/* Authenticates before updating replay state. On success returns a buffer-owned
 * plaintext ACIP packet. Datagram replay window is 64 packets per media flow;
 * reliable packets are strictly increasing independently on each stream. */
asciichat_error_t acip_quic_security_open(acip_quic_security_t *state, acip_traffic_class_t traffic_class,
                                          acip_delivery_t delivery, const void *packet, size_t length, uint8_t **out,
                                          size_t *out_length);
