#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// AES-256 key size
#define AES_KEY_SIZE 32
#define AES_BLOCK_SIZE 16
#define AES_IV_SIZE 16

// AES encryption context
typedef struct {
  uint8_t key[AES_KEY_SIZE];
  bool initialized;
  bool hw_available;
} aes_context_t;

// Initialize AES context with key derivation from passphrase
int aes_init_context(aes_context_t *ctx, const char *passphrase);

// Hardware-accelerated AES encryption (if available)
int aes_encrypt_hw(const aes_context_t *ctx, const uint8_t *plaintext, size_t len, uint8_t *ciphertext, uint8_t *iv);

// Hardware-accelerated AES decryption (if available)
int aes_decrypt_hw(const aes_context_t *ctx, const uint8_t *ciphertext, size_t len, uint8_t *plaintext,
                   const uint8_t *iv);

// Software fallback encryption
int aes_encrypt_sw(const aes_context_t *ctx, const uint8_t *plaintext, size_t len, uint8_t *ciphertext, uint8_t *iv);

// Software fallback decryption
int aes_decrypt_sw(const aes_context_t *ctx, const uint8_t *ciphertext, size_t len, uint8_t *plaintext,
                   const uint8_t *iv);

// Check if AES hardware acceleration is available
bool aes_hw_is_available(void);

// Derive AES key from passphrase using simple hash
void aes_derive_key(const char *passphrase, uint8_t key[AES_KEY_SIZE]);

// Generate key verification hash for handshake
uint32_t aes_key_verification_hash(const uint8_t key[AES_KEY_SIZE]);

// Main encryption/decryption functions - automatically select best implementation
#define aes_encrypt(ctx, plaintext, len, ciphertext, iv) aes_encrypt_hw((ctx), (plaintext), (len), (ciphertext), (iv))
#define aes_decrypt(ctx, ciphertext, len, plaintext, iv) aes_decrypt_hw((ctx), (ciphertext), (len), (plaintext), (iv))