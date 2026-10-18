#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/crc32.h>
#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/util/endian.h>
#include <emscripten.h>
#include <string.h>

typedef struct peer_output {
  struct peer_output *next;
  void *data;
  size_t length;
} peer_output_t;

typedef struct {
  acip_security_t security;
  acip_transport_t transport;
  peer_output_t *output, *tail;
  void *received;
  size_t received_length;
} browser_peer_crypto_t;

EMSCRIPTEN_KEEPALIVE const char *peer_crypto_error(void) {
  static asciichat_error_context_t error;
  return HAS_ERRNO(&error) ? error.context_message : "Peer security operation failed";
}

static asciichat_error_t peer_send(acip_transport_t *transport, const void *data, size_t length) {
  browser_peer_crypto_t *peer = transport->impl_data;
  socket_buffer_t input = {.data = data, .len = length};
  acip_packet_plan_t plan;
  crypto_context_t *send_crypto = peer->security.ready ? transport->crypto_ctx : NULL;
  asciichat_error_t result = acip_packet_plan(send_crypto, &input, 1, &plan);
  if (result != ASCIICHAT_OK)
    return result;
  peer_output_t *output = SAFE_CALLOC(1, sizeof(*output), peer_output_t *);
  if (!output)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate peer output");
  output->length = plan.encrypt ? sizeof(packet_header_t) + plan.ciphertext_capacity : length;
  output->data = buffer_alloc(plan.domain, output->length);
  if (!output->data) {
    SAFE_FREE(output);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate peer packet");
  }
  if (plan.encrypt) {
    packet_header_t header;
    size_t encrypted_length = 0;
    result = acip_packet_encrypt(transport->crypto_ctx, &plan, plan.domain, (uint8_t *)output->data + sizeof(header),
                                 plan.ciphertext_capacity, &header, &encrypted_length);
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
  if (peer->tail)
    peer->tail->next = output;
  else
    peer->output = output;
  peer->tail = output;
  return ASCIICHAT_OK;
}

static bool peer_connected(acip_transport_t *transport) {
  (void)transport;
  return true;
}

static const acip_transport_methods_t peer_methods = {.send = peer_send, .is_connected = peer_connected};

EMSCRIPTEN_KEEPALIVE void peer_crypto_output_pop(browser_peer_crypto_t *peer) {
  if (!peer || !peer->output)
    return;
  peer_output_t *output = peer->output;
  peer->output = output->next;
  if (!peer->output)
    peer->tail = NULL;
  buffer_release(output->data);
  SAFE_FREE(output);
}

EMSCRIPTEN_KEEPALIVE void peer_crypto_destroy(browser_peer_crypto_t *peer) {
  if (!peer)
    return;
  while (peer->output)
    peer_crypto_output_pop(peer);
  buffer_release(peer->received);
  acip_security_cleanup(&peer->security);
  SAFE_FREE(peer);
}

EMSCRIPTEN_KEEPALIVE browser_peer_crypto_t *peer_crypto_create(bool server, int encryption, const char *password,
                                                               const uint8_t *identity, const uint8_t *expected_key) {
  browser_peer_crypto_t *peer = SAFE_CALLOC(1, sizeof(*peer), browser_peer_crypto_t *);
  if (!peer)
    return NULL;
  crypto_policy_t policy = {.transport_secure = true,
                            .encrypt = encryption == 1,
                            .no_encrypt = encryption == 0,
                            .has_password = password && password[0],
                            .has_identity = identity != NULL,
                            .has_verification_key = expected_key != NULL};
  asciichat_error_t result = crypto_policy_resolve(&policy, &peer->security.mode);
  peer->security.forbid_encryption = policy.no_encrypt;
  peer->security.require_encryption = policy.encrypt;
  crypto_handshake_context_t *ctx = &peer->security.handshake;
  if (result == ASCIICHAT_OK)
    result = policy.has_password ? crypto_handshake_init_with_password("browser_peer", ctx, server, password)
                                 : crypto_handshake_init("browser_peer", ctx, server);
  if (result != ASCIICHAT_OK) {
    peer_crypto_destroy(peer);
    return NULL;
  }
  if (identity) {
    private_key_t *key = server ? &ctx->server_private_key : &ctx->client_private_key;
    public_key_t *public_key = server ? &ctx->server_public_key : &ctx->client_public_key;
    key->type = KEY_TYPE_ED25519;
    memcpy(key->key.ed25519, identity, 64);
    memcpy(key->public_key, identity + 32, 32);
    public_key->type = KEY_TYPE_ED25519;
    memcpy(public_key->key, identity + 32, 32);
  }
  if (expected_key && server) {
    ctx->client_whitelist = SAFE_CALLOC(1, sizeof(public_key_t), public_key_t *);
    if (!ctx->client_whitelist) {
      peer_crypto_destroy(peer);
      return NULL;
    }
    ctx->client_whitelist[0].type = KEY_TYPE_ED25519;
    memcpy(ctx->client_whitelist[0].key, expected_key, 32);
    ctx->num_whitelisted_clients = 1;
    ctx->require_client_auth = true;
  } else if (expected_key) {
    ctx->verify_server_key = true;
    ctx->expected_server_key_bytes_set = true;
    memcpy(ctx->expected_server_key_bytes, expected_key, 32);
  }
  peer->transport.methods = &peer_methods;
  peer->transport.impl_data = peer;
  if (acip_security_start(&peer->security, &peer->transport) != ASCIICHAT_OK) {
    peer_crypto_destroy(peer);
    return NULL;
  }
  return peer;
}

EMSCRIPTEN_KEEPALIVE bool peer_crypto_ready(browser_peer_crypto_t *peer) {
  return peer && peer->security.ready;
}

EMSCRIPTEN_KEEPALIVE const void *peer_crypto_output_data(browser_peer_crypto_t *peer) {
  return peer && peer->output ? peer->output->data : NULL;
}

EMSCRIPTEN_KEEPALIVE size_t peer_crypto_output_size(browser_peer_crypto_t *peer) {
  return peer && peer->output ? peer->output->length : 0;
}

EMSCRIPTEN_KEEPALIVE const void *peer_crypto_received_data(browser_peer_crypto_t *peer) {
  return peer ? peer->received : NULL;
}

EMSCRIPTEN_KEEPALIVE size_t peer_crypto_received_size(browser_peer_crypto_t *peer) {
  return peer ? peer->received_length : 0;
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t peer_crypto_send(browser_peer_crypto_t *peer, const void *data, size_t length) {
  if (!peer || !peer->security.ready)
    return SET_ERRNO(ERROR_INVALID_STATE, "Peer security is not ready");
  return peer_send(&peer->transport, data, length);
}

EMSCRIPTEN_KEEPALIVE asciichat_error_t peer_crypto_receive(browser_peer_crypto_t *peer, const void *data,
                                                           size_t length) {
  if (!peer || !data || length < sizeof(packet_header_t))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid peer packet");
  buffer_release(peer->received);
  peer->received = NULL;
  peer->received_length = 0;
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  size_t payload_length = length - sizeof(header);
  const void *payload = (const uint8_t *)data + sizeof(header);
  packet_type_t type = NET_TO_HOST_U16(header.type);
  if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC || NET_TO_HOST_U32(header.length) != payload_length ||
      asciichat_crc32(payload, payload_length) != NET_TO_HOST_U32(header.crc32))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid peer packet framing");
  if (!peer->security.ready)
    return acip_security_receive(&peer->security, &peer->transport, type, payload, payload_length);
  bool encrypt = ACIP_CRYPTO_HAS_ENCRYPT(peer->security.mode);
  if (packet_is_handshake_type(type) || (encrypt != (type == PACKET_TYPE_ENCRYPTED)))
    return SET_ERRNO(ERROR_CRYPTO, "Peer packet does not match negotiated security");
  size_t capacity = length;
  peer->received = buffer_alloc(NULL, capacity);
  if (!peer->received)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate peer input");
  if (!encrypt) {
    memcpy(peer->received, data, length);
    peer->received_length = length;
    return ASCIICHAT_OK;
  }
  crypto_result_t result = crypto_decrypt(&peer->security.handshake.crypto_ctx, payload, payload_length, peer->received,
                                          capacity, &peer->received_length);
  if (result != CRYPTO_OK)
    return SET_ERRNO(ERROR_CRYPTO, "Cannot decrypt peer packet");
  if (peer->received_length < sizeof(header))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Incomplete decrypted peer packet");
  memcpy(&header, peer->received, sizeof(header));
  type = NET_TO_HOST_U16(header.type);
  payload_length = peer->received_length - sizeof(header);
  if (NET_TO_HOST_U64(header.magic) != PACKET_MAGIC || NET_TO_HOST_U32(header.length) != payload_length ||
      asciichat_crc32((uint8_t *)peer->received + sizeof(header), payload_length) != NET_TO_HOST_U32(header.crc32) ||
      type == PACKET_TYPE_ENCRYPTED || packet_is_handshake_type(type))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid decrypted peer packet");
  return ASCIICHAT_OK;
}
