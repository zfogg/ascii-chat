#include "handshake.h"
#include "common.h"
#include "network.h"
#include "buffer_pool.h"
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
  ctx->verify_server_key = false;
  ctx->require_client_auth = false;

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
  ctx->verify_server_key = false;
  ctx->require_client_auth = false;
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

  // Send public key using proper packet protocol
  log_debug("Sending KEY_EXCHANGE_INIT packet with public key (%d bytes)", CRYPTO_PUBLIC_KEY_SIZE);
  int result =
      send_packet(client_socket, PACKET_TYPE_KEY_EXCHANGE_INIT, ctx->crypto_ctx.public_key, CRYPTO_PUBLIC_KEY_SIZE);
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

  // Verify payload size
  if (payload_len != CRYPTO_PUBLIC_KEY_SIZE) {
    log_error("Invalid public key size: %zu bytes (expected %d)", payload_len, CRYPTO_PUBLIC_KEY_SIZE);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // TODO: Verify server key against known_hosts if --server-key is specified
  if (ctx->verify_server_key) {
    // Check known_hosts for this server
    int known_host_result = check_known_host(ctx->server_hostname, ctx->server_port, payload);
    if (known_host_result == -1) {
      // Key mismatch - potential MITM attack
      display_mitm_warning(payload, payload);
      buffer_pool_free(payload, payload_len);
      return -1;
    } else if (known_host_result == 0) {
      // First connection - add to known_hosts
      add_known_host(ctx->server_hostname, ctx->server_port, payload);
      log_info("Added server to known_hosts: %s:%d", ctx->server_hostname, ctx->server_port);
    }
  }

  // Set peer's public key - this also derives the shared secret
  crypto_result_t crypto_result = crypto_set_peer_public_key(&ctx->crypto_ctx, payload);
  buffer_pool_free(payload, payload_len);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to set peer public key and derive shared secret: %s", crypto_result_to_string(crypto_result));
    return -1;
  }

  // Send our public key to server using proper packet protocol
  log_debug("Sending KEY_EXCHANGE_RESPONSE packet with public key (%d bytes)", CRYPTO_PUBLIC_KEY_SIZE);
  result =
      send_packet(client_socket, PACKET_TYPE_KEY_EXCHANGE_RESPONSE, ctx->crypto_ctx.public_key, CRYPTO_PUBLIC_KEY_SIZE);
  if (result != 0) {
    log_error("Failed to send KEY_EXCHANGE_RESPONSE packet");
    return -1;
  }

  ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;
  log_debug("Client sent public key to server");

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

  // Verify packet type
  if (packet_type != PACKET_TYPE_KEY_EXCHANGE_RESPONSE) {
    log_error("Expected KEY_EXCHANGE_RESPONSE, got packet type %d", packet_type);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Verify payload size
  if (payload_len != CRYPTO_PUBLIC_KEY_SIZE) {
    log_error("Invalid client public key size: %zu bytes (expected %d)", payload_len, CRYPTO_PUBLIC_KEY_SIZE);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Set peer's public key - this also derives the shared secret
  crypto_result_t crypto_result = crypto_set_peer_public_key(&ctx->crypto_ctx, payload);
  buffer_pool_free(payload, payload_len);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to set peer public key and derive shared secret: %s", crypto_result_to_string(crypto_result));
    return -1;
  }

  // TODO: Check client key against --client-keys whitelist if specified
  if (ctx->require_client_auth) {
    // TODO: Implement client key verification
    log_debug("Client authentication required (not yet implemented)");
  }

  // Generate nonce and store it in the context
  crypto_result = crypto_generate_nonce(ctx->crypto_ctx.auth_nonce);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to generate nonce: %s", crypto_result_to_string(crypto_result));
    return -1;
  }

  // Send AUTH_CHALLENGE with nonce
  log_debug("Sending AUTH_CHALLENGE packet with nonce (32 bytes)");
  result = send_packet(client_socket, PACKET_TYPE_AUTH_CHALLENGE, ctx->crypto_ctx.auth_nonce, 32);
  if (result != 0) {
    log_error("Failed to send AUTH_CHALLENGE packet");
    return -1;
  }

  ctx->state = CRYPTO_HANDSHAKE_AUTHENTICATING;
  log_debug("Server sent auth challenge to client");

  return 0;
}

