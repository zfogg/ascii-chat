#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// Initialize encryption system based on options
int encryption_init(void);

// Check if encryption is enabled and initialized
bool encryption_is_enabled(void);

// Get key verification hash for handshake
uint32_t encryption_get_key_hash(void);

// Encrypt packet data (allocates memory for ciphertext and IV)
int encryption_encrypt_packet(const uint8_t *plaintext, size_t plaintext_len, uint8_t **ciphertext_out,
                              size_t *ciphertext_len_out, uint8_t **iv_out);

// Decrypt packet data (allocates memory for plaintext)
int encryption_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, const uint8_t *iv,
                              uint8_t **plaintext_out, size_t *plaintext_len_out);

// Cleanup encryption system
void encryption_cleanup(void);