/**
 * @file crypto/handshake/server.c
 * @ingroup handshake
 * @brief Server-side handshake protocol implementation
 */

#include <ascii-chat/crypto/handshake/server.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/common.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/gpg/gpg.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <stdio.h>
#include <string.h>

// Server: Start crypto handshake by sending public key
asciichat_error_t crypto_handshake_server_start(crypto_handshake_context_t *ctx, acip_transport_t *transport) {
  log_debug("[HANDSHAKE_START] ===== ENTRY: crypto_handshake_server_start called =====");
  log_debug("[HANDSHAKE_START] ctx=%p, transport=%p", (void *)ctx, (void *)transport);

  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_INIT) {
    log_error("[HANDSHAKE_START] FAILED: Invalid state - ctx=%p, state=%d", (void *)ctx, ctx ? (int)ctx->state : -1);
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", (void *)ctx, ctx ? (int)ctx->state : -1);
  }
  log_debug("[HANDSHAKE_START] State check OK: state=%d (CRYPTO_HANDSHAKE_INIT)", ctx->state);

  int result;

  // Calculate packet size based on negotiated crypto parameters
  size_t expected_packet_size =
      ctx->crypto_ctx.public_key_size + ctx->crypto_ctx.auth_public_key_size + ctx->crypto_ctx.signature_size;

  log_debug("SERVER_KEY_EXCHANGE: kex_size=%u, auth_size=%u, sig_size=%u, expected_size=%zu",
            ctx->crypto_ctx.public_key_size, ctx->crypto_ctx.auth_public_key_size, ctx->crypto_ctx.signature_size,
            expected_packet_size);

  // Check if we have an identity key to send authenticated packet
  if (ctx->server_private_key.type == KEY_TYPE_ED25519) {
    // Extended packet format:
    // [ephemeral_key:kex_size][identity_key:auth_size][signature:sig_size]
    uint8_t *extended_packet;
    extended_packet = SAFE_MALLOC(expected_packet_size, uint8_t *);
    if (!extended_packet) {
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for extended packet");
    }

    // Copy ephemeral public key
    memcpy(extended_packet, ctx->crypto_ctx.public_key, ctx->crypto_ctx.public_key_size);

    // Copy identity public key
    memcpy(extended_packet + ctx->crypto_ctx.public_key_size, ctx->server_private_key.public_key,
           ctx->crypto_ctx.auth_public_key_size);

    // DEBUG: Print identity key being sent
    char hex[HEX_STRING_SIZE_32];
    for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
      safe_snprintf(hex + i * 2, 3, "%02x", ctx->server_private_key.public_key[i]);
    }
    hex[HEX_STRING_SIZE_32 - 1] = '\0';

    // Sign the ephemeral key with our identity key
    log_debug("Signing ephemeral key with server identity key");

    // Log key details in debug mode
    char hex_ephemeral[65], hex_identity[65];
    for (int i = 0; i < 32; i++) {
      safe_snprintf(hex_ephemeral + i * 2, 3, "%02x", ctx->crypto_ctx.public_key[i]);
      safe_snprintf(hex_identity + i * 2, 3, "%02x", ctx->server_private_key.public_key[i]);
    }
    hex_ephemeral[64] = hex_identity[64] = '\0';
    log_debug("SERVER: Ephemeral key (32 bytes): %s", hex_ephemeral);
    log_debug("SERVER: Identity public key: %s", hex_identity);

    if (ed25519_sign_message(&ctx->server_private_key, ctx->crypto_ctx.public_key, ctx->crypto_ctx.public_key_size,
                             extended_packet + ctx->crypto_ctx.public_key_size +
                                 ctx->crypto_ctx.auth_public_key_size) != 0) {
      SAFE_FREE(extended_packet);
      return SET_ERRNO(ERROR_CRYPTO, "Failed to sign ephemeral key with identity key");
    }

    log_debug("Sending authenticated KEY_EXCHANGE_INIT (%zu bytes: ephemeral + "
              "identity + signature)",
              expected_packet_size);
    log_debug("[HANDSHAKE_START] Calling packet_send_via_transport(type=%d, payload_len=%zu, 0)...",
              PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, expected_packet_size);
    result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, extended_packet,
                                       expected_packet_size, 0);
    log_debug("[HANDSHAKE_START] packet_send_via_transport returned %d", result);
    SAFE_FREE(extended_packet);
  } else {
    // No identity key - send just the ephemeral key
    log_debug("Sending simple KEY_EXCHANGE_INIT (%zu bytes: ephemeral key only)", ctx->crypto_ctx.public_key_size);
    log_debug("[HANDSHAKE_START] Calling packet_send_via_transport(type=%d, payload_len=%zu, 0)...",
              PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, ctx->crypto_ctx.public_key_size);
    result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, ctx->crypto_ctx.public_key,
                                       ctx->crypto_ctx.public_key_size, 0);
    log_debug("[HANDSHAKE_START] packet_send_via_transport returned %d", result);
  }

  if (result != ASCIICHAT_OK) {
    log_error("[HANDSHAKE_START] FAILED: packet_send returned %d", result);
    return SET_ERRNO(ERROR_NETWORK, "Failed to send KEY_EXCHANGE_INIT packet");
  }
  log_debug("[HANDSHAKE_START] ===== SUCCESS: KEY_EXCHANGE_INIT sent =====");

  ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;

  return ASCIICHAT_OK;
}
// Server: Process client's public key and send auth challenge
asciichat_error_t crypto_handshake_server_auth_challenge(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                         packet_type_t packet_type, const uint8_t *payload,
                                                         size_t payload_len) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
  }
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "transport is NULL");
  }

  // Note: Packet already received by ACIP handler
  int result;

  // Check if client sent NO_ENCRYPTION response
  if (packet_type == PACKET_TYPE_CRYPTO_NO_ENCRYPTION) {

    // Send AUTH_FAILED to inform client (though they already know)
    auth_failure_packet_t failure = {0};
    failure.reason_flags = 0; // No specific auth failure, just encryption mismatch
    int send_result =
        packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure), 0);
    if (send_result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_FAILED packet");
    }

    return SET_ERRNO(ERROR_CRYPTO, "SECURITY: Client sent NO_ENCRYPTION response - encryption mode "
                                   "mismatch. Server requires encryption, but "
                                   "client has --no-encrypt. Use matching encryption settings on "
                                   "both client and server");
  }

  // Verify packet type
  if (packet_type != PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected KEY_EXCHANGE_RESPONSE, got packet type %d", packet_type);
  }

  // Verify payload size - client can send either simple or authenticated format
  // Simple: kex_public_key_size bytes
  // Authenticated: public_key_size + client_auth_key_size + client_sig_size bytes
  size_t simple_size = ctx->crypto_ctx.public_key_size;
  // Ed25519 public key is always 32 bytes
  // Ed25519 signature is always 64 bytes
  size_t authenticated_size = ctx->crypto_ctx.public_key_size + ED25519_PUBLIC_KEY_SIZE + ED25519_SIGNATURE_SIZE;

  bool client_sent_identity = false;
  uint8_t *client_ephemeral_key = SAFE_MALLOC(ctx->crypto_ctx.public_key_size, uint8_t *);
  uint8_t *client_identity_key = SAFE_MALLOC(ED25519_PUBLIC_KEY_SIZE, uint8_t *);
  uint8_t *client_signature = SAFE_MALLOC(ED25519_SIGNATURE_SIZE, uint8_t *);

  if (!client_ephemeral_key || !client_identity_key || !client_signature) {
    if (client_ephemeral_key)
      SAFE_FREE(client_ephemeral_key);
    if (client_identity_key)
      SAFE_FREE(client_identity_key);
    if (client_signature)
      SAFE_FREE(client_signature);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for client keys");
  }

  // Validate packet size using session parameters
  asciichat_error_t validation_result =
      crypto_handshake_validate_packet_size(ctx, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP, payload_len);
  if (validation_result != ASCIICHAT_OK) {
    // Payload ownership remains with caller
    SAFE_FREE(client_ephemeral_key);
    SAFE_FREE(client_identity_key);
    SAFE_FREE(client_signature);
    return validation_result;
  }

  // Handle both authenticated and non-authenticated responses
  // authenticated_size is now a minimum - packet may be larger with GPG key ID
  if (payload_len >= authenticated_size) {
    // Authenticated format (may have optional GPG key ID):
    // [ephemeral:kex_size][identity:auth_size][signature:sig_size][gpg_key_id_len:1][gpg_key_id:0-16]
    memcpy(client_ephemeral_key, payload, ctx->crypto_ctx.public_key_size);
    memcpy(client_identity_key, payload + ctx->crypto_ctx.public_key_size, ED25519_PUBLIC_KEY_SIZE);
    memcpy(client_signature, payload + ctx->crypto_ctx.public_key_size + ED25519_PUBLIC_KEY_SIZE,
           ED25519_SIGNATURE_SIZE);
    client_sent_identity = true;
    ctx->client_sent_identity = true;

    // Extract GPG key ID if present
    const char *client_gpg_key_id = NULL;
    char gpg_key_id_buffer[41] = {0};
    size_t gpg_offset = ctx->crypto_ctx.public_key_size + ED25519_PUBLIC_KEY_SIZE + ED25519_SIGNATURE_SIZE;
    if (payload_len > gpg_offset) {
      uint8_t gpg_key_id_len = payload[gpg_offset];
      if (gpg_key_id_len > 0 && gpg_key_id_len <= 40 && payload_len >= gpg_offset + 1 + gpg_key_id_len) {
        memcpy(gpg_key_id_buffer, payload + gpg_offset + 1, gpg_key_id_len);
        gpg_key_id_buffer[gpg_key_id_len] = '\0';
        client_gpg_key_id = gpg_key_id_buffer;
        log_debug("Extracted client GPG key ID from KEY_EXCHANGE_RESPONSE: %s", client_gpg_key_id);
      }
    }

    // Check if client sent a null identity key (all zeros)
    bool client_has_null_identity = true;
    for (size_t i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
      if (client_identity_key[i] != 0) {
        client_has_null_identity = false;
        break;
      }
    }

    if (client_has_null_identity) {
      // Client has no identity key - this is allowed for servers without client authentication
      log_debug("Client sent null identity key - no client authentication required");
      client_sent_identity = false;
      ctx->client_sent_identity = false;
      log_warn("Client connected without identity authentication");
    } else {
      // Client has a real identity key
      // If server didn't specify --client-keys, skip signature verification
      // (server doesn't care about client identity verification)
      if (!ctx->require_client_auth) {
        log_info("Skipping client signature verification (no --client-keys specified)");
        log_warn("Connection is encrypted but client identity is NOT verified");
      } else {
        // Verify client's signature
        log_debug("Verifying client's signature");
        // Pass client's GPG key ID if available
        if (ed25519_verify_signature(client_identity_key, client_ephemeral_key, ctx->crypto_ctx.public_key_size,
                                     client_signature, client_gpg_key_id) != 0) {
          // Send AUTH_FAILED with specific reason
          auth_failure_packet_t failure = {0};
          failure.reason_flags = AUTH_FAIL_SIGNATURE_INVALID;
          int send_result =
              packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure), 0);
          if (send_result != 0) {
            return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_FAILED packet");
          }

          return SET_ERRNO(ERROR_CRYPTO, "Client signature verification FAILED - rejecting connection");
        }
      }
    }

    // Store the verified client identity for whitelist checking (only if client has identity)
    if (client_sent_identity) {
      ctx->client_ed25519_key.type = KEY_TYPE_ED25519;
      memcpy(ctx->client_ed25519_key.key, client_identity_key, ctx->crypto_ctx.public_key_size);
    }
  } else if (payload_len == simple_size) {
    // Non-authenticated format: [ephemeral:public_key_size] only
    log_debug("Client sent non-authenticated response (%zu bytes)", payload_len);
    memcpy(client_ephemeral_key, payload, ctx->crypto_ctx.public_key_size);
    client_sent_identity = false;
    ctx->client_sent_identity = false;
    log_warn("Client connected without identity authentication");
  } else {
    // Payload ownership remains with caller
    SAFE_FREE(client_ephemeral_key);
    SAFE_FREE(client_identity_key);
    SAFE_FREE(client_signature);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                     "Invalid client key response size: %zu bytes (expected %zu for "
                     "authenticated or %zu for simple)",
                     payload_len, authenticated_size, simple_size);
  }

  // Extract pointers for compatibility with existing code
  const uint8_t *client_x25519 = client_ephemeral_key;
  const uint8_t *client_ed25519 = client_sent_identity ? client_identity_key : NULL;

  // Check client Ed25519 key against whitelist if client provided one and
  // whitelist is enabled
  if (client_sent_identity && ctx->require_client_auth && ctx->client_whitelist && ctx->num_whitelisted_clients > 0) {
    bool key_found = false;

    // Debug: print client's Ed25519 identity key
    char client_ed25519_hex[HEX_STRING_SIZE_32];
    for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
      safe_snprintf(client_ed25519_hex + i * 2, 3, "%02x", client_ed25519[i]);
    }
    log_debug("Client Ed25519 identity key: %s", client_ed25519_hex);

    // Compare against whitelist (direct Ed25519 comparison - no conversion!)
    for (size_t i = 0; i < ctx->num_whitelisted_clients; i++) {
      // Debug: print whitelist Ed25519 key
      char whitelist_ed25519_hex[HEX_STRING_SIZE_32];
      for (int j = 0; j < ED25519_PUBLIC_KEY_SIZE; j++) {
        safe_snprintf(whitelist_ed25519_hex + j * 2, 3, "%02x", ctx->client_whitelist[i].key[j]);
      }
      log_debug("Whitelist[%zu] Ed25519 key: %s", i, whitelist_ed25519_hex);

      // Direct comparison of Ed25519 keys (constant-time to prevent timing
      // attacks)
      if (sodium_memcmp(client_ed25519, ctx->client_whitelist[i].key, ED25519_PUBLIC_KEY_SIZE) == 0) {
        key_found = true;
        ctx->client_ed25519_key_verified = true;

        // Store the client's Ed25519 key for signature verification
        memcpy(&ctx->client_ed25519_key, &ctx->client_whitelist[i], sizeof(public_key_t));

        log_debug("Client Ed25519 key authorized (whitelist entry %zu)", i);
        if (strlen(ctx->client_whitelist[i].comment) > 0) {
          log_info("Client identity: %s", ctx->client_whitelist[i].comment);
        }
        break;
      }
    }

    if (!key_found) {
      SET_ERRNO(ERROR_CRYPTO_AUTH, "Client Ed25519 key not in whitelist - rejecting connection");
      // Don't send AUTH_FAILED here - wait until server_complete
      // Just mark that the key was rejected
      ctx->client_ed25519_key_verified = false;
    }
  } else if (client_sent_identity) {
    // No whitelist checking - just store the client's Ed25519 key for later
    // (Already stored at line 313-314, but keep this for clarity)
    ctx->client_ed25519_key_verified = false;
  }

  // Set peer's X25519 encryption key - this also derives the shared secret
  crypto_result_t crypto_result = crypto_set_peer_public_key(&ctx->crypto_ctx, client_x25519);
  if (crypto_result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to set peer public key and derive shared secret: %s",
                     crypto_result_to_string(crypto_result));
  }

  // Clean up allocated memory
  SAFE_FREE(client_ephemeral_key);
  SAFE_FREE(client_identity_key);
  SAFE_FREE(client_signature);

  // Do authentication challenge if client provided identity key OR server
  // requires password
  if (client_sent_identity || ctx->crypto_ctx.has_password || ctx->require_client_auth) {
    // Generate nonce and store it in the context
    crypto_result = crypto_generate_nonce(ctx->crypto_ctx.auth_nonce);
    if (crypto_result != CRYPTO_OK) {
      return SET_ERRNO(ERROR_CRYPTO, "Failed to generate nonce: %s", crypto_result_to_string(crypto_result));
    }

    // Prepare AUTH_CHALLENGE packet: 1 byte flags + auth_challenge_size byte nonce
    size_t challenge_packet_size = AUTH_CHALLENGE_FLAGS_SIZE + ctx->crypto_ctx.auth_challenge_size;
    uint8_t challenge_packet[1 + HMAC_SHA256_SIZE]; // Maximum size buffer
    uint8_t auth_flags = 0;

    // Set flags based on server requirements
    if (ctx->crypto_ctx.has_password) {
      auth_flags |= AUTH_REQUIRE_PASSWORD;
    }
    if (ctx->require_client_auth) {
      auth_flags |= AUTH_REQUIRE_CLIENT_KEY;
    }

    challenge_packet[0] = auth_flags;
    memcpy(challenge_packet + 1, ctx->crypto_ctx.auth_nonce, ctx->crypto_ctx.auth_challenge_size);

    // Send AUTH_CHALLENGE with flags + nonce (challenge_packet_size bytes)
    result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_CHALLENGE, challenge_packet,
                                       challenge_packet_size, 0);
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_CHALLENGE packet");
    }

    ctx->state = CRYPTO_HANDSHAKE_AUTHENTICATING;
  } else {
    // No authentication needed - skip to completion
    log_debug("Skipping authentication (no password and client has no identity key)");

    // Send HANDSHAKE_COMPLETE immediately
    result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE, NULL, 0, 0);
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send HANDSHAKE_COMPLETE packet");
    }

    ctx->state = CRYPTO_HANDSHAKE_READY;
    ctx->crypto_ctx.handshake_complete = true; // Mark crypto context as ready for rekeying
    log_debug("Crypto handshake completed successfully (no authentication)");
  }

  return ASCIICHAT_OK;
}
// Server: Process auth response and complete handshake
asciichat_error_t crypto_handshake_server_complete(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                   packet_type_t packet_type, const uint8_t *payload,
                                                   size_t payload_len) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_AUTHENTICATING) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
  }
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "transport is NULL");
  }

  // Note: Packet already received by ACIP handler
  int result;

  // Verify packet type
  if (packet_type != PACKET_TYPE_CRYPTO_AUTH_RESPONSE) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected AUTH_RESPONSE, got packet type %d", packet_type);
  }

  // Verify password if required
  if (ctx->crypto_ctx.has_password) {
    // Validate packet size using session parameters
    asciichat_error_t validation_result =
        crypto_handshake_validate_packet_size(ctx, PACKET_TYPE_CRYPTO_AUTH_RESPONSE, payload_len);
    if (validation_result != ASCIICHAT_OK) {
      return validation_result;
    }

    // Ensure shared secret is derived before password verification
    // This is critical for password HMAC verification which binds to the shared secret
    if (!ctx->crypto_ctx.key_exchange_complete) {
      SET_ERRNO(ERROR_CRYPTO, "Password authentication failed - key exchange not complete");

      // Send AUTH_FAILED with specific reason
      auth_failure_packet_t failure = {0};
      failure.reason_flags = AUTH_FAIL_PASSWORD_INCORRECT;
      if (ctx->require_client_auth) {
        failure.reason_flags |= AUTH_FAIL_CLIENT_KEY_REQUIRED;
      }
      packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure), 0);
      return ERROR_NETWORK;
    }

    // Verify password HMAC (binds to DH shared_secret to prevent MITM)
    log_debug("Verifying password HMAC: has_password=%d, key_exchange_complete=%d", ctx->crypto_ctx.has_password,
              ctx->crypto_ctx.key_exchange_complete);
    if (!payload) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Payload is NULL in password authentication");
      return ERROR_CRYPTO;
    }
    if (!crypto_verify_auth_response(&ctx->crypto_ctx, ctx->crypto_ctx.auth_nonce, payload)) {
      log_debug("Password HMAC verification failed");
      // Enhanced error message when both password and whitelist are required
      if (ctx->require_client_auth) {
        SET_ERRNO(ERROR_CRYPTO,
                  "Password authentication failed - incorrect password (server also requires whitelisted client key)");
      } else {
        SET_ERRNO(ERROR_CRYPTO, "Password authentication failed - incorrect password");
      }

      // Send AUTH_FAILED with specific reason
      auth_failure_packet_t failure = {0};
      failure.reason_flags = AUTH_FAIL_PASSWORD_INCORRECT;
      if (ctx->require_client_auth) {
        failure.reason_flags |= AUTH_FAIL_CLIENT_KEY_REQUIRED;
      }
      packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure), 0);
      return ERROR_NETWORK;
    }

    // Extract client challenge nonce for mutual authentication
    // Use ctx->crypto_ctx.hmac_size and ctx->crypto_ctx.auth_challenge_size (negotiated during handshake)
    memcpy(ctx->client_challenge_nonce, payload + ctx->crypto_ctx.hmac_size, ctx->crypto_ctx.auth_challenge_size);
    log_info("Password authentication successful");
  } else {
    // Ed25519 signature auth (payload format: signature + client_nonce + gpg_key_id_len + gpg_key_id)
    size_t expected_min_signature_size =
        ctx->crypto_ctx.signature_size + ctx->crypto_ctx.auth_challenge_size + 1; // +1 for gpg_key_id_len byte
    size_t expected_password_size = ctx->crypto_ctx.hmac_size + ctx->crypto_ctx.auth_challenge_size;
    if (!payload) {
      SET_ERRNO(ERROR_INVALID_PARAM, "Payload is NULL in signature authentication");
      return ERROR_CRYPTO;
    }
    if (payload_len >= expected_min_signature_size) {
      // Signature + client nonce + optional GPG key ID - VERIFY the signature on the challenge nonce
      const uint8_t *signature = payload;
      const uint8_t *client_nonce = payload + ctx->crypto_ctx.signature_size;

      // Extract GPG key ID if present
      size_t gpg_offset = ctx->crypto_ctx.signature_size + ctx->crypto_ctx.auth_challenge_size;
      uint8_t gpg_key_id_len = payload[gpg_offset];
      const char *client_gpg_key_id = NULL;
      char gpg_key_id_buffer[41] = {0};

      if (gpg_key_id_len > 0 && gpg_key_id_len <= 40) {
        // Validate packet has enough bytes for GPG key ID
        if (payload_len >= gpg_offset + 1 + gpg_key_id_len) {
          memcpy(gpg_key_id_buffer, payload + gpg_offset + 1, gpg_key_id_len);
          gpg_key_id_buffer[gpg_key_id_len] = '\0';
          client_gpg_key_id = gpg_key_id_buffer;
          log_debug("Extracted client GPG key ID from AUTH_RESPONSE: %s", client_gpg_key_id);
        }
      }

      // Actually verify the Ed25519/GPG signature on the challenge nonce
      // This was missing, allowing authentication bypass
      if (ctx->client_ed25519_key_verified) {
        // Use unified verification function that handles both Ed25519 and GPG keys
        // This matches the KEY_EXCHANGE verification path
        log_debug("Verifying %s signature on challenge nonce",
                  ctx->client_ed25519_key.type == KEY_TYPE_GPG ? "GPG" : "Ed25519");

        asciichat_error_t verify_result = ed25519_verify_signature(
            ctx->client_ed25519_key.key, ctx->crypto_ctx.auth_nonce, ctx->crypto_ctx.auth_challenge_size, signature,
            client_gpg_key_id // GPG key ID for fallback verification
        );

        if (verify_result != ASCIICHAT_OK) {
          auth_failure_packet_t failure = {0};
          failure.reason_flags = AUTH_FAIL_CLIENT_KEY_REJECTED;
          SET_ERRNO(ERROR_CRYPTO_AUTH, "%s signature verification failed on challenge nonce",
                    ctx->client_ed25519_key.type == KEY_TYPE_GPG ? "GPG" : "Ed25519");
          packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure), 0);
          return ERROR_CRYPTO_AUTH;
        }
        log_debug("%s signature on challenge nonce verified successfully",
                  ctx->client_ed25519_key.type == KEY_TYPE_GPG ? "GPG" : "Ed25519");
      }

      memcpy(ctx->client_challenge_nonce, client_nonce, ctx->crypto_ctx.auth_challenge_size);
    } else if (payload_len == expected_password_size) {
      // Just client nonce (legacy or password-only mode without password enabled)
      // Use ctx->crypto_ctx.hmac_size and ctx->crypto_ctx.auth_challenge_size (negotiated during handshake)
      memcpy(ctx->client_challenge_nonce, payload + ctx->crypto_ctx.hmac_size, ctx->crypto_ctx.auth_challenge_size);
    } else {
      // Validate packet size using session parameters
      asciichat_error_t validation_result =
          crypto_handshake_validate_packet_size(ctx, PACKET_TYPE_CRYPTO_AUTH_RESPONSE, payload_len);
      if (validation_result != ASCIICHAT_OK) {
        return validation_result;
      }
    }
  }

  // Verify client key if required (whitelist)
  if (ctx->require_client_auth) {
    if (!ctx->client_ed25519_key_verified) {
      // Send AUTH_FAILED with specific reason
      auth_failure_packet_t failure = {0};

      // Check if client provided a key but it wasn't in whitelist
      if (ctx->client_sent_identity) {
        SET_ERRNO(ERROR_CRYPTO_AUTH, "Client key authentication failed - your key is not in the server's whitelist");
        failure.reason_flags = AUTH_FAIL_CLIENT_KEY_REJECTED;
      } else {
        SET_ERRNO(ERROR_CRYPTO_AUTH, "Client key authentication failed - client did not provide a key");
        failure.reason_flags = AUTH_FAIL_CLIENT_KEY_REQUIRED;
      }

      if (ctx->crypto_ctx.has_password) {
        // Password was verified, but key was not
        SET_ERRNO(ERROR_CRYPTO_AUTH, "Note: Password was correct, but client key is required");
      }
      packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure), 0);
      return ERROR_NETWORK;
    }
    log_info("Client key authentication successful (whitelist verified)");
    if (strlen(ctx->client_ed25519_key.comment) > 0) {
      log_info("Authenticated client: %s", ctx->client_ed25519_key.comment);
    }
  }

  // Send SERVER_AUTH_RESPONSE with server's HMAC for mutual authentication
  // Bind to DH shared_secret to prevent MITM (even if attacker knows password)
  // Allocate buffer using negotiated hmac_size (should match AUTH_HMAC_SIZE)
  uint8_t server_hmac[HMAC_SHA256_SIZE]; // Maximum size, actual size is ctx->hmac_size
  crypto_result_t crypto_result =
      crypto_compute_auth_response(&ctx->crypto_ctx, ctx->client_challenge_nonce, server_hmac);
  if (crypto_result != CRYPTO_OK) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to compute server HMAC for mutual authentication: %s",
              crypto_result_to_string(crypto_result));
    return ERROR_NETWORK;
  }

  log_debug("Sending SERVER_AUTH_RESPONSE packet with server HMAC (%u bytes) "
            "for mutual authentication",
            ctx->crypto_ctx.hmac_size);
  result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP, server_hmac,
                                     ctx->crypto_ctx.hmac_size, 0);
  if (result != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_NETWORK, "Failed to send SERVER_AUTH_RESPONSE packet");
    return ERROR_NETWORK;
  }

  ctx->state = CRYPTO_HANDSHAKE_READY;
  log_debug("Crypto handshake completed successfully (mutual authentication)");

  return ASCIICHAT_OK;
}

