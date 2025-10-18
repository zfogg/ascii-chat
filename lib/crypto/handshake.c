#include "handshake.h"
#include "asciichat_errno.h"
#include "buffer_pool.h"
#include "common.h"
#include "crypto.h"
#include "crypto/crypto.h"
#include "known_hosts.h"
#include "network/packet.h"
#include "platform/password.h"
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#else
#include <ws2tcpip.h>
#endif

// Initialize crypto handshake context
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
    ctx->kex_public_key_size = params->kex_public_key_size;
    ctx->auth_public_key_size = params->auth_public_key_size;
    ctx->signature_size = params->signature_size;
    ctx->shared_secret_size = params->shared_secret_size;
  } else {
    // Client: convert from network byte order
    ctx->kex_public_key_size = ntohs(params->kex_public_key_size);
    ctx->auth_public_key_size = ntohs(params->auth_public_key_size);
    ctx->signature_size = ntohs(params->signature_size);
    ctx->shared_secret_size = ntohs(params->shared_secret_size);
  }
  ctx->nonce_size = params->nonce_size;
  ctx->mac_size = params->mac_size;
  ctx->hmac_size = params->hmac_size;

  // Update crypto context with negotiated parameters
  ctx->crypto_ctx.nonce_size = ctx->nonce_size;
  ctx->crypto_ctx.mac_size = ctx->mac_size;
  ctx->crypto_ctx.hmac_size = ctx->hmac_size;
  ctx->crypto_ctx.encryption_key_size = ctx->shared_secret_size; // Use shared secret size as encryption key size
  ctx->crypto_ctx.public_key_size = ctx->kex_public_key_size;
  ctx->crypto_ctx.private_key_size = ctx->kex_public_key_size; // Same as public key for X25519
  ctx->crypto_ctx.shared_key_size = ctx->shared_secret_size;
  ctx->crypto_ctx.salt_size = ARGON2ID_SALT_SIZE; // Salt size doesn't change
  ctx->crypto_ctx.signature_size = ctx->signature_size;

  log_debug("Crypto parameters set: kex_key=%u, auth_key=%u, sig=%u, "
            "secret=%u, nonce=%u, mac=%u, hmac=%u",
            ctx->kex_public_key_size, ctx->auth_public_key_size, ctx->signature_size, ctx->shared_secret_size,
            ctx->nonce_size, ctx->mac_size, ctx->hmac_size);

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
      size_t simple_size = ctx->kex_public_key_size;
      size_t authenticated_size = ctx->kex_public_key_size + ctx->auth_public_key_size + ctx->signature_size;

      if (packet_size != simple_size && packet_size != authenticated_size) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                         "Invalid KEY_EXCHANGE_INIT size: %zu (expected %zu for simple or %zu for authenticated: "
                         "kex=%u + auth=%u + sig=%u)",
                         packet_size, simple_size, authenticated_size, ctx->kex_public_key_size,
                         ctx->auth_public_key_size, ctx->signature_size);
      }
    }
    break;

  case PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP:
    // Client can send either:
    // 1. Simple format: kex_public_key_size (when server has no identity key)
    // 2. Authenticated format: kex_public_key_size + client_auth_key_size + client_sig_size
    {
      size_t simple_size = ctx->kex_public_key_size;
      // For authenticated format, use Ed25519 sizes since client has Ed25519 key
      size_t ed25519_auth_size = ED25519_PUBLIC_KEY_SIZE; // Ed25519 public key is always 32 bytes
      size_t ed25519_sig_size = ED25519_SIGNATURE_SIZE;   // Ed25519 signature is always 64 bytes
      size_t authenticated_size = ctx->kex_public_key_size + ed25519_auth_size + ed25519_sig_size;

      if (packet_size != simple_size && packet_size != authenticated_size) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                         "Invalid KEY_EXCHANGE_RESP size: %zu (expected %zu for simple or %zu for authenticated: "
                         "kex=%u + auth=%u + sig=%u)",
                         packet_size, simple_size, authenticated_size, ctx->kex_public_key_size, ed25519_auth_size,
                         ed25519_sig_size);
      }
    }
    break;

  case PACKET_TYPE_CRYPTO_AUTH_CHALLENGE:
    // Server sends: 1 byte auth_flags + 32 bytes nonce = AUTH_CHALLENGE_PACKET_SIZE bytes
    if (packet_size != AUTH_CHALLENGE_PACKET_SIZE) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid AUTH_CHALLENGE size: %zu (expected %d)", packet_size,
                       AUTH_CHALLENGE_PACKET_SIZE);
    }
    break;

  case PACKET_TYPE_CRYPTO_AUTH_RESPONSE:
    // Client sends: hmac_size + AUTH_CHALLENGE_SIZE bytes client_nonce
    {
      size_t expected_size = ctx->hmac_size + AUTH_CHALLENGE_SIZE;
      if (packet_size != expected_size) {
        return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                         "Invalid AUTH_RESPONSE size: %zu (expected %zu: hmac=%u + "
                         "nonce=%d)",
                         packet_size, expected_size, ctx->hmac_size, AUTH_CHALLENGE_SIZE);
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
    if (packet_size != ctx->hmac_size) {
      return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid SERVER_AUTH_RESP size: %zu (expected %u)", packet_size,
                       ctx->hmac_size);
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

// Initialize crypto handshake context with password authentication
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
asciichat_error_t crypto_handshake_server_start(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_INIT) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
  }

  int result;

  // Calculate packet size based on negotiated crypto parameters
  size_t expected_packet_size = ctx->kex_public_key_size + ctx->auth_public_key_size + ctx->signature_size;

  log_debug("SERVER_KEY_EXCHANGE: kex_size=%u, auth_size=%u, sig_size=%u, expected_size=%zu", ctx->kex_public_key_size,
            ctx->auth_public_key_size, ctx->signature_size, expected_packet_size);

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
    memcpy(extended_packet, ctx->crypto_ctx.public_key, ctx->kex_public_key_size);

    // Copy identity public key
    memcpy(extended_packet + ctx->kex_public_key_size, ctx->server_private_key.public_key, ctx->auth_public_key_size);

    // DEBUG: Print identity key being sent
    char hex[HEX_STRING_SIZE_32];
    for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
      safe_snprintf(hex + i * 2, 3, "%02x", ctx->server_private_key.public_key[i]);
    }
    hex[HEX_STRING_SIZE_32 - 1] = '\0';

    // Sign the ephemeral key with our identity key
    log_debug("Signing ephemeral key with server identity key");
    if (ed25519_sign_message(&ctx->server_private_key, ctx->crypto_ctx.public_key, ctx->kex_public_key_size,
                             extended_packet + ctx->kex_public_key_size + ctx->auth_public_key_size) != 0) {
      SAFE_FREE(extended_packet);
      return SET_ERRNO(ERROR_CRYPTO, "Failed to sign ephemeral key with identity key");
    }

    log_info("Sending authenticated KEY_EXCHANGE_INIT (%zu bytes: ephemeral + "
             "identity + signature)",
             expected_packet_size);
    result = send_packet(client_socket, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, extended_packet, expected_packet_size);
    SAFE_FREE(extended_packet);
  } else {
    // No identity key - send just the ephemeral key
    log_info("Sending simple KEY_EXCHANGE_INIT (%zu bytes: ephemeral key only)", ctx->kex_public_key_size);
    result = send_packet(client_socket, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, ctx->crypto_ctx.public_key,
                         ctx->kex_public_key_size);
  }

  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send KEY_EXCHANGE_INIT packet");
  }

  ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;

  return ASCIICHAT_OK;
}

