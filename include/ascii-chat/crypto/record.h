#pragma once
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/asciichat_errno.h>
#include <limits.h>

#define CRYPTO_RECORD_CHANNELS 3
#define CRYPTO_RECORD_RELIABLE 0
#define CRYPTO_RECORD_DATAGRAM 1
#define CRYPTO_RECORD_ANY_CHANNEL UINT_MAX

/** Authenticated records over independent ordered channels and unordered delivery.
 * The caller serializes operations and keeps the borrowed crypto context alive.
 * One previous receive epoch survives until each channel crosses its marker. */
typedef struct crypto_record {
  crypto_context_t *crypto;
  uint64_t key_epoch, last_receive_epoch;
  crypto_context_t previous_crypto;
  uint64_t previous_epoch;
  bool previous_valid, pending_prepared, previous_allowed[CRYPTO_RECORD_CHANNELS];
  uint64_t send_sequence[CRYPTO_RECORD_CHANNELS][2];
  uint64_t receive_high[CRYPTO_RECORD_CHANNELS][2];
  uint64_t datagram_seen[CRYPTO_RECORD_CHANNELS];
  unsigned last_receive_channel, last_receive_delivery;
  bool server;
} crypto_record_t;

typedef asciichat_error_t (*crypto_record_validate_fn)(void *, unsigned, unsigned, const void *, size_t);
asciichat_error_t crypto_record_init(crypto_record_t *, crypto_context_t *, bool server);
/** Output buffers belong to the buffer library. No packet headers or CRCs are added here. */
asciichat_error_t crypto_record_seal_into(crypto_record_t *, unsigned channel, unsigned delivery, const void *, size_t,
                                          uint8_t *, size_t, size_t *);
asciichat_error_t crypto_record_open_sequence(crypto_record_t *, unsigned channel, unsigned delivery, const void *,
                                              size_t, uint8_t **, size_t *, uint64_t *, crypto_record_validate_fn,
                                              void *);
asciichat_error_t crypto_record_prepare_epoch(crypto_record_t *);
asciichat_error_t crypto_record_commit_epoch(crypto_record_t *);
asciichat_error_t crypto_record_retire_epoch_flow(crypto_record_t *, unsigned channel, uint64_t epoch);
void crypto_record_cleanup(crypto_record_t *);
