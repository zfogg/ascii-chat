#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "crypto.h"
#include "keys.h"
#include "known_hosts.h"

// Crypto handshake state
typedef enum {
  CRYPTO_HANDSHAKE_DISABLED = 0,   // No encryption
  CRYPTO_HANDSHAKE_INIT,           // Initial state
  CRYPTO_HANDSHAKE_KEY_EXCHANGE,   // DH key exchange in progress
  CRYPTO_HANDSHAKE_AUTHENTICATING, // Authentication challenge/response
  CRYPTO_HANDSHAKE_READY,          // Encryption ready
  CRYPTO_HANDSHAKE_FAILED          // Handshake failed
} crypto_handshake_state_t;

// Crypto handshake context for a connection
typedef struct {
  crypto_context_t crypto_ctx;    // Core crypto context
  crypto_handshake_state_t state; // Current handshake state

  // Server identity (server only)
  public_key_t server_public_key;   // Server's long-term public key
  private_key_t server_private_key; // Server's long-term private key

  // Client identity (client only)
  public_key_t client_public_key; // Client's public key
  char expected_server_key[256];  // Expected server key (client only)

  // Connection info for known_hosts
  char server_hostname[256]; // Server hostname
  uint16_t server_port;      // Server port

  // Authentication
  bool verify_server_key;     // Client: verify server key
  bool require_client_auth;   // Server: require client authentication
  char client_keys_path[256]; // Server: client keys file path

} crypto_handshake_context_t;

// Initialize crypto handshake context
int crypto_handshake_init(crypto_handshake_context_t *ctx, bool is_server);

// Cleanup crypto handshake context
void crypto_handshake_cleanup(crypto_handshake_context_t *ctx);

// Server: Start crypto handshake by sending public key
int crypto_handshake_server_start(crypto_handshake_context_t *ctx, socket_t client_socket);

// Client: Process server's public key and send our public key
int crypto_handshake_client_key_exchange(crypto_handshake_context_t *ctx, socket_t client_socket);

// Server: Process client's public key and send auth challenge
int crypto_handshake_server_auth_challenge(crypto_handshake_context_t *ctx, socket_t client_socket);

// Client: Process auth challenge and send response
int crypto_handshake_client_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket);

// Server: Process auth response and complete handshake
int crypto_handshake_server_complete(crypto_handshake_context_t *ctx, socket_t client_socket);

// Check if handshake is complete and encryption is ready
bool crypto_handshake_is_ready(const crypto_handshake_context_t *ctx);

// Get the crypto context for encryption/decryption
const crypto_context_t *crypto_handshake_get_context(const crypto_handshake_context_t *ctx);

// Encrypt a packet using the established crypto context
int crypto_handshake_encrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *plaintext,
                                    size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                                    size_t *ciphertext_len);

// Decrypt a packet using the established crypto context
int crypto_handshake_decrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *ciphertext,
                                    size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                                    size_t *plaintext_len);