// Client: Process server's public key and send our public key
asciichat_error_t crypto_handshake_client_key_exchange(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_INIT) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
  }

  // Receive server's KEY_EXCHANGE_INIT packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive KEY_EXCHANGE_INIT packet");
  }

  // Verify packet type
  if (packet_type != PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected KEY_EXCHANGE_INIT, got packet type %d", packet_type);
  }

  log_debug("CLIENT_KEY_EXCHANGE: Received packet with payload_len=%zu, kex_size=%u, auth_size=%u, sig_size=%u",
            payload_len, ctx->kex_public_key_size, ctx->auth_public_key_size, ctx->signature_size);

  // Check payload size - only authenticated format supported
  // Authenticated: kex_public_key_size + auth_public_key_size + signature_size
  // bytes
  size_t expected_auth_size = ctx->kex_public_key_size + ctx->auth_public_key_size + ctx->signature_size;

  uint8_t *server_ephemeral_key;
  // Use the crypto context's public key size to ensure compatibility
  size_t key_size = sizeof(ctx->crypto_ctx.public_key);
  server_ephemeral_key = SAFE_MALLOC(key_size, uint8_t *);
  if (!server_ephemeral_key) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for server ephemeral key");
  }
  uint8_t *server_identity_key;
  server_identity_key = SAFE_MALLOC(ctx->auth_public_key_size, uint8_t *);
  uint8_t *server_signature;
  server_signature = SAFE_MALLOC(ctx->signature_size, uint8_t *);

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
      buffer_pool_free(payload, payload_len);
    }
    SAFE_FREE(server_ephemeral_key);
    SAFE_FREE(server_identity_key);
    SAFE_FREE(server_signature);
    return validation_result;
  }

  // Check if server is using authentication (auth_public_key_size > 0 means
  // authenticated format)
  if (ctx->auth_public_key_size > 0 && payload_len == expected_auth_size) {
    // Authenticated format:
    // [ephemeral:kex_size][identity:auth_size][signature:sig_size]
    log_info("Received authenticated KEY_EXCHANGE_INIT (%zu bytes)", expected_auth_size);
    memcpy(server_ephemeral_key, payload, ctx->kex_public_key_size);
    memcpy(server_identity_key, payload + ctx->kex_public_key_size, ctx->auth_public_key_size);
    memcpy(server_signature, payload + ctx->kex_public_key_size + ctx->auth_public_key_size, ctx->signature_size);

    // Server is using client authentication
    ctx->server_uses_client_auth = true;

    // DEBUG: Print identity key received
    char hex_id[HEX_STRING_SIZE_32];
    for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
      safe_snprintf(hex_id + i * 2, 3, "%02x", server_identity_key[i]);
    }
    hex_id[HEX_STRING_SIZE_32 - 1] = '\0';
    log_info("CLIENT: Received identity key: %s", hex_id);

    // DEBUG: Print ephemeral key and signature
    char hex_eph[HEX_STRING_SIZE_32];
    for (int i = 0; i < ED25519_PUBLIC_KEY_SIZE; i++) {
      safe_snprintf(hex_eph + i * 2, 3, "%02x", server_ephemeral_key[i]);
    }
    hex_eph[HEX_STRING_SIZE_32 - 1] = '\0';
    log_info("CLIENT: Received ephemeral key: %s", hex_eph);

    char hex_sig[HEX_STRING_SIZE_64];
    for (int i = 0; i < ED25519_SIGNATURE_SIZE; i++) {
      safe_snprintf(hex_sig + i * 2, 3, "%02x", server_signature[i]);
    }
    hex_sig[HEX_STRING_SIZE_64 - 1] = '\0';
    log_info("CLIENT: Received signature: %s", hex_sig);

    // Verify signature: server identity signed the ephemeral key
    log_debug("Verifying server's signature over ephemeral key");
    if (ed25519_verify_signature(server_identity_key, server_ephemeral_key, ctx->kex_public_key_size,
                                 server_signature) != 0) {
      if (payload) {
        buffer_pool_free(payload, payload_len);
      }
      return SET_ERRNO(ERROR_CRYPTO, "Server signature verification FAILED - rejecting connection. "
                                     "This indicates: Server's identity key does not "
                                     "match its ephemeral key, Potential man-in-the-middle attack, "
                                     "Corrupted or malicious server");
    }
    log_info("Server signature verified successfully");

    // Verify server identity against expected key if --server-key is specified
    if (ctx->verify_server_key && strlen(ctx->expected_server_key) > 0) {
      // Parse the expected server key
      public_key_t expected_key;
      if (parse_public_key(ctx->expected_server_key, &expected_key) != 0) {
        if (payload) {
          buffer_pool_free(payload, payload_len);
        }
        return SET_ERRNO(ERROR_CONFIG,
                         "Failed to parse expected server key: %s. Check that "
                         "--server-key value is valid (ssh-ed25519 "
                         "format or hex)",
                         ctx->expected_server_key);
      }

      // Compare server's IDENTITY key with expected key (constant-time to
      // prevent timing attacks)
      if (sodium_memcmp(server_identity_key, expected_key.key, ED25519_PUBLIC_KEY_SIZE) != 0) {
        if (payload) {
          buffer_pool_free(payload, payload_len);
        }
        return SET_ERRNO(ERROR_CRYPTO,
                         "Server identity key mismatch - potential MITM attack! "
                         "Expected key: %s, Server presented a different key "
                         "than specified with --server-key, DO NOT CONNECT to this "
                         "server - likely man-in-the-middle attack!",
                         ctx->expected_server_key);
      }
      log_info("Server identity key verified against --server-key");
    }

    // Resolve server IP from socket if not already set
    log_debug("SECURITY_DEBUG: server_ip='%s', server_port=%u", ctx->server_ip, ctx->server_port);
    if (ctx->server_ip[0] == '\0') {
      log_debug("SECURITY_DEBUG: Server IP not set, resolving from socket");
      // Try to get server IP from socket
      struct sockaddr_storage server_addr;
      socklen_t addr_len = sizeof(server_addr);
      if (getpeername(client_socket, (struct sockaddr *)&server_addr, &addr_len) == 0) {
        char ip_str[INET6_ADDRSTRLEN];
        if (server_addr.ss_family == AF_INET) {
          struct sockaddr_in *addr_in = (struct sockaddr_in *)&server_addr;
          if (inet_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str))) {
            SAFE_STRNCPY(ctx->server_ip, ip_str, sizeof(ctx->server_ip) - 1);
            ctx->server_port = ntohs(addr_in->sin_port);
            log_debug("SECURITY: Resolved server IP from socket: %s:%u", ctx->server_ip, ctx->server_port);
          }
        } else if (server_addr.ss_family == AF_INET6) {
          struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)&server_addr;
          if (inet_ntop(AF_INET6, &addr_in6->sin6_addr, ip_str, sizeof(ip_str))) {
            SAFE_STRNCPY(ctx->server_ip, ip_str, sizeof(ctx->server_ip) - 1);
            ctx->server_port = ntohs(addr_in6->sin6_port);
            log_debug("SECURITY: Resolved server IP from socket: %s:%u", ctx->server_ip, ctx->server_port);
          }
        }
      } else {
        log_debug("SECURITY_DEBUG: Failed to get server address from socket");
      }
    } else {
      log_debug("SECURITY_DEBUG: Server IP already set: %s", ctx->server_ip);
    }

    // Check known_hosts for this server (if we have server IP and port)
    if (ctx->server_ip[0] != '\0' && ctx->server_port > 0) {
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
            buffer_pool_free(payload, payload_len);
          }
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
            buffer_pool_free(payload, payload_len);
          }
          return SET_ERRNO(ERROR_CRYPTO, "User declined to verify unknown host");
        }

        // User accepted - add to known_hosts
        if (add_known_host(ctx->server_ip, ctx->server_port, server_identity_key) != ASCIICHAT_OK) {
          if (payload) {
            buffer_pool_free(payload, payload_len);
          }
          return SET_ERRNO(ERROR_CONFIG,
                           "CRITICAL SECURITY ERROR: Failed to create known_hosts "
                           "file! This is a security vulnerability - the "
                           "program cannot track known hosts. Please check file "
                           "permissions and ensure the program can write to: %s",
                           get_known_hosts_path());
        }
        log_info("Server host added to known_hosts successfully");
      } else if (known_host_result == 1) {
        // Key matches - connection is secure!
        log_info("Server host key verified from known_hosts - connection secure");
      } else {
        // Unexpected error code from check_known_host
        if (payload) {
          buffer_pool_free(payload, payload_len);
        }
        return SET_ERRNO(known_host_result, "SECURITY: known_hosts verification failed with error code %d",
                         known_host_result);
      }
    }
  } else if (payload_len == ctx->kex_public_key_size) {
    // Simple format: just ephemeral key (no identity key)
    log_info("Received simple KEY_EXCHANGE_INIT (%zu bytes) - server has no "
             "identity key",
             payload_len);
    log_debug("SECURITY_DEBUG: Server sent simple format but known_hosts may have identity key entry");
    memcpy(server_ephemeral_key, payload, ctx->kex_public_key_size);

    // Clear identity key and signature for simple format
    memset(server_identity_key, 0, ctx->auth_public_key_size);
    memset(server_signature, 0, ctx->signature_size);

    // Server is not using client authentication in simple mode
    ctx->server_uses_client_auth = false;

    log_info("CLIENT: Received ephemeral key (simple format)");

    // SECURITY: For servers without identity keys, we implement a different security model:
    // 1. Verify IP address matches known_hosts entry
    // 2. Always require user confirmation (no silent connections)
    // 3. Store server fingerprint for future verification

    if (ctx->server_ip[0] == '\0' || ctx->server_port <= 0) {
      return SET_ERRNO(ERROR_CRYPTO, "Server IP or port not set, cannot check known_hosts");
    }

    log_debug("SECURITY_CHECK: Server has no identity key - implementing IP verification");

    // Check if this server was previously connected to (IP verification)
    int known_host_result;
    log_debug("SECURITY_CHECK: server_ip='%s', server_port=%u", ctx->server_ip, ctx->server_port);
    const char *env_skip_known_hosts_checking = platform_getenv("ASCII_CHAT_INSECURE_NO_HOST_IDENTITY_CHECK");
    if (env_skip_known_hosts_checking && strcmp(env_skip_known_hosts_checking, "1") == 0) {
      log_warn("Skipping known_hosts checking. This is a security vulnerability.");
      known_host_result = -1;
    } else {
      known_host_result = check_known_host_no_identity(ctx->server_ip, ctx->server_port);
    }

    if (known_host_result == 1) {
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
          buffer_pool_free(payload, payload_len);
        }
        return SET_ERRNO(ERROR_CRYPTO, "User declined to connect to unknown server without identity key");
      }

      // User accepted - add to known_hosts as no-identity entry
      // For servers without identity keys, pass zero key to indicate no-identity
      uint8_t zero_key[ZERO_KEY_SIZE] = {0};
      log_debug("SECURITY_DEBUG: Adding server to known_hosts with zero key for no-identity entry");
      if (add_known_host(ctx->server_ip, ctx->server_port, zero_key) != ASCIICHAT_OK) {
        if (payload) {
          buffer_pool_free(payload, payload_len);
        }
        return SET_ERRNO(ERROR_CONFIG,
                         "CRITICAL SECURITY ERROR: Failed to create known_hosts "
                         "file! This is a security vulnerability - the "
                         "program cannot track known hosts. Please check file "
                         "permissions and ensure the program can write to: %s",
                         get_known_hosts_path());
      }
      log_info("Server host added to known_hosts successfully");
    } else if (known_host_result == ERROR_CRYPTO_VERIFICATION) {
      // Server previously had identity key but now has none - potential security issue
      log_warn("SECURITY: Server previously had identity key but now has none - potential security issue");
      if (payload) {
        buffer_pool_free(payload, payload_len);
      }
      return SET_ERRNO(ERROR_CRYPTO_VERIFICATION, "Server key configuration changed - potential security issue");
    } else if (known_host_result != -1) {
      // Error checking known_hosts
      if (payload) {
        buffer_pool_free(payload, payload_len);
      }
      return SET_ERRNO(ERROR_CRYPTO, "Failed to verify server IP address");
    }
  } else {
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL,
                     "Invalid KEY_EXCHANGE_INIT size: %zu bytes (expected %zu or "
                     "%zu). This indicates: Protocol violation "
                     "or incompatible server version, Potential man-in-the-middle "
                     "attack, Network corruption",
                     payload_len, expected_auth_size, ctx->kex_public_key_size);
    // retry
  }

  // Set peer's public key (EPHEMERAL X25519) - this also derives the shared secret
  crypto_result_t crypto_result = crypto_set_peer_public_key(&ctx->crypto_ctx, server_ephemeral_key);
  if (payload) {
    buffer_pool_free(payload, payload_len);
  }
  if (crypto_result != CRYPTO_OK) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to set peer public key and derive shared secret: %s",
                     crypto_result_to_string(crypto_result));
  }

  // Determine if client has an identity key
  bool client_has_identity_key = (ctx->client_private_key.type == KEY_TYPE_ED25519);

  log_debug("CLIENT_KEY_EXCHANGE: client_private_key.type=%d, KEY_TYPE_ED25519=%d, client_has_identity_key=%d",
            ctx->client_private_key.type, KEY_TYPE_ED25519, client_has_identity_key);

  // Send authenticated response if server has identity key (auth_public_key_size > 0)
  // OR if server requires client authentication (require_client_auth)
  // Note: server_uses_client_auth is set when server has identity key, but we should
  // send authenticated response when server has identity key regardless of client auth requirement
  bool server_has_identity = (ctx->auth_public_key_size > 0 && ctx->signature_size > 0);
  bool server_requires_auth = server_has_identity || ctx->require_client_auth;

  log_debug("CLIENT_KEY_EXCHANGE: server_requires_auth=%d, auth_key_size=%u, sig_size=%u, server_uses_client_auth=%d, "
            "require_client_auth=%d",
            server_requires_auth, ctx->auth_public_key_size, ctx->signature_size, ctx->server_uses_client_auth,
            ctx->require_client_auth);

  if (server_requires_auth) {
    // Send authenticated packet:
    // [ephemeral:kex_size][identity:auth_size][signature:sig_size]
    // Use Ed25519 sizes since client has Ed25519 key
    size_t ed25519_pubkey_size = ED25519_PUBLIC_KEY_SIZE; // Ed25519 public key is always 32 bytes
    size_t ed25519_sig_size = ED25519_SIGNATURE_SIZE;     // Ed25519 signature is always 64 bytes
    size_t response_size = ctx->kex_public_key_size + ed25519_pubkey_size + ed25519_sig_size;

    uint8_t *key_response = SAFE_MALLOC(response_size, uint8_t *);
    memcpy(key_response, ctx->crypto_ctx.public_key,
           ctx->kex_public_key_size); // X25519 ephemeral for encryption

    if (client_has_identity_key) {
      // Client has identity key - send it with signature
      memcpy(key_response + ctx->kex_public_key_size, ctx->client_private_key.public_key,
             ed25519_pubkey_size); // Ed25519 identity

      // Sign ephemeral key with client identity key
      if (ed25519_sign_message(&ctx->client_private_key, ctx->crypto_ctx.public_key, ctx->kex_public_key_size,
                               key_response + ctx->kex_public_key_size + ed25519_pubkey_size) != 0) {
        SAFE_FREE(key_response);
        return SET_ERRNO(ERROR_CRYPTO, "Failed to sign client ephemeral key");
      }
    } else {
      // Client has no identity key - send null identity and null signature
      memset(key_response + ctx->kex_public_key_size, 0, ed25519_pubkey_size);                    // Null identity
      memset(key_response + ctx->kex_public_key_size + ed25519_pubkey_size, 0, ed25519_sig_size); // Null signature
    }

    log_debug("Sending KEY_EXCHANGE_RESPONSE packet with X25519 + Ed25519 + "
              "signature (%zu bytes)",
              response_size);
    result = send_packet(client_socket, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP, key_response, response_size);
    if (result != 0) {
      SAFE_FREE(key_response);
      return SET_ERRNO(ERROR_NETWORK, "Failed to send KEY_EXCHANGE_RESPONSE packet");
    }

    // Zero out the buffer before freeing
    sodium_memzero(key_response, response_size);
    SAFE_FREE(key_response);
  } else {
    // Send X25519 encryption key only to server (no identity key)
    // Format: [X25519 pubkey (kex_size)] = kex_size bytes total
    log_debug("Sending KEY_EXCHANGE_RESPONSE packet with X25519 key only (%u bytes)", ctx->kex_public_key_size);
    result = send_packet(client_socket, PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP, ctx->crypto_ctx.public_key,
                         ctx->kex_public_key_size);
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send KEY_EXCHANGE_RESPONSE packet");
    }
  }

  ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;

  return ASCIICHAT_OK;
}

