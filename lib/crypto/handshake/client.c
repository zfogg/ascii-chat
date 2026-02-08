/**
 * @file crypto/handshake/client.c
 * @ingroup handshake
 * @brief Client-side handshake protocol implementation
 */

#include <ascii-chat/crypto/handshake/client.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/buffer_pool.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/known_hosts.h>
#include <ascii-chat/crypto/gpg/gpg.h>
#include <ascii-chat/network/packet.h>
#include <ascii-chat/network/acip/transport.h>
#include <ascii-chat/network/acip/send.h>
#include <ascii-chat/util/password.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <ws2tcpip.h>
#endif

// Client: Process server's public key and send our public key
asciichat_error_t crypto_handshake_client_key_exchange(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                       packet_type_t packet_type, const uint8_t *payload,
                                                       size_t payload_len) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_INIT) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", (void *)ctx, ctx ? (int)ctx->state : -1);
  }
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "transport is NULL");
  }

  // Note: Packet already received by ACIP handler
  int result;

  // Verify packet type
  if (packet_type != PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT) {
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected KEY_EXCHANGE_INIT, got packet type %d", packet_type);
  }

  log_debug("CLIENT_KEY_EXCHANGE: Received packet with payload_len=%zu, kex_size=%u, auth_size=%u, sig_size=%u",
            payload_len, ctx->crypto_ctx.public_key_size, ctx->crypto_ctx.auth_public_key_size,
            ctx->crypto_ctx.signature_size);

  // Check payload size - only authenticated format supported
  // Authenticated: public_key_size + auth_public_key_size + signature_size bytes
  size_t expected_auth_size =
      ctx->crypto_ctx.public_key_size + ctx->crypto_ctx.auth_public_key_size + ctx->crypto_ctx.signature_size;

  uint8_t *server_ephemeral_key;
  // Use the crypto context's public key size to ensure compatibility
  size_t key_size = sizeof(ctx->crypto_ctx.public_key);
  server_ephemeral_key = SAFE_MALLOC(key_size, uint8_t *);
  if (!server_ephemeral_key) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for server ephemeral key");
  }
  uint8_t *server_identity_key;
  server_identity_key = SAFE_MALLOC(ctx->crypto_ctx.auth_public_key_size, uint8_t *);
  uint8_t *server_signature;
  server_signature = SAFE_MALLOC(ctx->crypto_ctx.signature_size, uint8_t *);

  if (!server_identity_key || !server_signature) {
    SAFE_FREE(server_ephemeral_key);
    if (server_identity_key)
      SAFE_FREE(server_identity_key);
    if (server_signature)
      SAFE_FREE(server_signature);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for server identity key or signature");
  }

  // Validate packet size using session parameters
  asciichat_error_t validation_result =
      crypto_handshake_validate_packet_size(ctx, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, payload_len);
  if (validation_result != ASCIICHAT_OK) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    SAFE_FREE(server_ephemeral_key);
    SAFE_FREE(server_identity_key);
    SAFE_FREE(server_signature);
    return validation_result;
  }

  // Check if server is using authentication (auth_public_key_size > 0 means
  // authenticated format)
  if (ctx->crypto_ctx.auth_public_key_size > 0 && payload_len == expected_auth_size) {
    // Authenticated format:
    // [ephemeral:public_key_size][identity:auth_public_key_size][signature:signature_size]
    log_debug("Received authenticated KEY_EXCHANGE_INIT (%zu bytes)", expected_auth_size);
    memcpy(server_ephemeral_key, payload, ctx->crypto_ctx.public_key_size);
    memcpy(server_identity_key, payload + ctx->crypto_ctx.public_key_size, ctx->crypto_ctx.auth_public_key_size);
    memcpy(server_signature, payload + ctx->crypto_ctx.public_key_size + ctx->crypto_ctx.auth_public_key_size,
           ctx->crypto_ctx.signature_size);

    // Server is using client authentication
    ctx->server_uses_client_auth = true;

    // DEBUG: Print identity key received
    char hex_id[HEX_STRING_SIZE_32];
    for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
      safe_snprintf(hex_id + i * 2, 3, "%02x", server_identity_key[i]);
    }
    hex_id[HEX_STRING_SIZE_32 - 1] = '\0';
    log_debug("Received identity key: %s", hex_id);

    // DEBUG: Print ephemeral key and signature
    char hex_eph[HEX_STRING_SIZE_32];
    for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
      safe_snprintf(hex_eph + i * 2, 3, "%02x", server_ephemeral_key[i]);
    }
    hex_eph[HEX_STRING_SIZE_32 - 1] = '\0';
    log_debug("Received ephemeral key: %s", hex_eph);

    char hex_sig[HEX_STRING_SIZE_64];
    for (int i = 0; i < ED25519_SIGNATURE_SIZE; i++) {
      safe_snprintf(hex_sig + i * 2, 3, "%02x", server_signature[i]);
    }
    hex_sig[HEX_STRING_SIZE_64 - 1] = '\0';
    log_debug("Received signature: %s", hex_sig);

    // Verify signature: server identity signed the ephemeral key
    log_debug("Verifying server's signature over ephemeral key (already logged above)");

    // If client didn't specify --server-key, skip signature verification
    // (client doesn't care about server identity verification)
    if (!ctx->verify_server_key) {
      log_info("Skipping server signature verification (no --server-key specified)");
      log_warn("Connection is encrypted but server identity is NOT verified (vulnerable to MITM)");
    } else {
      // Extract GPG key ID from expected_server_key if it's a GPG key (gpg:KEYID format)
      const char *gpg_key_id = NULL;
      if (ctx->expected_server_key[0] != '\0' && strncmp(ctx->expected_server_key, "gpg:", 4) == 0) {
        const char *key_id_start = ctx->expected_server_key + 4;
        size_t key_id_len = strlen(key_id_start);
        // Accept 8, 16, or 40 character GPG key IDs (short, long, or full fingerprint)
        if (key_id_len == 8 || key_id_len == 16 || key_id_len == 40) {
          gpg_key_id = key_id_start;
          log_debug("Using GPG key ID from --server-key for verification: %s", gpg_key_id);
        }
      }

      if (ed25519_verify_signature(server_identity_key, server_ephemeral_key, ctx->crypto_ctx.public_key_size,
                                   server_signature, gpg_key_id) != 0) {
        if (payload) {
          buffer_pool_free(NULL, payload, payload_len);
        }
        SAFE_FREE(server_ephemeral_key);
        SAFE_FREE(server_identity_key);
        SAFE_FREE(server_signature);
        return SET_ERRNO(ERROR_CRYPTO, "Server signature verification FAILED - rejecting connection. "
                                       "This indicates: Server's identity key does not "
                                       "match its ephemeral key, Potential man-in-the-middle attack, "
                                       "Corrupted or malicious server");
      }
      log_debug("Server signature verified successfully");
    }

    // Verify server identity against expected key if --server-key is specified
    if (ctx->verify_server_key && strlen(ctx->expected_server_key) > 0) {
      // Parse ALL expected server keys (github:/gitlab: may have multiple keys)
      public_key_t expected_keys[MAX_CLIENTS];
      size_t num_expected_keys = 0;
      if (parse_public_keys(ctx->expected_server_key, expected_keys, &num_expected_keys, MAX_CLIENTS) != 0 ||
          num_expected_keys == 0) {
        if (payload) {
          buffer_pool_free(NULL, payload, payload_len);
        }
        SAFE_FREE(server_ephemeral_key);
        SAFE_FREE(server_identity_key);
        SAFE_FREE(server_signature);
        return SET_ERRNO(ERROR_CONFIG,
                         "Failed to parse expected server key: %s. Check that "
                         "--server-key value is valid (ssh-ed25519 "
                         "format, github:username, or hex)",
                         ctx->expected_server_key);
      }

      // Compare server's IDENTITY key against ALL expected keys (match any one)
      // This supports users with multiple SSH keys (e.g., different machines)
      bool key_matched = false;
      for (size_t i = 0; i < num_expected_keys; i++) {
        if (sodium_memcmp(server_identity_key, expected_keys[i].key, ED25519_PUBLIC_KEY_SIZE) == 0) {
          key_matched = true;
          log_debug("Server identity key matched expected key %zu/%zu", i + 1, num_expected_keys);
          break;
        }
      }

      if (!key_matched) {
        if (payload) {
          buffer_pool_free(NULL, payload, payload_len);
        }
        SAFE_FREE(server_ephemeral_key);
        SAFE_FREE(server_identity_key);
        SAFE_FREE(server_signature);
        return SET_ERRNO(ERROR_CRYPTO,
                         "Server identity key mismatch - potential MITM attack! "
                         "Expected key(s) from: %s (checked %zu keys), Server presented a different key "
                         "than specified with --server-key, DO NOT CONNECT to this "
                         "server - likely man-in-the-middle attack!",
                         ctx->expected_server_key, num_expected_keys);
      }
      log_info("Server identity key verified against --server-key (%zu key(s) checked)", num_expected_keys);
    }

    // Note: Server IP resolution now handled by caller (TCP transport layer)
    // For WebSocket/WebRTC transports, server_ip should be set by the transport
    // before handshake begins. TCP clients will use the legacy wrapper which
    // handles this.
    if (ctx->server_ip[0] == '\0') {
      log_debug("Server IP not set - skipping known_hosts verification (non-TCP transport)");
    } else {
      log_debug("Server IP already set: %s", ctx->server_ip);
    }

    // Check known_hosts for this server (if we have server IP and port)
    // Check if known_hosts verification should be skipped
    bool skip_known_hosts = false;
    const char *env_skip = platform_getenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK");
    if (env_skip && strcmp(env_skip, STR_ONE) == 0) {
      log_warn(
          "Skipping known_hosts checking for authenticated connection (ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK=1)");
      skip_known_hosts = true;
    }
