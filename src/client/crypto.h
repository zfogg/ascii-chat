#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "crypto/crypto.h"
#include "platform/socket.h"

// Initialize client crypto handshake
int client_crypto_init(void);

// Perform crypto handshake with server
int client_crypto_handshake(socket_t socket);

// Check if crypto handshake is ready
bool crypto_client_is_ready(void);

// Get crypto context for encryption/decryption
const crypto_context_t *crypto_client_get_context(void);

// Encrypt a packet for transmission
int crypto_client_encrypt_packet(const uint8_t *plaintext, size_t plaintext_len, uint8_t *ciphertext,
                                 size_t ciphertext_size, size_t *ciphertext_len);

// Decrypt a received packet
int crypto_client_decrypt_packet(const uint8_t *ciphertext, size_t ciphertext_len, uint8_t *plaintext,
                                 size_t plaintext_size, size_t *plaintext_len);

// Cleanup crypto client resources
void crypto_client_cleanup(void);

// Session rekeying functions
bool crypto_client_should_rekey(void);
int crypto_client_initiate_rekey(void);
int crypto_client_process_rekey_request(const uint8_t *packet, size_t packet_len);
int crypto_client_send_rekey_response(void);
int crypto_client_process_rekey_response(const uint8_t *packet, size_t packet_len);
int crypto_client_send_rekey_complete(void);
