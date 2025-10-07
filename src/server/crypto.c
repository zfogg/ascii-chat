/**
 * @file crypto.c
 * @brief Server-side crypto handshake and encryption management
 *
 * This module handles the server-side crypto handshake process and provides
 * encryption/decryption functions for secure communication with clients.
 * Each client has its own crypto context for secure communication.
 */

#include "crypto.h"
#include "client.h"
#include "crypto/handshake.h"
#include "crypto/keys.h"
#include "options.h"

#include <string.h>
#include <stdio.h>
#include <sodium.h>

// External references to global server crypto state
extern bool g_server_encryption_enabled;
extern private_key_t g_server_private_key;

// Per-client crypto contexts are now stored in client_info_t structure
// No global crypto context needed

/**
 * Initialize server crypto system (global initialization)
 *
 * @return 0 on success, -1 on failure
 */
int server_crypto_init(void) {
  // Check if encryption is disabled
  if (opt_no_encrypt) {
    log_info("Encryption disabled via --no-encrypt");
    return 0;
  }

  log_info("Server crypto system initialized (per-client contexts will be created on demand)");
  return 0;
}

/**
 * Perform crypto handshake with client
 *
 * @param client_socket Connected socket to client
 * @return 0 on success, -1 on failure
 */
int server_crypto_handshake(socket_t client_socket) {
  if (opt_no_encrypt) {
    log_debug("Crypto handshake skipped (disabled)");
    return 0;
  }

  // Find the client by socket to get their crypto context
  client_info_t *client = find_client_by_socket(client_socket);
  if (!client) {
    log_error("Client not found for crypto handshake");
    return -1;
  }

  // Initialize crypto context for this specific client
  int init_result;
  if (g_server_encryption_enabled) {
    // Server has SSH key - use standard initialization
    log_debug("SERVER_CRYPTO_HANDSHAKE: Using SSH key authentication");
    init_result = crypto_handshake_init(&client->crypto_handshake_ctx, true); // true = server
  } else if (strlen(opt_encrypt_key) > 0 && strncmp(opt_encrypt_key, "gpg:", 4) != 0) {
    // It's a password - use password-based initialization
    log_debug("SERVER_CRYPTO_HANDSHAKE: Using password authentication");
    init_result =
        crypto_handshake_init_with_password(&client->crypto_handshake_ctx, true, opt_encrypt_key); // true = server
  } else {
    // No password or SSH key - use standard initialization with random keys
    log_debug("SERVER_CRYPTO_HANDSHAKE: Using standard initialization");
    init_result = crypto_handshake_init(&client->crypto_handshake_ctx, true); // true = server
  }

  if (init_result != 0) {
    log_error("Failed to initialize crypto handshake for client %u", atomic_load(&client->client_id));
    return -1;
  }
  client->crypto_initialized = true;

  // Set up server keys in the handshake context
  if (g_server_encryption_enabled) {
    // Copy server private key to handshake context
    memcpy(&client->crypto_handshake_ctx.server_private_key, &g_server_private_key, sizeof(private_key_t));

    // Set up the core crypto context with server's private key
    memcpy(client->crypto_handshake_ctx.crypto_ctx.private_key, g_server_private_key.key.x25519, 32);
    // Generate corresponding public key
    crypto_scalarmult_base(client->crypto_handshake_ctx.crypto_ctx.public_key,
                           client->crypto_handshake_ctx.crypto_ctx.private_key);

    log_debug("Set up server keys for client %u", atomic_load(&client->client_id));
  }

  // Set up client whitelist if specified
  extern public_key_t g_client_whitelist[];
  extern size_t g_num_whitelisted_clients;
  if (g_num_whitelisted_clients > 0) {
    client->crypto_handshake_ctx.require_client_auth = true;
    client->crypto_handshake_ctx.client_whitelist = g_client_whitelist;
    client->crypto_handshake_ctx.num_whitelisted_clients = g_num_whitelisted_clients;
    log_debug("Client whitelist enabled: %zu authorized keys", g_num_whitelisted_clients);
  }

  log_info("Starting crypto handshake with client %u...", atomic_load(&client->client_id));

  // Step 1: Send our public key to client
  int result = crypto_handshake_server_start(&client->crypto_handshake_ctx, client_socket);
  if (result != 0) {
    log_error("Failed to send server public key to client %u", atomic_load(&client->client_id));
    return -1;
  }

  // Step 2: Receive client's public key and send auth challenge
  result = crypto_handshake_server_auth_challenge(&client->crypto_handshake_ctx, client_socket);
  if (result != 0) {
    log_error("Crypto authentication challenge failed for client %u", atomic_load(&client->client_id));
    return -1;
  }

  // Check if handshake completed during auth challenge (no authentication needed)
  if (client->crypto_handshake_ctx.state == CRYPTO_HANDSHAKE_READY) {
    log_info("Crypto handshake completed successfully for client %u (no authentication)",
             atomic_load(&client->client_id));
    return 0;
  }

  // Step 3: Receive auth response and complete handshake
  result = crypto_handshake_server_complete(&client->crypto_handshake_ctx, client_socket);
  if (result != 0) {
    log_error("Crypto authentication response failed for client %u", atomic_load(&client->client_id));
    return -1;
  }

  log_info("Crypto handshake completed successfully for client %u", atomic_load(&client->client_id));
  return 0;
}

