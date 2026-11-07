#include <ascii-chat/crypto/rekey.h>
#include <ascii-chat/util/endian.h>
#include <sodium.h>
#include <string.h>

#define CRYPTO_REKEY_TIMEOUT_NS (30ULL * NS_PER_SEC_INT)

static asciichat_error_t clock_step(crypto_rekey_session_t *s, uint64_t now) {
  if (!s || !s->security || !s->security->crypto || !s->send || now < s->last_ns || now == UINT64_MAX)
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid session rekey state or monotonic clock");
  s->last_ns = now;
  return ASCIICHAT_OK;
}

static asciichat_error_t send_rekey_packet(crypto_rekey_session_t *s, unsigned flow, crypto_rekey_message_t type,
                                           const void *payload, size_t length) {
  if (length > CRYPTO_PUBLIC_KEY_SIZE)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Rekey message exceeds its bound");
  return s->send(s->context, flow, type, payload, length);
}

static bool settled(crypto_rekey_session_t *s) {
  return !s->deferred_request && !s->waiting_response && !s->expecting_complete && !s->remote_requested &&
         !s->security->pending_prepared && !s->security->previous_valid && s->peer_ack == s->security->key_epoch;
}

static asciichat_error_t observe_epoch(crypto_rekey_session_t *s) {
  if (s->seen_epoch != s->security->key_epoch) {
    if (s->seen_epoch == UINT64_MAX || s->security->key_epoch != s->seen_epoch + 1 || !s->exchange_ns)
      return SET_ERRNO(ERROR_CRYPTO, "Unexpected session key epoch transition");
    s->seen_epoch = s->security->key_epoch;
    s->markers_sent = true;
    s->ack_sent = false;
    uint64_t epoch = HOST_TO_NET_U64(s->seen_epoch);
    for (unsigned flow = 0; flow < CRYPTO_RECORD_CHANNELS; ++flow) {
      asciichat_error_t result = send_rekey_packet(s, (unsigned)flow, CRYPTO_REKEY_MARKER, &epoch, sizeof(epoch));
      if (result != ASCIICHAT_OK)
        return result;
    }
  }
  if (s->markers_sent && !s->ack_sent && !s->security->previous_valid && !s->expecting_complete) {
    uint64_t epoch = HOST_TO_NET_U64(s->seen_epoch);
    asciichat_error_t result = send_rekey_packet(s, 0, CRYPTO_REKEY_ACK, &epoch, sizeof(epoch));
    if (result != ASCIICHAT_OK)
      return result;
    s->ack_sent = true;
  }
  if (settled(s))
    s->exchange_ns = 0;
  return ASCIICHAT_OK;
}

asciichat_error_t crypto_rekey_session_init(crypto_rekey_session_t *s, crypto_record_t *security,
                                            crypto_rekey_emit_fn send, void *context, uint64_t now) {
  if (!s || s->security || !security || !security->crypto || !send || now == UINT64_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid session rekey initialization");
  *s = (crypto_rekey_session_t){.security = security,
                                .send = send,
                                .context = context,
                                .activated_ns = now,
                                .last_ns = now,
                                .seen_epoch = security->key_epoch,
                                .peer_ack = security->key_epoch};
  return ASCIICHAT_OK;
}

static bool request_allowed(crypto_rekey_session_t *s, uint64_t now) {
  return now - s->activated_ns >= (uint64_t)REKEY_MIN_INTERVAL &&
         (!s->last_request_ns || now - s->last_request_ns >= (uint64_t)REKEY_MIN_REQUEST_INTERVAL);
}

asciichat_error_t crypto_rekey_session_tick(crypto_rekey_session_t *s, uint64_t now) {
  asciichat_error_t result = clock_step(s, now);
  if (result != ASCIICHAT_OK)
    return result;
  result = observe_epoch(s);
  if (result != ASCIICHAT_OK)
    return result;
  if (s->exchange_ns && now - s->exchange_ns > CRYPTO_REKEY_TIMEOUT_NS)
    return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "session key exchange or flow retirement timed out");
  // A peer measures its interval at send time. Keep one authenticated request
  // until our receive-side interval elapses; network jitter must not turn a
  // valid rotation into a protocol violation or bypass the rate limit.
  if (s->deferred_request && request_allowed(s, now)) {
    crypto_context_t *crypto = s->security->crypto;
    s->deferred_request = false;
    s->expecting_complete = true;
    s->last_request_ns = now;
    if (crypto_rekey_init(crypto) != CRYPTO_OK || crypto_rekey_process_request(crypto, s->deferred_key) != CRYPTO_OK)
      return SET_ERRNO(ERROR_CRYPTO, "Cannot compute session rekey response");
    sodium_memzero(s->deferred_key, sizeof(s->deferred_key));
    result = crypto_record_prepare_epoch(s->security);
    if (result == ASCIICHAT_OK)
      result = send_rekey_packet(s, 0, CRYPTO_REKEY_RESPONSE, crypto->temp_public_key, CRYPTO_PUBLIC_KEY_SIZE);
    return result;
  }
  if (!settled(s) || !request_allowed(s, now))
    return ASCIICHAT_OK;
  crypto_context_t *crypto = s->security->crypto;
  bool threshold =
      crypto->rekey_packet_count >= crypto->rekey_packet_threshold ||
      (crypto->rekey_time_threshold > 0 && now - s->activated_ns >= (uint64_t)crypto->rekey_time_threshold);
  if (!s->requested && !threshold)
    return ASCIICHAT_OK;
  s->requested = false;
  s->exchange_ns = now;
  if (!s->security->server) {
    s->remote_requested = true;
    return send_rekey_packet(s, 0, CRYPTO_REKEY_NEEDED, NULL, 0);
  }
  if (crypto_rekey_init(crypto) != CRYPTO_OK)
    return SET_ERRNO(ERROR_CRYPTO, "Cannot initiate session ephemeral rekey");
  s->waiting_response = true;
  s->last_request_ns = now;
  return send_rekey_packet(s, 0, CRYPTO_REKEY_REQUEST, crypto->temp_public_key, CRYPTO_PUBLIC_KEY_SIZE);
}

