#pragma once

#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/network/acip/traffic.h>
#include <ascii-chat/asciichat_errno.h>

/* Private QUIC ACIP encryption framing. All operations, including access to
 * the borrowed crypto context and rekeying, belong to one serialized lane.
 * Zero-initialize once; flow sequences and replay windows span key epochs.
 * One previous receive epoch survives until each reliable flow crosses its marker. */
typedef struct {
  crypto_context_t *crypto;
  uint64_t key_epoch;
  crypto_context_t previous_crypto;
  uint64_t previous_epoch;
  bool previous_valid, pending_prepared, previous_allowed[3];
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
/* As above, also return the authenticated per-flow sequence after validation. */
asciichat_error_t acip_quic_security_open_sequence(acip_quic_security_t *state, acip_traffic_class_t traffic_class,
                                                   acip_delivery_t delivery, const void *packet, size_t length,
                                                   uint8_t **out, size_t *out_length, uint64_t *sequence_out);

/* Prepare only after an authenticated DH exchange computed temp_shared_key.
 * A valid packet in the next epoch can then prove peer key possession even
 * when its media flow overtakes the reliable control confirmation. */
asciichat_error_t acip_quic_security_prepare_epoch(acip_quic_security_t *state);
asciichat_error_t acip_quic_security_commit_epoch(acip_quic_security_t *state);
/* Called after a new-epoch marker arrives in order on this reliable flow.
 * Delayed datagrams from the previous epoch become stale on that flow. */
asciichat_error_t acip_quic_security_retire_epoch_flow(acip_quic_security_t *state, acip_traffic_class_t traffic_class,
                                                       uint64_t epoch);
/* Wipes retained key material without destroying the borrowed crypto context. */
void acip_quic_security_cleanup(acip_quic_security_t *state);
