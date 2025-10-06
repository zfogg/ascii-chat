#include "crypto.h"
#include "common.h"

#include <string.h>
#include <time.h>
#include <inttypes.h>

// Static initialization flag for libsodium
static bool g_libsodium_initialized = false;

// =============================================================================
// Internal helper functions
// =============================================================================

// Initialize libsodium (thread-safe, idempotent)
static crypto_result_t init_libsodium(void) {
  if (g_libsodium_initialized) {
    return CRYPTO_OK;
  }

  if (sodium_init() < 0) {
    log_error("Failed to initialize libsodium");
    return CRYPTO_ERROR_LIBSODIUM;
  }

  g_libsodium_initialized = true;
  log_debug("Libsodium initialized successfully");
  return CRYPTO_OK;
}

// Generate secure nonce with counter to prevent reuse
static void generate_nonce(crypto_context_t *ctx, uint8_t *nonce_out) {
  // Use counter in first 8 bytes, random in remaining 16 bytes
  // This prevents nonce reuse while maintaining security
  uint64_t counter = ctx->nonce_counter++;
  SAFE_MEMCPY(nonce_out, 8, &counter, 8);
  randombytes_buf(nonce_out + 8, CRYPTO_NONCE_SIZE - 8);
}

// Securely clear memory
static void secure_memzero(void *ptr, size_t len) {
  sodium_memzero(ptr, len);
}

// =============================================================================
// Core initialization and setup
// =============================================================================

crypto_result_t crypto_init(crypto_context_t *ctx) {
  if (!ctx) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Initialize libsodium
  crypto_result_t result = init_libsodium();
  if (result != CRYPTO_OK) {
    return result;
  }

  // Clear context
  secure_memzero(ctx, sizeof(crypto_context_t));

  // Generate key pair for X25519 key exchange
  result = crypto_generate_keypair(ctx);
  if (result != CRYPTO_OK) {
    return result;
  }

  ctx->initialized = true;
  ctx->has_password = false;
  ctx->key_exchange_complete = false;
  ctx->peer_key_received = false;
  ctx->handshake_complete = false;
  ctx->nonce_counter = 1; // Start from 1 (0 reserved for testing)
  ctx->bytes_encrypted = 0;
  ctx->bytes_decrypted = 0;

  log_info("Crypto context initialized with X25519 key exchange");
  return CRYPTO_OK;
}

crypto_result_t crypto_init_with_password(crypto_context_t *ctx, const char *password) {
  if (!ctx || !password) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (strlen(password) == 0) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // First initialize basic crypto
  crypto_result_t result = crypto_init(ctx);
  if (result != CRYPTO_OK) {
    return result;
  }

  // Derive password key
  result = crypto_derive_password_key(ctx, password);
  if (result != CRYPTO_OK) {
    crypto_cleanup(ctx);
    return result;
  }

  ctx->has_password = true;

  log_info("Crypto context initialized with password-based encryption");
  return CRYPTO_OK;
}

void crypto_cleanup(crypto_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return;
  }

  // Securely wipe sensitive data
  secure_memzero(ctx->private_key, sizeof(ctx->private_key));
  secure_memzero(ctx->shared_key, sizeof(ctx->shared_key));
  secure_memzero(ctx->password_key, sizeof(ctx->password_key));
  secure_memzero(ctx->password_salt, sizeof(ctx->password_salt));

  log_debug("Crypto context cleaned up (encrypted: %lu bytes, decrypted: %lu bytes)", ctx->bytes_encrypted,
            ctx->bytes_decrypted);

  // Clear entire context
  secure_memzero(ctx, sizeof(crypto_context_t));
}

crypto_result_t crypto_generate_keypair(crypto_context_t *ctx) {
  if (!ctx) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Generate X25519 key pair for key exchange
  if (crypto_box_keypair(ctx->public_key, ctx->private_key) != 0) {
    log_error("Failed to generate X25519 key pair");
    return CRYPTO_ERROR_KEY_GENERATION;
  }

  log_debug("Generated X25519 key pair for key exchange");
  return CRYPTO_OK;
}

// =============================================================================
// Key exchange protocol (automatic HTTPS-like key exchange)
// =============================================================================

