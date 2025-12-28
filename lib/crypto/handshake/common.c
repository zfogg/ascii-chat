/**
 * @file crypto/handshake/common.c
 * @ingroup handshake
 * @brief Common shared cryptographic handshake functions
 */

#include "common.h"
#include "asciichat_errno.h"
#include "buffer_pool.h"
#include "common.h" // lib/common.h for SAFE_* macros
#include "util/endian.h"
#include "util/ip.h"
#include "crypto.h"
#include "crypto/crypto.h"
#include "known_hosts.h"
#include "network/packet.h"
#include "util/password.h"
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <ws2tcpip.h>
#endif

asciichat_error_t crypto_handshake_init(crypto_handshake_context_t *ctx, bool is_server) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
  }

  // Zero out the context
  memset(ctx, 0, sizeof(crypto_handshake_context_t));

  // Initialize core crypto context
  crypto_result_t result = crypto_init(&ctx->crypto_ctx);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to initialize crypto context: %s", crypto_result_to_string(result));
  }

  ctx->state = CRYPTO_HANDSHAKE_INIT;
  ctx->is_server = is_server;
  ctx->verify_server_key = false;
  ctx->require_client_auth = false;
  ctx->server_uses_client_auth = false; // Set to true only if authenticated packet received

  // Load server keys if this is a server
  if (is_server) {
    log_info("Server crypto handshake initialized (ephemeral keys)");
  } else {
    log_info("Client crypto handshake initialized");
  }

  return ASCIICHAT_OK;
}

// Set crypto parameters from crypto_parameters_packet_t
asciichat_error_t crypto_handshake_set_parameters(crypto_handshake_context_t *ctx,
                                                  const crypto_parameters_packet_t *params) {
  if (!ctx || !params) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, params=%p", ctx, params);
  }

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
    // 2. Authenticated format: kex_public_key_size + client_auth_key_size + client_sig_size
    {
      size_t simple_size = ctx->crypto_ctx.public_key_size;
      // For authenticated format, use Ed25519 sizes since client has Ed25519 key
      size_t ed25519_auth_size = ED25519_PUBLIC_KEY_SIZE; // Ed25519 public key is always 32 bytes
      size_t ed25519_sig_size = ED25519_SIGNATURE_SIZE;   // Ed25519 signature is always 64 bytes
      size_t authenticated_size = ctx->crypto_ctx.public_key_size + ed25519_auth_size + ed25519_sig_size;

      if (packet_size != simple_size && packet_size != authenticated_size) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                         "Invalid KEY_EXCHANGE_RESP size: %zu (expected %zu for simple or %zu for authenticated: "
                         "kex=%u + auth=%u + sig=%u)",
                         packet_size, simple_size, authenticated_size, ctx->crypto_ctx.public_key_size,
                         ed25519_auth_size, ed25519_sig_size);
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
    // Client sends: hmac_size + auth_challenge_size bytes client_nonce
    {
      size_t expected_size = ctx->crypto_ctx.hmac_size + ctx->crypto_ctx.auth_challenge_size;
      if (packet_size != expected_size) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                         "Invalid AUTH_RESPONSE size: %zu (expected %zu: hmac=%u + "
                         "nonce=%u)",
                         packet_size, expected_size, ctx->crypto_ctx.hmac_size, ctx->crypto_ctx.auth_challenge_size);
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
asciichat_error_t crypto_handshake_init_with_password(crypto_handshake_context_t *ctx, bool is_server,
                                                      const char *password) {
  if (!ctx || !password) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, password=%p", ctx, password);
  }

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
  ctx->verify_server_key = false;
  ctx->require_client_auth = false;
  ctx->server_uses_client_auth = false; // Set to true only if authenticated packet received
  ctx->has_password = true;

  // Store password temporarily (will be cleared after key derivation)
  SAFE_STRNCPY(ctx->password, password, sizeof(ctx->password) - 1);

  return ASCIICHAT_OK;
}
void crypto_handshake_cleanup(crypto_handshake_context_t *ctx) {
  if (!ctx)
    return;

  // Cleanup core crypto context
  crypto_cleanup(&ctx->crypto_ctx);

  // Zero out sensitive data
  sodium_memzero(ctx, sizeof(crypto_handshake_context_t));
}
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

