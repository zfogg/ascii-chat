#include <ascii-chat/network/browser/session.h>
#include <ascii-chat/network/browser/transport.h>
#include <ascii-chat/crypto/sizes.h>
#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/acip/prepare.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/util/endian.h>
#include <emscripten.h>
#include <string.h>

struct browser_session {
  acip_security_t security;
  bool quic_handshake;
  browser_transport_t *browser;
  void *received;
  size_t received_length;
};

const char *browser_session_error(void) {
  static asciichat_error_context_t error;
  return HAS_ERRNO(&error) ? error.context_message : "Peer security operation failed";
}

void browser_session_output_pop(browser_session_t *peer) {
  if (peer)
    browser_transport_output_pop(peer->browser);
}

void browser_session_destroy(browser_session_t *peer) {
  if (!peer)
    return;
  buffer_release(peer->received);
  acip_security_cleanup(&peer->security);
  browser_transport_destroy(peer->browser);
  SAFE_FREE(peer);
}

browser_session_t *browser_session_create(bool server, int encryption, const char *password, const uint8_t *identity,
                                          const uint8_t *expected_key) {
  browser_session_t *peer = SAFE_CALLOC(1, sizeof(*peer), browser_session_t *);
  if (!peer) {
    SET_ERRNO(ERROR_MEMORY, "Cannot allocate peer security state");
    return NULL;
  }
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
    browser_session_destroy(peer);
    return NULL;
  }
  if (identity) {
    private_key_t *key = server ? &ctx->server_private_key : &ctx->client_private_key;
    public_key_t *public_key = server ? &ctx->server_public_key : &ctx->client_public_key;
    key->type = KEY_TYPE_ED25519;
    memcpy(key->key.ed25519, identity, ED25519_PRIVATE_KEY_SIZE);
    memcpy(key->public_key, identity + ED25519_SEED_SIZE, ED25519_PUBLIC_KEY_SIZE);
    public_key->type = KEY_TYPE_ED25519;
    memcpy(public_key->key, identity + ED25519_SEED_SIZE, ED25519_PUBLIC_KEY_SIZE);
  }
  if (expected_key && server) {
    ctx->client_whitelist = SAFE_CALLOC(1, sizeof(public_key_t), public_key_t *);
    if (!ctx->client_whitelist) {
      browser_session_destroy(peer);
      SET_ERRNO(ERROR_MEMORY, "Cannot allocate peer key whitelist");
      return NULL;
    }
    ctx->client_whitelist[0].type = KEY_TYPE_ED25519;
    memcpy(ctx->client_whitelist[0].key, expected_key, ED25519_PUBLIC_KEY_SIZE);
    ctx->num_whitelisted_clients = 1;
    ctx->require_client_auth = true;
  } else if (expected_key) {
    ctx->verify_server_key = true;
    ctx->expected_server_key_bytes_set = true;
    memcpy(ctx->expected_server_key_bytes, expected_key, ED25519_PUBLIC_KEY_SIZE);
  }
  peer->browser = browser_transport_create();
  if (!peer->browser) {
    browser_session_destroy(peer);
    return NULL;
  }
  // The browser owns the actual WebSocket/WebRTC channel.  It must explicitly
  // publish that channel's state; the transport is not connected merely
  // because its C object exists.
  return peer;
}

asciichat_error_t browser_session_start(browser_session_t *peer) {
  if (!peer)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer security state");
  return acip_security_start(&peer->security, browser_transport_base(peer->browser));
}

void browser_session_set_connected(browser_session_t *peer, bool connected) {
  if (peer)
    browser_transport_set_connected(peer->browser, connected);
}

bool browser_session_ready(browser_session_t *peer) {
  return peer && peer->security.ready;
}

size_t browser_session_queued_bytes(const browser_session_t *peer) {
  return peer ? browser_transport_queued_bytes(peer->browser) : 0;
}

bool browser_session_writable(const browser_session_t *peer, size_t additional_bytes) {
  return peer && browser_transport_writable(peer->browser, additional_bytes);
}

const void *browser_session_output_data(browser_session_t *peer) {
  return peer ? browser_transport_output_data(peer->browser) : NULL;
}

size_t browser_session_output_size(browser_session_t *peer) {
  return peer ? browser_transport_output_size(peer->browser) : 0;
}

const void *browser_session_received_data(browser_session_t *peer) {
  return peer ? peer->received : NULL;
}

size_t browser_session_received_size(browser_session_t *peer) {
  return peer ? peer->received_length : 0;
}

asciichat_error_t browser_session_send(browser_session_t *peer, const void *data, size_t length) {
  if (!peer || !data || !length)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer packet or security state");
  if (!peer->security.ready)
    return SET_ERRNO(ERROR_INVALID_STATE, "Peer security is not ready");
  return acip_transport_send(browser_transport_base(peer->browser), data, length);
}

