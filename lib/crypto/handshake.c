#include "handshake.h"
#include "common.h"
#include "network.h"
#include "buffer_pool.h"
#include "platform/password.h"
#include "known_hosts.h"
#include "../../src/client/server.h" // For connection_error_t enum
#include <string.h>
#include <stdio.h>

// Initialize crypto handshake context
int crypto_handshake_init(crypto_handshake_context_t *ctx, bool is_server) {
  if (!ctx)
    return -1;

  // Zero out the context
  memset(ctx, 0, sizeof(crypto_handshake_context_t));

  // Initialize core crypto context
  crypto_result_t result = crypto_init(&ctx->crypto_ctx);
  if (result != CRYPTO_OK) {
    log_error("Failed to initialize crypto context: %s", crypto_result_to_string(result));
    return -1;
  }

  ctx->state = CRYPTO_HANDSHAKE_INIT;
  ctx->is_server = is_server;
  ctx->verify_server_key = false;
  ctx->require_client_auth = false;
  ctx->server_uses_client_auth = false; // Set to true only if authenticated packet received

  // Load server keys if this is a server
  if (is_server) {
    // TODO: Load server private key from --ssh-key option
    // For now, generate ephemeral keys
    log_info("Server crypto handshake initialized (ephemeral keys)");
  } else {
    // TODO: Load expected server key from --server-key option
    log_info("Client crypto handshake initialized");
  }

  return 0;
}

// Initialize crypto handshake context with password authentication
int crypto_handshake_init_with_password(crypto_handshake_context_t *ctx, bool is_server, const char *password) {
  if (!ctx || !password)
    return -1;

  // Zero out the context
  memset(ctx, 0, sizeof(crypto_handshake_context_t));

  // Initialize core crypto context with password
  crypto_result_t result = crypto_init_with_password(&ctx->crypto_ctx, password);
  if (result != CRYPTO_OK) {
    log_error("Failed to initialize crypto context with password: %s", crypto_result_to_string(result));
    return -1;
  }

  ctx->state = CRYPTO_HANDSHAKE_INIT;
  ctx->is_server = is_server;
  ctx->verify_server_key = false;
  ctx->require_client_auth = false;
  ctx->server_uses_client_auth = false; // Set to true only if authenticated packet received
  ctx->has_password = true;

  // Store password temporarily (will be cleared after key derivation)
  SAFE_STRNCPY(ctx->password, password, sizeof(ctx->password) - 1);

  // Load server keys if this is a server
  if (is_server) {
    // TODO: Load server private key from --ssh-key option
    // For now, generate ephemeral keys
    log_info("Server crypto handshake initialized with password authentication (ephemeral keys)");
  } else {
    // TODO: Load expected server key from --server-key option
    log_info("Client crypto handshake initialized with password authentication");
  }

  return 0;
}

// Cleanup crypto handshake context
void crypto_handshake_cleanup(crypto_handshake_context_t *ctx) {
  if (!ctx)
    return;

  // Cleanup core crypto context
  crypto_cleanup(&ctx->crypto_ctx);

  // Zero out sensitive data
  sodium_memzero(ctx, sizeof(crypto_handshake_context_t));
}

// Server: Start crypto handshake by sending public key
int crypto_handshake_server_start(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_INIT)
    return -1;

  int result;

  // Check if we have an identity key to send authenticated packet
  if (ctx->server_private_key.type == KEY_TYPE_ED25519) {
    // Extended packet format: [ephemeral_key:32][identity_key:32][signature:64]
    uint8_t extended_packet[128];

    // Copy ephemeral X25519 public key
    memcpy(extended_packet, ctx->crypto_ctx.public_key, 32);

    // Copy Ed25519 identity public key
    memcpy(extended_packet + 32, ctx->server_private_key.public_key, 32);

    // Sign the ephemeral key with our identity key
    log_debug("Signing ephemeral key with server identity key");
    if (ed25519_sign_message(&ctx->server_private_key, ctx->crypto_ctx.public_key, 32, extended_packet + 64) != 0) {
      log_error("Failed to sign ephemeral key with identity key");
      return -1;
    }

    log_info("Sending authenticated KEY_EXCHANGE_INIT (128 bytes: ephemeral + identity + signature)");
    result = send_packet(client_socket, PACKET_TYPE_KEY_EXCHANGE_INIT, extended_packet, 128);
  } else {
    // Legacy format: just ephemeral key (backward compatible)
    log_debug("Sending KEY_EXCHANGE_INIT packet with public key only (%d bytes)", CRYPTO_PUBLIC_KEY_SIZE);
    result =
        send_packet(client_socket, PACKET_TYPE_KEY_EXCHANGE_INIT, ctx->crypto_ctx.public_key, CRYPTO_PUBLIC_KEY_SIZE);
  }

  if (result != 0) {
    log_error("Failed to send KEY_EXCHANGE_INIT packet");
    return -1;
  }

  ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;
  log_debug("Server sent public key to client");

  return 0;
}