// Encrypt a packet using the established crypto context
asciichat_error_t crypto_handshake_encrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *plaintext,
                                                  size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                                                  size_t *ciphertext_len) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, ready=%d", ctx, ctx ? crypto_handshake_is_ready(ctx) : 0);
    return ERROR_INVALID_STATE;
  }

  crypto_result_t result = crypto_encrypt((crypto_context_t *)&ctx->crypto_ctx, plaintext, plaintext_len, ciphertext,
                                          ciphertext_size, ciphertext_len);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to encrypt packet: %s", crypto_result_to_string(result));
  }

  return ASCIICHAT_OK;
}

// Decrypt a packet using the established crypto context
asciichat_error_t crypto_handshake_decrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *ciphertext,
                                                  size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                                                  size_t *plaintext_len) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, ready=%d", ctx, ctx ? crypto_handshake_is_ready(ctx) : 0);
    return ERROR_INVALID_STATE;
  }

  crypto_result_t result = crypto_decrypt((crypto_context_t *)&ctx->crypto_ctx, ciphertext, ciphertext_len, plaintext,
                                          plaintext_size, plaintext_len);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to decrypt packet: %s", crypto_result_to_string(result));
  }

  return ASCIICHAT_OK;
}

// Helper: Encrypt with automatic passthrough if crypto not ready
asciichat_error_t crypto_encrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                                       const uint8_t *plaintext, size_t plaintext_len,
                                                       uint8_t *ciphertext, size_t ciphertext_size,
                                                       size_t *ciphertext_len) {
  if (!crypto_ready) {
    // No encryption - just copy data
    if (plaintext_len > ciphertext_size) {
      SET_ERRNO(ERROR_BUFFER, "Plaintext too large for ciphertext buffer: %zu > %zu", plaintext_len, ciphertext_size);
      return ERROR_BUFFER;
    }
    memcpy(ciphertext, plaintext, plaintext_len);
    *ciphertext_len = plaintext_len;
    return ASCIICHAT_OK;
  }

  return crypto_handshake_encrypt_packet(ctx, plaintext, plaintext_len, ciphertext, ciphertext_size, ciphertext_len);
}

// Helper: Decrypt with automatic passthrough if crypto not ready
asciichat_error_t crypto_decrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                                       const uint8_t *ciphertext, size_t ciphertext_len,
                                                       uint8_t *plaintext, size_t plaintext_size,
                                                       size_t *plaintext_len) {
  if (!crypto_ready) {
    // No encryption - just copy data
    if (ciphertext_len > plaintext_size) {
      SET_ERRNO(ERROR_BUFFER, "Ciphertext too large for plaintext buffer: %zu > %zu", ciphertext_len, plaintext_size);
      return ERROR_BUFFER;
    }
    memcpy(plaintext, ciphertext, ciphertext_len);
    *plaintext_len = ciphertext_len;
    return ASCIICHAT_OK;
  }

  return crypto_handshake_decrypt_packet(ctx, ciphertext, ciphertext_len, plaintext, plaintext_size, plaintext_len);
}
// =============================================================================
// Session Rekeying Protocol Implementation
// =============================================================================

/**
 * Send REKEY_REQUEST packet (initiator side).
 * Sends the initiator's new ephemeral public key to the peer.
 */