// Server: Process client's public key and send auth challenge
asciichat_error_t crypto_handshake_server_auth_challenge(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
  }

  // Receive client's KEY_EXCHANGE_RESPONSE packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive KEY_EXCHANGE_RESPONSE packet");
  }

  // Check if client sent NO_ENCRYPTION response
  if (packet_type == PACKET_TYPE_CRYPTO_NO_ENCRYPTION) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
      payload = NULL; // Prevent double-free
    }

    // Send AUTH_FAILED to inform client (though they already know)
    auth_failure_packet_t failure = {0};
    failure.reason_flags = 0; // No specific auth failure, just encryption mismatch
    int result = send_packet(client_socket, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure));
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_FAILED packet");
    }

    return SET_ERRNO(ERROR_CRYPTO, "SECURITY: Client sent NO_ENCRYPTION response - encryption mode "
                                   "mismatch. Server requires encryption, but "
                                   "client has --no-encrypt. Use matching encryption settings on "
                                   "both client and server");
  }

  // Verify packet type
  if (packet_type != PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
      payload = NULL; // Prevent double-free
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected KEY_EXCHANGE_RESPONSE, got packet type %d", packet_type);
  }

  // Verify payload size - client can send either simple or authenticated format
  // Simple: kex_public_key_size bytes
  // Authenticated: kex_public_key_size + client_auth_key_size + client_sig_size bytes
  size_t simple_size = ctx->kex_public_key_size;
  size_t ed25519_auth_size = ED25519_PUBLIC_KEY_SIZE; // Ed25519 public key is always 32 bytes
  size_t ed25519_sig_size = ED25519_SIGNATURE_SIZE;   // Ed25519 signature is always 64 bytes
  size_t authenticated_size = ctx->kex_public_key_size + ed25519_auth_size + ed25519_sig_size;

  bool client_sent_identity = false;
  uint8_t *client_ephemeral_key = SAFE_MALLOC(ctx->kex_public_key_size, uint8_t *);
  uint8_t *client_identity_key = SAFE_MALLOC(ed25519_auth_size, uint8_t *);
  uint8_t *client_signature = SAFE_MALLOC(ed25519_sig_size, uint8_t *);

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
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    SAFE_FREE(client_ephemeral_key);
    SAFE_FREE(client_identity_key);
    SAFE_FREE(client_signature);
    return validation_result;
  }

  // Handle both authenticated and non-authenticated responses
  if (payload_len == authenticated_size) {
    // Authenticated format:
    // [ephemeral:kex_size][identity:auth_size][signature:sig_size]
    log_debug("Client sent authenticated response (%zu bytes)", authenticated_size);
    memcpy(client_ephemeral_key, payload, ctx->kex_public_key_size);
    memcpy(client_identity_key, payload + ctx->kex_public_key_size, ed25519_auth_size);
    memcpy(client_signature, payload + ctx->kex_public_key_size + ed25519_auth_size, ed25519_sig_size);
    client_sent_identity = true;
    ctx->client_sent_identity = true;

    // Check if client sent a null identity key (all zeros)
    bool client_has_null_identity = true;
    for (size_t i = 0; i < ed25519_auth_size; i++) {
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
      // Client has a real identity key - verify signature
      log_debug("Verifying client's signature over ephemeral key");
      if (ed25519_verify_signature(client_identity_key, client_ephemeral_key, ctx->kex_public_key_size,
                                   client_signature) != 0) {
        if (payload) {
          buffer_pool_free(payload, payload_len);
        }

        // Send AUTH_FAILED with specific reason
        auth_failure_packet_t failure = {0};
        failure.reason_flags = AUTH_FAIL_SIGNATURE_INVALID;
        int result = send_packet(client_socket, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure));
        if (result != 0) {
          return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_FAILED packet");
        }

        return SET_ERRNO(ERROR_CRYPTO, "Client signature verification FAILED - rejecting connection");
      }
    }

    // Store the verified client identity for whitelist checking (only if client has identity)
    if (client_sent_identity) {
      ctx->client_ed25519_key.type = KEY_TYPE_ED25519;
      memcpy(ctx->client_ed25519_key.key, client_identity_key, ctx->crypto_ctx.public_key_size);
    }
  } else if (ctx->auth_public_key_size == 0 && ctx->signature_size == 0 && payload_len == ctx->kex_public_key_size) {
    // Non-authenticated format: [ephemeral:kex_size] only
    log_debug("Client sent non-authenticated response (%zu bytes)", payload_len);
    memcpy(client_ephemeral_key, payload, ctx->kex_public_key_size);
    client_sent_identity = false;
    ctx->client_sent_identity = false;
    log_warn("Client connected without identity authentication");
  } else {
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
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

        log_info("Client Ed25519 key authorized (whitelist entry %zu)", i);
        if (strlen(ctx->client_whitelist[i].comment) > 0) {
          log_info("Client identity: %s", ctx->client_whitelist[i].comment);
        }
        break;
      }
    }

    if (!key_found) {
      SET_ERRNO(ERROR_CRYPTO_AUTH, "Client Ed25519 key not in whitelist - rejecting connection");
      if (payload) {
        buffer_pool_free(payload, payload_len);
        payload = NULL; // Prevent double-free
      }
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

  if (payload) {
    buffer_pool_free(payload, payload_len);
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

    // Prepare AUTH_CHALLENGE packet: 1 byte flags + AUTH_CHALLENGE_SIZE byte nonce
    uint8_t challenge_packet[AUTH_CHALLENGE_PACKET_SIZE];
    uint8_t auth_flags = 0;

    // Set flags based on server requirements
    if (ctx->crypto_ctx.has_password) {
      auth_flags |= AUTH_REQUIRE_PASSWORD;
    }
    if (ctx->require_client_auth) {
      auth_flags |= AUTH_REQUIRE_CLIENT_KEY;
    }

    challenge_packet[0] = auth_flags;
    memcpy(challenge_packet + 1, ctx->crypto_ctx.auth_nonce, AUTH_CHALLENGE_SIZE);

    // Send AUTH_CHALLENGE with flags + nonce (AUTH_CHALLENGE_PACKET_SIZE bytes)
    result =
        send_packet(client_socket, PACKET_TYPE_CRYPTO_AUTH_CHALLENGE, challenge_packet, AUTH_CHALLENGE_PACKET_SIZE);
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_CHALLENGE packet");
    }

    ctx->state = CRYPTO_HANDSHAKE_AUTHENTICATING;
  } else {
    // No authentication needed - skip to completion
    log_debug("Skipping authentication (no password and client has no identity key)");

    // Send HANDSHAKE_COMPLETE immediately
    result = send_packet(client_socket, PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE, NULL, 0);
    if (result != 0) {
      return SET_ERRNO(ERROR_NETWORK, "Failed to send HANDSHAKE_COMPLETE packet");
    }

    ctx->state = CRYPTO_HANDSHAKE_READY;
    log_info("Crypto handshake completed successfully (no authentication)");
  }

  return ASCIICHAT_OK;
}