// Client: Process server's public key and send our public key
int crypto_handshake_client_key_exchange(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_INIT)
    return -1;

  // Receive server's KEY_EXCHANGE_INIT packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != 1) {
    log_error("Failed to receive KEY_EXCHANGE_INIT packet");
    return -1;
  }

  // Verify packet type
  if (packet_type != PACKET_TYPE_KEY_EXCHANGE_INIT) {
    log_error("Expected KEY_EXCHANGE_INIT, got packet type %d", packet_type);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Check payload size - support both legacy (32 bytes) and authenticated (128 bytes) formats
  uint8_t server_ephemeral_key[32];
  uint8_t server_identity_key[32];
  uint8_t server_signature[64];

  if (payload_len == 32) {
    // Legacy format: just ephemeral key (no client authentication)
    log_debug("Received legacy KEY_EXCHANGE_INIT (32 bytes, no authentication)");
    log_warn("Server is not using client verification keys - connection is encrypted but server does not verify client "
             "identity");

    memcpy(server_ephemeral_key, payload, 32);
    // ctx->server_uses_client_auth remains false (default)
  } else if (payload_len == 128) {
    // Authenticated format: [ephemeral:32][identity:32][signature:64]
    log_info("Received authenticated KEY_EXCHANGE_INIT (128 bytes)");
    memcpy(server_ephemeral_key, payload, 32);
    memcpy(server_identity_key, payload + 32, 32);
    memcpy(server_signature, payload + 64, 64);

    // Server is using client authentication
    ctx->server_uses_client_auth = true;

    // Verify signature: server identity signed the ephemeral key
    log_debug("Verifying server's signature over ephemeral key");
    if (ed25519_verify_signature(server_identity_key, server_ephemeral_key, 32, server_signature) != 0) {
      log_error("Server signature verification FAILED - rejecting connection");
      buffer_pool_free(payload, payload_len);
      return -1;
    }
    log_info("Server signature verified successfully");

    // Verify server identity against expected key if --server-key is specified
    if (ctx->verify_server_key && strlen(ctx->expected_server_key) > 0) {
      // Parse the expected server key
      public_key_t expected_key;
      if (parse_public_key(ctx->expected_server_key, &expected_key) != 0) {
        log_error("Failed to parse expected server key: %s", ctx->expected_server_key);
        buffer_pool_free(payload, payload_len);
        return -1;
      }

      // Compare server's IDENTITY key with expected key (constant-time to prevent timing attacks)
      if (sodium_memcmp(server_identity_key, expected_key.key, 32) != 0) {
        log_error("Server identity key mismatch - potential MITM attack!");
        log_error("Expected key: %s", ctx->expected_server_key);
        buffer_pool_free(payload, payload_len);
        return -1;
      }
      log_info("Server identity key verified against --server-key");
    }

    // Check known_hosts for this server (if we have server IP and port)
    if (ctx->server_ip[0] != '\0' && ctx->server_port > 0) {
      int known_host_result = check_known_host(ctx->server_ip, ctx->server_port, server_identity_key);

      if (known_host_result == -1) {
        // Key mismatch - MITM warning! Return CONNECTION_ERROR_HOST_KEY_FAILED to exit immediately
        uint8_t stored_key[32] = {0}; // We don't have the stored key easily accessible, use zeros for now
        display_mitm_warning(ctx->server_ip, ctx->server_port, stored_key, server_identity_key);
        buffer_pool_free(payload, payload_len);
        return CONNECTION_ERROR_HOST_KEY_FAILED;
      } else if (known_host_result == 0) {
        // Unknown host - prompt user
        if (!prompt_unknown_host(ctx->server_ip, ctx->server_port, server_identity_key)) {
          // User declined to add host - return CONNECTION_ERROR_HOST_KEY_FAILED to exit immediately
          buffer_pool_free(payload, payload_len);
          return CONNECTION_ERROR_HOST_KEY_FAILED;
        }

        // User accepted - add to known_hosts
        if (add_known_host(ctx->server_ip, ctx->server_port, server_identity_key) != 0) {
          log_warn("Failed to add host to known_hosts file (continuing anyway)");
        }
      } else {
        // Key matches - all good!
        log_info("Server host key verified from known_hosts");
      }
    }
  } else {
    log_error("Invalid KEY_EXCHANGE_INIT size: %zu bytes (expected 32 or 128)", payload_len);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Set peer's public key (EPHEMERAL X25519) - this also derives the shared secret
  crypto_result_t crypto_result = crypto_set_peer_public_key(&ctx->crypto_ctx, server_ephemeral_key);
  buffer_pool_free(payload, payload_len);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to set peer public key and derive shared secret: %s", crypto_result_to_string(crypto_result));
    return -1;
  }

  // Determine if client has an identity key
  bool client_has_identity_key = (ctx->client_private_key.type == KEY_TYPE_ED25519);

  if (client_has_identity_key) {
    // Send authenticated packet: [ephemeral:32][identity:32][signature:64] = 128 bytes
    uint8_t key_response[128];
    memcpy(key_response, ctx->crypto_ctx.public_key, 32);              // X25519 ephemeral for encryption
    memcpy(key_response + 32, ctx->client_private_key.public_key, 32); // Ed25519 identity

    // Sign ephemeral key with client identity key
    log_debug("Signing client ephemeral key with identity key");
    if (ed25519_sign_message(&ctx->client_private_key, ctx->crypto_ctx.public_key, 32, key_response + 64) != 0) {
      log_error("Failed to sign client ephemeral key");
      return -1;
    }

    log_info("Sending authenticated KEY_EXCHANGE_RESPONSE (128 bytes: ephemeral + identity + signature)");
    result = send_packet(client_socket, PACKET_TYPE_KEY_EXCHANGE_RESPONSE, key_response, sizeof(key_response));
    if (result != 0) {
      log_error("Failed to send KEY_EXCHANGE_RESPONSE packet");
      return -1;
    }

    // Zero out the buffer
    sodium_memzero(key_response, sizeof(key_response));
    log_debug("Client sent authenticated response to server");
  } else {
    // Send X25519 encryption key only to server (no identity key)
    // Format: [X25519 pubkey (32)] = 32 bytes total (backward compatible)
    log_debug("Sending KEY_EXCHANGE_RESPONSE packet with X25519 key only (32 bytes)");
    result = send_packet(client_socket, PACKET_TYPE_KEY_EXCHANGE_RESPONSE, ctx->crypto_ctx.public_key, 32);
    if (result != 0) {
      log_error("Failed to send KEY_EXCHANGE_RESPONSE packet");
      return -1;
    }
    log_debug("Client sent X25519 encryption key only to server (no identity authentication)");
  }

  ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;

  return 0;
}

