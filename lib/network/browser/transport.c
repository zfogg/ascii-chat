#include <ascii-chat/network/browser/transport.h>
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/crypto/session.h>
#include <ascii-chat/util/time.h>
#include <string.h>

#define BROWSER_TRANSPORT_MAX_QUEUED_BYTES (4u * 1024u * 1024u)

typedef struct browser_output {
  struct browser_output *next;
  void *data;
  size_t length;
} browser_output_t;

struct browser_transport {
  acip_transport_t transport;
  browser_transport_state_t state;
  browser_transport_output_callback_t output_callback;
  void *output_user_data;
  size_t queued_bytes;
  void *client_state;
  browser_output_t *output;
  browser_output_t *tail;
};

static bool browser_connected(acip_transport_t *transport) {
  const browser_transport_t *browser = transport ? transport->impl_data : NULL;
  return browser && (browser->state == BROWSER_TRANSPORT_HANDSHAKE || browser->state == BROWSER_TRANSPORT_CONNECTED);
}

static void browser_transport_output_pop_internal(browser_transport_t *browser);

static asciichat_error_t browser_send(acip_transport_t *transport, const void *data, size_t length) {
  browser_transport_t *browser = transport->impl_data;
  socket_buffer_t input = {.data = data, .len = length};
  acip_packet_plan_t plan;
  crypto_context_t *send_crypto = transport->crypto_ctx && transport->crypto_ctx->handshake_complete
                                      ? transport->crypto_ctx
                                      : NULL;
  asciichat_error_t result = acip_packet_plan(send_crypto, &input, 1, &plan);
  if (result != ASCIICHAT_OK)
    return result;
  const size_t output_length = plan.encrypt ? sizeof(packet_header_t) + plan.ciphertext_capacity : length;
  if (!browser_transport_writable(browser, output_length))
    return SET_ERRNO(ERROR_NETWORK_SIZE, "Browser transport output queue is full");
  browser_output_t *output = SAFE_CALLOC(1, sizeof(*output), browser_output_t *);
  if (!output)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate browser transport output");
  output->length = output_length;
  output->data = buffer_alloc(plan.domain, output->length);
  if (!output->data) {
    SAFE_FREE(output);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate browser transport packet");
  }
  if (plan.encrypt) {
    packet_header_t header;
    size_t encrypted_length = 0;
    result = acip_packet_encrypt(transport->crypto_ctx, &plan, plan.domain,
                                 (uint8_t *)output->data + sizeof(header), plan.ciphertext_capacity, &header,
                                 &encrypted_length);
    if (result != ASCIICHAT_OK) {
      buffer_release(output->data);
      SAFE_FREE(output);
      return result;
    }
    memcpy(output->data, &header, sizeof(header));
    output->length = sizeof(header) + encrypted_length;
  } else {
    memcpy(output->data, data, length);
  }
  if (browser->tail)
    browser->tail->next = output;
  else
    browser->output = output;
  browser->tail = output;
  browser->queued_bytes += output->length;
  if (browser->output_callback) {
    browser->output_callback(output->data, output->length, browser->output_user_data);
    browser_transport_output_pop_internal(browser);
  }
  return ASCIICHAT_OK;
}

static const acip_transport_methods_t browser_methods = {.send = browser_send, .is_connected = browser_connected};

browser_transport_t *browser_transport_create(void) {
  browser_transport_t *browser = SAFE_CALLOC(1, sizeof(*browser), browser_transport_t *);
  if (!browser)
    return NULL;
  browser->transport.methods = &browser_methods;
  browser->transport.impl_data = browser;
  return browser;
}

void browser_transport_set_connected(browser_transport_t *browser, bool connected) {
  browser_transport_set_state(browser, connected ? BROWSER_TRANSPORT_CONNECTED : BROWSER_TRANSPORT_DISCONNECTED);
}

void browser_transport_set_state(browser_transport_t *browser, browser_transport_state_t state) {
  if (browser)
    browser->state = state;
}

void browser_transport_set_output_callback(browser_transport_t *browser, browser_transport_output_callback_t callback,
                                            void *user_data) {
  if (!browser)
    return;
  browser->output_callback = callback;
  browser->output_user_data = user_data;
}

size_t browser_transport_queued_bytes(const browser_transport_t *browser) {
  return browser ? browser->queued_bytes : 0;
}

bool browser_transport_writable(const browser_transport_t *browser, size_t additional_bytes) {
  return browser && additional_bytes <= BROWSER_TRANSPORT_MAX_QUEUED_BYTES -
                                      (browser->queued_bytes > BROWSER_TRANSPORT_MAX_QUEUED_BYTES
                                           ? BROWSER_TRANSPORT_MAX_QUEUED_BYTES
                                           : browser->queued_bytes);
}

browser_transport_state_t browser_transport_state(const browser_transport_t *browser) {
  return browser ? browser->state : BROWSER_TRANSPORT_DISCONNECTED;
}

bool browser_transport_is_connected(browser_transport_t *browser) {
  return browser && (browser->state == BROWSER_TRANSPORT_HANDSHAKE || browser->state == BROWSER_TRANSPORT_CONNECTED);
}

