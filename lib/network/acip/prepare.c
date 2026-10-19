#include <ascii-chat/network/acip/prepare.h>
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
  buffer_domain_t *domain = NULL;
  if (buffers[0].len >= sizeof(packet_header_t)) {
    packet_header_t header;
    memcpy(&header, buffers[0].data, sizeof(header));
    packet_type_t type = NET_TO_HOST_U16(header.type);
    if (packet_is_handshake_type(type) || type == PACKET_TYPE_ERROR_MESSAGE)
      domain = buffer_control_domain();
    encrypt = crypto && crypto->encrypt_data && crypto_is_ready(crypto) &&
              !packet_is_handshake_type(NET_TO_HOST_U16(header.type));
  } else {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing ACIP packet header");
  }
  size_t capacity = 0;
  if (encrypt) {
    if (size > CRYPTO_MAX_PLAINTEXT_SIZE ||
        size > SOCKET_SEND_BUFFER_MAX - sizeof(packet_header_t) - CRYPTO_NONCE_SIZE - CRYPTO_MAC_SIZE)
      return SET_ERRNO(ERROR_NETWORK_SIZE, "Encrypted ACIP packet too large");
    capacity = size + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  }
  *plan = (acip_packet_plan_t){buffers, count, size, capacity, encrypt, domain};
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
  crypto_result_t result =
      crypto_encrypt(crypto, plaintext, plan->plaintext_size, ciphertext, capacity, &encrypted_length);
  buffer_release(gathered);
  if (result != CRYPTO_OK)
    return SET_ERRNO(ERROR_CRYPTO, "Cannot encrypt ACIP packet: %s", crypto_result_to_string(result));
  *header = (packet_header_t){.magic = HOST_TO_NET_U64(PACKET_MAGIC),
                              .type = HOST_TO_NET_U16(PACKET_TYPE_ENCRYPTED),
                              .length = HOST_TO_NET_U32((uint32_t)encrypted_length),
                              .crc32 = HOST_TO_NET_U32(asciichat_crc32(ciphertext, encrypted_length)),
                              .client_id = 0};
  *length = encrypted_length;
  return ASCIICHAT_OK;
}