// Server: Process client's public key and send auth challenge
int crypto_handshake_server_auth_challenge(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE)
    return -1;

  // Receive client's KEY_EXCHANGE_RESPONSE packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != 1) {
    log_error("Failed to receive KEY_EXCHANGE_RESPONSE packet");
    return -1;
  }

  // Check if client sent NO_ENCRYPTION response
  if (packet_type == PACKET_TYPE_NO_ENCRYPTION) {
    log_error("SECURITY: Client sent NO_ENCRYPTION response - encryption mode mismatch");
    log_error("Server requires encryption, but client has --no-encrypt");
    log_error("Use matching encryption settings on both client and server");
    buffer_pool_free(payload, payload_len);

    // Send AUTH_FAILED to inform client (though they already know)
    auth_failure_packet_t failure = {0};
    failure.reason_flags = 0; // No specific auth failure, just encryption mismatch
    send_packet(client_socket, PACKET_TYPE_AUTH_FAILED, &failure, sizeof(failure));
    return -1;
  }

  // Verify packet type
  if (packet_type != PACKET_TYPE_KEY_EXCHANGE_RESPONSE) {
    log_error("Expected KEY_EXCHANGE_RESPONSE, got packet type %d", packet_type);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Verify payload size - can be:
  // - 32 bytes: X25519 only (client without key, no authentication)
  // - 128 bytes: X25519 + Ed25519 + signature (client with authenticated key)
  bool client_sent_identity = false;
  uint8_t client_ephemeral_key[32];
  uint8_t client_identity_key[32];
  uint8_t client_signature[64];

  if (payload_len == 32) {
    log_debug("Client sent X25519 key only (32 bytes) - no identity authentication");
    memcpy(client_ephemeral_key, payload, 32);
    client_sent_identity = false;
  } else if (payload_len == 128) {
    // Authenticated format: [ephemeral:32][identity:32][signature:64]
    log_info("Client sent authenticated response (128 bytes)");
    memcpy(client_ephemeral_key, payload, 32);
    memcpy(client_identity_key, payload + 32, 32);
    memcpy(client_signature, payload + 64, 64);
    client_sent_identity = true;

    // Verify signature: client identity signed the ephemeral key
    log_debug("Verifying client's signature over ephemeral key");
    if (ed25519_verify_signature(client_identity_key, client_ephemeral_key, 32, client_signature) != 0) {
      log_error("Client signature verification FAILED - rejecting connection");
      buffer_pool_free(payload, payload_len);

      // Send AUTH_FAILED with specific reason
      auth_failure_packet_t failure = {0};
      failure.reason_flags = AUTH_FAIL_SIGNATURE_INVALID;
      send_packet(client_socket, PACKET_TYPE_AUTH_FAILED, &failure, sizeof(failure));
      return -1;
    }
    log_info("Client signature verified successfully");

    // Store the verified client identity for whitelist checking
    ctx->client_ed25519_key.type = KEY_TYPE_ED25519;
    memcpy(ctx->client_ed25519_key.key, client_identity_key, 32);
  } else {
    log_error("Invalid client key response size: %zu bytes (expected 32 or 128)", payload_len);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Extract pointers for compatibility with existing code
  const uint8_t *client_x25519 = client_ephemeral_key;
  const uint8_t *client_ed25519 = client_sent_identity ? client_identity_key : NULL;

  // Check client Ed25519 key against whitelist if client provided one and whitelist is enabled
  if (client_sent_identity && ctx->require_client_auth && ctx->client_whitelist && ctx->num_whitelisted_clients > 0) {
    bool key_found = false;

    // Debug: print client's Ed25519 identity key
    char client_ed25519_hex[65];
    for (int i = 0; i < 32; i++) {
      snprintf(client_ed25519_hex + i * 2, 3, "%02x", client_ed25519[i]);
    }
    log_debug("Client Ed25519 identity key: %s", client_ed25519_hex);

    // Compare against whitelist (direct Ed25519 comparison - no conversion!)
    for (size_t i = 0; i < ctx->num_whitelisted_clients; i++) {
      // Debug: print whitelist Ed25519 key
      char whitelist_ed25519_hex[65];
      for (int j = 0; j < 32; j++) {
        snprintf(whitelist_ed25519_hex + j * 2, 3, "%02x", ctx->client_whitelist[i].key[j]);
      }
      log_debug("Whitelist[%zu] Ed25519 key: %s", i, whitelist_ed25519_hex);

      // Direct comparison of Ed25519 keys (constant-time to prevent timing attacks)
      if (sodium_memcmp(client_ed25519, ctx->client_whitelist[i].key, 32) == 0) {
        key_found = true;
        ctx->client_ed25519_key_verified = true;

        // Store the client's Ed25519 key for signature verification
        memcpy(&ctx->client_ed25519_key, &ctx->client_whitelist[i], sizeof(public_key_t));

        log_info("Client Ed25519 key authorized (whitelist entry %zu)", i);
        if (strlen(ctx->client_whitelist[i].comment) > 0) {
          log_info("Client identity: %s", ctx->client_whitelist[i].comment);
        }
        break;
      }
    }

    if (!key_found) {
      log_error("Client Ed25519 key not in whitelist - rejecting connection");
      buffer_pool_free(payload, payload_len);
      // Send AUTH_FAILED packet
      send_packet(client_socket, PACKET_TYPE_AUTH_FAILED, NULL, 0);
      return -1;
    }
  } else if (client_sent_identity) {
    // No whitelist checking - just store the client's Ed25519 key for later
    // (Already stored at line 313-314, but keep this for clarity)
    ctx->client_ed25519_key_verified = false;
  }

  // Set peer's X25519 encryption key - this also derives the shared secret
  crypto_result_t crypto_result = crypto_set_peer_public_key(&ctx->crypto_ctx, client_x25519);
  buffer_pool_free(payload, payload_len);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to set peer public key and derive shared secret: %s", crypto_result_to_string(crypto_result));
    return -1;
  }

  if (client_sent_identity) {
    log_debug("Client keys processed: X25519 for encryption, Ed25519 for identity");
  } else {
    log_debug("Client key processed: X25519 for encryption only (no identity authentication)");
  }

  // Do authentication challenge if client provided identity key OR server requires password
  if (client_sent_identity || ctx->crypto_ctx.has_password || ctx->require_client_auth) {
    // Generate nonce and store it in the context
    crypto_result = crypto_generate_nonce(ctx->crypto_ctx.auth_nonce);
    if (crypto_result != CRYPTO_OK) {
      log_error("Failed to generate nonce: %s", crypto_result_to_string(crypto_result));
      return -1;
    }

    // Prepare AUTH_CHALLENGE packet: 1 byte flags + 32 byte nonce
    uint8_t challenge_packet[33];
    uint8_t auth_flags = 0;

    // Set flags based on server requirements
    if (ctx->crypto_ctx.has_password) {
      auth_flags |= AUTH_REQUIRE_PASSWORD;
    }
    if (ctx->require_client_auth) {
      auth_flags |= AUTH_REQUIRE_CLIENT_KEY;
    }

    challenge_packet[0] = auth_flags;
    memcpy(challenge_packet + 1, ctx->crypto_ctx.auth_nonce, 32);

    // Send AUTH_CHALLENGE with flags + nonce (33 bytes)
    log_debug("Sending AUTH_CHALLENGE packet with flags=0x%02x and nonce (33 bytes)", auth_flags);
    result = send_packet(client_socket, PACKET_TYPE_AUTH_CHALLENGE, challenge_packet, 33);
    if (result != 0) {
      log_error("Failed to send AUTH_CHALLENGE packet");
      return -1;
    }

    ctx->state = CRYPTO_HANDSHAKE_AUTHENTICATING;
    log_debug("Server sent auth challenge to client (password: %s, whitelist: %s)",
              (auth_flags & AUTH_REQUIRE_PASSWORD) ? "required" : "no",
              (auth_flags & AUTH_REQUIRE_CLIENT_KEY) ? "required" : "no");
  } else {
    // No authentication needed - skip to completion
    log_debug("Skipping authentication (no password and client has no identity key)");

    // Send HANDSHAKE_COMPLETE immediately
    result = send_packet(client_socket, PACKET_TYPE_HANDSHAKE_COMPLETE, NULL, 0);
    if (result != 0) {
      log_error("Failed to send HANDSHAKE_COMPLETE packet");
      return -1;
    }

    ctx->state = CRYPTO_HANDSHAKE_READY;
    log_info("Crypto handshake completed successfully (no authentication)");
  }

  return 0;
}

