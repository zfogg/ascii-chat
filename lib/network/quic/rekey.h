#pragma once
#include "security.h"
#include <ascii-chat/util/time.h>

/* Runs on the same lane as security. Send copies a plaintext ACIP packet into
 * its indicated reliable flow without reentering this coordinator. */
typedef asciichat_error_t (*acip_quic_rekey_send_fn)(void *, acip_traffic_class_t, const void *, size_t);
typedef struct {
  acip_quic_security_t *security;
  acip_quic_rekey_send_fn send;
  void *context;
  uint64_t activated_ns, last_ns, last_request_ns, exchange_ns, seen_epoch, peer_ack;
  uint8_t deferred_key[CRYPTO_PUBLIC_KEY_SIZE];
  bool deferred_request;
  bool requested, remote_requested, waiting_response, expecting_complete, markers_sent, ack_sent;
} acip_quic_rekey_t;

bool quic_rekey_packet(uint16_t type);
asciichat_error_t quic_rekey_init(acip_quic_rekey_t *state, acip_quic_security_t *security,
                                       acip_quic_rekey_send_fn send, void *context, uint64_t now_ns);
asciichat_error_t quic_rekey_tick(acip_quic_rekey_t *state, uint64_t now_ns);
asciichat_error_t quic_rekey_request(acip_quic_rekey_t *state, uint64_t now_ns);
/* Call immediately after authenticated open, before application dispatch.
 * This also observes a pending-key commit caused by media overtaking control. */
asciichat_error_t quic_rekey_receive(acip_quic_rekey_t *state, acip_traffic_class_t flow, acip_delivery_t delivery,
                                          const void *packet, size_t length, uint64_t now_ns, bool *consumed);
void quic_rekey_cleanup(acip_quic_rekey_t *state);