asciichat_error_t crypto_handshake_rekey_request(crypto_handshake_context_t *ctx, socket_t socket) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Handshake not ready for rekeying: ctx=%p, ready=%d", ctx,
                     ctx ? crypto_handshake_is_ready(ctx) : 0);
  }

  // Initialize rekey process (generates new ephemeral keypair)
  crypto_result_t result = crypto_rekey_init(&ctx->crypto_ctx);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to initialize rekey: %s", crypto_result_to_string(result));
  }

  // Send REKEY_REQUEST with new ephemeral public key (32 bytes)
  log_info("Sending REKEY_REQUEST with new ephemeral X25519 public key (32 bytes)");
  int send_result =
      send_packet(socket, PACKET_TYPE_CRYPTO_REKEY_REQUEST, ctx->crypto_ctx.temp_public_key, CRYPTO_PUBLIC_KEY_SIZE);
  if (send_result != 0) {
    crypto_rekey_abort(&ctx->crypto_ctx); // Clean up temp keys on failure
    return SET_ERRNO(ERROR_NETWORK, "Failed to send REKEY_REQUEST packet");
  }

  log_debug("REKEY_REQUEST sent successfully, awaiting REKEY_RESPONSE");
  return ASCIICHAT_OK;
}

/**
 * Send REKEY_RESPONSE packet (responder side).
 * Sends the responder's new ephemeral public key to the peer.
 */
asciichat_error_t crypto_handshake_rekey_response(crypto_handshake_context_t *ctx, socket_t socket) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Handshake not ready for rekeying: ctx=%p, ready=%d", ctx,
                     ctx ? crypto_handshake_is_ready(ctx) : 0);
  }

  if (!ctx->crypto_ctx.rekey_in_progress || !ctx->crypto_ctx.has_temp_key) {
    return SET_ERRNO(ERROR_INVALID_STATE, "No rekey in progress or temp key missing");
  }

  // Send REKEY_RESPONSE with new ephemeral public key (32 bytes)
  log_info("Sending REKEY_RESPONSE with new ephemeral X25519 public key (32 bytes)");
  int send_result =
      send_packet(socket, PACKET_TYPE_CRYPTO_REKEY_RESPONSE, ctx->crypto_ctx.temp_public_key, CRYPTO_PUBLIC_KEY_SIZE);
  if (send_result != 0) {
    crypto_rekey_abort(&ctx->crypto_ctx); // Clean up temp keys on failure
    return SET_ERRNO(ERROR_NETWORK, "Failed to send REKEY_RESPONSE packet");
  }

  log_debug("REKEY_RESPONSE sent successfully, awaiting REKEY_COMPLETE");
  return ASCIICHAT_OK;
}

/**
 * Send REKEY_COMPLETE packet (initiator side).
 * CRITICAL: This packet is encrypted with the NEW shared secret.
 * It proves that both sides have computed the same shared secret.
 */
asciichat_error_t crypto_handshake_rekey_complete(crypto_handshake_context_t *ctx, socket_t socket) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Handshake not ready for rekeying: ctx=%p, ready=%d", ctx,
                     ctx ? crypto_handshake_is_ready(ctx) : 0);
  }

  if (!ctx->crypto_ctx.rekey_in_progress || !ctx->crypto_ctx.has_temp_key) {
    return SET_ERRNO(ERROR_INVALID_STATE, "No rekey in progress or temp key missing");
  }

  // Encrypt empty payload with NEW key to prove possession
  uint8_t plaintext[1] = {0}; // Minimal payload
  uint8_t ciphertext[256];    // Sufficient for nonce + MAC + minimal payload
  size_t ciphertext_len = 0;

  // Temporarily swap keys to encrypt with NEW key
  uint8_t old_shared_key[CRYPTO_SHARED_KEY_SIZE];
  memcpy(old_shared_key, ctx->crypto_ctx.shared_key, CRYPTO_SHARED_KEY_SIZE);
  memcpy(ctx->crypto_ctx.shared_key, ctx->crypto_ctx.temp_shared_key, CRYPTO_SHARED_KEY_SIZE);

  // Encrypt with NEW key
  crypto_result_t result =
      crypto_encrypt(&ctx->crypto_ctx, plaintext, sizeof(plaintext), ciphertext, sizeof(ciphertext), &ciphertext_len);

  // Restore old key immediately (commit will happen after successful send)
  memcpy(ctx->crypto_ctx.shared_key, old_shared_key, CRYPTO_SHARED_KEY_SIZE);
  sodium_memzero(old_shared_key, sizeof(old_shared_key));

  if (result != CRYPTO_OK) {
    crypto_rekey_abort(&ctx->crypto_ctx);
    return SET_ERRNO(ERROR_CRYPTO, "Failed to encrypt REKEY_COMPLETE: %s", crypto_result_to_string(result));
  }

  // Send encrypted REKEY_COMPLETE
  log_info("Sending REKEY_COMPLETE (encrypted with NEW key, %zu bytes)", ciphertext_len);
  int send_result = send_packet(socket, PACKET_TYPE_CRYPTO_REKEY_COMPLETE, ciphertext, ciphertext_len);
  if (send_result != 0) {
    crypto_rekey_abort(&ctx->crypto_ctx);
    return SET_ERRNO(ERROR_NETWORK, "Failed to send REKEY_COMPLETE packet");
  }

  // Commit to new key (atomic switch)
  result = crypto_rekey_commit(&ctx->crypto_ctx);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to commit rekey: %s", crypto_result_to_string(result));
  }

  log_info("Session rekeying completed successfully (initiator side)");
  return ASCIICHAT_OK;
}