// Helper: Send password-based authentication response with mutual auth
static int send_password_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket, const uint8_t nonce[32],
                                       const char *auth_context) {
  // Compute HMAC bound to shared_secret (MITM protection)
  uint8_t hmac_response[32];
  crypto_result_t crypto_result = crypto_compute_auth_response(&ctx->crypto_ctx, nonce, hmac_response);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to compute HMAC response: %s", crypto_result_to_string(crypto_result));
    return -1;
  }

  // Generate client challenge nonce for mutual authentication
  crypto_result = crypto_generate_nonce(ctx->client_challenge_nonce);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to generate client challenge nonce: %s", crypto_result_to_string(crypto_result));
    return -1;
  }

  // Combine HMAC + client nonce (32 + 32 = 64 bytes)
  uint8_t auth_packet[64];
  memcpy(auth_packet, hmac_response, 32);
  memcpy(auth_packet + 32, ctx->client_challenge_nonce, 32);

  log_debug("Sending AUTH_RESPONSE packet with HMAC + client nonce (64 bytes) - %s", auth_context);
  int result = send_packet(client_socket, PACKET_TYPE_AUTH_RESPONSE, auth_packet, sizeof(auth_packet));
  if (result != 0) {
    log_error("Failed to send AUTH_RESPONSE packet");
    return -1;
  }

  return 0;
}

