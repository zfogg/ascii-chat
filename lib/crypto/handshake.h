#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "platform/socket.h"
#include "crypto.h"
#include "keys/keys.h"
#include "network/packet_types.h" // For crypto_parameters_packet_t

// Authentication requirement flags (sent in AUTH_CHALLENGE)
#define AUTH_REQUIRE_PASSWORD 0x01   // Server requires password authentication
#define AUTH_REQUIRE_CLIENT_KEY 0x02 // Server requires client key (whitelist)

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
typedef struct crypto_handshake_context_t {
  crypto_context_t crypto_ctx;    // Core crypto context
  crypto_handshake_state_t state; // Current handshake state
  bool is_server;                 // True if this is the server side

  // Server identity (server only)
  public_key_t server_public_key;   // Server's long-term public key
  private_key_t server_private_key; // Server's long-term private key

  // Client identity (client only)
  public_key_t client_public_key;   // Client's Ed25519 public key (for authentication)
  private_key_t client_private_key; // Client's Ed25519 private key (for signing challenges)
  char expected_server_key[256];    // Expected server key (client only)

  // Connection info for known_hosts
  char server_hostname[256]; // Server hostname (user-provided)
  char server_ip[256];       // Server IP address (resolved from connection)
  uint16_t server_port;      // Server port

  // Authentication
  bool verify_server_key;       // Client: verify server key
  bool require_client_auth;     // Server: require client authentication
  bool server_uses_client_auth; // Client: whether server is using client verification
  char client_keys_path[256];   // Server: client keys file path

  // Dynamic crypto parameters (from crypto_parameters_packet_t)
  uint16_t kex_public_key_size;  // e.g., 32 for X25519, 1568 for Kyber1024
  uint16_t auth_public_key_size; // e.g., 32 for Ed25519, 1952 for Dilithium3
  uint16_t signature_size;       // e.g., 64 for Ed25519, 3309 for Dilithium3
  uint16_t shared_secret_size;   // e.g., 32 for X25519
  uint8_t nonce_size;            // e.g., 24 for XSalsa20 nonce
  uint8_t mac_size;              // e.g., 16 for Poly1305 MAC
  uint8_t hmac_size;             // e.g., 32 for HMAC-SHA256

  // Client whitelist (server only)
  public_key_t *client_whitelist;   // Pointer to whitelist array
  size_t num_whitelisted_clients;   // Number of whitelisted clients
  public_key_t client_ed25519_key;  // Client's Ed25519 key (received during handshake)
  bool client_ed25519_key_verified; // Whether client's Ed25519 key was verified against whitelist
  bool client_sent_identity;        // Whether client provided an identity key during handshake

  // Password authentication
  bool has_password;  // Whether password authentication is enabled
  char password[256]; // Password for authentication (temporary storage)

  // Mutual authentication (client challenges server)
  uint8_t client_challenge_nonce[32]; // Client-generated nonce for server to prove knowledge of shared secret

} crypto_handshake_context_t;

// Initialize crypto handshake context
asciichat_error_t crypto_handshake_init(crypto_handshake_context_t *ctx, bool is_server);

// Set crypto parameters from crypto_parameters_packet_t
asciichat_error_t crypto_handshake_set_parameters(crypto_handshake_context_t *ctx,
                                                  const crypto_parameters_packet_t *params);

// Validate crypto packet size based on session parameters
asciichat_error_t crypto_handshake_validate_packet_size(const crypto_handshake_context_t *ctx, uint16_t packet_type,
                                                        size_t packet_size);

// Initialize crypto handshake context with password authentication
asciichat_error_t crypto_handshake_init_with_password(crypto_handshake_context_t *ctx, bool is_server,
                                                      const char *password);

// Cleanup crypto handshake context
void crypto_handshake_cleanup(crypto_handshake_context_t *ctx);

// Server: Start crypto handshake by sending public key
asciichat_error_t crypto_handshake_server_start(crypto_handshake_context_t *ctx, socket_t client_socket);

// Client: Process server's public key and send our public key
asciichat_error_t crypto_handshake_client_key_exchange(crypto_handshake_context_t *ctx, socket_t client_socket);

// Server: Process client's public key and send auth challenge
asciichat_error_t crypto_handshake_server_auth_challenge(crypto_handshake_context_t *ctx, socket_t client_socket);

// Client: Process auth challenge and send response
asciichat_error_t crypto_handshake_client_auth_response(crypto_handshake_context_t *ctx, socket_t client_socket);

// Client: Wait for handshake complete confirmation
asciichat_error_t crypto_handshake_client_complete(crypto_handshake_context_t *ctx, socket_t client_socket);

// Server: Process auth response and complete handshake
asciichat_error_t crypto_handshake_server_complete(crypto_handshake_context_t *ctx, socket_t client_socket);

// Check if handshake is complete and encryption is ready
bool crypto_handshake_is_ready(const crypto_handshake_context_t *ctx);

// Get the crypto context for encryption/decryption
const crypto_context_t *crypto_handshake_get_context(const crypto_handshake_context_t *ctx);

// Encrypt a packet using the established crypto context
asciichat_error_t crypto_handshake_encrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *plaintext,
                                                  size_t plaintext_len, uint8_t *ciphertext, size_t ciphertext_size,
                                                  size_t *ciphertext_len);

// Decrypt a packet using the established crypto context
asciichat_error_t crypto_handshake_decrypt_packet(const crypto_handshake_context_t *ctx, const uint8_t *ciphertext,
                                                  size_t ciphertext_len, uint8_t *plaintext, size_t plaintext_size,
                                                  size_t *plaintext_len);

// Helper: Encrypt with automatic passthrough if crypto not ready
asciichat_error_t crypto_encrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                                       const uint8_t *plaintext, size_t plaintext_len,
                                                       uint8_t *ciphertext, size_t ciphertext_size,
                                                       size_t *ciphertext_len);

// Helper: Decrypt with automatic passthrough if crypto not ready
asciichat_error_t crypto_decrypt_packet_or_passthrough(const crypto_handshake_context_t *ctx, bool crypto_ready,
                                                       const uint8_t *ciphertext, size_t ciphertext_len,
                                                       uint8_t *plaintext, size_t plaintext_size,
                                                       size_t *plaintext_len);