crypto_result_t crypto_get_public_key(const crypto_context_t *ctx, uint8_t *public_key_out) {
  if (!ctx || !ctx->initialized || !public_key_out) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  SAFE_MEMCPY(public_key_out, CRYPTO_PUBLIC_KEY_SIZE, ctx->public_key, CRYPTO_PUBLIC_KEY_SIZE);
  return CRYPTO_OK;
}

crypto_result_t crypto_set_peer_public_key(crypto_context_t *ctx, const uint8_t *peer_public_key) {
  if (!ctx || !ctx->initialized || !peer_public_key) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Store peer's public key
  SAFE_MEMCPY(ctx->peer_public_key, CRYPTO_PUBLIC_KEY_SIZE, peer_public_key, CRYPTO_PUBLIC_KEY_SIZE);
  ctx->peer_key_received = true;

  // Compute shared secret using X25519
  if (crypto_box_beforenm(ctx->shared_key, peer_public_key, ctx->private_key) != 0) {
    log_error("Failed to compute shared secret from peer public key");
    return CRYPTO_ERROR_KEY_GENERATION;
  }

  ctx->key_exchange_complete = true;

  log_debug("Key exchange completed - shared secret computed");
  return CRYPTO_OK;
}

bool crypto_is_ready(const crypto_context_t *ctx) {
  if (!ctx || !ctx->initialized) {
    return false;
  }

  // Ready if either key exchange is complete OR password is set
  return ctx->key_exchange_complete || ctx->has_password;
}

// =============================================================================
// Password-based encryption (optional additional layer)
// =============================================================================