// Helper: Send Ed25519 signature-based authentication response with mutual auth
static int send_key_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket, const uint8_t nonce[32],
                                  const char *auth_context) {
  // Sign the challenge with our Ed25519 private key
  uint8_t signature[64];
  int sign_result = ed25519_sign_message(&ctx->client_private_key, nonce, 32, signature);
  if (sign_result != 0) {
    log_error("Failed to sign challenge with Ed25519 key");
    return -1;
  }

  // Generate client challenge nonce for mutual authentication
  crypto_result_t crypto_result = crypto_generate_nonce(ctx->client_challenge_nonce);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to generate client challenge nonce: %s", crypto_result_to_string(crypto_result));
    sodium_memzero(signature, sizeof(signature));
    return -1;
  }

  // Combine signature + client nonce (64 + 32 = 96 bytes)
  uint8_t auth_packet[96];
  memcpy(auth_packet, signature, 64);
  memcpy(auth_packet + 64, ctx->client_challenge_nonce, 32);
  sodium_memzero(signature, sizeof(signature));

  log_debug("Sending AUTH_RESPONSE packet with Ed25519 signature + client nonce (96 bytes) - %s", auth_context);
  int result = send_packet(client_socket, PACKET_TYPE_AUTH_RESPONSE, auth_packet, sizeof(auth_packet));
  if (result != 0) {
    log_error("Failed to send AUTH_RESPONSE packet");
    return -1;
  }

  return 0;
}

