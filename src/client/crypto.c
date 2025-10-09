/**
 * @file crypto_client.c
 * @brief Client-side crypto handshake integration
 *
 * This module integrates the crypto handshake into the client connection flow.
 * It handles the client-side crypto handshake after TCP connection is established
 * but before sending application data.
 */

#include "crypto.h"
#include "server.h"
#include "options.h"
#include "common.h"
#include "crypto/handshake.h"
#include "crypto/keys.h"
#include "crypto/known_hosts.h"
#include "buffer_pool.h"
#include "network.h"

#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sodium.h>

// Global crypto handshake context for this client connection
// NOTE: We use the crypto context from server.c to match the handshake
extern crypto_handshake_context_t g_crypto_ctx;
static bool g_crypto_initialized = false;

/**
 * Initialize client crypto handshake
 *
 * @return 0 on success, -1 on failure
 */
int client_crypto_init(void) {
  log_debug("CLIENT_CRYPTO_INIT: Starting crypto initialization");
  if (g_crypto_initialized) {
    log_debug("CLIENT_CRYPTO_INIT: Already initialized, cleaning up and reinitializing");
    crypto_handshake_cleanup(&g_crypto_ctx);
    g_crypto_initialized = false;
  }

  // Check if encryption is disabled
  if (opt_no_encrypt) {
    log_info("Encryption disabled via --no-encrypt");
    log_debug("CLIENT_CRYPTO_INIT: Encryption disabled, returning 0");
    return 0;
  }

  log_debug("CLIENT_CRYPTO_INIT: Initializing crypto handshake context");

  // Check if we have an SSH key, password, or neither
  int result;
  bool is_ssh_key = false;
  private_key_t private_key;

  // Load client private key if provided via --key
  if (strlen(opt_encrypt_key) > 0) {
    // --key supports file-based authentication (SSH keys, GPG keys via gpg:keyid)

    // For SSH key files (not gpg:keyid format), validate the file exists
    if (strncmp(opt_encrypt_key, "gpg:", 4) != 0) {
      if (validate_ssh_key_file(opt_encrypt_key) != 0) {
        return -1;
      }
    }

    // Parse key (handles SSH files and gpg:keyid format)
    log_debug("CLIENT_CRYPTO_INIT: Loading private key for authentication: %s", opt_encrypt_key);
    if (parse_private_key(opt_encrypt_key, &private_key) == ASCIICHAT_OK) {
      log_info("Successfully parsed SSH private key");
      is_ssh_key = true;
    } else {
      log_error("Failed to parse SSH key file: %s", opt_encrypt_key);
      log_error("This may be due to:");
      log_error("  - Wrong password for encrypted key");
      log_error("  - Unsupported key type (only Ed25519 is currently supported)");
      log_error("  - Corrupted key file");
      log_error("");
      log_error("Note: RSA and ECDSA keys are not yet supported");
      log_error("To generate an Ed25519 key: ssh-keygen -t ed25519");
      return -1;
    }
  }

  if (is_ssh_key) {
    // Use SSH private key for authentication
    log_debug("CLIENT_CRYPTO_INIT: Using SSH key for authentication");

    // Initialize crypto context (generates ephemeral X25519 keys)
    result = crypto_handshake_init(&g_crypto_ctx, false); // false = client
    if (result != 0) {
      log_error("Failed to initialize crypto handshake");
      return -1;
    }

    // Store the Ed25519 keys for authentication
    memcpy(&g_crypto_ctx.client_private_key, &private_key, sizeof(private_key_t));

    // Extract Ed25519 public key from private key
    g_crypto_ctx.client_public_key.type = KEY_TYPE_ED25519;
    memcpy(g_crypto_ctx.client_public_key.key, private_key.public_key, 32);
    SAFE_STRNCPY(g_crypto_ctx.client_public_key.comment, private_key.key_comment,
                 sizeof(g_crypto_ctx.client_public_key.comment) - 1);

    // Configure SSH key for handshake (shared logic between client and server)
    if (crypto_setup_ssh_key_for_handshake(&g_crypto_ctx, &private_key) != 0) {
      log_error("Failed to configure SSH key for handshake");
      return -1;
    }

    // Clear the temporary private_key variable (we've already copied it to g_crypto_ctx)
    sodium_memzero(&private_key, sizeof(private_key));

    // If password is also provided, derive password key for dual authentication
    if (strlen(opt_password) > 0) {
      log_debug("CLIENT_CRYPTO_INIT: Password also provided, deriving password key");
      crypto_result_t crypto_result = crypto_derive_password_key(&g_crypto_ctx.crypto_ctx, opt_password);
      if (crypto_result != CRYPTO_OK) {
        log_error("Failed to derive password key: %s", crypto_result_to_string(crypto_result));
        return -1;
      }
      g_crypto_ctx.crypto_ctx.has_password = true;
      log_info("Password authentication enabled alongside SSH key");
    }

  } else if (strlen(opt_password) > 0) {
    // Password provided - use password-based initialization
    log_debug("CLIENT_CRYPTO_INIT: Using password authentication");
    result = crypto_handshake_init_with_password(&g_crypto_ctx, false, opt_password); // false = client
    if (result != 0) {
      log_error("Failed to initialize crypto handshake with password");
      log_debug("CLIENT_CRYPTO_INIT: crypto_handshake_init_with_password failed with result=%d", result);
      return -1;
    }
  } else {
    // No password or SSH key - use standard initialization with random keys
    log_debug("CLIENT_CRYPTO_INIT: Using standard initialization");
    result = crypto_handshake_init(&g_crypto_ctx, false); // false = client
    if (result != 0) {
      log_error("Failed to initialize crypto handshake");
      log_debug("CLIENT_CRYPTO_INIT: crypto_handshake_init failed with result=%d", result);
      return -1;
    }
  }

  log_debug("CLIENT_CRYPTO_INIT: crypto_handshake_init succeeded");

  // Set up server connection info for known_hosts
  SAFE_STRNCPY(g_crypto_ctx.server_hostname, opt_address, sizeof(g_crypto_ctx.server_hostname) - 1);
  SAFE_STRNCPY(g_crypto_ctx.server_ip, server_connection_get_ip(), sizeof(g_crypto_ctx.server_ip) - 1);
  g_crypto_ctx.server_port = (uint16_t)strtoint_safe(opt_port);

  // Configure server key verification if specified
  if (strlen(opt_server_key) > 0) {
    g_crypto_ctx.verify_server_key = true;
    SAFE_STRNCPY(g_crypto_ctx.expected_server_key, opt_server_key, sizeof(g_crypto_ctx.expected_server_key) - 1);
    log_info("Server key verification enabled: %s", opt_server_key);
  }

  g_crypto_initialized = true;
  log_info("Client crypto handshake initialized");
  log_debug("CLIENT_CRYPTO_INIT: Initialization complete, g_crypto_initialized=true");
  return 0;
}