asciichat_error_t browser_session_receive(browser_session_t *peer, const void *data, size_t length) {
  if (!peer || !data)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer packet or security state");
  if (length < sizeof(packet_header_t))
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid peer packet");
  buffer_release(peer->received);
  peer->received = NULL;
  peer->received_length = 0;
  packet_header_t header;
  memcpy(&header, data, sizeof(header));
  size_t payload_length = length - sizeof(header);
  const void *payload = (const uint8_t *)data + sizeof(header);
  packet_type_t type = NET_TO_HOST_U16(header.type);
  asciichat_error_t validation = packet_validate(data, length, 0, NULL);
  if (validation != ASCIICHAT_OK)
    return validation;
  if (!peer->security.ready) {
    asciichat_error_t result = acip_security_receive(&peer->security, browser_transport_base(peer->browser), type,
                                                     payload, payload_length);
    if (result != ASCIICHAT_OK || !peer->quic_handshake || type != PACKET_TYPE_PROTOCOL_VERSION)
      return result;
    crypto_capabilities_packet_t caps = {0};
    caps.supported_kex_algorithms = HOST_TO_NET_U16(KEX_ALGO_X25519);
    caps.supported_auth_algorithms = HOST_TO_NET_U16(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE);
    caps.supported_cipher_algorithms = HOST_TO_NET_U16(CIPHER_ALGO_XSALSA20_POLY1305 | CIPHER_ALGO_NONE);
    caps.preferred_kex = KEX_ALGO_X25519;
    caps.preferred_auth = ACIP_CRYPTO_HAS_AUTH(peer->security.mode) ? AUTH_ALGO_ED25519 : AUTH_ALGO_NONE;
    caps.preferred_cipher = CIPHER_ALGO_XSALSA20_POLY1305;
    caps.requires_verification = peer->security.handshake.verify_server_key;
    result = packet_send_via_transport(browser_transport_base(peer->browser), PACKET_TYPE_CRYPTO_CAPABILITIES,
                                       &caps, sizeof(caps), 0);
    if (result != ASCIICHAT_OK || !caps.requires_verification)
      return result;
    return packet_send_via_transport(browser_transport_base(peer->browser), PACKET_TYPE_CRYPTO_CLIENT_HELLO,
                                     peer->security.handshake.expected_server_key_bytes, ED25519_PUBLIC_KEY_SIZE, 0);
  }
  bool encrypt = ACIP_CRYPTO_HAS_ENCRYPT(peer->security.mode);
  if (packet_is_initial_handshake_type(type) || (encrypt != (type == PACKET_TYPE_ENCRYPTED)))
    return SET_ERRNO(ERROR_CRYPTO, "Peer packet does not match negotiated security");
  if (!encrypt) {
    peer->received = buffer_alloc(NULL, length);
    if (!peer->received)
      return SET_ERRNO(ERROR_MEMORY, "Cannot allocate peer input");
    memcpy(peer->received, data, length);
    peer->received_length = length;
    return ASCIICHAT_OK;
  }
  bool consumed = false;
  uint64_t sequence;
  asciichat_error_t result =
      acip_security_open(&peer->security.handshake.crypto_ctx, CRYPTO_RECORD_ANY_CHANNEL, ACIP_DELIVERY_RELIABLE, data,
                         length, (uint8_t **)&peer->received, &peer->received_length, &sequence, &consumed);
  if (consumed) {
    buffer_release(peer->received);
    peer->received = NULL;
    peer->received_length = 0;
  }
  return result;
}

crypto_context_t *browser_session_security_context(browser_session_t *peer) {
  return peer ? &peer->security.handshake.crypto_ctx : NULL;
}

asciichat_error_t browser_session_tick(browser_session_t *peer) {
  if (!peer)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid peer security state");
  if (!peer->security.ready || peer->quic_handshake || !ACIP_CRYPTO_HAS_ENCRYPT(peer->security.mode))
    return ASCIICHAT_OK;
      return acip_security_flush(browser_transport_base(peer->browser));
}

asciichat_error_t browser_session_quic_begin(browser_session_t *peer) {
  if (!peer || peer->security.version_received || peer->quic_handshake || !ACIP_CRYPTO_HAS_ENCRYPT(peer->security.mode))
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid peer QUIC handshake transition");
  peer->quic_handshake = true;
  return ASCIICHAT_OK;
}

asciichat_error_t browser_session_security_init(browser_session_t *peer) {
  return peer ? browser_transport_security_init(peer->browser) : SET_ERRNO(ERROR_INVALID_STATE, "Browser session is closed");
}

asciichat_error_t browser_session_security_packet(browser_session_t *peer, bool seal, uint32_t channel, uint32_t delivery,
                                                   const uint8_t *packet, size_t length, uint8_t *output, size_t capacity,
                                                   size_t *out_length, uint64_t *sequence) {
  return peer ? browser_transport_security_packet(peer->browser, seal, channel, delivery, packet, length, output, capacity,
                                                  out_length, sequence)
              : SET_ERRNO(ERROR_INVALID_STATE, "Browser session is closed");
}

asciichat_error_t browser_session_security_tick(browser_session_t *peer) {
  return peer ? browser_transport_security_tick(peer->browser) : SET_ERRNO(ERROR_INVALID_STATE, "Browser session is closed");
}

asciichat_error_t browser_session_security_output(browser_session_t *peer, uint8_t *output, size_t capacity, size_t *length,
                                                  uint32_t *channel) {
  return peer ? browser_transport_security_output(peer->browser, output, capacity, length, channel)
              : SET_ERRNO(ERROR_INVALID_STATE, "Browser session is closed");
}

double browser_session_security_epoch(browser_session_t *peer) {
  return peer ? browser_transport_security_epoch(peer->browser) : -1;
}