crypto_result_t crypto_derive_password_key(crypto_context_t *ctx, const char *password) {
  if (!ctx || !ctx->initialized || !password) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (strlen(password) == 0) {
    log_error("Empty password provided");
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Use deterministic salt for consistent key derivation across client/server
  // This ensures the same password produces the same key on both sides
  const char *deterministic_salt = "ascii-chat-password-salt-v1";
  memcpy(ctx->password_salt, deterministic_salt, CRYPTO_SALT_SIZE);

  // Derive key using Argon2id (memory-hard, secure against GPU attacks)
  if (crypto_pwhash(ctx->password_key, CRYPTO_ENCRYPTION_KEY_SIZE, password, strlen(password), ctx->password_salt,
                    crypto_pwhash_OPSLIMIT_INTERACTIVE, // ~0.1 seconds
                    crypto_pwhash_MEMLIMIT_INTERACTIVE, // ~64MB
                    crypto_pwhash_ALG_DEFAULT) != 0) {
    log_error("Password key derivation failed - possibly out of memory");
    return CRYPTO_ERROR_PASSWORD_DERIVATION;
  }

  log_debug("Password key derived successfully using Argon2id with deterministic salt");
  return CRYPTO_OK;
}

bool crypto_verify_password(const crypto_context_t *ctx, const char *password) {
  if (!ctx || !ctx->initialized || !ctx->has_password || !password) {
    return false;
  }

  uint8_t test_key[CRYPTO_ENCRYPTION_KEY_SIZE];

  // Use the same deterministic salt for verification
  const char *deterministic_salt = "ascii-chat-password-salt-v1";
  uint8_t salt[CRYPTO_SALT_SIZE];
  memcpy(salt, deterministic_salt, CRYPTO_SALT_SIZE);

  // Derive key with same salt
  if (crypto_pwhash(test_key, CRYPTO_ENCRYPTION_KEY_SIZE, password, strlen(password), salt,
                    crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                    crypto_pwhash_ALG_DEFAULT) != 0) {
    secure_memzero(test_key, sizeof(test_key));
    return false;
  }

  // Constant-time comparison
  bool match = (sodium_memcmp(test_key, ctx->password_key, CRYPTO_ENCRYPTION_KEY_SIZE) == 0);

  secure_memzero(test_key, sizeof(test_key));
  return match;
}

// Derive a deterministic encryption key from password for handshake
crypto_result_t crypto_derive_password_encryption_key(const char *password,
                                                      uint8_t encryption_key[CRYPTO_ENCRYPTION_KEY_SIZE]) {
  if (!password || !encryption_key) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (strlen(password) == 0) {
    log_error("Empty password provided");
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Use deterministic salt for consistent key derivation across client/server
  const char *deterministic_salt = "ascii-chat-password-salt-v1";
  uint8_t salt[CRYPTO_SALT_SIZE];
  memcpy(salt, deterministic_salt, CRYPTO_SALT_SIZE);

  // Derive key using Argon2id (memory-hard, secure against GPU attacks)
  if (crypto_pwhash(encryption_key, CRYPTO_ENCRYPTION_KEY_SIZE, password, strlen(password), salt,
                    crypto_pwhash_OPSLIMIT_INTERACTIVE, // ~0.1 seconds
                    crypto_pwhash_MEMLIMIT_INTERACTIVE, // ~64MB
                    crypto_pwhash_ALG_DEFAULT) != 0) {
    log_error("Password encryption key derivation failed - possibly out of memory");
    return CRYPTO_ERROR_PASSWORD_DERIVATION;
  }

  log_debug("Password encryption key derived successfully using Argon2id");
  return CRYPTO_OK;
}

// =============================================================================
// Encryption/Decryption operations
// =============================================================================

crypto_result_t crypto_encrypt(crypto_context_t *ctx, const uint8_t *plaintext, size_t plaintext_len,
                               uint8_t *ciphertext_out, size_t ciphertext_out_size, size_t *ciphertext_len_out) {
  if (!ctx || !ctx->initialized || !plaintext || !ciphertext_out || !ciphertext_len_out) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (plaintext_len == 0 || plaintext_len > CRYPTO_MAX_PLAINTEXT_SIZE) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (!crypto_is_ready(ctx)) {
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  // Check output buffer size
  size_t required_size = plaintext_len + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  if (ciphertext_out_size < required_size) {
    return CRYPTO_ERROR_BUFFER_TOO_SMALL;
  }

  // Check for nonce counter exhaustion (extremely unlikely)
  if (ctx->nonce_counter == 0 || ctx->nonce_counter == UINT64_MAX) {
    log_error("Nonce counter exhausted - key rotation required");
    return CRYPTO_ERROR_NONCE_EXHAUSTED;
  }

  // Generate nonce and place at beginning of ciphertext
  uint8_t nonce[CRYPTO_NONCE_SIZE];
  generate_nonce(ctx, nonce);
  SAFE_MEMCPY(ciphertext_out, CRYPTO_NONCE_SIZE, nonce, CRYPTO_NONCE_SIZE);

  // Choose encryption key (prefer shared key over password key)
  const uint8_t *encryption_key = NULL;
  if (ctx->key_exchange_complete) {
    encryption_key = ctx->shared_key;
  } else if (ctx->has_password) {
    encryption_key = ctx->password_key;
  } else {
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  // Encrypt using NaCl secretbox (XSalsa20 + Poly1305)
  if (crypto_secretbox_easy(ciphertext_out + CRYPTO_NONCE_SIZE, plaintext, plaintext_len, nonce, encryption_key) != 0) {
    log_error("Encryption failed");
    return CRYPTO_ERROR_ENCRYPTION;
  }

  *ciphertext_len_out = required_size;
  ctx->bytes_encrypted += plaintext_len;

  log_debug("Encrypted %zu bytes (using %s key)", plaintext_len, ctx->key_exchange_complete ? "shared" : "password");
  return CRYPTO_OK;
}

crypto_result_t crypto_decrypt(crypto_context_t *ctx, const uint8_t *ciphertext, size_t ciphertext_len,
                               uint8_t *plaintext_out, size_t plaintext_out_size, size_t *plaintext_len_out) {
  if (!ctx || !ctx->initialized || !ciphertext || !plaintext_out || !plaintext_len_out) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (!crypto_is_ready(ctx)) {
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  // Check minimum ciphertext size (nonce + MAC)
  if (ciphertext_len < CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t plaintext_len = ciphertext_len - CRYPTO_NONCE_SIZE - CRYPTO_MAC_SIZE;
  if (plaintext_out_size < plaintext_len) {
    return CRYPTO_ERROR_BUFFER_TOO_SMALL;
  }

  // Extract nonce from beginning of ciphertext
  const uint8_t *nonce = ciphertext;
  const uint8_t *encrypted_data = ciphertext + CRYPTO_NONCE_SIZE;

  // Choose decryption key (prefer shared key over password key)
  const uint8_t *decryption_key = NULL;
  if (ctx->key_exchange_complete) {
    decryption_key = ctx->shared_key;
  } else if (ctx->has_password) {
    decryption_key = ctx->password_key;
  } else {
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  // Decrypt using NaCl secretbox (XSalsa20 + Poly1305)
  if (crypto_secretbox_open_easy(plaintext_out, encrypted_data, ciphertext_len - CRYPTO_NONCE_SIZE, nonce,
                                 decryption_key) != 0) {
    log_error("Decryption failed - invalid MAC or corrupted data");
    return CRYPTO_ERROR_INVALID_MAC;
  }

  *plaintext_len_out = plaintext_len;
  ctx->bytes_decrypted += plaintext_len;

  log_debug("Decrypted %zu bytes (using %s key)", plaintext_len, ctx->key_exchange_complete ? "shared" : "password");
  return CRYPTO_OK;
}

// =============================================================================
// Utility functions
// =============================================================================

const char *crypto_result_to_string(crypto_result_t result) {
  switch (result) {
  case CRYPTO_OK:
    return "Success";
  case CRYPTO_ERROR_INIT_FAILED:
    return "Initialization failed";
  case CRYPTO_ERROR_INVALID_PARAMS:
    return "Invalid parameters";
  case CRYPTO_ERROR_MEMORY:
    return "Memory allocation failed";
  case CRYPTO_ERROR_LIBSODIUM:
    return "Libsodium error";
  case CRYPTO_ERROR_KEY_GENERATION:
    return "Key generation failed";
  case CRYPTO_ERROR_PASSWORD_DERIVATION:
    return "Password derivation failed";
  case CRYPTO_ERROR_ENCRYPTION:
    return "Encryption failed";
  case CRYPTO_ERROR_DECRYPTION:
    return "Decryption failed";
  case CRYPTO_ERROR_INVALID_MAC:
    return "Invalid MAC or corrupted data";
  case CRYPTO_ERROR_BUFFER_TOO_SMALL:
    return "Buffer too small";
  case CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE:
    return "Key exchange not complete";
  case CRYPTO_ERROR_NONCE_EXHAUSTED:
    return "Nonce counter exhausted";
  default:
    return "Unknown error";
  }
}

void crypto_get_status(const crypto_context_t *ctx, char *status_buffer, size_t buffer_size) {
  if (!ctx || !status_buffer || buffer_size == 0) {
    return;
  }

  if (!ctx->initialized) {
    SAFE_SNPRINTF(status_buffer, buffer_size, "Not initialized");
    return;
  }

  SAFE_SNPRINTF(status_buffer, buffer_size,
                "Initialized: %s, Password: %s, Key Exchange: %s, Ready: %s, "
                "Encrypted: %" PRIu64 " bytes, Decrypted: %" PRIu64 " bytes, Nonce: %" PRIu64,
                ctx->initialized ? "yes" : "no", ctx->has_password ? "yes" : "no",
                ctx->key_exchange_complete ? "complete" : "incomplete", crypto_is_ready(ctx) ? "yes" : "no",
                ctx->bytes_encrypted, ctx->bytes_decrypted, ctx->nonce_counter);
}

bool crypto_secure_compare(const uint8_t *lhs, const uint8_t *rhs, size_t len) {
  if (!lhs || !rhs) {
    return false;
  }
  return sodium_memcmp(lhs, rhs, len) == 0;
}

crypto_result_t crypto_random_bytes(uint8_t *buffer, size_t len) {
  if (!buffer || len == 0) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  crypto_result_t result = init_libsodium();
  if (result != CRYPTO_OK) {
    return result;
  }

  randombytes_buf(buffer, len);
  return CRYPTO_OK;
}

// =============================================================================
// Network integration helpers
// =============================================================================

crypto_result_t crypto_create_public_key_packet(const crypto_context_t *ctx, uint8_t *packet_out, size_t packet_size,
                                                size_t *packet_len_out) {
  if (!ctx || !ctx->initialized || !packet_out || !packet_len_out) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t required_size = sizeof(uint32_t) + CRYPTO_PUBLIC_KEY_SIZE; // type + key
  if (packet_size < required_size) {
    return CRYPTO_ERROR_BUFFER_TOO_SMALL;
  }

  // Pack packet: [type:4][public_key:32]
  uint32_t packet_type = CRYPTO_PACKET_PUBLIC_KEY;
  SAFE_MEMCPY(packet_out, sizeof(packet_type), &packet_type, sizeof(packet_type));
  SAFE_MEMCPY(packet_out + sizeof(packet_type), CRYPTO_PUBLIC_KEY_SIZE, ctx->public_key, CRYPTO_PUBLIC_KEY_SIZE);

  *packet_len_out = required_size;
  return CRYPTO_OK;
}

crypto_result_t crypto_process_public_key_packet(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len) {
  if (!ctx || !ctx->initialized || !packet) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t expected_size = sizeof(uint32_t) + CRYPTO_PUBLIC_KEY_SIZE;
  if (packet_len != expected_size) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Unpack packet: [type:4][public_key:32]
  uint32_t packet_type;
  SAFE_MEMCPY(&packet_type, sizeof(packet_type), packet, sizeof(packet_type));

  if (packet_type != CRYPTO_PACKET_PUBLIC_KEY) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  const uint8_t *peer_public_key = packet + sizeof(packet_type);
  return crypto_set_peer_public_key(ctx, peer_public_key);
}

crypto_result_t crypto_create_encrypted_packet(crypto_context_t *ctx, const uint8_t *data, size_t data_len,
                                               uint8_t *packet_out, size_t packet_size, size_t *packet_len_out) {
  if (!ctx || !data || !packet_out || !packet_len_out) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (!crypto_is_ready(ctx)) {
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  size_t encrypted_size = data_len + CRYPTO_NONCE_SIZE + CRYPTO_MAC_SIZE;
  size_t required_size = sizeof(uint32_t) + sizeof(uint32_t) + encrypted_size; // type + len + encrypted_data

  if (packet_size < required_size) {
    return CRYPTO_ERROR_BUFFER_TOO_SMALL;
  }

  // Encrypt the data
  size_t ciphertext_len;
  uint8_t *encrypted_data = packet_out + sizeof(uint32_t) + sizeof(uint32_t);
  crypto_result_t result = crypto_encrypt(ctx, data, data_len, encrypted_data,
                                          packet_size - sizeof(uint32_t) - sizeof(uint32_t), &ciphertext_len);
  if (result != CRYPTO_OK) {
    return result;
  }

  // Pack packet: [type:4][length:4][encrypted_data:var]
  uint32_t packet_type = CRYPTO_PACKET_ENCRYPTED_DATA;
  uint32_t data_length = (uint32_t)ciphertext_len;

  SAFE_MEMCPY(packet_out, sizeof(packet_type), &packet_type, sizeof(packet_type));
  SAFE_MEMCPY(packet_out + sizeof(packet_type), sizeof(data_length), &data_length, sizeof(data_length));

  *packet_len_out = required_size;
  return CRYPTO_OK;
}

crypto_result_t crypto_process_encrypted_packet(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len,
                                                uint8_t *data_out, size_t data_size, size_t *data_len_out) {
  if (!ctx || !packet || !data_out || !data_len_out) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (!crypto_is_ready(ctx)) {
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  if (packet_len < sizeof(uint32_t) + sizeof(uint32_t)) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Unpack packet: [type:4][length:4][encrypted_data:var]
  uint32_t packet_type;
  uint32_t data_length;
  SAFE_MEMCPY(&packet_type, sizeof(packet_type), packet, sizeof(packet_type));
  SAFE_MEMCPY(&data_length, sizeof(data_length), packet + sizeof(packet_type), sizeof(data_length));

  if (packet_type != CRYPTO_PACKET_ENCRYPTED_DATA) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (packet_len != sizeof(uint32_t) + sizeof(uint32_t) + data_length) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  const uint8_t *encrypted_data = packet + sizeof(uint32_t) + sizeof(uint32_t);
  return crypto_decrypt(ctx, encrypted_data, data_length, data_out, data_size, data_len_out);
}

// =============================================================================
// Authentication and handshake functions
// =============================================================================

crypto_result_t crypto_generate_nonce(uint8_t nonce[32]) {
  if (!nonce) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  crypto_result_t result = init_libsodium();
  if (result != CRYPTO_OK) {
    return result;
  }

  randombytes_buf(nonce, 32);
  return CRYPTO_OK;
}

crypto_result_t crypto_compute_hmac(const uint8_t key[32], const uint8_t data[32], uint8_t hmac[32]) {
  if (!key || !data || !hmac) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  crypto_result_t result = init_libsodium();
  if (result != CRYPTO_OK) {
    return result;
  }

  crypto_auth_hmacsha256(hmac, data, 32, key);
  return CRYPTO_OK;
}

bool crypto_verify_hmac(const uint8_t key[32], const uint8_t data[32], const uint8_t expected_hmac[32]) {
  if (!key || !data || !expected_hmac) {
    return false;
  }

  uint8_t computed_hmac[32];
  if (crypto_auth_hmacsha256(computed_hmac, data, 32, key) != 0) {
    return false;
  }

  return sodium_memcmp(computed_hmac, expected_hmac, 32) == 0;
}

crypto_result_t crypto_create_auth_challenge(const crypto_context_t *ctx, uint8_t *packet_out, size_t packet_size,
                                             size_t *packet_len_out) {
  if (!ctx || !ctx->initialized || !packet_out || !packet_len_out) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t required_size = sizeof(uint32_t) + 32; // type + nonce
  if (packet_size < required_size) {
    return CRYPTO_ERROR_BUFFER_TOO_SMALL;
  }

  // Generate random nonce
  crypto_result_t result = crypto_generate_nonce((uint8_t *)ctx->auth_nonce);
  if (result != CRYPTO_OK) {
    return result;
  }

  // Pack packet: [type:4][nonce:32]
  uint32_t packet_type = CRYPTO_PACKET_AUTH_CHALLENGE;
  SAFE_MEMCPY(packet_out, sizeof(packet_type), &packet_type, sizeof(packet_type));
  SAFE_MEMCPY(packet_out + sizeof(packet_type), 32, ctx->auth_nonce, 32);

  *packet_len_out = required_size;
  return CRYPTO_OK;
}

crypto_result_t crypto_process_auth_challenge(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len) {
  if (!ctx || !ctx->initialized || !packet) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t expected_size = sizeof(uint32_t) + 32; // type + nonce
  if (packet_len != expected_size) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Unpack packet: [type:4][nonce:32]
  uint32_t packet_type;
  SAFE_MEMCPY(&packet_type, sizeof(packet_type), packet, sizeof(packet_type));

  if (packet_type != CRYPTO_PACKET_AUTH_CHALLENGE) {
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Store the nonce for HMAC computation
  SAFE_MEMCPY(ctx->auth_nonce, 32, packet + sizeof(packet_type), 32);

  log_debug("Auth challenge received and processed");
  return CRYPTO_OK;
}

crypto_result_t crypto_process_auth_response(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len) {
  if (!ctx || !ctx->initialized || !packet) {
    log_error("crypto_process_auth_response: Invalid context or packet (ctx=%p, initialized=%d, packet=%p)", ctx,
              ctx ? ctx->initialized : 0, packet);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t expected_size = sizeof(uint32_t) + 32; // type + hmac
  if (packet_len != expected_size) {
    log_error("crypto_process_auth_response: Invalid packet size (expected=%zu, got=%zu)", expected_size, packet_len);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Unpack packet: [type:4][hmac:32]
  uint32_t packet_type;
  SAFE_MEMCPY(&packet_type, sizeof(packet_type), packet, sizeof(packet_type));

  if (packet_type != CRYPTO_PACKET_AUTH_RESPONSE) {
    log_error("crypto_process_auth_response: Invalid packet type (expected=0x%x, got=0x%x)",
              CRYPTO_PACKET_AUTH_RESPONSE, packet_type);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  const uint8_t *received_hmac = packet + sizeof(packet_type);

  // Verify HMAC using shared secret
  if (!crypto_verify_hmac(ctx->shared_key, ctx->auth_nonce, received_hmac)) {
    return CRYPTO_ERROR_INVALID_MAC;
  }

  ctx->handshake_complete = true;
  log_debug("Authentication successful - handshake complete");
  return CRYPTO_OK;
}