// Helper: Send password-based authentication response with mutual auth
static asciichat_error_t send_password_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket,
                                                     const uint8_t nonce[AUTH_CHALLENGE_SIZE],
                                                     const char *auth_context) {
  // Compute HMAC bound to shared_secret (MITM protection)
  uint8_t hmac_response[AUTH_HMAC_SIZE];
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

  // Combine HMAC + client nonce (AUTH_HMAC_SIZE + AUTH_CHALLENGE_SIZE = AUTH_RESPONSE_PASSWORD_SIZE bytes)
  uint8_t auth_packet[AUTH_RESPONSE_PASSWORD_SIZE];
  memcpy(auth_packet, hmac_response, AUTH_HMAC_SIZE);
  memcpy(auth_packet + AUTH_HMAC_SIZE, ctx->client_challenge_nonce, AUTH_CHALLENGE_SIZE);

  log_debug("Sending AUTH_RESPONSE packet with HMAC + client nonce (%d bytes) - %s", AUTH_RESPONSE_PASSWORD_SIZE,
            auth_context);
  int result = send_packet(client_socket, PACKET_TYPE_CRYPTO_AUTH_RESPONSE, auth_packet, sizeof(auth_packet));
  if (result != 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_RESPONSE packet");
  }

  return ASCIICHAT_OK;
}