// Client: Process auth challenge and send response
int crypto_handshake_client_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE)
    return -1;

  // Receive AUTH_CHALLENGE packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != 1) {
    log_error("Failed to receive AUTH_CHALLENGE packet");
    return -1;
  }

  // Verify packet type
  if (packet_type != PACKET_TYPE_AUTH_CHALLENGE) {
    log_error("Expected AUTH_CHALLENGE, got packet type %d", packet_type);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Verify nonce size
  if (payload_len != 32) {
    log_error("Invalid nonce size: %zu bytes (expected 32)", payload_len);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Compute HMAC response using password key (if set) or shared secret
  uint8_t auth_response[32];
  const uint8_t *auth_key = ctx->crypto_ctx.has_password ? ctx->crypto_ctx.password_key : ctx->crypto_ctx.shared_key;
  crypto_result_t crypto_result = crypto_compute_hmac(auth_key, payload, auth_response);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to compute HMAC response: %s", crypto_result_to_string(crypto_result));
    buffer_pool_free(payload, payload_len);
    return -1;
  }
  buffer_pool_free(payload, payload_len);

  // Send AUTH_RESPONSE with HMAC
  log_debug("Sending AUTH_RESPONSE packet with HMAC (32 bytes)");
  result = send_packet(client_socket, PACKET_TYPE_AUTH_RESPONSE, auth_response, sizeof(auth_response));
  if (result != 0) {
    log_error("Failed to send AUTH_RESPONSE packet");
    return -1;
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
    log_error("Server rejected authentication");
    buffer_pool_free(payload, payload_len);
    return -2; // Special code for auth failure - do not retry
  }

  if (packet_type != PACKET_TYPE_HANDSHAKE_COMPLETE) {
    log_error("Expected HANDSHAKE_COMPLETE or AUTH_FAILED, got packet type %d", packet_type);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  buffer_pool_free(payload, payload_len);

  ctx->state = CRYPTO_HANDSHAKE_READY;
  log_info("Crypto handshake completed successfully");

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

  // Verify HMAC size
  if (payload_len != 32) {
    log_error("Invalid HMAC size: %zu bytes (expected 32)", payload_len);
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  // Verify HMAC matches expected value using password key (if set) or shared secret
  uint8_t expected_hmac[32];
  const uint8_t *auth_key = ctx->crypto_ctx.has_password ? ctx->crypto_ctx.password_key : ctx->crypto_ctx.shared_key;
  crypto_result_t crypto_result = crypto_compute_hmac(auth_key, ctx->crypto_ctx.auth_nonce, expected_hmac);
  if (crypto_result != CRYPTO_OK) {
    log_error("Failed to compute expected HMAC: %s", crypto_result_to_string(crypto_result));
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  if (memcmp(payload, expected_hmac, 32) != 0) {
    log_error("HMAC verification failed - authentication rejected");
    buffer_pool_free(payload, payload_len);
    // Send AUTH_FAILED packet
    send_packet(client_socket, PACKET_TYPE_AUTH_FAILED, NULL, 0);
    return -1;
  }
  buffer_pool_free(payload, payload_len);

  // Send HANDSHAKE_COMPLETE packet
  log_debug("Sending HANDSHAKE_COMPLETE packet");
  result = send_packet(client_socket, PACKET_TYPE_HANDSHAKE_COMPLETE, NULL, 0);
  if (result != 0) {
    log_error("Failed to send HANDSHAKE_COMPLETE packet");
    return -1;
  }

  ctx->state = CRYPTO_HANDSHAKE_READY;
  log_info("Crypto handshake completed successfully");

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