// =============================================================================
// Legacy TCP Socket Wrappers (backward compatibility)
// =============================================================================
// These wrappers maintain the old socket-based interface for TCP clients
// that do handshake BEFORE creating ACIP transport. Will be removed in Phase 5.

/**
 * @brief Legacy wrapper: Start handshake using socket (TCP clients only)
 */
asciichat_error_t crypto_handshake_server_start_socket(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Context cannot be NULL");
  }

  // Create temporary TCP transport for handshake
  acip_transport_t *temp_transport = acip_tcp_transport_create("crypto_handshake_temp_socket", client_socket, NULL);
  if (!temp_transport) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to create temporary transport");
  }

  asciichat_error_t result = crypto_handshake_server_start(ctx, temp_transport);

  // Destroy temporary transport (socket remains open)
  acip_transport_destroy(temp_transport);

  return result;
}

/**
 * @brief Legacy wrapper: Auth challenge using socket (TCP clients only)
 */
asciichat_error_t crypto_handshake_server_auth_challenge_socket(crypto_handshake_context_t *ctx,
                                                                socket_t client_socket) {
  // Receive packet using old method
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive KEY_EXCHANGE_RESPONSE packet");
  }

  // Create temporary TCP transport
  acip_transport_t *temp_transport = acip_tcp_transport_create("crypto_handshake_temp_socket", client_socket, NULL);
  if (!temp_transport) {
    // Payload will be freed below since we own it from receive_packet()
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK, "Failed to create temporary transport");
  }

  // Call new function
  asciichat_error_t handshake_result =
      crypto_handshake_server_auth_challenge(ctx, temp_transport, packet_type, payload, payload_len);

  // Free payload (handshake function makes copies of what it needs)
  // We own the payload from receive_packet(), so we must free it
  if (payload) {
    buffer_pool_free(NULL, payload, payload_len);
  }

  // Destroy temporary transport
  acip_transport_destroy(temp_transport);

  return handshake_result;
}

/**
 * @brief Legacy wrapper: Complete handshake using socket (TCP clients only)
 */
asciichat_error_t crypto_handshake_server_complete_socket(crypto_handshake_context_t *ctx, socket_t client_socket) {
  // Receive packet using old method
  packet_type_t packet_type = 0;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive AUTH_RESPONSE packet");
  }

  // Create temporary TCP transport
  acip_transport_t *temp_transport = acip_tcp_transport_create("crypto_handshake_temp_socket", client_socket, NULL);
  if (!temp_transport) {
    // Payload will be freed below since we own it from receive_packet()
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK, "Failed to create temporary transport");
  }

  // Call new function
  asciichat_error_t handshake_result =
      crypto_handshake_server_complete(ctx, temp_transport, packet_type, payload, payload_len);

  // Free payload (handshake function makes copies of what it needs)
  // We own the payload from receive_packet(), so we must free it
  if (payload) {
    buffer_pool_free(NULL, payload, payload_len);
  }

  // Destroy temporary transport
  acip_transport_destroy(temp_transport);

  return handshake_result;
}