/**
 * Process received REKEY_REQUEST packet (responder side).
 * Extracts peer's new ephemeral public key and computes new shared secret.
 */
asciichat_error_t crypto_handshake_process_rekey_request(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                         size_t packet_len) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Handshake not ready for rekeying: ctx=%p, ready=%d", ctx,
                     ctx ? crypto_handshake_is_ready(ctx) : 0);
  }

  // DDoS PROTECTION: Rate limit rekey requests
  time_t now = time(NULL);
  if (ctx->crypto_ctx.rekey_last_request_time > 0) {
    time_t elapsed = now - ctx->crypto_ctx.rekey_last_request_time;
    if (elapsed < REKEY_MIN_REQUEST_INTERVAL) {
      return SET_ERRNO(ERROR_CRYPTO,
                       "SECURITY: Rekey request rejected - too frequent (%ld sec since last, minimum %d sec required)",
                       (long)elapsed, REKEY_MIN_REQUEST_INTERVAL);
    }
  }

  // Update last request time
  ctx->crypto_ctx.rekey_last_request_time = now;

  // Validate packet size (should be 32 bytes for X25519 public key)
  if (packet_len != CRYPTO_PUBLIC_KEY_SIZE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid REKEY_REQUEST packet size: %zu (expected %d)", packet_len,
                     CRYPTO_PUBLIC_KEY_SIZE);
  }

  log_info("Received REKEY_REQUEST with peer's new ephemeral public key (32 bytes)");

  // Initialize our rekey process (generates our new ephemeral keypair)
  crypto_result_t result = crypto_rekey_init(&ctx->crypto_ctx);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to initialize rekey: %s", crypto_result_to_string(result));
  }

  // Process peer's public key and compute new shared secret
  result = crypto_rekey_process_request(&ctx->crypto_ctx, packet);
  if (result != CRYPTO_OK) {
    crypto_rekey_abort(&ctx->crypto_ctx);
    return SET_ERRNO(ERROR_CRYPTO, "Failed to process REKEY_REQUEST: %s", crypto_result_to_string(result));
  }

  log_debug("REKEY_REQUEST processed successfully, new shared secret computed (responder side)");
  return ASCIICHAT_OK;
}

/**
 * Process received REKEY_RESPONSE packet (initiator side).
 * Extracts peer's new ephemeral public key and computes new shared secret.
 */
