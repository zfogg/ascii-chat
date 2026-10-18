#pragma once

#include <ascii-chat/buffer.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/platform/socket.h>

/**
 * A borrowed packet description. Neither the vector array nor its bytes are owned.
 * Keep both alive and immutable through encryption or synchronous delivery. Async
 * delivery must copy/retain them before returning (platform_event_stream_write does).
 * Serialize planning and encryption with other sends using the same crypto context.
 */
typedef struct {
  const socket_buffer_t *buffers;
  size_t count;
  size_t plaintext_size;
  size_t ciphertext_capacity;
  bool encrypt;
  buffer_domain_t *domain; ///< Borrowed control reserve for handshake/error traffic; NULL otherwise.
} acip_packet_plan_t;

/** Validate sizes and decide eligibility without allocating or advancing crypto state. */
asciichat_error_t acip_packet_plan(crypto_context_t *crypto, const socket_buffer_t *buffers, size_t count,
                                   acip_packet_plan_t *plan);

/**
 * Encrypt a successful encrypted plan into caller-owned ciphertext storage and
 * construct its wire header. The caller chooses contiguous or vectored delivery,
 * and must release storage only after delivery has consumed/copied it. This helper
 * owns and releases only its temporary gather buffer, allocated from domain.
 * Output length is zero on failure; no output may be sent on failure.
 */
asciichat_error_t acip_packet_encrypt(crypto_context_t *crypto, const acip_packet_plan_t *plan, buffer_domain_t *domain,
                                      void *ciphertext, size_t capacity, packet_header_t *header, size_t *length);
