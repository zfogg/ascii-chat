
/* Existing buffer budgets in bytes, including NUL for text storage. */
#define HANDSHAKE_ERROR_PLAINTEXT_CAPACITY 256

#include <ascii-chat/otel.h>
/**
 * @file crypto/handshake/common.c
 * @ingroup handshake
 * @brief Common handshake functions: initialization, cleanup, encryption, rekeying
 */

#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/buffer.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/debug/named.h>
#include <stdio.h>
#include <string.h>

// Initialize named crypto handshake context
asciichat_error_t crypto_handshake_init(const char *name, crypto_handshake_context_t *ctx, bool is_server) {
  if (!name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Crypto context name is required");
  }

  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
  }

  // Register crypto context with debug naming system
  NAMED_REGISTER_CRYPTO_CONTEXT(ctx, name, NULL);

  // Zero out the context
  memset(ctx, 0, sizeof(crypto_handshake_context_t));

  // Initialize core crypto context
  crypto_result_t result = crypto_init(&ctx->crypto_ctx);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to initialize crypto context: %s", crypto_result_to_string(result));
  }

  ctx->state = CRYPTO_HANDSHAKE_INIT;
  ctx->is_server = is_server;
  ctx->crypto_ctx.server_role = is_server;
  ctx->crypto_ctx.server_role_known = true;
  ctx->verify_server_key = false;
  ctx->require_client_auth = false;
  ctx->server_uses_client_auth = false; // Set to true only if authenticated packet received

  // Load server keys if this is a server
  if (is_server) {
    log_dev("Server crypto handshake initialized (ephemeral keys)");
  } else {
    log_dev("Client crypto handshake initialized");
  }

  return ASCIICHAT_OK;
}

// Set crypto parameters from crypto_parameters_packet_t
asciichat_error_t crypto_handshake_set_parameters(crypto_handshake_context_t *ctx,
                                                  const crypto_parameters_packet_t *params) {
  if (!ctx || !params) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, params=%p", ctx, params);
  }

  ctx->crypto_ctx.encrypt_data = params->selected_cipher != CIPHER_ALGO_NONE;

  // Client receives network byte order and must convert
  // Server uses host byte order and must NOT convert
  if (ctx->is_server) {
    // Server: values are already in host byte order
    // Update crypto context with negotiated parameters directly
    ctx->crypto_ctx.public_key_size = params->kex_public_key_size;
    ctx->crypto_ctx.auth_public_key_size = params->auth_public_key_size;
    ctx->crypto_ctx.shared_key_size = params->shared_secret_size;
    ctx->crypto_ctx.signature_size = params->signature_size;
  } else {
    ctx->require_client_auth = params->verification_enabled != 0;
    // Client: convert from network byte order
    // Update crypto context with negotiated parameters directly
    ctx->crypto_ctx.public_key_size = NET_TO_HOST_U16(params->kex_public_key_size);
    ctx->crypto_ctx.auth_public_key_size = NET_TO_HOST_U16(params->auth_public_key_size);
    ctx->crypto_ctx.shared_key_size = NET_TO_HOST_U16(params->shared_secret_size);
    ctx->crypto_ctx.signature_size = NET_TO_HOST_U16(params->signature_size);
  }
  // Update crypto context with negotiated parameters directly
  ctx->crypto_ctx.nonce_size = params->nonce_size;
  ctx->crypto_ctx.mac_size = params->mac_size;
  ctx->crypto_ctx.hmac_size = params->hmac_size;
  ctx->crypto_ctx.auth_challenge_size =
      AUTH_CHALLENGE_SIZE; // Auth challenge size is fixed for now, could be negotiated later
  ctx->crypto_ctx.encryption_key_size =
      (uint8_t)ctx->crypto_ctx.shared_key_size;                       // Use shared key size as encryption key size
  ctx->crypto_ctx.private_key_size = ctx->crypto_ctx.public_key_size; // Same as public key for X25519
  ctx->crypto_ctx.salt_size = ARGON2ID_SALT_SIZE;                     // Salt size doesn't change

  log_debug("Crypto parameters set: kex_key=%u, auth_key=%u, sig=%u, "
            "secret=%u, nonce=%u, mac=%u, hmac=%u",
            ctx->crypto_ctx.public_key_size, ctx->crypto_ctx.auth_public_key_size, ctx->crypto_ctx.signature_size,
            ctx->crypto_ctx.shared_key_size, ctx->crypto_ctx.nonce_size, ctx->crypto_ctx.mac_size,
            ctx->crypto_ctx.hmac_size);

  return ASCIICHAT_OK;
}

