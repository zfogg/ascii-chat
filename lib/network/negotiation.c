#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/network/negotiation.h>
#include <ascii-chat/network/acip/acds_protocol.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/network/errors.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/log/log.h>
#include <sodium.h>
#include <string.h>

/** Advance one media-server handshake step without consuming application packets.
 * Version/policy admission (0) precedes algorithm selection (1), optional pinned
 * identity selection (2), key exchange (3), and authentication completion (4).
 * Only a completed handshake publishes the transport's encryption context. */
static asciichat_error_t media_receive(network_negotiation_t *n, acip_transport_t *transport, packet_type_t type,
                                       const void *payload, size_t length) {
  crypto_handshake_context_t *ctx = n->handshake;
  asciichat_error_t result = ASCIICHAT_OK;
  if (n->step == 0) {
    if (type != PACKET_TYPE_PROTOCOL_VERSION || length != sizeof(protocol_version_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected protocol version before application traffic");
    protocol_version_packet_t version;
    memcpy(&version, payload, sizeof(version));
    uint8_t mode = version.supports_encryption;
    if (mode > ACIP_CRYPTO_FULL || NET_TO_HOST_U16(version.protocol_version) != 1)
      return SET_ERRNO(ERROR_CRYPTO, "Unsupported crypto mode or protocol version");
    bool requires_auth = n->require_auth;
    if (requires_auth && !ACIP_CRYPTO_HAS_AUTH(mode))
      return SET_ERRNO(ERROR_CRYPTO_AUTH, "Server requires authentication");
    if ((n->require_encryption && !ACIP_CRYPTO_HAS_ENCRYPT(mode)) ||
        (n->forbid_encryption && ACIP_CRYPTO_HAS_ENCRYPT(mode)))
      return SET_ERRNO(ERROR_CRYPTO, "Requested encryption conflicts with server policy");
    if (!n->secure && !n->forbid_encryption && !ACIP_CRYPTO_HAS_ENCRYPT(mode))
      return SET_ERRNO(ERROR_CRYPTO, "Plain transports require encryption unless --no-encrypt is explicit");
    if (mode == ACIP_CRYPTO_NONE) {
      result = packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
      n->ready = result == ASCIICHAT_OK;
      n->step = 5;
      return result;
    }
    const char *password = n->password ? n->password : "";
    result = n->initialized ? ASCIICHAT_OK
             : password[0]  ? crypto_handshake_init_with_password("server_async", ctx, true, password)
                            : crypto_handshake_init("server_async", ctx, true);
    if (result != ASCIICHAT_OK)
      return result;
    n->initialized = true;
    n->encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(version.supports_encryption);
    if (n->identity_key_count && n->identity_keys[0].type == KEY_TYPE_ED25519) {
      ctx->server_private_key = n->identity_keys[0];
      ctx->server_public_key.type = KEY_TYPE_ED25519;
      memcpy(ctx->server_public_key.key, n->identity_keys[0].public_key, sizeof(n->identity_keys[0].public_key));
    }
    ctx->require_client_auth = n->require_identity;
    ctx->client_whitelist = n->whitelist;
    ctx->num_whitelisted_clients = n->whitelist_count;
    version.protocol_version = HOST_TO_NET_U16(1);
    version.protocol_revision = 0;
    version.compression_algorithms = 0;
    version.compression_threshold = 0;
    version.feature_flags = HOST_TO_NET_U16(PROTOCOL_FEATURE_RECOVERABLE_ERRORS);
    result = packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
    n->step = 1;
    return result;
  }
  if (n->step == 1) {
    // Select algorithms once; a pinned identity request defers the server hello.
    if (type != PACKET_TYPE_CRYPTO_CAPABILITIES || length != sizeof(crypto_capabilities_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected crypto capabilities");
    crypto_capabilities_packet_t capabilities;
    memcpy(&capabilities, payload, sizeof(capabilities));
    crypto_parameters_packet_t parameters = {0};
    parameters.selected_kex = KEX_ALGO_X25519;
    parameters.selected_cipher = n->encrypt_data ? CIPHER_ALGO_XSALSA20_POLY1305 : CIPHER_ALGO_NONE;
    parameters.selected_auth =
        n->identity_key_count && n->identity_keys[0].type == KEY_TYPE_ED25519 ? AUTH_ALGO_ED25519 : AUTH_ALGO_NONE;
    parameters.verification_enabled = n->whitelist_count > 0;
    parameters.kex_public_key_size = CRYPTO_PUBLIC_KEY_SIZE;
    parameters.shared_secret_size = CRYPTO_PUBLIC_KEY_SIZE;
    parameters.nonce_size = CRYPTO_NONCE_SIZE;
    parameters.mac_size = CRYPTO_MAC_SIZE;
    parameters.hmac_size = CRYPTO_HMAC_SIZE;
    if (parameters.selected_auth == AUTH_ALGO_ED25519) {
      parameters.auth_public_key_size = ED25519_PUBLIC_KEY_SIZE;
      parameters.signature_size = ED25519_SIGNATURE_SIZE;
    }
    result = crypto_handshake_set_parameters(ctx, &parameters);
    if (result != ASCIICHAT_OK)
      return result;
    parameters.kex_public_key_size = HOST_TO_NET_U16(parameters.kex_public_key_size);
    parameters.auth_public_key_size = HOST_TO_NET_U16(parameters.auth_public_key_size);
    parameters.signature_size = HOST_TO_NET_U16(parameters.signature_size);
    parameters.shared_secret_size = HOST_TO_NET_U16(parameters.shared_secret_size);
    result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_PARAMETERS, &parameters, sizeof(parameters), 0);
    if (result != ASCIICHAT_OK)
      return result;
    if (n->identity_key_count > 0 && capabilities.requires_verification) {
      n->step = 2;
      return ASCIICHAT_OK;
    }
    n->step = 3;
    return crypto_handshake_server_start(ctx, transport);
  }
  if (n->step == 2) {
    // A legacy single-key client can send its key exchange response directly.
    if (type != PACKET_TYPE_CRYPTO_CLIENT_HELLO) {
      if (n->identity_key_count != 1 || type != PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP)
        return SET_ERRNO(ERROR_CRYPTO, "Expected pinned server identity");
      // Keep this packet for the key-exchange step after sending the default key.
      ctx->server_private_key = n->identity_keys[0];
      ctx->server_public_key.type = KEY_TYPE_ED25519;
      memcpy(ctx->server_public_key.key, n->identity_keys[0].public_key, sizeof(n->identity_keys[0].public_key));
      result = crypto_handshake_server_start(ctx, transport);
      if (result != ASCIICHAT_OK)
        return result;
      n->step = 3;
    } else {
      if (length != ED25519_PUBLIC_KEY_SIZE)
        return SET_ERRNO(ERROR_CRYPTO, "Expected pinned server identity");
      bool found = false;
      for (size_t i = 0; i < n->identity_key_count; ++i) {
        if (sodium_memcmp(payload, n->identity_keys[i].public_key, ED25519_PUBLIC_KEY_SIZE) == 0) {
          ctx->server_private_key = n->identity_keys[i];
          ctx->server_public_key.type = KEY_TYPE_ED25519;
          memcpy(ctx->server_public_key.key, n->identity_keys[i].public_key, sizeof(n->identity_keys[i].public_key));
          found = true;
          break;
        }
      }
      if (!found)
        return SET_ERRNO(ERROR_CRYPTO, "Unknown pinned server identity");
      n->step = 3;
      return crypto_handshake_server_start(ctx, transport);
    }
  }
  if (n->step == 3) {
    // Keep challenge/response verification separate from key derivation.
    result = crypto_handshake_server_auth_challenge(ctx, transport, type, payload, length);
    n->step = 4;
  } else if (n->step == 4) {
    result = crypto_handshake_server_complete(ctx, transport, type, payload, length);
  } else {
    return SET_ERRNO(ERROR_INVALID_STATE, "Unexpected server handshake step");
  }
  if (result == ASCIICHAT_OK && ctx->state == CRYPTO_HANDSHAKE_READY) {
    n->initialized = true;
    ctx->crypto_ctx.encrypt_data = n->encrypt_data;
    transport->crypto_ctx = (crypto_context_t *)crypto_handshake_get_context(ctx);
    n->ready = true;
    n->step = 5;
  }
  return result;
}

asciichat_error_t network_negotiation_receive(network_negotiation_t *n, acip_transport_t *transport, packet_type_t type,
                                              const void *payload, size_t length) {
  if (!n || !n->handshake || !transport || n->ready)
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid connection negotiation state");
  if (n->protocol == NETWORK_PROTOCOL_MEDIA)
    return media_receive(n, transport, type, payload, length);
  crypto_handshake_context_t *ctx = n->handshake;
  asciichat_error_t result;
  switch (n->step) {
  case 0: {
    if (type != PACKET_TYPE_PROTOCOL_VERSION || length != sizeof(protocol_version_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected discovery protocol version");
    protocol_version_packet_t version;
    memcpy(&version, payload, sizeof(version));
    if (NET_TO_HOST_U16(version.protocol_version) != 1)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unsupported discovery protocol version");
    uint8_t mode = version.supports_encryption;
    result = crypto_policy_check_mode(n->secure, n->require_encryption, n->forbid_encryption, n->require_auth, mode);
    if (result != ASCIICHAT_OK)
      return result;
    ctx->crypto_ctx.encrypt_data = n->encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(mode);
    if (mode == ACIP_CRYPTO_NONE) {
      ctx->state = CRYPTO_HANDSHAKE_DISABLED;
      n->ready = true;
      return ASCIICHAT_OK;
    }
    result = crypto_handshake_server_send_parameters(ctx, transport);
    if (result == ASCIICHAT_OK)
      result = crypto_handshake_server_start(ctx, transport);
    break;
  }
  case 1:
    result = crypto_handshake_server_auth_challenge(ctx, transport, type, payload, length);
    break;
  case 2:
    result = crypto_handshake_server_complete(ctx, transport, type, payload, length);
    break;
  default:
    return SET_ERRNO(ERROR_CRYPTO, "Invalid discovery negotiation state");
  }
  if (result != ASCIICHAT_OK)
    return result;
  ++n->step;
  n->ready = ctx->state == CRYPTO_HANDSHAKE_READY;
  if (n->ready)
    transport->crypto_ctx = &ctx->crypto_ctx;
  return ASCIICHAT_OK;
}

static asciichat_error_t negotiation_read(acip_transport_t *transport, uint64_t deadline, packet_type_t *type,
                                          void **payload, size_t *length, void **allocation) {
  for (;;) {
    uint64_t now = time_get_ns();
    if (APP_CALLBACK_BOOL(should_exit)) {
      acip_transport_interrupt(transport);
      return SET_ERRNO(ERROR_NETWORK, "Connection negotiation cancelled");
    }
    if (now >= deadline)
      return SET_ERRNO(ERROR_NETWORK_TIMEOUT, "Connection negotiation deadline expired");
    transport->receive_timeout_ns = MIN(deadline - now, 100 * NS_PER_MS_INT);
    asciichat_errno_scope_t scope = asciichat_errno_scope_begin();
    asciichat_error_t result = packet_receive_via_transport(transport, type, payload, length, allocation);
    if (result == ERROR_NETWORK_TIMEOUT || result == ERROR_NETWORK_INCOMPLETE) {
      asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_DISMISSED);
      continue;
    }
    asciichat_errno_scope_end(scope, ASCIICHAT_ERRNO_HANDLED);
    if (result == ASCIICHAT_OK && *type == PACKET_TYPE_ERROR_MESSAGE)
      result = network_error_decode(*type, *payload, *length);
    return result;
  }
}
static asciichat_error_t client_negotiate(acip_transport_t *transport, crypto_handshake_context_t *ctx, uint8_t mode) {
  const uint64_t deadline = time_get_ns() + 30 * NS_PER_SEC_INT;
  protocol_version_packet_t version = {.protocol_version = HOST_TO_NET_U16(1),
                                       .supports_encryption = mode,
                                       .feature_flags = HOST_TO_NET_U16(PROTOCOL_FEATURE_RECOVERABLE_ERRORS)};
  asciichat_error_t result =
      packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
  packet_type_t type = 0;
  void *payload = NULL, *allocation = NULL;
  size_t length = 0;
  if (result == ASCIICHAT_OK)
    result = negotiation_read(transport, deadline, &type, &payload, &length, &allocation);
  if (result == ASCIICHAT_OK) {
    if (type != PACKET_TYPE_PROTOCOL_VERSION || length != sizeof(version))
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected media protocol version");
    else {
      memcpy(&version, payload, sizeof(version));
      if (NET_TO_HOST_U16(version.protocol_version) != 1 || version.supports_encryption != mode)
        result = SET_ERRNO(ERROR_CRYPTO_AUTH, "Peer changed requested security mode");
    }
  }
  buffer_release(allocation);
  if (result != ASCIICHAT_OK)
    return result;
  if (mode == ACIP_CRYPTO_NONE) {
    ctx->state = CRYPTO_HANDSHAKE_DISABLED;
    return ASCIICHAT_OK;
  }
  crypto_capabilities_packet_t caps = {
      .supported_kex_algorithms = HOST_TO_NET_U16(KEX_ALGO_X25519),
      .supported_auth_algorithms = HOST_TO_NET_U16(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE),
      .supported_cipher_algorithms = HOST_TO_NET_U16(CIPHER_ALGO_XSALSA20_POLY1305 | CIPHER_ALGO_NONE),
      .requires_verification = ctx->verify_server_key && ctx->expected_server_key[0],
      .preferred_kex = KEX_ALGO_X25519,
      .preferred_auth = ACIP_CRYPTO_HAS_AUTH(mode) ? AUTH_ALGO_ED25519 : AUTH_ALGO_NONE,
      .preferred_cipher = ACIP_CRYPTO_HAS_ENCRYPT(mode) ? CIPHER_ALGO_XSALSA20_POLY1305 : CIPHER_ALGO_NONE};
  result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_CAPABILITIES, &caps, sizeof(caps), 0);
  allocation = NULL;
  if (result == ASCIICHAT_OK)
    result = negotiation_read(transport, deadline, &type, &payload, &length, &allocation);
  if (result == ASCIICHAT_OK) {
    crypto_parameters_packet_t params;
    if (type != PACKET_TYPE_CRYPTO_PARAMETERS || length != sizeof(params))
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected crypto parameters");
    else {
      memcpy(&params, payload, sizeof(params));
      if (params.selected_kex != KEX_ALGO_X25519 || params.selected_cipher != caps.preferred_cipher)
        result = SET_ERRNO(ERROR_CRYPTO_AUTH, "Unsupported negotiated algorithms");
      else {
        result = crypto_handshake_set_parameters(ctx, &params);
        if (params.verification_enabled)
          ctx->server_uses_client_auth = ctx->require_client_auth = true;
      }
    }
  }
  buffer_release(allocation);
  if (result != ASCIICHAT_OK)
    return result;
  if (caps.requires_verification) {
    public_key_t keys[MAX_CLIENTS];
    size_t count = 0;
    result = parse_public_keys(ctx->expected_server_key, keys, &count, MAX_CLIENTS);
    if (result != ASCIICHAT_OK || !count)
      return SET_ERRNO(ERROR_CRYPTO_AUTH, "Cannot resolve pinned server identity");
    result =
        packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_CLIENT_HELLO, keys[0].key, ED25519_PUBLIC_KEY_SIZE, 0);
    if (result != ASCIICHAT_OK)
      return result;
  }
  for (unsigned step = 0; step < 3 && ctx->state != CRYPTO_HANDSHAKE_READY; ++step) {
    allocation = NULL;
    result = negotiation_read(transport, deadline, &type, &payload, &length, &allocation);
    if (result == ASCIICHAT_OK) {
      if (!step)
        result = crypto_handshake_client_key_exchange(ctx, transport, type, payload, length);
      else if (step == 1)
        result = crypto_handshake_client_auth_response(ctx, transport, type, payload, length);
      else
        result = crypto_handshake_client_complete(ctx, transport, type, payload, length);
    }
    buffer_release(allocation);
    if (result != ASCIICHAT_OK)
      return result;
  }
  if (ctx->state != CRYPTO_HANDSHAKE_READY)
    return SET_ERRNO(ERROR_CRYPTO_HANDSHAKE, "Negotiation did not reach readiness");
  ctx->crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(mode);
  transport->crypto_ctx = &ctx->crypto_ctx;
  return ASCIICHAT_OK;
}
static asciichat_error_t discovery_client_negotiate(acip_transport_t *transport, crypto_handshake_context_t *ctx,
                                                    uint8_t mode) {
  ctx->crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(mode);
  asciichat_error_t result = acds_handshake_begin(transport, mode);
  if (result != ASCIICHAT_OK)
    return result;
  if (mode == ACIP_CRYPTO_NONE) {
    ctx->state = CRYPTO_HANDSHAKE_DISABLED;
    return ASCIICHAT_OK;
  }
  uint64_t deadline = time_get_ns() + 30 * NS_PER_SEC_INT;
  for (unsigned step = 0; step < 3 && ctx->state != CRYPTO_HANDSHAKE_READY; ++step) {
    packet_type_t type;
    void *payload = NULL, *allocation = NULL;
    size_t length = 0;
    result = negotiation_read(transport, deadline, &type, &payload, &length, &allocation);
    if (result == ASCIICHAT_OK)
      result = acds_handshake_receive(ctx, transport, type, payload, length);
    buffer_release(allocation);
    if (result != ASCIICHAT_OK)
      return result;
  }
  if (ctx->state != CRYPTO_HANDSHAKE_READY)
    return SET_ERRNO(ERROR_CRYPTO_HANDSHAKE, "Discovery negotiation did not reach readiness");
  transport->crypto_ctx = &ctx->crypto_ctx;
  return ASCIICHAT_OK;
}
asciichat_error_t network_client_negotiate(acip_transport_t *transport, crypto_handshake_context_t *ctx, uint8_t mode,
                                           network_protocol_t protocol) {
  if (!transport || !ctx || mode > ACIP_CRYPTO_FULL || protocol > NETWORK_PROTOCOL_DISCOVERY)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid client negotiation");
  uint64_t timeout = transport->receive_timeout_ns;
  asciichat_error_t result = protocol == NETWORK_PROTOCOL_DISCOVERY ? discovery_client_negotiate(transport, ctx, mode)
                                                                    : client_negotiate(transport, ctx, mode);
  transport->receive_timeout_ns = timeout;
  return result;
}
