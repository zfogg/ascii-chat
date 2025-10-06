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

// Check if crypto handshake is ready
bool crypto_server_is_ready(void);

// Get crypto context for encryption/decryption
const crypto_context_t* crypto_server_get_context(void);

// Encrypt a packet for transmission
int crypto_server_encrypt_packet(const uint8_t* plaintext, size_t plaintext_len,
                                uint8_t* ciphertext, size_t ciphertext_size,
                                size_t* ciphertext_len);

// Decrypt a received packet
int crypto_server_decrypt_packet(const uint8_t* ciphertext, size_t ciphertext_len,
                                uint8_t* plaintext, size_t plaintext_size,
                                size_t* plaintext_len);

// Cleanup crypto server resources
void crypto_server_cleanup(void);