// Client: Process auth challenge and send response
int crypto_handshake_client_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE)
    return -1;

  // Receive AUTH_CHALLENGE or HANDSHAKE_COMPLETE packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != 1) {
    log_error("Failed to receive packet from server");
    return -1;
  }

  // If server sent HANDSHAKE_COMPLETE, authentication was skipped (client has no key)
  if (packet_type == PACKET_TYPE_HANDSHAKE_COMPLETE) {
    buffer_pool_free(payload, payload_len);
    ctx->state = CRYPTO_HANDSHAKE_READY;
    log_info("Crypto handshake completed successfully (no authentication required)");
    return 0;
  }

  // If server sent AUTH_FAILED, client is not authorized
  if (packet_type == PACKET_TYPE_AUTH_FAILED) {
    log_error("Server rejected authentication - client key not authorized");
    buffer_pool_free(payload, payload_len);
    return -2; // Authentication failure - do not retry
  }

  // Otherwise, verify packet type is AUTH_CHALLENGE
  if (packet_type != PACKET_TYPE_AUTH_CHALLENGE) {
    log_error("Expected AUTH_CHALLENGE, HANDSHAKE_COMPLETE, or AUTH_FAILED, got packet type %d", packet_type);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Verify challenge packet size (1 byte flags + 32 byte nonce)
  if (payload_len != 33) {
    log_error("Invalid AUTH_CHALLENGE size: %zu bytes (expected 33)", payload_len);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Parse auth requirement flags
  uint8_t auth_flags = payload[0];
  const uint8_t *nonce = payload + 1;

  log_debug("Server auth requirements: password=%s, client_key=%s",
            (auth_flags & AUTH_REQUIRE_PASSWORD) ? "required" : "no",
            (auth_flags & AUTH_REQUIRE_CLIENT_KEY) ? "required" : "no");

  // Check if we can satisfy the server's authentication requirements
  bool has_password = ctx->crypto_ctx.has_password;
  bool has_client_key = (ctx->client_private_key.type == KEY_TYPE_ED25519);
  bool password_required = (auth_flags & AUTH_REQUIRE_PASSWORD);
  bool client_key_required = (auth_flags & AUTH_REQUIRE_CLIENT_KEY);

  // Provide specific error messages based on what's missing
  if (password_required && !has_password) {
    if (client_key_required && !has_client_key) {
      log_error("Server requires both password and client key authentication");
      log_error("Please provide --password and --key to authenticate");
      buffer_pool_free(payload, payload_len);
      return -2; // Authentication failure
    } else {
      // Prompt for password interactively
      char prompted_password[256];
      if (platform_prompt_password("Server password required - please enter password:", prompted_password,
                                   sizeof(prompted_password)) != 0) {
        log_error("Failed to read password");
        buffer_pool_free(payload, payload_len);
        return -2; // Authentication failure
      }

      // Derive password key from prompted password
      log_debug("Deriving key from prompted password");
      crypto_result_t crypto_result = crypto_derive_password_key(&ctx->crypto_ctx, prompted_password);
      sodium_memzero(prompted_password, sizeof(prompted_password));

      if (crypto_result != CRYPTO_OK) {
        log_error("Failed to derive password key: %s", crypto_result_to_string(crypto_result));
        buffer_pool_free(payload, payload_len);
        return -2; // Authentication failure
      }

      // Mark that password auth is now available
      ctx->crypto_ctx.has_password = true;
      has_password = true; // Update flag for logic below
    }
  }

  // Authentication response priority:
  // NOTE: Identity verification happens during KEY_EXCHANGE phase, not AUTH_RESPONSE!
  // 1. If server requires password → MUST send HMAC (32 bytes), error if no password
  // 2. Else if server requires identity (whitelist) → MUST send Ed25519 signature (64 bytes), error if no key
  // 3. Else if client has password → send HMAC (optional password auth)
  // 4. Else if client has SSH key → send Ed25519 signature (optional identity proof)
  // 5. Else → no authentication available

  // Clean up payload before any early returns
  buffer_pool_free(payload, payload_len);

  if (password_required) {
    // Server requires password - HIGHEST PRIORITY
    // (Identity was already verified in KEY_EXCHANGE phase if whitelist is enabled)
    if (!has_password) {
      log_error("Server requires password authentication");
      log_error("Please provide --password for this server");
      return -2; // Authentication failure - exit
    }

    result = send_password_auth_response(ctx, client_socket, nonce, "required password");
    if (result != 0) {
      return result;
    }
  } else if (client_key_required) {
    // Server requires client key (whitelist) - SECOND PRIORITY
    if (!has_client_key) {
      log_error("Server requires client key authentication (whitelist)");
      log_error("Please provide --key with your authorized Ed25519 key");
      return -2; // Authentication failure - exit
    }

    result = send_key_auth_response(ctx, client_socket, nonce, "required client key");
    if (result != 0) {
      return result;
    }
  } else if (has_password) {
    // No server requirements, but client has password → send HMAC + client nonce (optional)
    result = send_password_auth_response(ctx, client_socket, nonce, "optional password");
    if (result != 0) {
      return result;
    }
  } else if (has_client_key) {
    // No server requirements, but client has SSH key → send Ed25519 signature + client nonce (optional)
    result = send_key_auth_response(ctx, client_socket, nonce, "optional identity");
    if (result != 0) {
      return result;
    }
  } else {
    // No authentication method available
    // Continue without authentication (server will decide if this is acceptable)
    log_debug("No authentication credentials provided - continuing without authentication");
  }

  ctx->state = CRYPTO_HANDSHAKE_AUTHENTICATING;
  log_debug("Client sent auth response to server");

  return 0;
}

// Client: Wait for handshake complete confirmation
int crypto_handshake_client_complete(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_AUTHENTICATING)
    return -1;

  // Receive HANDSHAKE_COMPLETE or AUTH_FAILED packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != 1) {
    log_error("Failed to receive handshake completion packet");
    return -1;
  }

  // Check packet type
  if (packet_type == PACKET_TYPE_AUTH_FAILED) {
    // Parse the auth failure packet to get specific reasons
    if (payload_len >= sizeof(auth_failure_packet_t)) {
      auth_failure_packet_t *failure = (auth_failure_packet_t *)payload;
      log_error("Server rejected authentication:");

      if (failure->reason_flags & AUTH_FAIL_PASSWORD_INCORRECT) {
        log_error("  - Incorrect password");
      }
      if (failure->reason_flags & AUTH_FAIL_PASSWORD_REQUIRED) {
        log_error("  - Server requires a password (use --password)");
      }
      if (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REQUIRED) {
        log_error("  - Server requires a whitelisted client key (use --key with your SSH key)");
      }
      if (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REJECTED) {
        log_error("  - Your client key is not in the server's whitelist");
      }
      if (failure->reason_flags & AUTH_FAIL_SIGNATURE_INVALID) {
        log_error("  - Client signature verification failed");
      }

      // Provide helpful guidance
      if (failure->reason_flags & (AUTH_FAIL_PASSWORD_INCORRECT | AUTH_FAIL_CLIENT_KEY_REQUIRED)) {
        if ((failure->reason_flags & AUTH_FAIL_PASSWORD_INCORRECT) &&
            (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REQUIRED)) {
          log_error("Hint: Server requires BOTH correct password AND whitelisted key");
        } else if (failure->reason_flags & AUTH_FAIL_PASSWORD_INCORRECT) {
          log_error("Hint: Check your password and try again");
        } else if (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REQUIRED) {
          log_error("Hint: Provide your SSH key with --key ~/.ssh/id_ed25519");
        }
      }
    } else {
      log_error("Server rejected authentication (no details provided)");
    }
    buffer_pool_free(payload, payload_len);
    return -2; // Special code for auth failure - do not retry
  }

  if (packet_type != PACKET_TYPE_SERVER_AUTH_RESPONSE) {
    log_error("Expected SERVER_AUTH_RESPONSE or AUTH_FAILED, got packet type %d", packet_type);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Verify server's HMAC for mutual authentication
  if (payload_len != 32) {
    log_error("Invalid SERVER_AUTH_RESPONSE size: %zu bytes (expected 32)", payload_len);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Verify server's HMAC (binds to DH shared_secret to prevent MITM)
  if (!crypto_verify_auth_response(&ctx->crypto_ctx, ctx->client_challenge_nonce, payload)) {
    log_error("SECURITY: Server authentication failed - incorrect HMAC");
    log_error("This may indicate a man-in-the-middle attack!");
    buffer_pool_free(payload, payload_len);
    return -2; // Authentication failure - do not retry
  }

  buffer_pool_free(payload, payload_len);

  ctx->state = CRYPTO_HANDSHAKE_READY;
  log_info("Server authentication successful - mutual authentication complete");

  return 0;
}

// Server: Process auth response and complete handshake
int crypto_handshake_server_complete(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_AUTHENTICATING)
    return -1;

  // Receive AUTH_RESPONSE packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != 1) {
    log_error("Failed to receive AUTH_RESPONSE packet");
    return -1;
  }

  // Verify packet type
  if (packet_type != PACKET_TYPE_AUTH_RESPONSE) {
    log_error("Expected AUTH_RESPONSE, got packet type %d", packet_type);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Verify password if required
  if (ctx->crypto_ctx.has_password) {
    // Password-based auth: Verify HMAC (payload is now HMAC(32) + client_nonce(32) = 64 bytes)
    if (payload_len != 64) {
      log_error("Invalid AUTH_RESPONSE size: %zu bytes (expected 64: HMAC + client nonce)", payload_len);
      buffer_pool_free(payload, payload_len);
      return -1;
    }

    // Verify password HMAC (binds to DH shared_secret to prevent MITM)
    if (!crypto_verify_auth_response(&ctx->crypto_ctx, ctx->crypto_ctx.auth_nonce, payload)) {
      log_error("Password authentication failed - incorrect password");
      buffer_pool_free(payload, payload_len);

      // Send AUTH_FAILED with specific reason
      auth_failure_packet_t failure = {0};
      failure.reason_flags = AUTH_FAIL_PASSWORD_INCORRECT;
      if (ctx->require_client_auth) {
        failure.reason_flags |= AUTH_FAIL_CLIENT_KEY_REQUIRED;
      }
      send_packet(client_socket, PACKET_TYPE_AUTH_FAILED, &failure, sizeof(failure));
      return -1;
    }

    // Extract client challenge nonce for mutual authentication
    memcpy(ctx->client_challenge_nonce, payload + 32, 32);
    log_info("Password authentication successful");
  } else {
    // Ed25519 signature auth (payload is signature(64) + client_nonce(32) = 96 bytes)
    // Note: Ed25519 verification happens during key exchange, not here
    // Just extract the client nonce from the payload
    if (payload_len == 96) {
      // Signature + client nonce
      memcpy(ctx->client_challenge_nonce, payload + 64, 32);
    } else if (payload_len == 64) {
      // Just client nonce (legacy or password-only mode without password enabled)
      memcpy(ctx->client_challenge_nonce, payload + 32, 32);
    } else {
      log_error("Invalid AUTH_RESPONSE size: %zu bytes (expected 64 or 96)", payload_len);
      buffer_pool_free(payload, payload_len);
      return -1;
    }
  }

  // Verify client key if required (whitelist)
  if (ctx->require_client_auth) {
    if (!ctx->client_ed25519_key_verified) {
      log_error("Client key authentication failed - client did not provide a whitelisted key");
      buffer_pool_free(payload, payload_len);

      // Send AUTH_FAILED with specific reason
      auth_failure_packet_t failure = {0};
      failure.reason_flags = AUTH_FAIL_CLIENT_KEY_REQUIRED;
      if (ctx->crypto_ctx.has_password) {
        // Password was verified, but key was not
        log_error("Note: Password was correct, but client key is required");
      }
      send_packet(client_socket, PACKET_TYPE_AUTH_FAILED, &failure, sizeof(failure));
      return -1;
    }
    log_info("Client key authentication successful (whitelist verified)");
    if (strlen(ctx->client_ed25519_key.comment) > 0) {
      log_info("Authenticated client: %s", ctx->client_ed25519_key.comment);
    }
  }

  buffer_pool_free(payload, payload_len);

  // Send SERVER_AUTH_RESPONSE with server's HMAC for mutual authentication
  // Bind to DH shared_secret to prevent MITM (even if attacker knows password)
  uint8_t server_hmac[32];
  crypto_result_t crypto_result =
      crypto_compute_auth_response(&ctx->crypto_ctx, ctx->client_challenge_nonce, server_hmac);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to compute server HMAC for mutual authentication: %s", crypto_result_to_string(crypto_result));
    return -1;
  }

  log_debug("Sending SERVER_AUTH_RESPONSE packet with server HMAC (32 bytes) for mutual authentication");
  result = send_packet(client_socket, PACKET_TYPE_SERVER_AUTH_RESPONSE, server_hmac, sizeof(server_hmac));
  if (result != 0) {
    log_error("Failed to send SERVER_AUTH_RESPONSE packet");
    return -1;
  }

  ctx->state = CRYPTO_HANDSHAKE_READY;
  log_info("Crypto handshake completed successfully (mutual authentication)");

  return 0;
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

