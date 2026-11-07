#pragma once
#include <ascii-chat/crypto/rekey.h>
#include <ascii-chat/platform/mutex.h>

#define CRYPTO_SESSION_MESSAGES 16

typedef struct {
  unsigned channel;
  crypto_rekey_message_t type;
  size_t length;
  uint8_t payload[CRYPTO_PUBLIC_KEY_SIZE];
} crypto_session_message_t;

typedef struct crypto_session {
  crypto_context_t *owner;
  mutex_t mutex;
  crypto_record_t records;
  crypto_rekey_session_t rotation;
  crypto_session_message_t messages[CRYPTO_SESSION_MESSAGES];
  size_t head, count;
  asciichat_error_t failure;
} crypto_session_t;

asciichat_error_t crypto_session_create(crypto_context_t *);
void crypto_session_destroy(crypto_context_t *);
/** Lock and activate the common authenticated-record session. The caller must
 * unlock on every successful return. No transport callbacks run under this lock. */
asciichat_error_t crypto_session_lock(crypto_context_t *, crypto_session_t **);
void crypto_session_unlock(crypto_session_t *);
asciichat_error_t crypto_session_tick(crypto_session_t *, uint64_t now_ns);
asciichat_error_t crypto_session_receive(crypto_session_t *, unsigned, unsigned, crypto_rekey_message_t, const void *,
                                         size_t, uint64_t now_ns, bool *consumed);