/**
 * Check if crypto handshake is ready for a specific client
 *
 * @param client_id Client ID to check
 * @return true if encryption is ready, false otherwise
 */
bool crypto_server_is_ready(uint32_t client_id) {
  if (opt_no_encrypt) {
    log_debug("Crypto disabled by --no-encrypt flag");
    return false;
  }

  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    log_debug("Client %u not found for crypto check", client_id);
    return false;
  }

  if (!client->crypto_initialized) {
    log_debug("Crypto not initialized for client %u", client_id);
    return false;
  }

  bool ready = crypto_handshake_is_ready(&client->crypto_handshake_ctx);
  log_debug("Crypto ready check for client %u: initialized=%d, ready=%d", client_id, client->crypto_initialized, ready);
  return ready;
}

/**
 * Get crypto context for encryption/decryption for a specific client
 *
 * @param client_id Client ID to get context for
 * @return crypto context or NULL if not ready
 */
const crypto_context_t *crypto_server_get_context(uint32_t client_id) {
  if (!crypto_server_is_ready(client_id)) {
    return NULL;
  }

  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return NULL;
  }

  return crypto_handshake_get_context(&client->crypto_handshake_ctx);
}

/**
 * Encrypt a packet for transmission to a specific client
 *
 * @param client_id Client ID to encrypt for
 * @param plaintext Plaintext data to encrypt
 * @param plaintext_len Length of plaintext data
 * @param ciphertext Output buffer for encrypted data
 * @param ciphertext_size Size of output buffer
 * @param ciphertext_len Output length of encrypted data
 * @return 0 on success, -1 on failure
 */
int crypto_server_encrypt_packet(uint32_t client_id, const uint8_t *plaintext, size_t plaintext_len,
                                 uint8_t *ciphertext, size_t ciphertext_size, size_t *ciphertext_len) {
  if (!crypto_server_is_ready(client_id)) {
    // No encryption - just copy data
    if (plaintext_len > ciphertext_size) {
      return -1;
    }
    memcpy(ciphertext, plaintext, plaintext_len);
    *ciphertext_len = plaintext_len;
    return 0;
  }

  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return -1;
  }

  return crypto_handshake_encrypt_packet(&client->crypto_handshake_ctx, plaintext, plaintext_len, ciphertext,
                                         ciphertext_size, ciphertext_len);
}

/**
 * Decrypt a received packet from a specific client
 *
 * @param client_id Client ID that sent the packet
 * @param ciphertext Encrypted data to decrypt
 * @param ciphertext_len Length of encrypted data
 * @param plaintext Output buffer for decrypted data
 * @param plaintext_size Size of output buffer
 * @param plaintext_len Output length of decrypted data
 * @return 0 on success, -1 on failure
 */
int crypto_server_decrypt_packet(uint32_t client_id, const uint8_t *ciphertext, size_t ciphertext_len,
                                 uint8_t *plaintext, size_t plaintext_size, size_t *plaintext_len) {
  if (!crypto_server_is_ready(client_id)) {
    // No encryption - just copy data
    if (ciphertext_len > plaintext_size) {
      return -1;
    }
    memcpy(plaintext, ciphertext, ciphertext_len);
    *plaintext_len = ciphertext_len;
    return 0;
  }

  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return -1;
  }

  return crypto_handshake_decrypt_packet(&client->crypto_handshake_ctx, ciphertext, ciphertext_len, plaintext,
                                         plaintext_size, plaintext_len);
}

/**
 * Cleanup crypto resources for a specific client
 *
 * @param client_id Client ID to cleanup crypto for
 */
void crypto_server_cleanup_client(uint32_t client_id) {
  client_info_t *client = find_client_by_id(client_id);
  if (!client) {
    return;
  }

  if (client->crypto_initialized) {
    crypto_handshake_cleanup(&client->crypto_handshake_ctx);
    client->crypto_initialized = false;
    log_debug("Crypto handshake cleaned up for client %u", client_id);
  }
}

/**
 * Cleanup crypto server resources (legacy function for compatibility)
 */
void crypto_server_cleanup(void) {
  // No global crypto context to cleanup anymore
  log_debug("Server crypto cleanup (per-client contexts managed individually)");
}