asciichat_error_t crypto_handshake_process_rekey_response(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                          size_t packet_len) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Handshake not ready for rekeying: ctx=%p, ready=%d", ctx,
                     ctx ? crypto_handshake_is_ready(ctx) : 0);
  }

  // Validate packet size (should be 32 bytes for X25519 public key)
  if (packet_len != CRYPTO_PUBLIC_KEY_SIZE) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid REKEY_RESPONSE packet size: %zu (expected %d)", packet_len,
                     CRYPTO_PUBLIC_KEY_SIZE);
  }

  if (!ctx->crypto_ctx.rekey_in_progress || !ctx->crypto_ctx.has_temp_key) {
    return SET_ERRNO(ERROR_INVALID_STATE, "No rekey in progress or temp key missing");
  }

  log_info("Received REKEY_RESPONSE with peer's new ephemeral public key (32 bytes)");

  // Process peer's public key and compute new shared secret
  crypto_result_t result = crypto_rekey_process_response(&ctx->crypto_ctx, packet);
  if (result != CRYPTO_OK) {
    crypto_rekey_abort(&ctx->crypto_ctx);
    return SET_ERRNO(ERROR_CRYPTO, "Failed to process REKEY_RESPONSE: %s", crypto_result_to_string(result));
  }

  log_debug("REKEY_RESPONSE processed successfully, new shared secret computed (initiator side)");
  return ASCIICHAT_OK;
}

/**
 * Process received REKEY_COMPLETE packet (responder side).
 * Verifies that the packet decrypts with the new shared secret.
 * If successful, commits to the new key.
 */
asciichat_error_t crypto_handshake_process_rekey_complete(crypto_handshake_context_t *ctx, const uint8_t *packet,
                                                          size_t packet_len) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Handshake not ready for rekeying: ctx=%p, ready=%d", ctx,
                     ctx ? crypto_handshake_is_ready(ctx) : 0);
  }

  if (!ctx->crypto_ctx.rekey_in_progress || !ctx->crypto_ctx.has_temp_key) {
    return SET_ERRNO(ERROR_INVALID_STATE, "No rekey in progress or temp key missing");
  }

  log_info("Received REKEY_COMPLETE packet (%zu bytes), verifying with NEW key", packet_len);

  // Temporarily swap keys to decrypt with NEW key
  uint8_t old_shared_key[CRYPTO_SHARED_KEY_SIZE];
  memcpy(old_shared_key, ctx->crypto_ctx.shared_key, CRYPTO_SHARED_KEY_SIZE);
  memcpy(ctx->crypto_ctx.shared_key, ctx->crypto_ctx.temp_shared_key, CRYPTO_SHARED_KEY_SIZE);

  // Attempt to decrypt with NEW key
  uint8_t plaintext[256];
  size_t plaintext_len = 0;
  crypto_result_t result =
      crypto_decrypt(&ctx->crypto_ctx, packet, packet_len, plaintext, sizeof(plaintext), &plaintext_len);

  // Restore old key immediately
  memcpy(ctx->crypto_ctx.shared_key, old_shared_key, CRYPTO_SHARED_KEY_SIZE);
  sodium_memzero(old_shared_key, sizeof(old_shared_key));

  if (result != CRYPTO_OK) {
    crypto_rekey_abort(&ctx->crypto_ctx);
    return SET_ERRNO(ERROR_CRYPTO, "REKEY_COMPLETE decryption failed (key mismatch): %s",
                     crypto_result_to_string(result));
  }

  log_info("REKEY_COMPLETE verified successfully, committing to new key");

  // Commit to new key (atomic switch)
  result = crypto_rekey_commit(&ctx->crypto_ctx);
  if (result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to commit rekey: %s", crypto_result_to_string(result));
  }

  log_info("Session rekeying completed successfully (responder side)");
  return ASCIICHAT_OK;
}

/**
 * Check if rekeying should be triggered for this handshake context.
 * Wrapper around crypto_should_rekey() for handshake context.
 */
bool crypto_handshake_should_rekey(const crypto_handshake_context_t *ctx) {
  if (!ctx || !crypto_handshake_is_ready(ctx)) {
    return false;
  }
  return crypto_should_rekey(&ctx->crypto_ctx);
}
