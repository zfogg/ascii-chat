#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "crypto/crypto.h"
#include "platform/abstraction.h"

// Initialize server crypto handshake
int server_crypto_init(void);

// Perform crypto handshake with client
int server_crypto_handshake(socket_t client_socket);

// Check if crypto handshake is ready for a specific client
bool crypto_server_is_ready(uint32_t client_id);

// Get crypto context for encryption/decryption for a specific client
const crypto_context_t* crypto_server_get_context(uint32_t client_id);

// Encrypt a packet for transmission to a specific client
int crypto_server_encrypt_packet(uint32_t client_id, const uint8_t* plaintext, size_t plaintext_len,
                                uint8_t* ciphertext, size_t ciphertext_size,
                                size_t* ciphertext_len);

// Decrypt a received packet from a specific client
int crypto_server_decrypt_packet(uint32_t client_id, const uint8_t* ciphertext, size_t ciphertext_len,
                                uint8_t* plaintext, size_t plaintext_size,
                                size_t* plaintext_len);

// Cleanup crypto resources for a specific client
void crypto_server_cleanup_client(uint32_t client_id);

// Cleanup crypto server resources (legacy function for compatibility)
void crypto_server_cleanup(void);