#ifndef NDEBUG
    // In debug builds, also skip for Claude Code (LLM automation can't do interactive prompts)
    else if (platform_getenv("CLAUDECODE")) {
      log_warn("Skipping known_hosts checking (CLAUDECODE set in debug build)");
      skip_known_hosts = true;
    }
#endif

    if (!skip_known_hosts && ctx->server_ip[0] != '\0' && ctx->server_port > 0) {
      asciichat_error_t known_host_result = check_known_host(ctx->server_ip, ctx->server_port, server_identity_key);
      if (known_host_result == ERROR_CRYPTO_VERIFICATION) {
        // Key mismatch - MITM attack detected! Prompt user for confirmation
        log_error("SECURITY: Server key does NOT match known_hosts entry!\n"
                  "This indicates a possible man-in-the-middle attack!");
        uint8_t stored_key[ZERO_KEY_SIZE] = {0}; // We don't have the stored key easily
                                                 // accessible, use zeros for now
        if (!display_mitm_warning(ctx->server_ip, ctx->server_port, stored_key, server_identity_key)) {
          // User declined to continue - ABORT connection for security
          if (payload) {
            buffer_pool_free(NULL, payload, payload_len);
          }
          SAFE_FREE(server_ephemeral_key);
          SAFE_FREE(server_identity_key);
          SAFE_FREE(server_signature);
          return SET_ERRNO(ERROR_CRYPTO_VERIFICATION,
                           "SECURITY: Connection aborted - server key mismatch (possible MITM attack)");
        }
        // User accepted the risk - continue with connection
        log_warn("SECURITY WARNING: User accepted MITM risk - continuing with connection");
      } else if (known_host_result == ASCIICHAT_OK) {
        // Unknown host (first connection) - prompt user to verify fingerprint
        if (!prompt_unknown_host(ctx->server_ip, ctx->server_port, server_identity_key)) {
          // User declined to add host - ABORT connection
          if (payload) {
            buffer_pool_free(NULL, payload, payload_len);
          }
          SAFE_FREE(server_ephemeral_key);
          SAFE_FREE(server_identity_key);
          SAFE_FREE(server_signature);
          return SET_ERRNO(ERROR_CRYPTO, "User declined to verify unknown host");
        }

        // User accepted - add to known_hosts
        if (add_known_host(ctx->server_ip, ctx->server_port, server_identity_key) != ASCIICHAT_OK) {
          if (payload) {
            buffer_pool_free(NULL, payload, payload_len);
          }
          SAFE_FREE(server_ephemeral_key);
          SAFE_FREE(server_identity_key);
          SAFE_FREE(server_signature);
          return SET_ERRNO(ERROR_CONFIG,
                           "CRITICAL SECURITY ERROR: Failed to create known_hosts "
                           "file! This is a security vulnerability - the "
                           "program cannot track known hosts. Please check file "
                           "permissions and ensure the program can write to: %s",
                           get_known_hosts_path());
        }
        log_debug("Server host added to known_hosts successfully");
      } else if (known_host_result == 1) {
        // Key matches - connection is secure!
        log_info("Server host key verified from known_hosts - connection secure");
      } else {
        // Unexpected error code from check_known_host
        if (payload) {
          buffer_pool_free(NULL, payload, payload_len);
        }
        SAFE_FREE(server_ephemeral_key);
        SAFE_FREE(server_identity_key);
        SAFE_FREE(server_signature);
        return SET_ERRNO(known_host_result, "SECURITY: known_hosts verification failed with error code %d",
                         known_host_result);
      }
    }
  } else if (payload_len == ctx->crypto_ctx.public_key_size) {
    // Simple format: just ephemeral key (no identity key)
    log_debug("Received simple KEY_EXCHANGE_INIT (%zu bytes) - server has no "
              "identity key",
              payload_len);
    memcpy(server_ephemeral_key, payload, ctx->crypto_ctx.public_key_size);

    // Clear identity key and signature for simple format
    memset(server_identity_key, 0, ctx->crypto_ctx.auth_public_key_size);
    memset(server_signature, 0, ctx->crypto_ctx.signature_size);

    // Server is not using client authentication in simple mode
    ctx->server_uses_client_auth = false;

    log_debug("Received ephemeral key (simple format)");

    // SECURITY: For servers without identity keys, we implement a different security model:
    // 1. Verify IP address matches known_hosts entry
    // 2. Always require user confirmation (no silent connections)
    // 3. Store server fingerprint for future verification

    if (ctx->server_ip[0] == '\0' || ctx->server_port <= 0) {
      SAFE_FREE(server_ephemeral_key);
      SAFE_FREE(server_identity_key);
      SAFE_FREE(server_signature);
      return SET_ERRNO(ERROR_CRYPTO, "Server IP or port not set, cannot check known_hosts");
    }

    // Check if this server was previously connected to (IP verification)
    bool skip_known_hosts = false;
    asciichat_error_t known_host_result = ASCIICHAT_OK;
    const char *env_skip_known_hosts_checking = platform_getenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK");
    if (env_skip_known_hosts_checking && strcmp(env_skip_known_hosts_checking, STR_ONE) == 0) {
      log_warn("Skipping known_hosts checking. This is a security vulnerability.");
      skip_known_hosts = true;
    }
#ifndef NDEBUG
    // In debug builds, also skip for Claude Code (LLM automation can't do interactive prompts)
    else if (platform_getenv("CLAUDECODE")) {
      log_warn("Skipping known_hosts checking (CLAUDECODE set in debug build).");
      skip_known_hosts = true;
    }
#endif
    else {
      known_host_result = check_known_host_no_identity(ctx->server_ip, ctx->server_port);
    }

    if (skip_known_hosts || known_host_result == 1) {
      // Server IP is known and verified - allow connection without warnings
      log_info("SECURITY: Server IP %s:%u is known (no-identity entry found) - connection verified", ctx->server_ip,
               ctx->server_port);
    } else if (known_host_result == ASCIICHAT_OK) {
      // Server IP is unknown - require user confirmation
      log_warn("SECURITY: Unknown server IP %s:%u with no identity key\n"
               "This connection is vulnerable to man-in-the-middle attacks\n"
               "Anyone can intercept your connection and read your data",
               ctx->server_ip, ctx->server_port);

      if (!prompt_unknown_host_no_identity(ctx->server_ip, ctx->server_port)) {
        if (payload) {
          buffer_pool_free(NULL, payload, payload_len);
        }
        SAFE_FREE(server_ephemeral_key);
        SAFE_FREE(server_identity_key);
        SAFE_FREE(server_signature);
        return SET_ERRNO(ERROR_CRYPTO, "User declined to connect to unknown server without identity key");
      }

      // User accepted - add to known_hosts as no-identity entry
      // For servers without identity keys, pass zero key to indicate no-identity
      uint8_t zero_key[ZERO_KEY_SIZE] = {0};
      if (add_known_host(ctx->server_ip, ctx->server_port, zero_key) != ASCIICHAT_OK) {
        if (payload) {
          buffer_pool_free(NULL, payload, payload_len);
        }
        SAFE_FREE(server_ephemeral_key);
        SAFE_FREE(server_identity_key);
        SAFE_FREE(server_signature);
        return SET_ERRNO(ERROR_CONFIG,
                         "CRITICAL SECURITY ERROR: Failed to create known_hosts "
                         "file! This is a security vulnerability - the "
                         "program cannot track known hosts. Please check file "
                         "permissions and ensure the program can write to: %s",
                         get_known_hosts_path());
      }
      log_debug("Server host added to known_hosts successfully");
    } else if (known_host_result == ERROR_CRYPTO_VERIFICATION) {
      // Server previously had identity key but now has none - potential security issue
      log_warn("SECURITY: Server previously had identity key but now has none - potential security issue");
      if (payload) {
        buffer_pool_free(NULL, payload, payload_len);
      }
      SAFE_FREE(server_ephemeral_key);
      SAFE_FREE(server_identity_key);
      SAFE_FREE(server_signature);
      return SET_ERRNO(ERROR_CRYPTO_VERIFICATION, "Server key configuration changed - potential security issue");
    } else {
      // Other error checking known_hosts (e.g., ERROR_INVALID_PARAM)
      if (payload) {
        buffer_pool_free(NULL, payload, payload_len);
      }
      SAFE_FREE(server_ephemeral_key);
      SAFE_FREE(server_identity_key);
      SAFE_FREE(server_signature);
      return SET_ERRNO(ERROR_CRYPTO, "Failed to verify server IP address");
    }
  } else {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    SAFE_FREE(server_ephemeral_key);
    SAFE_FREE(server_identity_key);
    SAFE_FREE(server_signature);
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                     "Invalid KEY_EXCHANGE_INIT size: %zu bytes (expected %zu or "
                     "%zu). This indicates: Protocol violation "
                     "or incompatible server version, Potential man-in-the-middle "
                     "attack, Network corruption",
                     payload_len, expected_auth_size, ctx->crypto_ctx.public_key_size);
    // retry
  }

  // Set peer's public key (EPHEMERAL X25519) - this also derives the shared secret
  crypto_result_t crypto_result = crypto_set_peer_public_key(&ctx->crypto_ctx, server_ephemeral_key);
  if (payload) {
    buffer_pool_free(NULL, payload, payload_len);
  }
  if (crypto_result != CRYPTO_OK) {
    SAFE_FREE(server_ephemeral_key);
    SAFE_FREE(server_identity_key);
    SAFE_FREE(server_signature);
    return SET_ERRNO(ERROR_CRYPTO, "Failed to set peer public key and derive shared secret: %s",
                     crypto_result_to_string(crypto_result));
  }

  // Determine if client has an identity key
  bool client_has_identity_key = (ctx->client_private_key.type == KEY_TYPE_ED25519);

  // Send authenticated response if server has identity key (auth_public_key_size > 0)
  // OR if server requires client authentication (require_client_auth)
  // Note: server_uses_client_auth is set when server has identity key, but we should
  // send authenticated response when server has identity key regardless of client auth requirement
  bool server_has_identity = (ctx->crypto_ctx.auth_public_key_size > 0 && ctx->crypto_ctx.signature_size > 0);
  bool server_requires_auth = server_has_identity || ctx->require_client_auth;

  if (server_requires_auth) {
    // Send authenticated packet:
    // [ephemeral:kex_size][identity:auth_size][signature:sig_size][gpg_key_id_len:1][gpg_key_id:0-16]
    // Use Ed25519 sizes since client has Ed25519 key
    size_t ed25519_pubkey_size = ED25519_PUBLIC_KEY_SIZE; // Ed25519 public key is always 32 bytes
    size_t ed25519_sig_size = ED25519_SIGNATURE_SIZE;     // Ed25519 signature is always 64 bytes
    size_t response_size = ctx->crypto_ctx.public_key_size + ed25519_pubkey_size + ed25519_sig_size;

    // Check if client has a GPG key ID to send
    uint8_t gpg_key_id_len = 0;
    if (ctx->client_gpg_key_id[0] != '\0') {
      gpg_key_id_len = (uint8_t)strlen(ctx->client_gpg_key_id);
      if (gpg_key_id_len > 40) {
        gpg_key_id_len = 40; // Truncate to max length (full fingerprint)
      }
      response_size += 1 + gpg_key_id_len; // 1 byte for length + key ID
    } else {
      response_size += 1; // Just the length byte (0)
    }

    uint8_t *key_response = SAFE_MALLOC(response_size, uint8_t *);
    size_t offset = 0;

    // Copy ephemeral key
    memcpy(key_response + offset, ctx->crypto_ctx.public_key,
           ctx->crypto_ctx.public_key_size); // X25519 ephemeral for encryption
    offset += ctx->crypto_ctx.public_key_size;

    if (client_has_identity_key) {
      // Client has identity key - send it with signature
      memcpy(key_response + offset, ctx->client_private_key.public_key, ed25519_pubkey_size); // Ed25519 identity
      offset += ed25519_pubkey_size;

      // Sign ephemeral key with client identity key
      if (ed25519_sign_message(&ctx->client_private_key, ctx->crypto_ctx.public_key, ctx->crypto_ctx.public_key_size,
                               key_response + offset) != 0) {
        SAFE_FREE(key_response);
        SAFE_FREE(server_ephemeral_key);
        SAFE_FREE(server_identity_key);
        SAFE_FREE(server_signature);
        return SET_ERRNO(ERROR_CRYPTO, "Failed to sign client ephemeral key");
      }
      offset += ed25519_sig_size;
    } else {
      // Client has no identity key - send null identity and null signature
      memset(key_response + offset, 0, ed25519_pubkey_size); // Null identity
      offset += ed25519_pubkey_size;
      memset(key_response + offset, 0, ed25519_sig_size); // Null signature
      offset += ed25519_sig_size;
    }

    // Append GPG key ID length
    key_response[offset] = gpg_key_id_len;
    offset += 1;

    // Append GPG key ID if present
    if (gpg_key_id_len > 0) {
      memcpy(key_response + offset, ctx->client_gpg_key_id, gpg_key_id_len);
      offset += gpg_key_id_len;
      log_debug("Including client GPG key ID in KEY_EXCHANGE_RESPONSE: %.*s", gpg_key_id_len, ctx->client_gpg_key_id);
    }

    result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP, key_response, response_size);
    if (result != 0) {
      SAFE_FREE(key_response);
      SAFE_FREE(server_ephemeral_key);
      SAFE_FREE(server_identity_key);
      SAFE_FREE(server_signature);
      return SET_ERRNO(ERROR_NETWORK, "Failed to send KEY_EXCHANGE_RESPONSE packet");
    }

    // Zero out the buffer before freeing
    sodium_memzero(key_response, response_size);
    SAFE_FREE(key_response);
  } else {
    // Send X25519 encryption key only to server (no identity key)
    // Format: [X25519 pubkey (kex_size)] = kex_size bytes total
    result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP, ctx->crypto_ctx.public_key,
                                       ctx->crypto_ctx.public_key_size);
    if (result != 0) {
      SAFE_FREE(server_ephemeral_key);
      SAFE_FREE(server_identity_key);
      SAFE_FREE(server_signature);
      return SET_ERRNO(ERROR_NETWORK, "Failed to send KEY_EXCHANGE_RESPONSE packet");
    }
  }

  ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;

  // Free temporary buffers before successful return
  SAFE_FREE(server_ephemeral_key);
  SAFE_FREE(server_identity_key);
  SAFE_FREE(server_signature);

  return ASCIICHAT_OK;
}
// Helper: Send password-based authentication response with mutual auth
static asciichat_error_t send_password_auth_response(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                     const uint8_t *nonce, const char *auth_context) {
  // Ensure shared secret is derived before computing password HMAC
  // This is critical for password HMAC computation which binds to the shared secret
  if (!ctx->crypto_ctx.key_exchange_complete) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to compute password HMAC - key exchange not complete");
  }

  // Compute HMAC bound to shared_secret (MITM protection)
  uint8_t hmac_response[HMAC_SHA256_SIZE]; // Maximum size buffer
  crypto_result_t crypto_result = crypto_compute_auth_response(&ctx->crypto_ctx, nonce, hmac_response);
  if (crypto_result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to compute HMAC response: %s", crypto_result_to_string(crypto_result));
  }

  // Generate client challenge nonce for mutual authentication
  crypto_result = crypto_generate_nonce(ctx->client_challenge_nonce);
  if (crypto_result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to generate client challenge nonce: %s",
                     crypto_result_to_string(crypto_result));
  }

  // Combine HMAC + client nonce (hmac_size + auth_challenge_size bytes)
  // Use ctx->crypto_ctx.hmac_size and ctx->crypto_ctx.auth_challenge_size (negotiated during handshake)
  size_t auth_packet_size = ctx->crypto_ctx.hmac_size + ctx->crypto_ctx.auth_challenge_size;
  uint8_t auth_packet[HMAC_SHA256_SIZE + HMAC_SHA256_SIZE]; // Maximum size buffer (hmac + challenge)
  memcpy(auth_packet, hmac_response, ctx->crypto_ctx.hmac_size);
  memcpy(auth_packet + ctx->crypto_ctx.hmac_size, ctx->client_challenge_nonce, ctx->crypto_ctx.auth_challenge_size);

  log_debug("Sending AUTH_RESPONSE packet with HMAC + client nonce (%zu bytes) - %s", auth_packet_size, auth_context);
  int result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_RESPONSE, auth_packet, auth_packet_size);
  if (result != 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_RESPONSE packet");
  }

  return ASCIICHAT_OK;
}

