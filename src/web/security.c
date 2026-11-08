#include "security.h"
#include <ascii-chat/buffer.h>
#include <emscripten.h>
#include <string.h>

EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_security_init(crypto_context_t *ctx) {
  crypto_session_t *session;
  asciichat_error_t result = crypto_session_lock(ctx, &session);
  if (result == ASCIICHAT_OK)
    crypto_session_unlock(session);
  return result;
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_security_packet(crypto_context_t *ctx, bool seal, uint32_t channel,
                                                               uint32_t delivery, const uint8_t *packet, size_t length,
                                                               uint8_t *output, size_t capacity, size_t *out_length,
                                                               uint64_t *sequence) {
  const size_t overhead = sizeof(packet_header_t) + 32 + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  if (!packet || !output || !out_length || !sequence || length > SIZE_MAX - overhead ||
      capacity < length + (seal ? overhead : 0))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser security packet buffer");
  *out_length = 0;
  *sequence = 0;
  uint8_t *result_packet = NULL;
  size_t result_length = 0;
  bool consumed = false;
  bool quiet = !seal && delivery == ACIP_DELIVERY_DATAGRAM;
  asciichat_errno_scope_t scope;
  if (quiet)
    scope = errno_scope_begin();
  asciichat_error_t result =
      seal ? acip_security_seal(ctx, channel, delivery, packet, length, &result_packet, &result_length)
           : acip_security_open(ctx, channel, delivery, packet, length, &result_packet, &result_length, sequence,
                                &consumed);
  if (quiet)
    errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  if (result == ASCIICHAT_OK && !consumed) {
    memcpy(output, result_packet, result_length);
    *out_length = result_length;
  }
  buffer_release(result_packet);
  return result;
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_security_tick(crypto_context_t *ctx) {
  crypto_session_t *session;
  asciichat_error_t result = crypto_session_lock(ctx, &session);
  if (result == ASCIICHAT_OK) {
    result = crypto_session_tick(session, time_get_ns());
    crypto_session_unlock(session);
  }
  return result;
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_security_output(crypto_context_t *ctx, uint8_t *output, size_t capacity,
                                                               size_t *length, uint32_t *channel) {
  // The largest control is a public key plus the common framing and record overhead.
  if (!output || !length || !channel || capacity < 256)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser security control buffer");
  uint8_t *packet = NULL;
  acip_traffic_class_t flow;
  *length = 0;
  asciichat_error_t result = acip_security_next_control(ctx, &flow, &packet, length);
  if (result == ASCIICHAT_OK && *length) {
    memcpy(output, packet, *length);
    *channel = flow;
  }
  buffer_release(packet);
  return result;
}

EMSCRIPTEN_KEEPALIVE double browser_security_epoch(crypto_context_t *ctx) {
  crypto_session_t *session;
  if (crypto_session_lock(ctx, &session) != ASCIICHAT_OK)
    return -1;
  double epoch = (double)session->records.key_epoch;
  crypto_session_unlock(session);
  return epoch;
}
