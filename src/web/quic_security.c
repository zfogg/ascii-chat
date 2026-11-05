#include "quic_security.h"
#include <ascii-chat/buffer.h>
#include <emscripten.h>
#include <string.h>
#include <sodium.h>

static asciichat_error_t enqueue(void *context, acip_traffic_class_t flow, const void *packet, size_t length) {
  browser_quic_security_t *state = context;
  if (state->count == BROWSER_QUIC_REKEY_QUEUE)
    return SET_ERRNO(ERROR_NETWORK, "Browser QUIC rekey queue is full");
  size_t i = state->count;
  asciichat_error_t result = acip_quic_security_seal(&state->security, flow, ACIP_DELIVERY_RELIABLE, packet, length,
                                                     &state->output[i], &state->length[i]);
  if (result == ASCIICHAT_OK) {
    state->flow[i] = flow;
    ++state->count;
  }
  return result;
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_quic_tick(browser_quic_security_t *state) {
  if (!state || !state->security.crypto)
    return SET_ERRNO(ERROR_INVALID_STATE, "Browser QUIC security is unavailable");
  if (state->failure)
    return state->failure;
  uint64_t now = time_get_ns();
  if (!state->rekey.security)
    state->failure = acip_quic_rekey_init(&state->rekey, &state->security, enqueue, state, now);
  if (!state->failure)
    state->failure = acip_quic_rekey_tick(&state->rekey, now);
  return state->failure;
}

asciichat_error_t browser_quic_receive(browser_quic_security_t *state, acip_traffic_class_t flow,
                                       acip_delivery_t delivery, const void *packet, size_t length, bool *consumed) {
  *consumed = false;
  if (state->failure)
    return state->failure;
  if (!state->rekey.security)
    return ASCIICHAT_OK;
  state->failure = acip_quic_rekey_receive(&state->rekey, flow, delivery, packet, length, time_get_ns(), consumed);
  return state->failure;
}

/* Copies and removes one sealed reliable packet; zero length means empty. */
EMSCRIPTEN_KEEPALIVE asciichat_error_t browser_quic_output(browser_quic_security_t *state, uint8_t *output,
                                                           size_t capacity, size_t *length, uint32_t *flow) {
  if (!state || !output || !length || !flow)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser QUIC output");
  *length = 0;
  if (state->failure)
    return state->failure;
  if (!state->count)
    return ASCIICHAT_OK;
  if (capacity < state->length[0])
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Browser QUIC output buffer is too small");
  *length = state->length[0];
  *flow = state->flow[0];
  memcpy(output, state->output[0], *length);
  buffer_release(state->output[0]);
  --state->count;
  memmove(state->output, state->output + 1, state->count * sizeof(state->output[0]));
  memmove(state->length, state->length + 1, state->count * sizeof(state->length[0]));
  memmove(state->flow, state->flow + 1, state->count * sizeof(state->flow[0]));
  state->output[state->count] = NULL;
  return ASCIICHAT_OK;
}

void browser_quic_cleanup(browser_quic_security_t *state) {
  acip_quic_rekey_cleanup(&state->rekey);
  for (size_t i = 0; i < state->count; ++i)
    buffer_release(state->output[i]);
  acip_quic_security_cleanup(&state->security);
  sodium_memzero(state, sizeof(*state));
}

EMSCRIPTEN_KEEPALIVE double browser_quic_epoch(browser_quic_security_t *state) {
  return state && state->security.crypto ? (double)state->security.key_epoch : -1;
}