asciichat_error_t crypto_rekey_session_request(crypto_rekey_session_t *s, uint64_t now) {
  asciichat_error_t result = clock_step(s, now);
  if (result != ASCIICHAT_OK)
    return result;
  if (!s->exchange_ns)
    s->requested = true;
  return crypto_rekey_session_tick(s, now);
}

asciichat_error_t crypto_rekey_session_receive(crypto_rekey_session_t *s, unsigned flow, unsigned delivery,
                                               crypto_rekey_message_t type, const void *payload, size_t bytes,
                                               uint64_t now, bool *consumed) {
  if (!consumed || (!payload && bytes) || flow >= CRYPTO_RECORD_CHANNELS || type > CRYPTO_REKEY_NEEDED)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid rekey input");
  *consumed = false;
  asciichat_error_t result = clock_step(s, now);
  if (result != ASCIICHAT_OK)
    return result;
  result = observe_epoch(s);
  if (result != ASCIICHAT_OK)
    return result;
  if (type == CRYPTO_REKEY_NONE)
    return crypto_rekey_session_tick(s, now);
  *consumed = true;
  uint64_t epoch = s->security->key_epoch;
  if (delivery != CRYPTO_RECORD_RELIABLE || s->security->last_receive_epoch != epoch ||
      (type != CRYPTO_REKEY_MARKER && flow != 0))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "session rekey message has invalid flow, delivery, or epoch");
  crypto_context_t *crypto = s->security->crypto;
  if (type == CRYPTO_REKEY_MARKER || type == CRYPTO_REKEY_ACK) {
    uint64_t value;
    if (bytes != sizeof(value))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid session epoch marker length");
    memcpy(&value, payload, sizeof(value));
    if (!epoch || NET_TO_HOST_U64(value) != epoch || !s->markers_sent)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected session epoch marker");
    if (type == CRYPTO_REKEY_MARKER) {
      if (!s->security->previous_valid || !s->security->previous_allowed[flow])
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Duplicate session flow retirement marker");
      result = crypto_record_retire_epoch_flow(s->security, flow, epoch);
    } else {
      s->peer_ack = epoch;
    }
  } else if (type == CRYPTO_REKEY_NEEDED) {
    if (!s->security->server || bytes)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid session rekey request signal");
    if (!s->exchange_ns)
      s->requested = true;
  } else if (type == CRYPTO_REKEY_REQUEST) {
    if (s->security->server || bytes != CRYPTO_PUBLIC_KEY_SIZE || s->waiting_response || s->expecting_complete ||
        s->deferred_request || s->security->previous_valid || s->security->pending_prepared || s->peer_ack != epoch)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected session ephemeral rekey request");
    s->exchange_ns = now;
    s->remote_requested = false;
    s->deferred_request = true;
    memcpy(s->deferred_key, payload, sizeof(s->deferred_key));
  } else if (type == CRYPTO_REKEY_RESPONSE) {
    if (!s->security->server || !s->waiting_response || bytes != CRYPTO_PUBLIC_KEY_SIZE)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected session ephemeral rekey response");
    if (crypto_rekey_process_response(crypto, payload) != CRYPTO_OK)
      return SET_ERRNO(ERROR_CRYPTO, "Cannot compute session next epoch");
    result = crypto_record_prepare_epoch(s->security);
    if (result == ASCIICHAT_OK)
      result = crypto_record_commit_epoch(s->security);
    if (result == ASCIICHAT_OK) {
      s->waiting_response = false;
      s->activated_ns = now;
      uint8_t proof = 0;
      result = send_rekey_packet(s, 0, CRYPTO_REKEY_COMPLETE, &proof, 1);
    }
  } else {
    if (s->security->server || !s->expecting_complete || bytes != 1 || ((const uint8_t *)payload)[0] || !epoch ||
        s->security->pending_prepared)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected session rekey confirmation");
    s->expecting_complete = false;
    s->activated_ns = now;
  }
  if (result != ASCIICHAT_OK)
    return result;
  result = observe_epoch(s);
  if (result != ASCIICHAT_OK)
    return result;
  return crypto_rekey_session_tick(s, now);
}

void crypto_rekey_session_cleanup(crypto_rekey_session_t *s) {
  if (!s)
    return;
  if (s->exchange_ns && s->security && s->security->crypto && s->security->crypto->rekey_in_progress)
    crypto_rekey_abort(s->security->crypto);
  sodium_memzero(s, sizeof(*s));
}
