/**
 * @file server/crypto.h
 * @ingroup server_crypto
 * @brief Server cryptographic operations and per-client handshake management
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ascii-chat/crypto/crypto.h>
#include "client.h"

// Initialize server crypto handshake
int server_crypto_init(void);

// Perform crypto handshake with client
int server_crypto_handshake(client_info_t *client);

// Check if crypto handshake is ready for a specific client
bool crypto_server_is_ready(const char *client_id);

// Get crypto context for encryption/decryption for a specific client
const crypto_context_t *crypto_server_get_context(const char *client_id);

// Encrypt a packet for transmission to a specific client
int crypto_server_encrypt_packet(const char *client_id, const uint8_t *plaintext, size_t plaintext_len,
                                 uint8_t *ciphertext, size_t ciphertext_size, size_t *ciphertext_len);

// Decrypt a received packet from a specific client
int crypto_server_decrypt_packet(const char *client_id, const uint8_t *ciphertext, size_t ciphertext_len,
                                 uint8_t *plaintext, size_t plaintext_size, size_t *plaintext_len);

// Cleanup crypto resources for a specific client
void crypto_server_cleanup_client(const char *client_id);
