#include "security.h"

#include <ascii-chat/buffer.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/util/endian.h>
#include <string.h>
#include <sodium.h>

#define QUIC_SECURE_PREFIX_SIZE 24
#define QUIC_WIRE_EPOCH_SIZE 8

static bool valid_flow(acip_traffic_class_t traffic_class, acip_delivery_t delivery) {
  return (unsigned)traffic_class <= ACIP_TRAFFIC_VISUAL && (unsigned)delivery <= ACIP_DELIVERY_DATAGRAM &&
         (delivery != ACIP_DELIVERY_DATAGRAM || traffic_class != ACIP_TRAFFIC_CONTROL);
}

static bool ready(const acip_quic_security_t *state) {
  return state && state->crypto && state->crypto->initialized && state->crypto->encrypt_data &&
         state->crypto->key_exchange_complete && state->crypto->handshake_complete &&
         state->crypto->nonce_size == CRYPTO_NONCE_SIZE && state->crypto->mac_size == CRYPTO_MAC_SIZE &&
         state->crypto->rekey_count == state->key_epoch;
}

static asciichat_error_t validate_packet(const void *packet, size_t length, acip_traffic_class_t traffic_class) {
  if (!packet || length < sizeof(packet_header_t) || length - sizeof(packet_header_t) > MAX_PACKET_SIZE)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid ACIP packet size");
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  uint16_t type = NET_TO_HOST_U16(header.type);
  if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC || NET_TO_HOST_U32(header.length) != length - sizeof(header) ||
      type == PACKET_TYPE_ENCRYPTED || acip_packet_traffic_class(type) != traffic_class ||
      NET_TO_HOST_U32(header.crc32) !=
          asciichat_crc32((const uint8_t *)packet + sizeof(header), length - sizeof(header)))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid ACIP packet or traffic class");
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_security_init(acip_quic_security_t *state, crypto_context_t *crypto, bool server) {
  if (!state || state->crypto || !crypto || !crypto->initialized || !crypto->encrypt_data ||
      !crypto->key_exchange_complete || !crypto->handshake_complete || crypto->nonce_size != CRYPTO_NONCE_SIZE ||
      crypto->mac_size != CRYPTO_MAC_SIZE || crypto->rekey_in_progress)
    return SET_ERRNO(ERROR_INVALID_STATE, "ACIP authentication and encryption must be ready");
  *state = (acip_quic_security_t){.crypto = crypto, .key_epoch = crypto->rekey_count, .server = server};
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_security_seal(acip_quic_security_t *state, acip_traffic_class_t traffic_class,
                                          acip_delivery_t delivery, const void *packet, size_t length, uint8_t **out,
                                          size_t *out_length) {
  if (!out || !out_length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC encrypted packet output");
  *out = NULL;
  *out_length = 0;
  if (!ready(state) || !valid_flow(traffic_class, delivery))
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC ACIP encryption is not ready for this flow");
  asciichat_error_t result = validate_packet(packet, length, traffic_class);
  if (result != ASCIICHAT_OK)
    return result;
  if (length > CRYPTO_MAX_PLAINTEXT_SIZE - QUIC_SECURE_PREFIX_SIZE ||
      state->send_sequence[traffic_class][delivery] == UINT64_MAX)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "QUIC secure packet or sequence limit exceeded");
  size_t plain_length = QUIC_SECURE_PREFIX_SIZE + length;
  size_t cipher_capacity = plain_length + state->crypto->nonce_size + state->crypto->mac_size;
  uint8_t *plain = buffer_alloc(NULL, plain_length);
  uint8_t *wire = buffer_alloc(NULL, sizeof(packet_header_t) + QUIC_WIRE_EPOCH_SIZE + cipher_capacity);
  if (!plain || !wire) {
    buffer_release(plain);
    buffer_release(wire);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC secure packet");
  }
  memcpy(plain, "ACQ2", 4);
  plain[4] = state->server ? 1 : 0;
  plain[5] = (uint8_t)traffic_class;
  plain[6] = (uint8_t)delivery;
  plain[7] = 0;
  uint64_t sequence = state->send_sequence[traffic_class][delivery] + 1;
  uint64_t network_sequence = HOST_TO_NET_U64(sequence);
  memcpy(plain + 8, &network_sequence, sizeof(network_sequence));
  uint64_t network_epoch = HOST_TO_NET_U64(state->key_epoch);
  memcpy(plain + 16, &network_epoch, sizeof(network_epoch));
  memcpy(plain + QUIC_SECURE_PREFIX_SIZE, packet, length);
  size_t cipher_length = 0;
  crypto_result_t crypto_result =
      crypto_encrypt(state->crypto, plain, plain_length, wire + sizeof(packet_header_t) + QUIC_WIRE_EPOCH_SIZE,
                     cipher_capacity, &cipher_length);
  buffer_release(plain);
  if (crypto_result != CRYPTO_OK) {
    buffer_release(wire);
    return SET_ERRNO(ERROR_CRYPTO, "Cannot encrypt QUIC ACIP packet");
  }
  memcpy(wire + sizeof(packet_header_t), &network_epoch, sizeof(network_epoch));
  cipher_length += QUIC_WIRE_EPOCH_SIZE;
  packet_header_t outer = {.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                           .type = HOST_TO_NET_U16(PACKET_TYPE_ENCRYPTED),
                           .length = HOST_TO_NET_U32((uint32_t)cipher_length),
                           .crc32 = HOST_TO_NET_U32(asciichat_crc32(wire + sizeof(packet_header_t), cipher_length))};
  memcpy(wire, &outer, sizeof(outer));
  state->send_sequence[traffic_class][delivery] = sequence;
  *out = wire;
  *out_length = sizeof(outer) + cipher_length;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_security_open_sequence(acip_quic_security_t *state, acip_traffic_class_t traffic_class,
                                                   acip_delivery_t delivery, const void *packet, size_t length,
                                                   uint8_t **out, size_t *out_length, uint64_t *sequence_out) {
  if (!out || !out_length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing QUIC decrypted packet output");
  *out = NULL;
  *out_length = 0;
  if (sequence_out)
    *sequence_out = 0;
  if (!ready(state) || !valid_flow(traffic_class, delivery))
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC ACIP decryption is not ready for this flow");
  if (!packet ||
      length < sizeof(packet_header_t) + QUIC_WIRE_EPOCH_SIZE + state->crypto->nonce_size + state->crypto->mac_size ||
      length > sizeof(packet_header_t) + QUIC_WIRE_EPOCH_SIZE + CRYPTO_MAX_PLAINTEXT_SIZE + state->crypto->nonce_size +
                   state->crypto->mac_size)
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid QUIC encrypted packet size");
  packet_header_t header;
  memcpy(&header, packet, sizeof(header));
  const uint8_t *cipher = (const uint8_t *)packet + sizeof(header);
  size_t cipher_length = length - sizeof(header);
  if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC || NET_TO_HOST_U16(header.type) != PACKET_TYPE_ENCRYPTED ||
      NET_TO_HOST_U32(header.length) != cipher_length ||
      NET_TO_HOST_U32(header.crc32) != asciichat_crc32(cipher, cipher_length))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid QUIC encrypted packet framing");
  uint64_t network_epoch;
  memcpy(&network_epoch, cipher, sizeof(network_epoch));
  uint64_t epoch = NET_TO_HOST_U64(network_epoch);
  crypto_context_t pending_crypto;
  crypto_context_t *decrypt_crypto = state->crypto;
  bool pending = false;
  if (epoch != state->key_epoch) {
    if (state->previous_valid && epoch == state->previous_epoch && state->previous_allowed[traffic_class]) {
      decrypt_crypto = &state->previous_crypto;
    } else if (state->key_epoch != UINT64_MAX && epoch == state->key_epoch + 1 && state->pending_prepared &&
               state->crypto->rekey_in_progress && state->crypto->has_temp_key) {
      pending_crypto = *state->crypto;
      memcpy(pending_crypto.shared_key, state->crypto->temp_shared_key, sizeof(pending_crypto.shared_key));
      pending_crypto.rekey_in_progress = false;
      pending_crypto.has_temp_key = false;
      decrypt_crypto = &pending_crypto;
      pending = true;
    } else {
      return SET_ERRNO(ERROR_CRYPTO, "Unknown or retired QUIC key epoch");
    }
  }
  cipher += QUIC_WIRE_EPOCH_SIZE;
  cipher_length -= QUIC_WIRE_EPOCH_SIZE;
  uint8_t *plain = buffer_alloc(NULL, cipher_length);
  if (!plain) {
    if (pending)
      sodium_memzero(&pending_crypto, sizeof(pending_crypto));
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate QUIC decrypted packet");
  }
  size_t plain_length = 0;
  crypto_result_t crypto_result =
      crypto_decrypt(decrypt_crypto, cipher, cipher_length, plain, cipher_length, &plain_length);
  if (pending)
    sodium_memzero(&pending_crypto, sizeof(pending_crypto));
  if (crypto_result != CRYPTO_OK) {
    buffer_release(plain);
    return SET_ERRNO(ERROR_CRYPTO, "Cannot authenticate QUIC ACIP packet");
  }
  if (plain_length < QUIC_SECURE_PREFIX_SIZE || memcmp(plain, "ACQ2", 4) || plain[4] != (state->server ? 0 : 1) ||
      plain[5] != (uint8_t)traffic_class || plain[6] != (uint8_t)delivery || plain[7] ||
      memcmp(plain + 16, &network_epoch, sizeof(network_epoch))) {
    buffer_release(plain);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid authenticated QUIC flow metadata");
  }
  uint64_t network_sequence;
  memcpy(&network_sequence, plain + 8, sizeof(network_sequence));
  uint64_t sequence = NET_TO_HOST_U64(network_sequence);
  uint64_t high = state->receive_high[traffic_class][delivery];
  uint64_t seen = state->datagram_seen[traffic_class];
  if (!sequence || (delivery == ACIP_DELIVERY_RELIABLE && sequence <= high) ||
      (delivery == ACIP_DELIVERY_DATAGRAM && sequence <= high &&
       (high - sequence >= 64 || (seen & (UINT64_C(1) << (high - sequence)))))) {
    buffer_release(plain);
    return SET_ERRNO(ERROR_CRYPTO, "Replayed or stale QUIC ACIP packet");
  }
  size_t inner_length = plain_length - QUIC_SECURE_PREFIX_SIZE;
  asciichat_error_t result = validate_packet(plain + QUIC_SECURE_PREFIX_SIZE, inner_length, traffic_class);
  if (result != ASCIICHAT_OK) {
    buffer_release(plain);
    return result;
  }
  if (pending) {
    result = acip_quic_security_commit_epoch(state);
    if (result != ASCIICHAT_OK) {
      buffer_release(plain);
      return result;
    }
  }
  if (delivery == ACIP_DELIVERY_DATAGRAM) {
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
  memmove(plain, plain + QUIC_SECURE_PREFIX_SIZE, inner_length);
  *out = plain;
  *out_length = inner_length;
  if (sequence_out)
    *sequence_out = sequence;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_security_open(acip_quic_security_t *state, acip_traffic_class_t traffic_class,
                                          acip_delivery_t delivery, const void *packet, size_t length, uint8_t **out,
                                          size_t *out_length) {
  return acip_quic_security_open_sequence(state, traffic_class, delivery, packet, length, out, out_length, NULL);
}

asciichat_error_t acip_quic_security_prepare_epoch(acip_quic_security_t *state) {
  if (!ready(state) || state->previous_valid || state->pending_prepared || state->key_epoch == UINT64_MAX ||
      !state->crypto->rekey_in_progress || !state->crypto->has_temp_key)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC key epoch is not ready to prepare");
  state->pending_prepared = true;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_security_commit_epoch(acip_quic_security_t *state) {
  if (!ready(state) || !state->pending_prepared || state->previous_valid || !state->crypto->rekey_in_progress ||
      !state->crypto->has_temp_key)
    return SET_ERRNO(ERROR_INVALID_STATE, "QUIC key epoch is not ready to commit");
  crypto_context_t previous = *state->crypto;
  previous.rekey_in_progress = previous.has_temp_key = false;
  sodium_memzero(previous.temp_private_key, sizeof(previous.temp_private_key));
  sodium_memzero(previous.temp_public_key, sizeof(previous.temp_public_key));
  sodium_memzero(previous.temp_shared_key, sizeof(previous.temp_shared_key));
  if (crypto_rekey_commit(state->crypto) != CRYPTO_OK) {
    sodium_memzero(&previous, sizeof(previous));
    return SET_ERRNO(ERROR_CRYPTO, "Cannot commit QUIC key epoch");
  }
  state->previous_crypto = previous;
  sodium_memzero(&previous, sizeof(previous));
  state->previous_epoch = state->key_epoch;
  state->key_epoch = state->crypto->rekey_count;
  state->previous_valid = true;
  state->pending_prepared = false;
  for (unsigned i = 0; i < 3; ++i)
    state->previous_allowed[i] = true;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_quic_security_retire_epoch_flow(acip_quic_security_t *state, acip_traffic_class_t traffic_class,
                                                       uint64_t epoch) {
  if (!ready(state) || (unsigned)traffic_class > ACIP_TRAFFIC_VISUAL || epoch != state->key_epoch ||
      !state->previous_valid)
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid QUIC epoch retirement marker");
  state->previous_allowed[traffic_class] = false;
  if (!state->previous_allowed[0] && !state->previous_allowed[1] && !state->previous_allowed[2]) {
    sodium_memzero(&state->previous_crypto, sizeof(state->previous_crypto));
    state->previous_valid = false;
  }
  return ASCIICHAT_OK;
}

void acip_quic_security_cleanup(acip_quic_security_t *state) {
  if (state)
    sodium_memzero(state, sizeof(*state));
}