// Helper: Send Ed25519 signature-based authentication response with mutual auth
static asciichat_error_t send_key_auth_response(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                const uint8_t *nonce, const char *auth_context) {
  // Sign the challenge with our Ed25519 private key
  uint8_t signature[ED25519_SIGNATURE_SIZE]; // Maximum size buffer
  asciichat_error_t sign_result =
      ed25519_sign_message(&ctx->client_private_key, nonce, ctx->crypto_ctx.auth_challenge_size, signature);
  if (sign_result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to sign challenge with Ed25519 key");
  }

  // Generate client challenge nonce for mutual authentication
  crypto_result_t crypto_result = crypto_generate_nonce(ctx->client_challenge_nonce);
  if (crypto_result != CRYPTO_OK) {
    sodium_memzero(signature, sizeof(signature));
    return SET_ERRNO(ERROR_CRYPTO, "Failed to generate client challenge nonce: %s",
                     crypto_result_to_string(crypto_result));
  }

  // Combine signature + client nonce + optional GPG key ID
  // Packet format: [signature:64][nonce:32][gpg_key_id_len:1][gpg_key_id:0-40]
  size_t auth_packet_size = ctx->crypto_ctx.signature_size + ctx->crypto_ctx.auth_challenge_size;

  // Check if client has a GPG key ID to send
  uint8_t gpg_key_id_len = 0;
  if (ctx->client_gpg_key_id[0] != '\0') {
    gpg_key_id_len = (uint8_t)strlen(ctx->client_gpg_key_id);
    if (gpg_key_id_len > 40) {
      gpg_key_id_len = 40; // Truncate to max length (full fingerprint)
    }
    auth_packet_size += 1 + gpg_key_id_len; // 1 byte for length + key ID
  } else {
    auth_packet_size += 1; // Just the length byte (0)
  }

  uint8_t auth_packet[ED25519_SIGNATURE_SIZE + HMAC_SHA256_SIZE +
                      41]; // Maximum size buffer (signature + challenge + len + key_id[40])
  size_t offset = 0;

  // Copy signature
  memcpy(auth_packet + offset, signature, ctx->crypto_ctx.signature_size);
  offset += ctx->crypto_ctx.signature_size;

  // Copy client nonce
  memcpy(auth_packet + offset, ctx->client_challenge_nonce, ctx->crypto_ctx.auth_challenge_size);
  offset += ctx->crypto_ctx.auth_challenge_size;

  // Copy GPG key ID length
  auth_packet[offset] = gpg_key_id_len;
  offset += 1;

  // Copy GPG key ID if present
  if (gpg_key_id_len > 0) {
    memcpy(auth_packet + offset, ctx->client_gpg_key_id, gpg_key_id_len);
    offset += gpg_key_id_len;
    log_debug("Including client GPG key ID in AUTH_RESPONSE: %.*s", gpg_key_id_len, ctx->client_gpg_key_id);
  }

  sodium_memzero(signature, sizeof(signature));

  log_debug("Sending AUTH_RESPONSE packet with Ed25519 signature + client "
            "nonce + GPG key ID (%zu bytes) - %s",
            auth_packet_size, auth_context);
  int result = packet_send_via_transport(transport, PACKET_TYPE_CRYPTO_AUTH_RESPONSE, auth_packet, auth_packet_size);
  if (result != 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_RESPONSE packet");
  }

  return ASCIICHAT_OK;
}
// Client: Process auth challenge and send response
asciichat_error_t crypto_handshake_client_auth_response(crypto_handshake_context_t *ctx, acip_transport_t *transport,
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

  // If server sent HANDSHAKE_COMPLETE, authentication was skipped (client has
  // no key)
  if (packet_type == PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    ctx->state = CRYPTO_HANDSHAKE_READY;
    ctx->crypto_ctx.handshake_complete = true; // Mark crypto context as ready for rekeying
    log_debug("Crypto handshake completed successfully (no authentication required)");
    return ASCIICHAT_OK;
  }

  // If server sent AUTH_FAILED, client is not authorized
  if (packet_type == PACKET_TYPE_CRYPTO_AUTH_FAILED) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_CRYPTO, "Server rejected authentication - client key not authorized");
  }

  // Otherwise, verify packet type is AUTH_CHALLENGE
  if (packet_type != PACKET_TYPE_CRYPTO_AUTH_CHALLENGE) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                     "Expected AUTH_CHALLENGE, HANDSHAKE_COMPLETE, or AUTH_FAILED, "
                     "got packet type %d",
                     packet_type);
  }

  // Validate packet size using session parameters
  asciichat_error_t validation_result =
      crypto_handshake_validate_packet_size(ctx, PACKET_TYPE_CRYPTO_AUTH_CHALLENGE, payload_len);
  if (validation_result != ASCIICHAT_OK) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return validation_result;
  }

  // Parse auth requirement flags
  uint8_t auth_flags = payload[0];

  // Copy nonce to local buffer before freeing payload
  // Use auth_challenge_size since that's what the server sent
  // Note: auth_challenge_size is uint8_t (max 255), buffer is 256 bytes, so always sufficient
  uint8_t nonce_buffer[256];
  memcpy(nonce_buffer, payload + 1, ctx->crypto_ctx.auth_challenge_size);
  const uint8_t *nonce = nonce_buffer;

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
      if (payload) {
        buffer_pool_free(NULL, payload, payload_len);
      }
      return SET_ERRNO(ERROR_CRYPTO, "Server requires both password and client key authentication. Please "
                                     "provide --password and --key to authenticate");
    }
    // Prompt for password interactively
    char prompted_password[PASSWORD_BUFFER_SIZE];
    if (prompt_password("Server password required - please enter password:", prompted_password,
                        sizeof(prompted_password)) != 0) {
      if (payload) {
        buffer_pool_free(NULL, payload, payload_len);
      }
      return SET_ERRNO(ERROR_CRYPTO, "Failed to read password");
    }

    // Derive password key from prompted password
    log_debug("Deriving key from prompted password");
    crypto_result_t crypto_result = crypto_derive_password_key(&ctx->crypto_ctx, prompted_password);
    sodium_memzero(prompted_password, sizeof(prompted_password));

    if (crypto_result != CRYPTO_OK) {
      if (payload) {
        buffer_pool_free(NULL, payload, payload_len);
      }
      return SET_ERRNO(ERROR_CRYPTO, "Failed to derive password key: %s", crypto_result_to_string(crypto_result));
    }

    // Mark that password auth is now available
    ctx->crypto_ctx.has_password = true;
    has_password = true; // Update flag for logic below
  }

  // Authentication response priority:
  // NOTE: Identity verification happens during KEY_EXCHANGE phase, not
  // AUTH_RESPONSE!
  // 1. If server requires password → MUST send HMAC (hmac_size bytes), error if no password
  // 2. Else if server requires identity (whitelist) → MUST send Ed25519 signature (signature_size bytes), error if no
  // key
  // 3. Else if client has password → send HMAC (optional password auth)
  // 4. Else if client has SSH key → send Ed25519 signature (optional identity
  // proof)
  // 5. Else → no authentication available

  // Clean up payload before any early returns
  if (payload) {
    buffer_pool_free(NULL, payload, payload_len);
  }

  if (password_required) {
    // Server requires password - HIGHEST PRIORITY
    // (Identity was already verified in KEY_EXCHANGE phase if whitelist is
    // enabled)
    if (!has_password) {
      return SET_ERRNO(ERROR_CRYPTO, "Server requires password authentication\n"
                                     "Please provide --password for this server");
    }

    result = send_password_auth_response(ctx, transport, nonce, "required password");
    if (result != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send password auth response");
      return result;
    }
  } else if (client_key_required) {
    // Server requires client key (whitelist) - SECOND PRIORITY
    if (!has_client_key) {
      return SET_ERRNO(ERROR_CRYPTO, "Server requires client key authentication (whitelist)\n"
                                     "Please provide --key with your authorized Ed25519 key");
    }

    result = send_key_auth_response(ctx, transport, nonce, "required client key");
    if (result != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send key auth response");
      return result;
    }
  } else if (has_password) {
    // No server requirements, but client has password → send HMAC + client
    // nonce (optional)
    result = send_password_auth_response(ctx, transport, nonce, "optional password");
    if (result != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send password auth response");
      return result;
    }
  } else if (has_client_key) {
    // No server requirements, but client has SSH key → send Ed25519 signature +
    // client nonce (optional)
    result = send_key_auth_response(ctx, transport, nonce, "optional identity");
    if (result != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send key auth response");
      return result;
    }
  } else {
    // No authentication method available
    // Continue without authentication (server will decide if this is
    // acceptable)
    log_debug("No authentication credentials provided - continuing without "
              "authentication");
  }

  ctx->state = CRYPTO_HANDSHAKE_AUTHENTICATING;

  return ASCIICHAT_OK;
}
// Client: Wait for handshake complete confirmation
asciichat_error_t crypto_handshake_client_complete(crypto_handshake_context_t *ctx, acip_transport_t *transport,
                                                   packet_type_t packet_type, const uint8_t *payload,
                                                   size_t payload_len) {
  // Accept both KEY_EXCHANGE and AUTHENTICATING states for simple mode compatibility
  // In simple mode, server skips AUTH_CHALLENGE and sends HANDSHAKE_COMPLETE directly
  if (!ctx || (ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE && ctx->state != CRYPTO_HANDSHAKE_AUTHENTICATING)) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
    return ERROR_INVALID_STATE;
  }
  if (!transport) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "transport is NULL");
  }

  // Note: Packet already received by ACIP handler
  (void)transport; // Unused parameter (this function only receives, doesn't send)

  // Check packet type
  if (packet_type == PACKET_TYPE_CRYPTO_AUTH_FAILED) {
    // Parse the auth failure packet to get specific reasons
    if (payload_len >= sizeof(auth_failure_packet_t)) {
      auth_failure_packet_t *failure = (auth_failure_packet_t *)payload;
      SET_ERRNO(ERROR_CRYPTO_AUTH, "Server rejected authentication:");

      if (failure->reason_flags & AUTH_FAIL_PASSWORD_INCORRECT) {
        SET_ERRNO(ERROR_CRYPTO_AUTH, "  - Incorrect password");
      }
      if (failure->reason_flags & AUTH_FAIL_PASSWORD_REQUIRED) {
        SET_ERRNO(ERROR_CRYPTO_AUTH, "  - Server requires a password (use --password)");
      }
      if (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REQUIRED) {
        SET_ERRNO(ERROR_CRYPTO_AUTH, "  - Server requires a whitelisted client key (use --key "
                                     "with your SSH key)");
      }
      if (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REJECTED) {
        SET_ERRNO(ERROR_CRYPTO_AUTH, "  - Your client key is not in the server's whitelist");
      }
      if (failure->reason_flags & AUTH_FAIL_SIGNATURE_INVALID) {
        SET_ERRNO(ERROR_CRYPTO_AUTH, "  - Client signature verification failed");
      }

      // Provide helpful guidance
      if (failure->reason_flags & (AUTH_FAIL_PASSWORD_INCORRECT | AUTH_FAIL_CLIENT_KEY_REQUIRED)) {
        if ((failure->reason_flags & AUTH_FAIL_PASSWORD_INCORRECT) &&
            (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REQUIRED)) {
          SET_ERRNO(ERROR_CRYPTO_AUTH, "Hint: Server requires BOTH correct password AND "
                                       "whitelisted key");
        } else if (failure->reason_flags & AUTH_FAIL_PASSWORD_INCORRECT) {
          SET_ERRNO(ERROR_CRYPTO_AUTH, "Hint: Check your password and try again");
        } else if (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REQUIRED) {
          SET_ERRNO(ERROR_CRYPTO_AUTH, "Hint: Provide your SSH key with --key ~/.ssh/id_ed25519");
        } else if (failure->reason_flags & AUTH_FAIL_CLIENT_KEY_REJECTED) {
          SET_ERRNO(ERROR_CRYPTO_AUTH, "Hint: Your key needs to be added to the server's whitelist");
        }
      }
    } else {
      SET_ERRNO(ERROR_CRYPTO_AUTH, "Server rejected authentication (no details provided)");
    }
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_CRYPTO_AUTH,
                     "Server authentication failed - incorrect HMAC"); // Special code for
                                                                       // auth failure - do
                                                                       // not retry
  }

  // Handle no-auth flow: server sends HANDSHAKE_COMPLETE directly
  if (packet_type == PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    ctx->state = CRYPTO_HANDSHAKE_READY;
    log_info("Handshake complete (no authentication required)");
    return ASCIICHAT_OK;
  }

  // Handle with-auth flow: server sends SERVER_AUTH_RESP after authentication
  if (packet_type != PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                     "Expected HANDSHAKE_COMPLETE, SERVER_AUTH_RESPONSE, or AUTH_FAILED, got packet type %d",
                     packet_type);
  }

  // Verify server's HMAC for mutual authentication
  // Use ctx->crypto_ctx.hmac_size (negotiated during handshake) rather than SERVER_AUTH_RESPONSE_SIZE constant
  if (payload_len != ctx->crypto_ctx.hmac_size) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid SERVER_AUTH_RESPONSE size: %zu bytes (expected %u)", payload_len,
                     ctx->crypto_ctx.hmac_size);
  }

  // Verify server's HMAC (binds to DH shared_secret to prevent MITM)
  if (!crypto_verify_auth_response(&ctx->crypto_ctx, ctx->client_challenge_nonce, payload)) {
    SET_ERRNO(ERROR_CRYPTO_AUTH, "SECURITY: Server authentication failed - incorrect HMAC");
    SET_ERRNO(ERROR_CRYPTO_AUTH, "This may indicate a man-in-the-middle attack!");
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_CRYPTO_AUTH,
                     "Server authentication failed - incorrect HMAC"); // Authentication
                                                                       // failure - do not
                                                                       // retry
  }

  if (payload) {
    buffer_pool_free(NULL, payload, payload_len);
  }

  ctx->state = CRYPTO_HANDSHAKE_READY;
  log_info("Server authentication successful - mutual authentication complete");

  return ASCIICHAT_OK;
}

