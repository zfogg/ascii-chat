#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/util/endian.h>
#include <string.h>

void acip_security_cleanup(acip_security_t *security) {
  if (!security)
    return;
  SAFE_FREE(security->handshake.client_whitelist);
  crypto_handshake_destroy(&security->handshake);
}

asciichat_error_t acip_security_start(acip_security_t *security, acip_transport_t *transport) {
  if (!security || !transport || security->mode > ACIP_CRYPTO_FULL ||
      security->handshake.state != CRYPTO_HANDSHAKE_INIT)
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid peer security initialization");
  security->handshake.addressless_transport = true;
  transport->crypto_ctx = &security->handshake.crypto_ctx;
  security->handshake.crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(security->mode);
  if (security->handshake.is_server)
    return ASCIICHAT_OK;
  protocol_version_packet_t version = {0};
  version.protocol_version = HOST_TO_NET_U16(1);
  version.supports_encryption = security->mode;
  return packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
}

asciichat_error_t acip_security_receive(acip_security_t *security, acip_transport_t *transport, packet_type_t type,
                                        const void *payload, size_t length) {
  if (!security || !transport || (!payload && length) || security->ready)
    return SET_ERRNO(ERROR_INVALID_STATE, "Unexpected peer security packet");
  crypto_handshake_context_t *ctx = &security->handshake;
  asciichat_error_t result = ASCIICHAT_OK;
  if (!security->version_received) {
    if (type != PACKET_TYPE_PROTOCOL_VERSION || length != sizeof(protocol_version_packet_t))
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected peer security negotiation before media");
    protocol_version_packet_t version;
    memcpy(&version, payload, sizeof(version));
    uint8_t mode = version.supports_encryption;
    if (NET_TO_HOST_U16(version.protocol_version) != 1 || mode > ACIP_CRYPTO_FULL)
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unsupported peer security protocol");
    if (ctx->is_server) {
      // Both peers' requirements must hold; a remote request cannot disable local security.
      mode |= security->mode;
      if (security->forbid_encryption && ACIP_CRYPTO_HAS_ENCRYPT(mode))
        return SET_ERRNO(ERROR_CRYPTO, "Peer requires encryption but local policy disables it");
      version.supports_encryption = mode;
      result = packet_send_via_transport(transport, PACKET_TYPE_PROTOCOL_VERSION, &version, sizeof(version), 0);
      if (result != ASCIICHAT_OK)
        return result;
    } else if ((mode & security->mode) != security->mode) {
      return SET_ERRNO(ERROR_CRYPTO, "Peer attempted to downgrade requested security");
    }
    if (security->forbid_encryption && ACIP_CRYPTO_HAS_ENCRYPT(mode))
      return SET_ERRNO(ERROR_CRYPTO, "Peer requires encryption but local policy disables it");
    security->mode = mode;
    security->version_received = true;
    ctx->crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(mode);
    if (mode == ACIP_CRYPTO_NONE) {
      ctx->state = CRYPTO_HANDSHAKE_DISABLED;
      security->ready = true;
      return ASCIICHAT_OK;
    }
    if (!ctx->is_server)
      return ASCIICHAT_OK;
    result = crypto_handshake_server_send_parameters(ctx, transport);
    if (result == ASCIICHAT_OK)
      result = crypto_handshake_server_start(ctx, transport);
    return result;
  }
  if (ctx->is_server) {
    if (ctx->state == CRYPTO_HANDSHAKE_KEY_EXCHANGE)
      result = crypto_handshake_server_auth_challenge(ctx, transport, type, payload, length);
    else if (ctx->state == CRYPTO_HANDSHAKE_AUTHENTICATING)
      result = crypto_handshake_server_complete(ctx, transport, type, payload, length);
    else
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unexpected peer authentication state");
  } else {
    switch (type) {
    case PACKET_TYPE_CRYPTO_PARAMETERS:
      if (security->parameters_received || ctx->state != CRYPTO_HANDSHAKE_INIT ||
          length != sizeof(crypto_parameters_packet_t))
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid peer crypto parameters");
      result = crypto_handshake_set_parameters(ctx, payload);
      if (result == ASCIICHAT_OK && ctx->crypto_ctx.encrypt_data != !!ACIP_CRYPTO_HAS_ENCRYPT(security->mode))
        return SET_ERRNO(ERROR_CRYPTO, "Peer cipher does not match negotiated policy");
      security->parameters_received = result == ASCIICHAT_OK;
      break;
    case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT:
      if (!security->parameters_received)
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Missing peer crypto parameters");
      if (ctx->verify_server_key && length == ctx->crypto_ctx.public_key_size)
        return SET_ERRNO(ERROR_CRYPTO_AUTH, "Peer omitted the required identity key");
      result = crypto_handshake_client_key_exchange(ctx, transport, type, payload, length);
      break;
    case PACKET_TYPE_CRYPTO_AUTH_CHALLENGE:
      result = crypto_handshake_client_auth_response(ctx, transport, type, payload, length);
      break;
    case PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE:
      if (ctx->has_password || ctx->client_private_key.type == KEY_TYPE_ED25519)
        return SET_ERRNO(ERROR_CRYPTO_AUTH, "Peer skipped required mutual authentication");
      result = crypto_handshake_client_complete(ctx, transport, type, payload, length);
      break;
    case PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP:
    case PACKET_TYPE_CRYPTO_AUTH_FAILED:
      result = crypto_handshake_client_complete(ctx, transport, type, payload, length);
      break;
    default:
      result = SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Application packet before peer authentication");
    }
  }
  if (result == ASCIICHAT_OK)
    security->ready = ctx->state == CRYPTO_HANDSHAKE_READY;
  else
    ctx->state = CRYPTO_HANDSHAKE_FAILED;
  return result;
}
