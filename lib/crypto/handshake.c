#include "handshake.h"
#include "common.h"
#include "network.h"
#include <string.h>
#include <stdio.h>

// Initialize crypto handshake context
int crypto_handshake_init(crypto_handshake_context_t* ctx, bool is_server) {
    if (!ctx) return -1;

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

// Cleanup crypto handshake context
void crypto_handshake_cleanup(crypto_handshake_context_t* ctx) {
    if (!ctx) return;

    // Cleanup core crypto context
    crypto_cleanup(&ctx->crypto_ctx);

    // Zero out sensitive data
    sodium_memzero(ctx, sizeof(crypto_handshake_context_t));
}

// Server: Start crypto handshake by sending public key
int crypto_handshake_server_start(crypto_handshake_context_t* ctx, socket_t client_socket) {
    if (!ctx || ctx->state != CRYPTO_HANDSHAKE_INIT) return -1;

    // Create public key packet
    uint8_t packet[1024];
    size_t packet_len;

    crypto_result_t result = crypto_create_public_key_packet(&ctx->crypto_ctx, packet, sizeof(packet), &packet_len);
    if (result != CRYPTO_OK) {
        log_error("Failed to create public key packet: %s", crypto_result_to_string(result));
        return -1;
    }

    // Send public key to client
    ssize_t sent = socket_send(client_socket, packet, packet_len, 0);
    if (sent != (ssize_t)packet_len) {
        log_error("Failed to send public key packet");
        return -1;
    }

    ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;
    log_debug("Server sent public key to client");

    return 0;
}

// Client: Process server's public key and send our public key
int crypto_handshake_client_key_exchange(crypto_handshake_context_t* ctx, socket_t client_socket) {
    if (!ctx || ctx->state != CRYPTO_HANDSHAKE_INIT) return -1;

    // Receive server's public key
    uint8_t packet[1024];
    ssize_t received = socket_recv(client_socket, packet, sizeof(packet), 0);
    if (received < 0) {
        log_error("Failed to receive server public key");
        return -1;
    }

    // Process server's public key
    crypto_result_t result = crypto_process_public_key_packet(&ctx->crypto_ctx, packet, received);
    if (result != CRYPTO_OK) {
        log_error("Failed to process server public key: %s", crypto_result_to_string(result));
        return -1;
    }

    // TODO: Verify server key against known_hosts if --server-key is specified
    if (ctx->verify_server_key) {
        // Check known_hosts for this server
        int known_host_result = check_known_host(ctx->server_hostname, ctx->server_port, ctx->crypto_ctx.peer_public_key);
        if (known_host_result == -1) {
            // Key mismatch - potential MITM attack
            display_mitm_warning(ctx->crypto_ctx.peer_public_key, ctx->crypto_ctx.peer_public_key);
            return -1;
        } else if (known_host_result == 0) {
            // First connection - add to known_hosts
            add_known_host(ctx->server_hostname, ctx->server_port, ctx->crypto_ctx.peer_public_key);
            log_info("Added server to known_hosts: %s:%d", ctx->server_hostname, ctx->server_port);
        }
    }

    // Send our public key to server
    size_t packet_len;
    result = crypto_create_public_key_packet(&ctx->crypto_ctx, packet, sizeof(packet), &packet_len);
    if (result != CRYPTO_OK) {
        log_error("Failed to create client public key packet: %s", crypto_result_to_string(result));
        return -1;
    }

    ssize_t sent = socket_send(client_socket, packet, packet_len, 0);
    if (sent != (ssize_t)packet_len) {
        log_error("Failed to send client public key");
        return -1;
    }

    ctx->state = CRYPTO_HANDSHAKE_KEY_EXCHANGE;
    log_debug("Client sent public key to server");

    return 0;
}

// Server: Process client's public key and send auth challenge
int crypto_handshake_server_auth_challenge(crypto_handshake_context_t* ctx, socket_t client_socket) {
    if (!ctx || ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE) return -1;

    // Receive client's public key
    uint8_t packet[1024];
    ssize_t received = socket_recv(client_socket, packet, sizeof(packet), 0);
    if (received < 0) {
        log_error("Failed to receive client public key");
        return -1;
    }

    // Process client's public key
    crypto_result_t result = crypto_process_public_key_packet(&ctx->crypto_ctx, packet, received);
    if (result != CRYPTO_OK) {
        log_error("Failed to process client public key: %s", crypto_result_to_string(result));
        return -1;
    }

    // TODO: Check client key against --client-keys whitelist if specified
    if (ctx->require_client_auth) {
        // TODO: Implement client key verification
        log_debug("Client authentication required (not yet implemented)");
    }

    // Create authentication challenge
    size_t packet_len;
    result = crypto_create_auth_challenge(&ctx->crypto_ctx, packet, sizeof(packet), &packet_len);
    if (result != CRYPTO_OK) {
        log_error("Failed to create auth challenge: %s", crypto_result_to_string(result));
        return -1;
    }

    // Send auth challenge to client
    ssize_t sent = socket_send(client_socket, packet, packet_len, 0);
    if (sent != (ssize_t)packet_len) {
        log_error("Failed to send auth challenge");
        return -1;
    }

    ctx->state = CRYPTO_HANDSHAKE_AUTHENTICATING;
    log_debug("Server sent auth challenge to client");

    return 0;
}

// Client: Process auth challenge and send response
int crypto_handshake_client_auth_response(crypto_handshake_context_t* ctx, socket_t client_socket) {
    if (!ctx || ctx->state != CRYPTO_HANDSHAKE_KEY_EXCHANGE) return -1;

    // Receive auth challenge
    uint8_t packet[1024];
    ssize_t received = socket_recv(client_socket, packet, sizeof(packet), 0);
    if (received < 0) {
        log_error("Failed to receive auth challenge");
        return -1;
    }

    // Process auth challenge
    crypto_result_t result = crypto_process_auth_challenge(&ctx->crypto_ctx, packet, received);
    if (result != CRYPTO_OK) {
        log_error("Failed to process auth challenge: %s", crypto_result_to_string(result));
        return -1;
    }

    // Compute HMAC response using shared secret
    uint8_t auth_response[32];
    crypto_result_t hmac_result = crypto_compute_hmac(ctx->crypto_ctx.shared_key, ctx->crypto_ctx.auth_nonce, auth_response);
    if (hmac_result != CRYPTO_OK) {
        log_error("Failed to compute HMAC response: %s", crypto_result_to_string(hmac_result));
        return -1;
    }

    // Create auth response packet
    uint32_t packet_type = CRYPTO_PACKET_AUTH_RESPONSE;
    memcpy(packet, &packet_type, sizeof(packet_type));
    memcpy(packet + sizeof(packet_type), auth_response, 32);

    size_t packet_len = sizeof(packet_type) + 32;

    // Send auth response to server
    ssize_t sent = socket_send(client_socket, packet, packet_len, 0);
    if (sent != (ssize_t)packet_len) {
        log_error("Failed to send auth response");
        return -1;
    }

    ctx->state = CRYPTO_HANDSHAKE_AUTHENTICATING;
    log_debug("Client sent auth response to server");

    return 0;
}

// Server: Process auth response and complete handshake
int crypto_handshake_server_complete(crypto_handshake_context_t* ctx, socket_t client_socket) {
    if (!ctx || ctx->state != CRYPTO_HANDSHAKE_AUTHENTICATING) return -1;

    // Receive auth response
    uint8_t packet[1024];
    ssize_t received = socket_recv(client_socket, packet, sizeof(packet), 0);
    if (received < 0) {
        log_error("Failed to receive auth response");
        return -1;
    }

    // Process auth response
    crypto_result_t result = crypto_process_auth_response(&ctx->crypto_ctx, packet, received);
    if (result != CRYPTO_OK) {
        log_error("Failed to process auth response: %s", crypto_result_to_string(result));
        return -1;
    }

    // Send handshake complete message
    uint32_t packet_type = CRYPTO_PACKET_HANDSHAKE_COMPLETE;
    ssize_t sent = socket_send(client_socket, &packet_type, sizeof(packet_type), 0);
    if (sent != sizeof(packet_type)) {
        log_error("Failed to send handshake complete");
        return -1;
    }

    ctx->state = CRYPTO_HANDSHAKE_READY;
    log_info("Crypto handshake completed successfully");

    return 0;
}

// Check if handshake is complete and encryption is ready
bool crypto_handshake_is_ready(const crypto_handshake_context_t* ctx) {
    if (!ctx) return false;
    return ctx->state == CRYPTO_HANDSHAKE_READY && crypto_is_ready(&ctx->crypto_ctx);
}

// Get the crypto context for encryption/decryption
const crypto_context_t* crypto_handshake_get_context(const crypto_handshake_context_t* ctx) {
    if (!ctx || !crypto_handshake_is_ready(ctx)) return NULL;
    return &ctx->crypto_ctx;
}

// Encrypt a packet using the established crypto context
int crypto_handshake_encrypt_packet(const crypto_handshake_context_t* ctx,
                                   const uint8_t* plaintext, size_t plaintext_len,
                                   uint8_t* ciphertext, size_t ciphertext_size,
                                   size_t* ciphertext_len) {
    if (!ctx || !crypto_handshake_is_ready(ctx)) return -1;

    crypto_result_t result = crypto_encrypt((crypto_context_t*)&ctx->crypto_ctx, plaintext, plaintext_len,
                                           ciphertext, ciphertext_size, ciphertext_len);
    if (result != CRYPTO_OK) {
        log_error("Failed to encrypt packet: %s", crypto_result_to_string(result));
        return -1;
    }

    return 0;
}

// Decrypt a packet using the established crypto context
int crypto_handshake_decrypt_packet(const crypto_handshake_context_t* ctx,
                                   const uint8_t* ciphertext, size_t ciphertext_len,
                                   uint8_t* plaintext, size_t plaintext_size,
                                   size_t* plaintext_len) {
    if (!ctx || !crypto_handshake_is_ready(ctx)) return -1;

    crypto_result_t result = crypto_decrypt((crypto_context_t*)&ctx->crypto_ctx, ciphertext, ciphertext_len,
                                          plaintext, plaintext_size, plaintext_len);
    if (result != CRYPTO_OK) {
        log_error("Failed to decrypt packet: %s", crypto_result_to_string(result));
        return -1;
    }

    return 0;
}
