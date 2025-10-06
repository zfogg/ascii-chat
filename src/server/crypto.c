/**
 * @file crypto_server.c
 * @brief Server-side crypto handshake integration
 *
 * This module integrates the crypto handshake into the server client acceptance flow.
 * It handles the server-side crypto handshake after accepting a new client connection
 * but before processing application data.
 */

#include "crypto.h"
#include "client.h"
#include "options.h"
#include "common.h"
#include "crypto/handshake.h"
#include "crypto/keys.h"

#include <string.h>
#include <stdio.h>

// Global crypto handshake context for each client connection
// Note: In a real implementation, this would be per-client, not global
static crypto_handshake_context_t g_crypto_ctx = {0};
static bool g_crypto_initialized = false;

/**
 * Initialize server crypto handshake
 *
 * @return 0 on success, -1 on failure
 */
int server_crypto_init(void) {
    if (g_crypto_initialized) {
        return 0; // Already initialized
    }

    // Check if encryption is disabled
    if (opt_no_encrypt) {
        log_info("Encryption disabled via --no-encrypt");
        return 0;
    }

    // Initialize crypto handshake context
    int result = crypto_handshake_init(&g_crypto_ctx, true); // true = server
    if (result != 0) {
        log_error("Failed to initialize crypto handshake");
        return -1;
    }

    // Configure client authentication if specified
    if (strlen(opt_client_keys) > 0) {
        g_crypto_ctx.require_client_auth = true;
        SAFE_STRNCPY(g_crypto_ctx.client_keys_path, opt_client_keys, sizeof(g_crypto_ctx.client_keys_path) - 1);
        log_info("Client authentication enabled: %s", opt_client_keys);
    }

    g_crypto_initialized = true;
    log_info("Server crypto handshake initialized");
    return 0;
}

/**
 * Perform crypto handshake with client
 *
 * @param client_socket Connected socket to client
 * @return 0 on success, -1 on failure
 */
int server_crypto_handshake(socket_t client_socket) {
    if (!g_crypto_initialized || opt_no_encrypt) {
        log_debug("Crypto handshake skipped (disabled or not initialized)");
        return 0;
    }

    // Reset crypto context for each new client connection
    // TODO: In a proper implementation, each client should have its own context
    crypto_handshake_cleanup(&g_crypto_ctx);
    int init_result = crypto_handshake_init(&g_crypto_ctx, true); // true = server
    if (init_result != 0) {
        log_error("Failed to reinitialize crypto handshake");
        return -1;
    }

    log_info("Starting crypto handshake with client...");

    // Step 1: Send our public key to client
    int result = crypto_handshake_server_start(&g_crypto_ctx, client_socket);
    if (result != 0) {
        log_error("Failed to send server public key");
        return -1;
    }

    // Step 2: Receive client's public key and send auth challenge
    result = crypto_handshake_server_auth_challenge(&g_crypto_ctx, client_socket);
    if (result != 0) {
        log_error("Crypto authentication challenge failed");
        return -1;
    }

    // Step 3: Receive auth response and complete handshake
    result = crypto_handshake_server_complete(&g_crypto_ctx, client_socket);
    if (result != 0) {
        log_error("Crypto authentication response failed");
        return -1;
    }

    log_info("Crypto handshake completed successfully");
    return 0;
}

/**
 * Check if crypto handshake is ready
 *
 * @return true if encryption is ready, false otherwise
 */
bool crypto_server_is_ready(void) {
    if (!g_crypto_initialized || opt_no_encrypt) {
        return false;
    }

    return crypto_handshake_is_ready(&g_crypto_ctx);
}

/**
 * Get crypto context for encryption/decryption
 *
 * @return crypto context or NULL if not ready
 */
const crypto_context_t* crypto_server_get_context(void) {
    if (!crypto_server_is_ready()) {
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
int crypto_server_encrypt_packet(const uint8_t* plaintext, size_t plaintext_len,
                                uint8_t* ciphertext, size_t ciphertext_size,
                                size_t* ciphertext_len) {
    if (!crypto_server_is_ready()) {
        // No encryption - just copy data
        if (plaintext_len > ciphertext_size) {
            return -1;
        }
        memcpy(ciphertext, plaintext, plaintext_len);
        *ciphertext_len = plaintext_len;
        return 0;
    }

    return crypto_handshake_encrypt_packet(&g_crypto_ctx, plaintext, plaintext_len,
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
int crypto_server_decrypt_packet(const uint8_t* ciphertext, size_t ciphertext_len,
                                uint8_t* plaintext, size_t plaintext_size,
                                size_t* plaintext_len) {
    if (!crypto_server_is_ready()) {
        // No encryption - just copy data
        if (ciphertext_len > plaintext_size) {
            return -1;
        }
        memcpy(plaintext, ciphertext, ciphertext_len);
        *plaintext_len = ciphertext_len;
        return 0;
    }

    return crypto_handshake_decrypt_packet(&g_crypto_ctx, ciphertext, ciphertext_len,
                                         plaintext, plaintext_size, plaintext_len);
}

/**
 * Cleanup crypto server resources
 */
void crypto_server_cleanup(void) {
    if (g_crypto_initialized) {
        crypto_handshake_cleanup(&g_crypto_ctx);
        g_crypto_initialized = false;
        log_debug("Server crypto handshake cleaned up");
    }
}