// Helper: Send Ed25519 signature-based authentication response with mutual auth
static asciichat_error_t send_key_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket,
                                                const uint8_t nonce[AUTH_CHALLENGE_SIZE], const char *auth_context) {
  // Sign the challenge with our Ed25519 private key
  uint8_t signature[AUTH_SIGNATURE_SIZE];
  int sign_result = ed25519_sign_message(&ctx->client_private_key, nonce, AUTH_CHALLENGE_SIZE, signature);
  if (sign_result != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to sign challenge with Ed25519 key");
  }

  // Generate client challenge nonce for mutual authentication
  crypto_result_t crypto_result = crypto_generate_nonce(ctx->client_challenge_nonce);
  if (crypto_result != CRYPTO_OK) {
    sodium_memzero(signature, sizeof(signature));
    return SET_ERRNO(ERROR_CRYPTO, "Failed to generate client challenge nonce: %s",
                     crypto_result_to_string(crypto_result));
  }

  // Combine signature + client nonce (AUTH_SIGNATURE_SIZE + AUTH_CHALLENGE_SIZE = AUTH_RESPONSE_SIGNATURE_SIZE bytes)
  uint8_t auth_packet[AUTH_RESPONSE_SIGNATURE_SIZE];
  memcpy(auth_packet, signature, AUTH_SIGNATURE_SIZE);
  memcpy(auth_packet + AUTH_SIGNATURE_SIZE, ctx->client_challenge_nonce, AUTH_CHALLENGE_SIZE);
  sodium_memzero(signature, sizeof(signature));

  log_debug("Sending AUTH_RESPONSE packet with Ed25519 signature + client "
            "nonce (%d bytes) - %s",
            AUTH_RESPONSE_SIGNATURE_SIZE, auth_context);
  int result = send_packet(client_socket, PACKET_TYPE_CRYPTO_AUTH_RESPONSE, auth_packet, sizeof(auth_packet));
  if (result != 0) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to send AUTH_RESPONSE packet");
  }

  return ASCIICHAT_OK;
}