// Encrypt a packet using the established crypto context
int crypto_handshake_encrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *plaintext,
                                    size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                                    size_t *ciphertext_len) {
  if (!ctx || !crypto_handshake_is_ready(ctx))
    return -1;

  crypto_result_t result = crypto_encrypt((crypto_context_t *)&ctx->crypto_ctx, plaintext, plaintext_len, ciphertext,
                                          ciphertext_size, ciphertext_len);
  if (result != CRYPTO_OK) {
    log_error("Failed to encrypt packet: %s", crypto_result_to_string(result));
    return -1;
  }

  return 0;
}

// Decrypt a packet using the established crypto context
int crypto_handshake_decrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *ciphertext,
                                    size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                                    size_t *plaintext_len) {
  if (!ctx || !crypto_handshake_is_ready(ctx))
    return -1;

  crypto_result_t result = crypto_decrypt((crypto_context_t *)&ctx->crypto_ctx, ciphertext, ciphertext_len, plaintext,
                                          plaintext_size, plaintext_len);
  if (result != CRYPTO_OK) {
    log_error("Failed to decrypt packet: %s", crypto_result_to_string(result));
    return -1;
  }

  return 0;
}

// Helper: Encrypt with automatic passthrough if crypto not ready
int crypto_encrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                         const uint8_t *plaintext, size_t plaintext_len, uint8_t *ciphertext,
                                         size_t ciphertext_size, size_t *ciphertext_len) {
  if (!crypto_ready) {
    // No encryption - just copy data
    if (plaintext_len > ciphertext_size) {
      return -1;
    }
    memcpy(ciphertext, plaintext, plaintext_len);
    *ciphertext_len = plaintext_len;
    return 0;
  }

  return crypto_handshake_encrypt_packet(ctx, plaintext, plaintext_len, ciphertext, ciphertext_size, ciphertext_len);
}

// Helper: Decrypt with automatic passthrough if crypto not ready
int crypto_decrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                         const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext,
                                         size_t plaintext_size, size_t *plaintext_len) {
  if (!crypto_ready) {
    // No encryption - just copy data
    if (ciphertext_len > plaintext_size) {
      return -1;
    }
    memcpy(plaintext, ciphertext, ciphertext_len);
    *plaintext_len = ciphertext_len;
    return 0;
  }

  return crypto_handshake_decrypt_packet(ctx, ciphertext, ciphertext_len, plaintext, plaintext_size, plaintext_len);
}
