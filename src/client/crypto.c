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

#include <string.h>
#include <stdio.h>

// Global crypto handshake context for this client connection
static crypto_handshake_context_t g_crypto_ctx = {0};
static bool g_crypto_initialized = false;

/**
 * Initialize client crypto handshake
 *
 * @return 0 on success, -1 on failure
 */
int client_crypto_init(void) {
    if (g_crypto_initialized) {
        return 0; // Already initialized
    }

    // Check if encryption is disabled
    if (opt_no_encrypt) {
        log_info("Encryption disabled via --no-encrypt");
        return 0;
    }

    // Initialize crypto handshake context
    int result = crypto_handshake_init(&g_crypto_ctx, false); // false = client
    if (result != 0) {
        log_error("Failed to initialize crypto handshake");
        return -1;
    }

    // Set up server connection info for known_hosts
    SAFE_STRNCPY(g_crypto_ctx.server_hostname, opt_address, sizeof(g_crypto_ctx.server_hostname) - 1);
    g_crypto_ctx.server_port = (uint16_t)strtoint_safe(opt_port);

    // Configure server key verification if specified
    if (strlen(opt_server_key) > 0) {
        g_crypto_ctx.verify_server_key = true;
        SAFE_STRNCPY(g_crypto_ctx.expected_server_key, opt_server_key, sizeof(g_crypto_ctx.expected_server_key) - 1);
        log_info("Server key verification enabled: %s", opt_server_key);
    }

    g_crypto_initialized = true;
    log_info("Client crypto handshake initialized");
    return 0;
}

/**
 * Perform crypto handshake with server
 *
 * @param socket Connected socket to server
 * @return 0 on success, -1 on failure
 */
int client_crypto_handshake(socket_t socket) {
    if (!g_crypto_initialized || opt_no_encrypt) {
        log_debug("Crypto handshake skipped (disabled or not initialized)");
        return 0;
    }

    log_info("Starting crypto handshake with server...");

    // Step 1: Receive server's public key and send our public key
    int result = crypto_handshake_client_key_exchange(&g_crypto_ctx, socket);
    if (result != 0) {
        log_error("Crypto key exchange failed");
        return -1;
    }

    // Step 2: Receive auth challenge and send response
    result = crypto_handshake_client_auth_response(&g_crypto_ctx, socket);
    if (result != 0) {
        log_error("Crypto authentication failed");
        return -1;
    }

    // Step 3: Receive handshake complete message
    uint32_t packet_type;
    ssize_t received = socket_recv(socket, &packet_type, sizeof(packet_type), 0);
    if (received != sizeof(packet_type)) {
        log_error("Failed to receive handshake complete message");
        return -1;
    }

    if (packet_type != CRYPTO_PACKET_HANDSHAKE_COMPLETE) {
        log_error("Invalid handshake complete message");
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
bool crypto_client_is_ready(void) {
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
const crypto_context_t* crypto_client_get_context(void) {
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
int crypto_client_encrypt_packet(const uint8_t* plaintext, size_t plaintext_len,
                                uint8_t* ciphertext, size_t ciphertext_size,
                                size_t* ciphertext_len) {
    if (!crypto_client_is_ready()) {
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
int crypto_client_decrypt_packet(const uint8_t* ciphertext, size_t ciphertext_len,
                                uint8_t* plaintext, size_t plaintext_size,
                                size_t* plaintext_len) {
    if (!crypto_client_is_ready()) {
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
 * Cleanup crypto client resources
 */
void crypto_client_cleanup(void) {
    if (g_crypto_initialized) {
        crypto_handshake_cleanup(&g_crypto_ctx);
        g_crypto_initialized = false;
        log_debug("Client crypto handshake cleaned up");
    }
}