// Client: Process auth challenge and send response
asciichat_error_t crypto_handshake_client_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
  }

  // Receive AUTH_CHALLENGE or HANDSHAKE_COMPLETE packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive packet from server");
  }

  // If server sent HANDSHAKE_COMPLETE, authentication was skipped (client has
  // no key)
  if (packet_type == PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    ctx->state = CRYPTO_HANDSHAKE_READY;
    log_info("Crypto handshake completed successfully (no authentication required)");
    return ASCIICHAT_OK;
  }

  // If server sent AUTH_FAILED, client is not authorized
  if (packet_type == PACKET_TYPE_CRYPTO_AUTH_FAILED) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return SET_ERRNO(ERROR_CRYPTO, "Server rejected authentication - client key not authorized");
  }

  // Otherwise, verify packet type is AUTH_CHALLENGE
  if (packet_type != PACKET_TYPE_CRYPTO_AUTH_CHALLENGE) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
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
      buffer_pool_free(payload, payload_len);
    }
    return validation_result;
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
      if (payload) {
        buffer_pool_free(payload, payload_len);
      }
      return SET_ERRNO(ERROR_CRYPTO, "Server requires both password and client key authentication. Please "
                                     "provide --password and --key to authenticate");
    }
    // Prompt for password interactively
    char prompted_password[PASSWORD_BUFFER_SIZE];
    if (platform_prompt_password("Server password required - please enter password:", prompted_password,
                                 sizeof(prompted_password)) != 0) {
      if (payload) {
        buffer_pool_free(payload, payload_len);
      }
      return SET_ERRNO(ERROR_CRYPTO, "Failed to read password");
    }

    // Derive password key from prompted password
    log_debug("Deriving key from prompted password");
    crypto_result_t crypto_result = crypto_derive_password_key(&ctx->crypto_ctx, prompted_password);
    sodium_memzero(prompted_password, sizeof(prompted_password));

    if (crypto_result != CRYPTO_OK) {
      if (payload) {
        buffer_pool_free(payload, payload_len);
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
  // 1. If server requires password → MUST send HMAC (AUTH_HMAC_SIZE bytes), error if no
  // password
  // 2. Else if server requires identity (whitelist) → MUST send Ed25519
  // signature (AUTH_SIGNATURE_SIZE bytes), error if no key
  // 3. Else if client has password → send HMAC (optional password auth)
  // 4. Else if client has SSH key → send Ed25519 signature (optional identity
  // proof)
  // 5. Else → no authentication available

  // Clean up payload before any early returns
  if (payload) {
    buffer_pool_free(payload, payload_len);
  }

  if (password_required) {
    // Server requires password - HIGHEST PRIORITY
    // (Identity was already verified in KEY_EXCHANGE phase if whitelist is
    // enabled)
    if (!has_password) {
      return SET_ERRNO(ERROR_CRYPTO, "Server requires password authentication\n"
                                     "Please provide --password for this server");
    }

    result = send_password_auth_response(ctx, client_socket, nonce, "required password");
    if (result != 0) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send password auth response");
      return result;
    }
  } else if (client_key_required) {
    // Server requires client key (whitelist) - SECOND PRIORITY
    if (!has_client_key) {
      return SET_ERRNO(ERROR_CRYPTO, "Server requires client key authentication (whitelist)\n"
                                     "Please provide --key with your authorized Ed25519 key");
    }

    result = send_key_auth_response(ctx, client_socket, nonce, "required client key");
    if (result != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send key auth response");
      return result;
    }
  } else if (has_password) {
    // No server requirements, but client has password → send HMAC + client
    // nonce (optional)
    result = send_password_auth_response(ctx, client_socket, nonce, "optional password");
    if (result != ASCIICHAT_OK) {
      SET_ERRNO(ERROR_NETWORK, "Failed to send password auth response");
      return result;
    }
  } else if (has_client_key) {
    // No server requirements, but client has SSH key → send Ed25519 signature +
    // client nonce (optional)
    result = send_key_auth_response(ctx, client_socket, nonce, "optional identity");
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
asciichat_error_t crypto_handshake_client_complete(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_AUTHENTICATING) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
    return ERROR_INVALID_STATE;
  }

  // Receive HANDSHAKE_COMPLETE or AUTH_FAILED packet
  packet_type_t packet_type;
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_NETWORK, "Failed to receive handshake completion packet");
    return ERROR_NETWORK;
  }

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
      buffer_pool_free(payload, payload_len);
    }
    return SET_ERRNO(ERROR_CRYPTO_AUTH,
                     "Server authentication failed - incorrect HMAC"); // Special code for
                                                                       // auth failure - do
                                                                       // not retry
  }

  if (packet_type != PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected SERVER_AUTH_RESPONSE or AUTH_FAILED, got packet type %d",
                     packet_type);
  }

  // Verify server's HMAC for mutual authentication
  if (payload_len != SERVER_AUTH_RESPONSE_SIZE) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Invalid SERVER_AUTH_RESPONSE size: %zu bytes (expected %d)", payload_len,
                     SERVER_AUTH_RESPONSE_SIZE);
  }

  // Verify server's HMAC (binds to DH shared_secret to prevent MITM)
  if (!crypto_verify_auth_response(&ctx->crypto_ctx, ctx->client_challenge_nonce, payload)) {
    SET_ERRNO(ERROR_CRYPTO_AUTH, "SECURITY: Server authentication failed - incorrect HMAC");
    SET_ERRNO(ERROR_CRYPTO_AUTH, "This may indicate a man-in-the-middle attack!");
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return SET_ERRNO(ERROR_CRYPTO_AUTH,
                     "Server authentication failed - incorrect HMAC"); // Authentication
                                                                       // failure - do not
                                                                       // retry
  }

  if (payload) {
    buffer_pool_free(payload, payload_len);
  }

  ctx->state = CRYPTO_HANDSHAKE_READY;
  log_info("Server authentication successful - mutual authentication complete");

  return ASCIICHAT_OK;
}

