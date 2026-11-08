#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/crypto/session.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

asciichat_error_t acip_packet_plan(crypto_context_t *crypto, const socket_buffer_t *buffers, size_t count,
                                   acip_packet_plan_t *plan) {
  if (!plan)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing ACIP packet plan");
  *plan = (acip_packet_plan_t){0};
  if (!buffers || !count || count > SOCKET_IOV_MAX)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ACIP packet vectors");
  size_t size = 0;
  for (size_t i = 0; i < count; ++i) {
    if ((buffers[i].len && !buffers[i].data) || buffers[i].len > SOCKET_SEND_BUFFER_MAX - size)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Invalid ACIP packet vector size");
    size += buffers[i].len;
  }
  bool encrypt = false;
  acip_send_options_t options = {.delivery = ACIP_DELIVERY_RELIABLE};
  buffer_domain_t *domain = NULL;
  if (buffers[0].len >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, buffers[0].data, sizeof(header));
    packet_type_t type = NET_TO_HOST_U16(header.type);
    if (packet_is_handshake_type(type) || type == PACKET_TYPE_ERROR_MESSAGE)
      domain = buffer_control_domain();
    options.traffic_class = type == PACKET_TYPE_CRYPTO_EPOCH_MARKER
                                ? (acip_traffic_class_t)NET_TO_HOST_U32(header.client_id)
                                : acip_packet_traffic_class(type);
    encrypt = crypto && crypto->encrypt_data && crypto_is_ready(crypto) && type != PACKET_TYPE_ENCRYPTED &&
              !packet_is_initial_handshake_type(type);
  } else {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing ACIP packet header");
  }
  size_t capacity = 0;
  crypto_record_t *records = NULL;
  if (encrypt) {
    if (size > CRYPTO_MAX_PLAINTEXT_SIZE - 24 ||
        size > SOCKET_SEND_BUFFER_MAX - sizeof(packet_header_t) - CRYPTO_NONCE_SIZE - CRYPTO_MAC_SIZE)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Encrypted ACIP packet too large");
    crypto_session_t *session = NULL;
    asciichat_error_t result = crypto_session_lock(crypto, &session);
    if (result != ASCIICHAT_OK)
      return result;
    records = &session->records;
    crypto_session_unlock(session);
    capacity = size + 32 + crypto->nonce_size + crypto->mac_size;
  }
  *plan = (acip_packet_plan_t){.buffers = buffers,
                               .count = count,
                               .plaintext_size = size,
                               .ciphertext_capacity = capacity,
                               .encrypt = encrypt,
                               .domain = domain,
                               .records = records,
                               .options = options};
  return ASCIICHAT_OK;
}

asciichat_error_t acip_packet_encrypt(crypto_context_t *crypto, const acip_packet_plan_t *plan, buffer_domain_t *domain,
                                      void *ciphertext, size_t capacity, packet_header_t *header, size_t *length) {
  if (length)
    *length = 0;
  if (!crypto || !plan || !plan->encrypt || !plan->buffers || !plan->count || !ciphertext || !header || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ACIP encryption arguments");
  if (capacity < plan->ciphertext_capacity)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "ACIP ciphertext storage too small");
  const uint8_t *plaintext = plan->buffers[0].data;
  uint8_t *gathered = NULL;
  if (plan->count > 1) {
    gathered = buffer_alloc(domain, plan->plaintext_size);
    if (!gathered)
      return SET_ERRNO(ERROR_MEMORY, "Cannot gather ACIP encryption input");
    size_t offset = 0;
    for (size_t i = 0; i < plan->count; ++i) {
      if (plan->buffers[i].len)
        memcpy(gathered + offset, plan->buffers[i].data, plan->buffers[i].len);
      offset += plan->buffers[i].len;
    }
    plaintext = gathered;
  }
  size_t encrypted_length = 0;
  crypto_session_t *session = NULL;
  bool shared = !plan->records_locked && crypto && crypto->session && plan->records == &crypto->session->records;
  asciichat_error_t encrypted = shared ? crypto_session_lock(crypto, &session) : ASCIICHAT_OK;
  if (encrypted == ASCIICHAT_OK)
    encrypted = plan->records
                    ? crypto_record_seal_into(plan->records, plan->options.traffic_class, plan->options.delivery,
                                              plaintext, plan->plaintext_size, ciphertext, capacity, &encrypted_length)
                    : SET_ERRNO(ERROR_INVALID_STATE, "Missing authenticated record context");
  if (session)
    crypto_session_unlock(session);
  buffer_release(gathered);
  if (encrypted != ASCIICHAT_OK)
    return encrypted;
  socket_buffer_t slice = {ciphertext, encrypted_length};
  asciichat_error_t result = packet_header_build(header, PACKET_TYPE_ENCRYPTED, &slice, 1, 0);
  if (result != ASCIICHAT_OK)
    return result;
  *length = encrypted_length;
  return ASCIICHAT_OK;
}

