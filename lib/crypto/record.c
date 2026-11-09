#include <ascii-chat/tooling/defer/defer.h>
#include <ascii-chat/crypto/record.h>

#include <ascii-chat/buffer.h>
#include <ascii-chat/util/endian.h>
#include <string.h>
#include <sodium.h>

#define CRYPTO_RECORD_PREFIX_SIZE 24
#define CRYPTO_RECORD_EPOCH_SIZE 8

static bool valid_flow(unsigned traffic_class, unsigned delivery) {
  return (unsigned)traffic_class <= (CRYPTO_RECORD_CHANNELS - 1) && (unsigned)delivery <= CRYPTO_RECORD_DATAGRAM;
}

static bool ready(const crypto_record_t *state) {
  return state && state->crypto && state->crypto->initialized && state->crypto->encrypt_data &&
         state->crypto->key_exchange_complete && state->crypto->handshake_complete &&
         state->crypto->nonce_size == CRYPTO_NONCE_SIZE && state->crypto->mac_size == CRYPTO_MAC_SIZE &&
         state->crypto->rekey_count == state->key_epoch;
}

asciichat_error_t crypto_record_init(crypto_record_t *state, crypto_context_t *crypto, bool server) {
  if (!state || state->crypto || !crypto || !crypto->initialized || !crypto->encrypt_data ||
      !crypto->key_exchange_complete || !crypto->handshake_complete || crypto->nonce_size != CRYPTO_NONCE_SIZE ||
      crypto->mac_size != CRYPTO_MAC_SIZE || crypto->rekey_in_progress)
    return SET_ERRNO(ERROR_INVALID_STATE, "Session handshake and encryption must be ready");
  *state = (crypto_record_t){.crypto = crypto, .key_epoch = crypto->rekey_count, .server = server};
  return ASCIICHAT_OK;
}

asciichat_error_t crypto_record_seal_into(crypto_record_t *state, unsigned traffic_class, unsigned delivery,
                                          const void *packet, size_t length, uint8_t *wire, size_t capacity,
                                          size_t *out_length) {
  return crypto_record_seal_into_domain(state, traffic_class, delivery, packet, length, wire, capacity, out_length,
                                        NULL);
}

