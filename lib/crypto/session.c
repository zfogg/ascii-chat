#include <ascii-chat/crypto/session.h>
#include <ascii-chat/common.h>
#include <sodium.h>
#include <string.h>

static asciichat_error_t emit_message(void *context, unsigned channel, crypto_rekey_message_t type, const void *payload,
                                      size_t length) {
  crypto_session_t *session = context;
  if (session->count == CRYPTO_SESSION_MESSAGES || length > CRYPTO_PUBLIC_KEY_SIZE)
    return SET_ERRNO(ERROR_BUFFER_FULL, "Security control queue is full");
  crypto_session_message_t *message = &session->messages[(session->head + session->count) % CRYPTO_SESSION_MESSAGES];
  *message = (crypto_session_message_t){.channel = channel, .type = type, .length = length};
  if (length)
    memcpy(message->payload, payload, length);
  ++session->count;
  return ASCIICHAT_OK;
}

asciichat_error_t crypto_session_create(crypto_context_t *crypto) {
  if (!crypto || crypto->session)
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid security session owner");
  crypto_session_t *session = SAFE_CALLOC(1, sizeof(*session), crypto_session_t *);
  if (!session)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate security session");
  if (mutex_init(&session->mutex, "crypto_session") != 0) {
    SAFE_FREE(session);
    return SET_ERRNO(ERROR_THREAD, "Cannot initialize security session mutex");
  }
  session->owner = crypto;
  crypto->session = session;
  return ASCIICHAT_OK;
}

void crypto_session_destroy(crypto_context_t *crypto) {
  if (!crypto || !crypto->session)
    return;
  crypto_session_t *session = crypto->session;
  crypto->session = NULL;
  crypto_rekey_session_cleanup(&session->rotation);
  crypto_record_cleanup(&session->records);
  mutex_destroy(&session->mutex);
  sodium_memzero(session, sizeof(*session));
  SAFE_FREE(session);
}

asciichat_error_t crypto_session_lock(crypto_context_t *crypto, crypto_session_t **output) {
  if (!crypto || !output || !crypto->session || crypto->session->owner != crypto)
    return SET_ERRNO(ERROR_INVALID_STATE, "Missing security session owner");
  crypto_session_t *session = crypto->session;
  mutex_lock(&session->mutex);
  if (!session->failure && !session->records.crypto) {
    bool server = crypto->server_role_known
                      ? crypto->server_role
                      : memcmp(crypto->public_key, crypto->peer_public_key, CRYPTO_PUBLIC_KEY_SIZE) > 0;
    session->failure = crypto_record_init(&session->records, crypto, server);
    if (!session->failure)
      session->failure =
          crypto_rekey_session_init(&session->rotation, &session->records, emit_message, session, time_get_ns());
  }
  if (session->failure) {
    asciichat_error_t result = session->failure;
    mutex_unlock(&session->mutex);
    return result;
  }
  *output = session;
  return ASCIICHAT_OK;
}

void crypto_session_unlock(crypto_session_t *session) {
  mutex_unlock(&session->mutex);
}

asciichat_error_t crypto_session_tick(crypto_session_t *session, uint64_t now) {
  if (!session->failure)
    session->failure = crypto_rekey_session_tick(&session->rotation, now);
  return session->failure;
}

asciichat_error_t crypto_session_receive(crypto_session_t *session, unsigned channel, unsigned delivery,
                                         crypto_rekey_message_t type, const void *payload, size_t length, uint64_t now,
                                         bool *consumed) {
  if (!session->failure)
    session->failure =
        crypto_rekey_session_receive(&session->rotation, channel, delivery, type, payload, length, now, consumed);
  return session->failure;
}