static asciichat_error_t validate_record_packet(void *context, unsigned flow, unsigned delivery, const void *packet,
                                                size_t length) {
  (void)context;
  packet_header_t header;
  asciichat_error_t result = packet_validate(packet, length, 0, &header);
  if (result != ASCIICHAT_OK)
    return result;
  uint16_t type = NET_TO_HOST_U16(header.type);
  if (flow > ACIP_TRAFFIC_VISUAL || delivery > ACIP_DELIVERY_DATAGRAM ||
      (delivery == ACIP_DELIVERY_DATAGRAM && flow == ACIP_TRAFFIC_CONTROL) || type == PACKET_TYPE_ENCRYPTED ||
      packet_is_initial_handshake_type(type) ||
      (type == PACKET_TYPE_CRYPTO_EPOCH_MARKER ? delivery != ACIP_DELIVERY_RELIABLE
                                               : acip_packet_traffic_class(type) != flow))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid authenticated packet channel");
  return ASCIICHAT_OK;
}

asciichat_error_t acip_packet_seal(crypto_record_t *records, acip_traffic_class_t flow, acip_delivery_t delivery,
                                   const void *packet, size_t length, uint8_t **output, size_t *written) {
  if (!output || !written || !records || !records->crypto)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid packet encryption arguments");
  *output = NULL;
  *written = 0;
  if ((unsigned)flow > ACIP_TRAFFIC_VISUAL || (unsigned)delivery > ACIP_DELIVERY_DATAGRAM ||
      (flow == ACIP_TRAFFIC_CONTROL && delivery == ACIP_DELIVERY_DATAGRAM))
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid packet output channel");
  asciichat_error_t result = validate_record_packet(NULL, flow, delivery, packet, length);
  if (result != ASCIICHAT_OK)
    return result;
  socket_buffer_t slice = {packet, length};
  if (length > CRYPTO_MAX_PLAINTEXT_SIZE - 24)
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Packet exceeds record plaintext capacity");
  acip_packet_plan_t plan = {.buffers = &slice,
                             .count = 1,
                             .plaintext_size = length,
                             .encrypt = true,
                             .records = records,
                             .records_locked = true,
                             .options = {.traffic_class = flow, .delivery = delivery},
                             .ciphertext_capacity =
                                 length + 32 + records->crypto->nonce_size + records->crypto->mac_size};
  uint8_t *wire = buffer_alloc(plan.domain, sizeof(packet_header_t) + plan.ciphertext_capacity);
  if (!wire)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate encrypted packet");
  packet_header_t header;
  size_t encrypted_length = 0;
  result = acip_packet_encrypt(records->crypto, &plan, plan.domain, wire + sizeof(header), plan.ciphertext_capacity,
                               &header, &encrypted_length);
  if (result != ASCIICHAT_OK) {
    buffer_release(wire);
    return result;
  }
  memcpy(wire, &header, sizeof(header));
  *output = wire;
  *written = sizeof(header) + encrypted_length;
  return ASCIICHAT_OK;
}

asciichat_error_t acip_packet_open_sequence(crypto_record_t *records, acip_traffic_class_t flow,
                                            acip_delivery_t delivery, const void *packet, size_t length,
                                            uint8_t **output, size_t *written, uint64_t *sequence) {
  if (!output || !written)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing decrypted packet output");
  *output = NULL;
  *written = 0;
  packet_header_t header;
  asciichat_error_t result = packet_validate(packet, length, 0, &header);
  if (result != ASCIICHAT_OK)
    return result;
  if (NET_TO_HOST_U16(header.type) != PACKET_TYPE_ENCRYPTED)
    return SET_ERRNO(ERROR_CRYPTO, "Expected encrypted packet");
  return acip_packet_decrypt_payload(records, flow, delivery, (const uint8_t *)packet + sizeof(header),
                                     length - sizeof(header), output, written, sequence);
}

asciichat_error_t acip_packet_open(crypto_record_t *records, acip_traffic_class_t flow, acip_delivery_t delivery,
                                   const void *packet, size_t length, uint8_t **output, size_t *written) {
  return acip_packet_open_sequence(records, flow, delivery, packet, length, output, written, NULL);
}

asciichat_error_t acip_packet_decrypt_payload(crypto_record_t *records, unsigned flow, unsigned delivery,
                                              const void *payload, size_t length, uint8_t **output, size_t *written,
                                              uint64_t *sequence) {
  return crypto_record_open_sequence(records, flow, delivery, payload, length, output, written, sequence,
                                     validate_record_packet, NULL);
}