// Validate crypto packet size based on session parameters
asciichat_error_t crypto_handshake_validate_packet_size(const crypto_handshake_context_t *ctx, uint16_t packet_type,
                                                        size_t packet_size) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
  }

  switch (packet_type) {
  case PACKET_TYPE_CRYPTO_CAPABILITIES:
    if (packet_size != sizeof(crypto_capabilities_packet_t)) {
      // Don't  return an error code, just set the errno and return the error code
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid crypto capabilities packet size: %zu (expected %zu)",
                       packet_size, sizeof(crypto_capabilities_packet_t));
    }
    break;

  case PACKET_TYPE_CRYPTO_PARAMETERS:
    if (packet_size != sizeof(crypto_parameters_packet_t)) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid crypto parameters packet size: %zu (expected %zu)", packet_size,
                       sizeof(crypto_parameters_packet_t));
    }
    break;

  case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT:
    // Server can send either:
    // 1. Simple format: kex_public_key_size (when server has no identity key)
    // 2. Authenticated format: kex_public_key_size + auth_public_key_size + signature_size
    {
      size_t simple_size = ctx->crypto_ctx.public_key_size;
      size_t authenticated_size =
          ctx->crypto_ctx.public_key_size + ctx->crypto_ctx.auth_public_key_size + ctx->crypto_ctx.signature_size;

      if (packet_size != simple_size && packet_size != authenticated_size) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                         "Invalid KEY_EXCHANGE_INIT size: %zu (expected %zu for simple or %zu for authenticated: "
                         "kex=%u + auth=%u + sig=%u)",
                         packet_size, simple_size, authenticated_size, ctx->crypto_ctx.public_key_size,
                         ctx->crypto_ctx.auth_public_key_size, ctx->crypto_ctx.signature_size);
      }
    }
    break;

  case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP:
    // Client can send either:
    // 1. Simple format: kex_public_key_size (when server has no identity key)
    // 2. Authenticated format: kex_public_key_size + client_auth_key_size + client_sig_size + [gpg_key_id_len:1] +
    // [gpg_key_id:0-16]
    {
      size_t simple_size = ctx->crypto_ctx.public_key_size;
      // For authenticated format, use Ed25519 sizes since client has Ed25519 key
      size_t ed25519_auth_size = ED25519_PUBLIC_KEY_SIZE; // Ed25519 public key is always 32 bytes
      size_t ed25519_sig_size = ED25519_SIGNATURE_SIZE;   // Ed25519 signature is always 64 bytes
      size_t authenticated_min_size = ctx->crypto_ctx.public_key_size + ed25519_auth_size + ed25519_sig_size;
      size_t authenticated_max_size = authenticated_min_size + 1 + 40; // +1 for length, +40 for max GPG key ID

      if (packet_size != simple_size &&
          (packet_size < authenticated_min_size || packet_size > authenticated_max_size)) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                         "Invalid KEY_EXCHANGE_RESP size: %zu (expected %zu for simple or %zu-%zu for authenticated: "
                         "kex=%u + auth=%u + sig=%u + optional GPG key ID)",
                         packet_size, simple_size, authenticated_min_size, authenticated_max_size,
                         ctx->crypto_ctx.public_key_size, ed25519_auth_size, ed25519_sig_size);
      }
    }
    break;

  case PACKET_TYPE_CRYPTO_AUTH_CHALLENGE:
    // Server sends: 1 byte auth_flags + auth_challenge_size byte nonce
    {
      size_t expected_size = AUTH_CHALLENGE_FLAGS_SIZE + ctx->crypto_ctx.auth_challenge_size;
      if (packet_size != expected_size) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid AUTH_CHALLENGE size: %zu (expected %zu: flags=%d + nonce=%u)",
                         packet_size, expected_size, AUTH_CHALLENGE_FLAGS_SIZE, ctx->crypto_ctx.auth_challenge_size);
      }
    }
    break;

  case PACKET_TYPE_CRYPTO_AUTH_RESPONSE:
    // Client sends: hmac_size + auth_challenge_size bytes client_nonce + [gpg_key_id_len:1] + [gpg_key_id:0-40]
    {
      size_t min_size = ctx->crypto_ctx.hmac_size + ctx->crypto_ctx.auth_challenge_size;
      size_t max_size = min_size + 1 + 40; // +1 for length, +40 for max GPG key ID
      if (packet_size < min_size || packet_size > max_size) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                         "Invalid AUTH_RESPONSE size: %zu (expected %zu-%zu: hmac=%u + "
                         "nonce=%u + optional GPG key ID)",
                         packet_size, min_size, max_size, ctx->crypto_ctx.hmac_size,
                         ctx->crypto_ctx.auth_challenge_size);
      }
    }
    break;

  case PACKET_TYPE_CRYPTO_AUTH_FAILED:
    // Variable size - just check reasonable limits
    if (packet_size > MAX_AUTH_FAILED_PACKET_SIZE) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid AUTH_FAILED size: %zu (max %d)", packet_size,
                       MAX_AUTH_FAILED_PACKET_SIZE);
    }
    break;

  case PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP:
    // Server sends: hmac_size bytes
    if (packet_size != ctx->crypto_ctx.hmac_size) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid SERVER_AUTH_RESP size: %zu (expected %u)", packet_size,
                       ctx->crypto_ctx.hmac_size);
    }
    break;

  case PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE:
    // Empty packet
    if (packet_size != 0) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid HANDSHAKE_COMPLETE size: %zu (expected 0)", packet_size);
    }
    break;

  case PACKET_TYPE_CRYPTO_NO_ENCRYPTION:
    // Empty packet
    if (packet_size != 0) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid NO_ENCRYPTION size: %zu (expected 0)", packet_size);
    }
    break;

  case PACKET_TYPE_ENCRYPTED:
    // Variable size - check reasonable limits
    if (packet_size > MAX_ENCRYPTED_PACKET_SIZE) { // 64KB max for encrypted packets
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid ENCRYPTED size: %zu (max %d)", packet_size,
                       MAX_ENCRYPTED_PACKET_SIZE);
    }
    break;

  default:
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Unknown crypto packet type: %u", packet_type);
  }

  return ASCIICHAT_OK;
}