/**
 * Perform crypto handshake with server
 *
 * @param socket Connected socket to server
 * @return 0 on success, -1 on failure
 */
int client_crypto_handshake(socket_t socket) {
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Starting crypto handshake");
  log_debug("CLIENT_CRYPTO_HANDSHAKE: g_crypto_initialized=%d, opt_no_encrypt=%d", g_crypto_initialized,
            opt_no_encrypt);

  // If client has --no-encrypt, skip handshake entirely
  if (opt_no_encrypt) {
    log_debug("Client has --no-encrypt, skipping crypto handshake");
    return 0;
  }

  // If we reach here, crypto must be initialized for encryption
  if (!g_crypto_initialized) {
    log_error("Crypto not initialized but server requires encryption");
    log_error("Server requires encrypted connection but client has no encryption configured");
    log_error("Use --key to specify a client key or --password for password authentication");
    return CONNECTION_ERROR_AUTH_FAILED; // No retry - configuration error
  }

  log_info("Starting crypto handshake with server...");
  log_debug("CLIENT_CRYPTO: Starting crypto handshake with server...");

  // Step 0a: Send protocol version to server
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Sending protocol version");
  protocol_version_packet_t client_version = {0};
  client_version.protocol_version = htons(1);      // Protocol version 1
  client_version.protocol_revision = htons(0);     // Revision 0
  client_version.supports_encryption = 1;          // We support encryption
  client_version.compression_algorithms = 0;       // No compression for now
  client_version.compression_threshold = 0;
  client_version.feature_flags = 0;

  int result = send_protocol_version_packet(socket, &client_version);
  if (result != 0) {
    log_error("Failed to send protocol version to server");
    return -1;
  }
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Protocol version sent successfully");

  // Step 0b: Receive server's protocol version
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Receiving server protocol version");
  packet_type_t packet_type;
  void *payload = NULL;
  size_t payload_len = 0;

  result = receive_packet(socket, &packet_type, &payload, &payload_len);
  if (result != 1 || packet_type != PACKET_TYPE_PROTOCOL_VERSION) {
    log_error("Failed to receive server protocol version (got type %u)", packet_type);
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return -1;
  }

  if (payload_len != sizeof(protocol_version_packet_t)) {
    log_error("Invalid protocol version packet size: %zu, expected %zu", payload_len, sizeof(protocol_version_packet_t));
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  protocol_version_packet_t server_version;
  memcpy(&server_version, payload, sizeof(protocol_version_packet_t));
  buffer_pool_free(payload, payload_len);

  // Convert from network byte order
  uint16_t server_proto_version = ntohs(server_version.protocol_version);
  uint16_t server_proto_revision = ntohs(server_version.protocol_revision);

  log_info("Server protocol version: %u.%u (encryption: %s)",
           server_proto_version, server_proto_revision,
           server_version.supports_encryption ? "yes" : "no");

  if (!server_version.supports_encryption) {
    log_error("Server does not support encryption");
    return CONNECTION_ERROR_AUTH_FAILED;
  }

  // Step 0c: Send crypto capabilities to server
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Sending crypto capabilities");
  crypto_capabilities_packet_t client_caps = {0};
  client_caps.supported_kex_algorithms = htons(KEX_ALGO_X25519);
  client_caps.supported_auth_algorithms = htons(AUTH_ALGO_ED25519 | AUTH_ALGO_NONE);
  client_caps.supported_cipher_algorithms = htons(CIPHER_ALGO_XSALSA20_POLY1305);
  client_caps.requires_verification = 0;  // Client doesn't require server verification (uses known_hosts)
  client_caps.preferred_kex = KEX_ALGO_X25519;
  client_caps.preferred_auth = AUTH_ALGO_ED25519;
  client_caps.preferred_cipher = CIPHER_ALGO_XSALSA20_POLY1305;

  result = send_crypto_capabilities_packet(socket, &client_caps);
  if (result != 0) {
    log_error("Failed to send crypto capabilities to server");
    return -1;
  }
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Crypto capabilities sent successfully");

  // Step 0d: Receive server's crypto parameters
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Receiving server crypto parameters");
  payload = NULL;
  payload_len = 0;

  result = receive_packet(socket, &packet_type, &payload, &payload_len);
  if (result != 1 || packet_type != PACKET_TYPE_CRYPTO_PARAMETERS) {
    log_error("Failed to receive server crypto parameters (got type %u)", packet_type);
    if (payload) {
      buffer_pool_free(payload, payload_len);
    }
    return -1;
  }

  if (payload_len != sizeof(crypto_parameters_packet_t)) {
    log_error("Invalid crypto parameters packet size: %zu, expected %zu", payload_len, sizeof(crypto_parameters_packet_t));
    buffer_pool_free(payload, payload_len);
    return -1;
  }

  crypto_parameters_packet_t server_params;
  memcpy(&server_params, payload, sizeof(crypto_parameters_packet_t));
  buffer_pool_free(payload, payload_len);

  // Convert from network byte order
  uint16_t kex_pubkey_size = ntohs(server_params.kex_public_key_size);
  uint16_t signature_size = ntohs(server_params.signature_size);

  log_info("Server crypto parameters: KEX=%u, Auth=%u, Cipher=%u (key_size=%u, sig_size=%u)",
           server_params.selected_kex, server_params.selected_auth, server_params.selected_cipher,
           kex_pubkey_size, signature_size);

  // Validate that server chose algorithms we support
  if (server_params.selected_kex != KEX_ALGO_X25519) {
    log_error("Server selected unsupported KEX algorithm: %u", server_params.selected_kex);
    return CONNECTION_ERROR_AUTH_FAILED;
  }

  if (server_params.selected_cipher != CIPHER_ALGO_XSALSA20_POLY1305) {
    log_error("Server selected unsupported cipher algorithm: %u", server_params.selected_cipher);
    return CONNECTION_ERROR_AUTH_FAILED;
  }

  log_debug("CLIENT_CRYPTO_HANDSHAKE: Protocol negotiation completed successfully");

  // Step 1: Receive server's public key and send our public key
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Starting key exchange");
  result = crypto_handshake_client_key_exchange(&g_crypto_ctx, socket);
  if (result != 0) {
    log_error("Crypto key exchange failed");
    log_debug("CLIENT_CRYPTO_HANDSHAKE: Key exchange failed with result=%d", result);
    return result; // Propagate error code (-3 for host key failure, -2 for auth failure, -1 for other errors)
  }
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Key exchange completed successfully");

  // Check if server is using client authentication
  if (!g_crypto_ctx.server_uses_client_auth) {
    log_warn("Server is not using client verification keys");

    // Display warning to user
    fprintf(stderr, "\n");
    fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
    fprintf(stderr, "@  WARNING: SERVER NOT USING CLIENT AUTHENTICATION                              @\n");
    fprintf(stderr, "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "The server is not configured to verify client keys.\n");
    fprintf(stderr, "Your connection is encrypted, but the server cannot verify your identity.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "This may indicate:\n");
    fprintf(stderr, "  - Server is running without --client-keys\n");
    fprintf(stderr, "  - Server is accepting all clients without authentication\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "If you trust this server, you can continue. Otherwise, disconnect now.\n");
    fprintf(stderr, "\n");
    fflush(stderr);
  }

  // Step 2: Receive auth challenge and send response
  log_debug("CLIENT_CRYPTO: Sending auth response to server...");
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Starting auth response");
  result = crypto_handshake_client_auth_response(&g_crypto_ctx, socket);
  if (result != 0) {
    log_error("Crypto authentication failed");
    log_debug("CLIENT_CRYPTO_HANDSHAKE: Auth response failed with result=%d", result);
    return result; // Propagate error code (-2 for auth failure, -1 for other errors)
  }
  log_debug("CLIENT_CRYPTO: Auth response sent successfully");
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Auth response completed successfully");

  // Check if handshake completed during auth response (no authentication needed)
  if (g_crypto_ctx.state == CRYPTO_HANDSHAKE_READY) {
    log_info("Crypto handshake completed successfully (no authentication)");
    return 0;
  }

  // Step 3: Receive handshake complete message
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Waiting for handshake complete message");
  result = crypto_handshake_client_complete(&g_crypto_ctx, socket);
  if (result != 0) {
    log_error("Crypto handshake completion failed");
    log_debug("CLIENT_CRYPTO_HANDSHAKE: Handshake completion failed with result=%d", result);
    return result; // Propagate error code (-2 for auth failure, -1 for other errors)
  }

  log_info("Crypto handshake completed successfully");
  log_debug("CLIENT_CRYPTO_HANDSHAKE: Handshake completed successfully, state=%d", g_crypto_ctx.state);
  return 0;
}

/**
 * Check if crypto handshake is ready
 *
 * @return true if encryption is ready, false otherwise
 */
bool crypto_client_is_ready(void) {
  if (!g_crypto_initialized || opt_no_encrypt) {
    log_debug("CLIENT_CRYPTO_READY: Not ready - initialized=%d, no_encrypt=%d", g_crypto_initialized, opt_no_encrypt);
    return false;
  }

  bool ready = crypto_handshake_is_ready(&g_crypto_ctx);
  log_debug("CLIENT_CRYPTO_READY: handshake_ready=%d, state=%d", ready, g_crypto_ctx.state);
  return ready;
}

/**
 * Get crypto context for encryption/decryption
 *
 * @return crypto context or NULL if not ready
 */
const crypto_context_t *crypto_client_get_context(void) {
  if (!crypto_client_is_ready()) {
    return NULL;
  }

  return crypto_handshake_get_context(&g_crypto_ctx);
}

/**
 * Encrypt a packet for transmission
 *
 * @param plaintext Plaintext data to encrypt
 * @param plaintext_len Length of plaintext data
 * @param ciphertext Output buffer for encrypted data
 * @param ciphertext_size Size of output buffer
 * @param ciphertext_len Output length of encrypted data
 * @return 0 on success, -1 on failure
 */
int crypto_client_encrypt_packet(const uint8_t *plaintext, size_t plaintext_len, uint8_t *ciphertext,
                                 size_t ciphertext_size, size_t *ciphertext_len) {
  return crypto_encrypt_packet_or_passthrough(&g_crypto_ctx, crypto_client_is_ready(), plaintext, plaintext_len,
                                              ciphertext, ciphertext_size, ciphertext_len);
}

/**
 * Decrypt a received packet
 *
 * @param ciphertext Encrypted data to decrypt
 * @param ciphertext_len Length of encrypted data
 * @param plaintext Output buffer for decrypted data
 * @param plaintext_size Size of output buffer
 * @param plaintext_len Output length of decrypted data
 * @return 0 on success, -1 on failure
 */
int crypto_client_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext,
                                 size_t plaintext_size, size_t *plaintext_len) {
  return crypto_decrypt_packet_or_passthrough(&g_crypto_ctx, crypto_client_is_ready(), ciphertext, ciphertext_len,
                                              plaintext, plaintext_size, plaintext_len);
}

/**
 * Cleanup crypto client resources
 */
void crypto_client_cleanup(void) {
  if (g_crypto_initialized) {
    crypto_handshake_cleanup(&g_crypto_ctx);
    g_crypto_initialized = false;
    log_debug("Client crypto handshake cleaned up");
  }
}