// Server: Process auth response and complete handshake
asciichat_error_t crypto_handshake_server_complete(crypto_handshake_context_t *ctx, socket_t client_socket) {
  if (!ctx || ctx->state != CRYPTO_HANDSHAKE_AUTHENTICATING) {
    return SET_ERRNO(ERROR_INVALID_STATE, "Invalid state: ctx=%p, state=%d", ctx, ctx ? ctx->state : -1);
  }

  // Receive AUTH_RESPONSE packet
  packet_type_t packet_type = 0; // Initialize to 0 to detect connection closure
  uint8_t *payload = NULL;
  size_t payload_len = 0;
  int result = receive_packet(client_socket, &packet_type, (void **)&payload, &payload_len);
  if (result != ASCIICHAT_OK) {
    return SET_ERRNO(ERROR_NETWORK, "Failed to receive AUTH_RESPONSE packet");
  }

  // Check if client disconnected (connection closed)
  log_debug("DEBUG: packet_type=%d, payload_len=%zu, payload=%p", packet_type, payload_len, payload);
  if (payload_len == 0 && payload == NULL && packet_type == 0) {
    return SET_ERRNO(ERROR_NETWORK, "Client disconnected during authentication");
  }

  // Verify packet type
  if (packet_type != PACKET_TYPE_CRYPTO_AUTH_RESPONSE) {
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return SET_ERRNO(ERROR_NETWORK_PROTOCOL, "Expected AUTH_RESPONSE, got packet type %d", packet_type);
  }

  // Verify password if required
  if (ctx->crypto_ctx.has_password) {
    // Validate packet size using session parameters
    asciichat_error_t validation_result =
        crypto_handshake_validate_packet_size(ctx, PACKET_TYPE_CRYPTO_AUTH_RESPONSE, payload_len);
    if (validation_result != ASCIICHAT_OK) {
      if (payload) {
        buffer_pool_free(payload, payload_len);
      }
      return validation_result;
    }

    // Verify password HMAC (binds to DH shared_secret to prevent MITM)
    if (!crypto_verify_auth_response(&ctx->crypto_ctx, ctx->crypto_ctx.auth_nonce, payload)) {
      SET_ERRNO(ERROR_CRYPTO, "Password authentication failed - incorrect password");
      if (payload) {
        buffer_pool_free(payload, payload_len);
      }

      // Send AUTH_FAILED with specific reason
      auth_failure_packet_t failure = {0};
      failure.reason_flags = AUTH_FAIL_PASSWORD_INCORRECT;
      if (ctx->require_client_auth) {
        failure.reason_flags |= AUTH_FAIL_CLIENT_KEY_REQUIRED;
      }
      send_packet(client_socket, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure));
      return ERROR_NETWORK;
    }

    // Extract client challenge nonce for mutual authentication
    memcpy(ctx->client_challenge_nonce, payload + AUTH_HMAC_SIZE, AUTH_CHALLENGE_SIZE);
    log_info("Password authentication successful");
  } else {
    // Ed25519 signature auth (payload is signature(AUTH_SIGNATURE_SIZE) + client_nonce(AUTH_CHALLENGE_SIZE) =
    // AUTH_RESPONSE_SIGNATURE_SIZE bytes) Note: Ed25519 verification happens during key exchange, not here Just extract
    // the client nonce from the payload
    if (payload_len == AUTH_RESPONSE_SIGNATURE_SIZE) {
      // Signature + client nonce
      memcpy(ctx->client_challenge_nonce, payload + AUTH_SIGNATURE_SIZE, AUTH_CHALLENGE_SIZE);
    } else if (payload_len == AUTH_RESPONSE_PASSWORD_SIZE) {
      // Just client nonce (legacy or password-only mode without password
      // enabled)
      memcpy(ctx->client_challenge_nonce, payload + AUTH_HMAC_SIZE, AUTH_CHALLENGE_SIZE);
    } else {
      // Validate packet size using session parameters
      asciichat_error_t validation_result =
          crypto_handshake_validate_packet_size(ctx, PACKET_TYPE_CRYPTO_AUTH_RESPONSE, payload_len);
      if (validation_result != ASCIICHAT_OK) {
        if (payload) {
          buffer_pool_free(payload, payload_len);
        }
        return validation_result;
      }
    }
  }

  // Verify client key if required (whitelist)
  if (ctx->require_client_auth) {
    if (!ctx->client_ed25519_key_verified) {
      if (payload) {
        buffer_pool_free(payload, payload_len);
      }

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
      send_packet(client_socket, PACKET_TYPE_CRYPTO_AUTH_FAILED, &failure, sizeof(failure));
      return ERROR_NETWORK;
    }
    log_info("Client key authentication successful (whitelist verified)");
    if (strlen(ctx->client_ed25519_key.comment) > 0) {
      log_info("Authenticated client: %s", ctx->client_ed25519_key.comment);
    }
  }

  if (payload) {
    buffer_pool_free(payload, payload_len);
  }

  // Send SERVER_AUTH_RESPONSE with server's HMAC for mutual authentication
  // Bind to DH shared_secret to prevent MITM (even if attacker knows password)
  uint8_t server_hmac[AUTH_HMAC_SIZE];
  crypto_result_t crypto_result =
      crypto_compute_auth_response(&ctx->crypto_ctx, ctx->client_challenge_nonce, server_hmac);
  if (crypto_result != CRYPTO_OK) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to compute server HMAC for mutual authentication: %s",
              crypto_result_to_string(crypto_result));
    return ERROR_NETWORK;
  }

  log_debug("Sending SERVER_AUTH_RESPONSE packet with server HMAC (%d bytes) "
            "for mutual authentication",
            AUTH_HMAC_SIZE);
  result = send_packet(client_socket, PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP, server_hmac, sizeof(server_hmac));
  if (result != ASCIICHAT_OK) {
    SET_ERRNO(ERROR_NETWORK, "Failed to send SERVER_AUTH_RESPONSE packet");
    return ERROR_NETWORK;
  }

  ctx->state = CRYPTO_HANDSHAKE_READY;
  log_info("Crypto handshake completed successfully (mutual authentication)");

  return ASCIICHAT_OK;
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