void browser_transport_output_pop(browser_transport_t *browser) {
  browser_transport_output_pop_internal(browser);
}

asciichat_error_t browser_transport_security_init(browser_transport_t *browser) {
  if (!browser || !browser->transport.crypto_ctx)
    return SET_ERRNO(ERROR_INVALID_STATE, "Browser transport security is unavailable");
  crypto_session_t *session = NULL;
  const asciichat_error_t result = crypto_session_lock(browser->transport.crypto_ctx, &session);
  if (result == ASCIICHAT_OK)
    crypto_session_unlock(session);
  return result;
}

asciichat_error_t browser_transport_security_packet(browser_transport_t *browser, bool seal, uint32_t channel,
                                                     uint32_t delivery, const uint8_t *packet, size_t length,
                                                     uint8_t *output, size_t capacity, size_t *out_length,
                                                     uint64_t *sequence) {
  if (!browser || !browser->transport.crypto_ctx)
    return SET_ERRNO(ERROR_INVALID_STATE, "Browser transport security is unavailable");
  const size_t overhead = sizeof(packet_header_t) + 32 + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  if (!packet || !output || !out_length || !sequence || length > SIZE_MAX - overhead ||
      capacity < length + (seal ? overhead : 0))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser security packet buffer");
  *out_length = 0;
  *sequence = 0;
  uint8_t *result_packet = NULL;
  size_t result_length = 0;
  bool consumed = false;
  const bool quiet = !seal && delivery == ACIP_DELIVERY_DATAGRAM;
  asciichat_errno_scope_t scope;
  if (quiet)
    scope = errno_scope_begin();
  const asciichat_error_t result =
      seal ? acip_security_seal(browser->transport.crypto_ctx, channel, delivery, packet, length, &result_packet,
                                &result_length)
           : acip_security_open(browser->transport.crypto_ctx, channel, delivery, packet, length, &result_packet,
                                &result_length, sequence, &consumed);
  if (quiet)
    errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
  if (result == ASCIICHAT_OK && !consumed) {
    memcpy(output, result_packet, result_length);
    *out_length = result_length;
  }
  buffer_release(result_packet);
  return result;
}

asciichat_error_t browser_transport_security_tick(browser_transport_t *browser) {
  if (!browser || !browser->transport.crypto_ctx)
    return SET_ERRNO(ERROR_INVALID_STATE, "Browser transport security is unavailable");
  crypto_session_t *session = NULL;
  asciichat_error_t result = crypto_session_lock(browser->transport.crypto_ctx, &session);
  if (result == ASCIICHAT_OK) {
    result = crypto_session_tick(session, time_get_ns());
    crypto_session_unlock(session);
  }
  return result;
}

asciichat_error_t browser_transport_security_output(browser_transport_t *browser, uint8_t *output, size_t capacity,
                                                    size_t *length, uint32_t *channel) {
  if (!browser || !browser->transport.crypto_ctx)
    return SET_ERRNO(ERROR_INVALID_STATE, "Browser transport security is unavailable");
  if (!output || !length || !channel || capacity < 256)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid browser security control buffer");
  uint8_t *packet = NULL;
  acip_traffic_class_t flow;
  *length = 0;
  const asciichat_error_t result = acip_security_next_control(browser->transport.crypto_ctx, &flow, &packet, length);
  if (result == ASCIICHAT_OK && *length) {
    memcpy(output, packet, *length);
    *channel = flow;
  }
  buffer_release(packet);
  return result;
}

double browser_transport_security_epoch(browser_transport_t *browser) {
  if (!browser || !browser->transport.crypto_ctx)
    return -1;
  crypto_session_t *session = NULL;
  if (crypto_session_lock(browser->transport.crypto_ctx, &session) != ASCIICHAT_OK)
    return -1;
  const double epoch = (double)session->records.key_epoch;
  crypto_session_unlock(session);
  return epoch;
}

static void browser_transport_output_pop_internal(browser_transport_t *browser) {
  if (!browser || !browser->output)
    return;
  browser_output_t *output = browser->output;
  browser->output = output->next;
  if (!browser->output)
    browser->tail = NULL;
  browser->queued_bytes -= output->length;
  buffer_release(output->data);
  SAFE_FREE(output);
}

void browser_transport_destroy(browser_transport_t *browser) {
  if (!browser)
    return;
  while (browser->output)
    browser_transport_output_pop(browser);
  SAFE_FREE(browser);
}

acip_transport_t *browser_transport_base(browser_transport_t *browser) {
  return browser ? &browser->transport : NULL;
}

void *browser_transport_client_state(browser_transport_t *browser) {
  return browser ? browser->client_state : NULL;
}

void browser_transport_set_client_state(browser_transport_t *browser, void *state) {
  if (browser)
    browser->client_state = state;
}



const void *browser_transport_output_data(browser_transport_t *browser) {
  return browser && browser->output ? browser->output->data : NULL;
}

size_t browser_transport_output_size(browser_transport_t *browser) {
  return browser && browser->output ? browser->output->length : 0;
}
