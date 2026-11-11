#pragma once
#include <ascii-chat/crypto/record.h>
#include <ascii-chat/util/time.h>

/** Logical rotation messages; the protocol adapter owns their wire encoding. */
typedef enum {
  CRYPTO_REKEY_NONE,
  CRYPTO_REKEY_REQUEST,
  CRYPTO_REKEY_RESPONSE,
  CRYPTO_REKEY_COMPLETE,
  CRYPTO_REKEY_MARKER,
  CRYPTO_REKEY_ACK,
  CRYPTO_REKEY_NEEDED,
} crypto_rekey_message_t;
typedef asciichat_error_t (*crypto_rekey_emit_fn)(void *, unsigned, crypto_rekey_message_t, const void *, size_t);

/**
 * @brief crypto rekey session is intentionally a small, concrete record. Fields such as `security`, `send`, `context`, `activated_ns` give callers the identity and current value they need. It is normally borrowed from, or owned by, the surrounding subsystem—not independently managed.
 */
typedef struct {
  crypto_record_t *security;
  crypto_rekey_emit_fn send;
  void *context;
  uint64_t activated_ns, last_ns, last_request_ns, exchange_ns, seen_epoch, peer_ack;
  uint8_t deferred_key[CRYPTO_PUBLIC_KEY_SIZE];
  bool deferred_request;
  bool requested, remote_requested, waiting_response, expecting_complete, markers_sent, ack_sent;
} crypto_rekey_session_t;

/** The caller serializes access with the associated record context. */
asciichat_error_t crypto_rekey_session_init(crypto_rekey_session_t *, crypto_record_t *, crypto_rekey_emit_fn, void *,
                                            uint64_t now_ns);
asciichat_error_t crypto_rekey_session_tick(crypto_rekey_session_t *, uint64_t now_ns);
asciichat_error_t crypto_rekey_session_request(crypto_rekey_session_t *, uint64_t now_ns);
asciichat_error_t crypto_rekey_session_receive(crypto_rekey_session_t *, unsigned, unsigned, crypto_rekey_message_t,
                                               const void *, size_t, uint64_t now_ns, bool *consumed);
void crypto_rekey_session_cleanup(crypto_rekey_session_t *);