asciichat_error_t crypto_record_seal_into_domain(crypto_record_t *state, unsigned traffic_class, unsigned delivery,
                                                 const void *packet, size_t length, uint8_t *wire, size_t capacity,
                                                 size_t *out_length, buffer_domain_t *domain) {
  if (!wire || !out_length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing Record encrypted packet output");
  *out_length = 0;
  if (!ready(state) || !valid_flow(traffic_class, delivery))
    return SET_ERRNO(ERROR_INVALID_STATE, "Record encryption is not ready for this flow");
  if (!packet || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing record plaintext");
  if (length > CRYPTO_MAX_PLAINTEXT_SIZE - CRYPTO_RECORD_PREFIX_SIZE ||
      state->send_sequence[traffic_class][delivery] == UINT64_MAX)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Record packet or sequence limit exceeded");
  size_t plain_length = CRYPTO_RECORD_PREFIX_SIZE + length;
  size_t cipher_capacity = plain_length + state->crypto->nonce_size + state->crypto->mac_size;
  if (capacity < CRYPTO_RECORD_EPOCH_SIZE + cipher_capacity)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Record ciphertext output is too small");
  uint8_t *plain = buffer_alloc(domain, plain_length);
  if (!plain) {
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate Record packet");
  }
  memcpy(plain, "ACR1", 4);
  plain[4] = state->server ? 1 : 0;
  plain[5] = (uint8_t)traffic_class;
  plain[6] = (uint8_t)delivery;
  plain[7] = 0;
  uint64_t sequence = state->send_sequence[traffic_class][delivery] + 1;
  uint64_t network_sequence = HOST_TO_NET_U64(sequence);
  memcpy(plain + 8, &network_sequence, sizeof(network_sequence));
  uint64_t network_epoch = HOST_TO_NET_U64(state->key_epoch);
  memcpy(plain + 16, &network_epoch, sizeof(network_epoch));
  memcpy(plain + CRYPTO_RECORD_PREFIX_SIZE, packet, length);
  size_t cipher_length = 0;
  crypto_result_t crypto_result = crypto_encrypt(state->crypto, plain, plain_length, wire + CRYPTO_RECORD_EPOCH_SIZE,
                                                 cipher_capacity, &cipher_length);
  buffer_release(plain);
  if (crypto_result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Cannot encrypt Record packet");
  }
  memcpy(wire, &network_epoch, sizeof(network_epoch));
  cipher_length += CRYPTO_RECORD_EPOCH_SIZE;
  state->send_sequence[traffic_class][delivery] = sequence;
  *out_length = cipher_length;
  return ASCIICHAT_OK;
}

asciichat_error_t crypto_record_open_sequence(crypto_record_t *state, unsigned traffic_class, unsigned delivery,
                                              const void *packet, size_t length, uint8_t **out, size_t *out_length,
                                              uint64_t *sequence_out, crypto_record_validate_fn validate,
                                              void *context) {
  if (!out || !out_length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing record decrypted packet output");
  *out = NULL;
  *out_length = 0;
  if (sequence_out)
    *sequence_out = 0;
  if (!ready(state) || delivery > CRYPTO_RECORD_DATAGRAM ||
      (traffic_class != CRYPTO_RECORD_ANY_CHANNEL && !valid_flow(traffic_class, delivery)))
    return SET_ERRNO(ERROR_INVALID_STATE, "Record decryption is not ready for this flow");
  if (!packet || length < CRYPTO_RECORD_EPOCH_SIZE + state->crypto->nonce_size + state->crypto->mac_size ||
      length >
          CRYPTO_RECORD_EPOCH_SIZE + CRYPTO_MAX_PLAINTEXT_SIZE + state->crypto->nonce_size + state->crypto->mac_size)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid Record encrypted packet size");
  const uint8_t *cipher = packet;
  size_t cipher_length = length;
  uint64_t network_epoch;
  memcpy(&network_epoch, cipher, sizeof(network_epoch));
  uint64_t epoch = NET_TO_HOST_U64(network_epoch);
  crypto_context_t pending_crypto;
  crypto_context_t *decrypt_crypto = state->crypto;
  bool pending = false;
  if (epoch != state->key_epoch) {
    if (state->previous_valid && epoch == state->previous_epoch) {
      decrypt_crypto = &state->previous_crypto;
    } else if (state->key_epoch != UINT64_MAX && epoch == state->key_epoch + 1 && state->pending_prepared &&
               state->crypto->rekey_in_progress && state->crypto->has_temp_key) {
      pending_crypto = *state->crypto;
      pending_crypto.session = NULL;
      memcpy(pending_crypto.shared_key, state->crypto->temp_shared_key, sizeof(pending_crypto.shared_key));
      pending_crypto.rekey_in_progress = false;
      pending_crypto.has_temp_key = false;
      decrypt_crypto = &pending_crypto;
      pending = true;
    } else {
      return SET_ERRNO(ERROR_CRYPTO, "Unknown or retired record key epoch");
    }
  }
  cipher += CRYPTO_RECORD_EPOCH_SIZE;
  cipher_length -= CRYPTO_RECORD_EPOCH_SIZE;
  uint8_t *plain = buffer_alloc(NULL, cipher_length);
  if (!plain) {
    if (pending)
      sodium_memzero(&pending_crypto, sizeof(pending_crypto));
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate record decrypted packet");
  }
  defer(if (plain) buffer_release(plain));
  size_t plain_length = 0;
  crypto_result_t crypto_result =
      crypto_decrypt(decrypt_crypto, cipher, cipher_length, plain, cipher_length, &plain_length);
  if (pending)
    sodium_memzero(&pending_crypto, sizeof(pending_crypto));
  if (crypto_result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Cannot authenticate Record packet");
  }
  if (plain_length < CRYPTO_RECORD_PREFIX_SIZE || memcmp(plain, "ACR1", 4) || plain[4] != (state->server ? 0 : 1) ||
      (traffic_class != CRYPTO_RECORD_ANY_CHANNEL && plain[5] != traffic_class) || plain[5] >= CRYPTO_RECORD_CHANNELS ||
      plain[6] != (uint8_t)delivery || plain[7] || memcmp(plain + 16, &network_epoch, sizeof(network_epoch))) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid authenticated record flow metadata");
  }
  traffic_class = plain[5];
  if (epoch == state->previous_epoch && state->previous_valid && !state->previous_allowed[traffic_class]) {
    return SET_ERRNO(ERROR_CRYPTO, "Retired record channel epoch");
  }
  uint64_t network_sequence;
  memcpy(&network_sequence, plain + 8, sizeof(network_sequence));
  uint64_t sequence = NET_TO_HOST_U64(network_sequence);
  uint64_t high = state->receive_high[traffic_class][delivery];
  uint64_t seen = state->datagram_seen[traffic_class];
  if (!sequence || (delivery == CRYPTO_RECORD_RELIABLE && sequence <= high) ||
      (delivery == CRYPTO_RECORD_DATAGRAM && sequence <= high &&
       (high - sequence >= 64 || (seen & (UINT64_C(1) << (high - sequence)))))) {
    return SET_ERRNO(ERROR_CRYPTO, "Replayed or stale Record packet");
  }
  size_t inner_length = plain_length - CRYPTO_RECORD_PREFIX_SIZE;
  asciichat_error_t result =
      validate ? validate(context, traffic_class, delivery, plain + CRYPTO_RECORD_PREFIX_SIZE, inner_length)
               : ASCIICHAT_OK;
  if (result != ASCIICHAT_OK) {
    return result;
  }
  if (pending) {
    result = crypto_record_commit_epoch(state);
    if (result != ASCIICHAT_OK) {
      return result;
    }
  }
  if (delivery == CRYPTO_RECORD_DATAGRAM) {
    if (sequence > high) {
      uint64_t advance = sequence - high;
      seen = advance >= 64 ? UINT64_C(1) : (seen << advance) | UINT64_C(1);
      high = sequence;
    } else {
      seen |= UINT64_C(1) << (high - sequence);
    }
    state->datagram_seen[traffic_class] = seen;
  } else {
    high = sequence;
  }
  state->receive_high[traffic_class][delivery] = high;
  state->last_receive_epoch = epoch;
  state->last_receive_channel = traffic_class;
  state->last_receive_delivery = delivery;
  memmove(plain, plain + CRYPTO_RECORD_PREFIX_SIZE, inner_length);
  *out = plain;
  plain = NULL;
  *out_length = inner_length;
  if (sequence_out)
    *sequence_out = sequence;
  return ASCIICHAT_OK;
}

asciichat_error_t crypto_record_prepare_epoch(crypto_record_t *state) {
  if (!ready(state) || state->previous_valid || state->pending_prepared || state->key_epoch == UINT64_MAX ||
      !state->crypto->rekey_in_progress || !state->crypto->has_temp_key)
    return SET_ERRNO(ERROR_INVALID_STATE, "record key epoch is not ready to prepare");
  state->pending_prepared = true;
  return ASCIICHAT_OK;
}

asciichat_error_t crypto_record_commit_epoch(crypto_record_t *state) {
  if (!ready(state) || !state->pending_prepared || state->previous_valid || !state->crypto->rekey_in_progress ||
      !state->crypto->has_temp_key)
    return SET_ERRNO(ERROR_INVALID_STATE, "record key epoch is not ready to commit");
  crypto_context_t previous = *state->crypto;
  previous.session = NULL;
  previous.rekey_in_progress = previous.has_temp_key = false;
  sodium_memzero(previous.temp_private_key, sizeof(previous.temp_private_key));
  sodium_memzero(previous.temp_public_key, sizeof(previous.temp_public_key));
  sodium_memzero(previous.temp_shared_key, sizeof(previous.temp_shared_key));
  if (crypto_rekey_commit(state->crypto) != CRYPTO_OK) {
    sodium_memzero(&previous, sizeof(previous));
    return SET_ERRNO(ERROR_CRYPTO, "Cannot commit record key epoch");
  }
  state->previous_crypto = previous;
  sodium_memzero(&previous, sizeof(previous));
  state->previous_epoch = state->key_epoch;
  state->key_epoch = state->crypto->rekey_count;
  state->previous_valid = true;
  state->pending_prepared = false;
  for (unsigned i = 0; i < CRYPTO_RECORD_CHANNELS; ++i)
    state->previous_allowed[i] = true;
  return ASCIICHAT_OK;
}

asciichat_error_t crypto_record_retire_epoch_flow(crypto_record_t *state, unsigned traffic_class, uint64_t epoch) {
  if (!ready(state) || (unsigned)traffic_class > (CRYPTO_RECORD_CHANNELS - 1) || epoch != state->key_epoch ||
      !state->previous_valid)
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid record epoch retirement marker");
  state->previous_allowed[traffic_class] = false;
  if (!state->previous_allowed[0] && !state->previous_allowed[1] && !state->previous_allowed[2]) {
    sodium_memzero(&state->previous_crypto, sizeof(state->previous_crypto));
    state->previous_valid = false;
  }
  return ASCIICHAT_OK;
}

void crypto_record_cleanup(crypto_record_t *state) {
  if (state)
    sodium_memzero(state, sizeof(*state));
}