// =============================================================================
// Legacy TCP Socket Wrappers (backward compatibility)
// =============================================================================
// These wrappers maintain the old socket-based interface for TCP clients
// that do handshake BEFORE creating ACIP transport. Will be removed in Phase 5.

/**
 * @brief Legacy wrapper: Key exchange using socket (TCP clients only)
 */
asciichat_error_t crypto_handshake_client_key_exchange_socket(crypto_handshake_context_t *ctx, socket_t client_socket) {
  // Receive packet using old method
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive KEY_EXCHANGE_INIT packet");
  }

  // Create temporary TCP transport
  acip_transport_t *temp_transport = acip_tcp_transport_create(client_socket, NULL);
  if (!temp_transport) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK, "Failed to create temporary transport");
  }

  // Call new function (takes ownership of payload and will free it)
  asciichat_error_t handshake_result =
      crypto_handshake_client_key_exchange(ctx, temp_transport, packet_type, payload, payload_len);

  // Destroy temporary transport
  acip_transport_destroy(temp_transport);

  return handshake_result;
}

/**
 * @brief Legacy wrapper: Auth response using socket (TCP clients only)
 */
asciichat_error_t crypto_handshake_client_auth_response_socket(crypto_handshake_context_t *ctx,
                                                               socket_t client_socket) {
  // Receive packet using old method
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive packet from server");
  }

  // Create temporary TCP transport
  acip_transport_t *temp_transport = acip_tcp_transport_create(client_socket, NULL);
  if (!temp_transport) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK, "Failed to create temporary transport");
  }

  // Call new function (takes ownership of payload and will free it)
  asciichat_error_t handshake_result =
      crypto_handshake_client_auth_response(ctx, temp_transport, packet_type, payload, payload_len);

  // Destroy temporary transport
  acip_transport_destroy(temp_transport);

  return handshake_result;
}

/**
 * @brief Legacy wrapper: Complete handshake using socket (TCP clients only)
 */
asciichat_error_t crypto_handshake_client_complete_socket(crypto_handshake_context_t *ctx, socket_t client_socket) {
  // Receive packet using old method
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive handshake completion packet");
  }

  // Create temporary TCP transport
  acip_transport_t *temp_transport = acip_tcp_transport_create(client_socket, NULL);
  if (!temp_transport) {
    if (payload) {
      buffer_pool_free(NULL, payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK, "Failed to create temporary transport");
  }

  // Call new function (takes ownership of payload and will free it)
  asciichat_error_t handshake_result =
      crypto_handshake_client_complete(ctx, temp_transport, packet_type, payload, payload_len);

  // Destroy temporary transport
  acip_transport_destroy(temp_transport);

  return handshake_result;
}
