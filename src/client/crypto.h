#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "crypto/crypto.h"
#include "platform/abstraction.h"

// Initialize client crypto handshake
int client_crypto_init(void);

// Perform crypto handshake with server
int client_crypto_handshake(socket_t socket);

// Check if crypto handshake is ready
bool crypto_client_is_ready(void);

// Get crypto context for encryption/decryption
const crypto_context_t* crypto_client_get_context(void);

// Encrypt a packet for transmission
int crypto_client_encrypt_packet(const uint8_t* plaintext, size_t plaintext_len,
                                uint8_t* ciphertext, size_t ciphertext_size,
                                size_t* ciphertext_len);

// Decrypt a received packet
int crypto_client_decrypt_packet(const uint8_t* ciphertext, size_t ciphertext_len,
                                uint8_t* plaintext, size_t plaintext_size,
                                size_t* plaintext_len);

// Cleanup crypto client resources
void crypto_client_cleanup(void);