// Initialize named crypto handshake context with password authentication
asciichat_error_t crypto_handshake_init_with_password(const char *name, crypto_handshake_context_t *ctx, bool is_server,
                                                      const char *password) {
  if (!name) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Crypto context name is required");
  }

  if (!ctx || !password) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, password=%p", ctx, password);
  }

  // Register crypto context with debug naming system
  NAMED_REGISTER_CRYPTO_CONTEXT(ctx, name, NULL);

  // Zero out the context
  memset(ctx, 0, sizeof(crypto_handshake_context_t));

  // Initialize core crypto context with password
  crypto_result_t result = crypto_init_with_password(&ctx->crypto_ctx, password);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to initialize crypto context with password: %s",
                     crypto_result_to_string(result));
  }

  ctx->state = CRYPTO_HANDSHAKE_INIT;
  ctx->is_server = is_server;
  ctx->crypto_ctx.server_role = is_server;
  ctx->crypto_ctx.server_role_known = true;
  ctx->verify_server_key = false;
  ctx->require_client_auth = false;
  ctx->server_uses_client_auth = false; // Set to true only if authenticated packet received
  ctx->has_password = true;

  // Store password temporarily (will be cleared after key derivation)
  SAFE_STRNCPY(ctx->password, password, sizeof(ctx->password) - 1);

  return ASCIICHAT_OK;
}

// Cleanup crypto handshake context
void crypto_handshake_destroy(crypto_handshake_context_t *ctx) {
  if (!ctx)
    return;

  // Cleanup core crypto context
  crypto_destroy(&ctx->crypto_ctx);

  // Zero out sensitive data
  sodium_memzero(ctx, sizeof(crypto_handshake_context_t));
}

// Check if handshake is complete and encryption is ready
bool crypto_handshake_is_ready(const crypto_handshake_context_t *ctx) {
  if (!ctx)
    return false;
  return ctx->state == CRYPTO_HANDSHAKE_READY && crypto_is_ready(&ctx->crypto_ctx);
}

// Get the crypto context for encryption/decryption
const crypto_context_t *crypto_handshake_get_context(const crypto_handshake_context_t *ctx) {
  if (!ctx || !crypto_handshake_is_ready(ctx))
    return NULL;
  return &ctx->crypto_ctx;
}
