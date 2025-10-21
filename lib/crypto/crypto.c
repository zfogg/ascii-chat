#include "crypto.h"
#include "common.h"
#include "asciichat_errno.h"

#include <string.h>
#include <time.h>
#include <inttypes.h>

// Static initialization flag for libsodium
static bool g_libsodium_initialized = false;

// Internal packet type constants (for test helper functions only)
// These are NOT part of the main network protocol - they're used for simple
// packet serialization in crypto module test utilities
static const uint32_t CRYPTO_PACKET_PUBLIC_KEY = 100;
static const uint32_t CRYPTO_PACKET_ENCRYPTED_DATA = 102;
static const uint32_t CRYPTO_PACKET_AUTH_CHALLENGE = 103;
static const uint32_t CRYPTO_PACKET_AUTH_RESPONSE = 104;

// =============================================================================
// Internal helper functions
// =============================================================================

// Initialize libsodium (thread-safe, idempotent)
static crypto_result_t init_libsodium(void) {
  if (g_libsodium_initialized) {
    return CRYPTO_OK;
  }

  if (sodium_init() < 0) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to initialize libsodium");
    return CRYPTO_ERROR_LIBSODIUM;
  }

  g_libsodium_initialized = true;
  return CRYPTO_OK;
}

// Generate secure nonce with session ID and counter to prevent reuse
static void generate_nonce(crypto_context_t *ctx, uint8_t *nonce_out) {
  // Nonce format (dynamic size based on algorithm):
  // - Bytes 0-15: Session ID (constant per connection, random across connections)
  // - Bytes 16+: Counter (increments per packet, prevents reuse within session)
  //
  // This prevents replay attacks both within and across sessions:
  // - Different session_id prevents cross-session replay
  // - Counter prevents within-session replay

  SAFE_MEMCPY(nonce_out, 16, ctx->session_id, 16);
  uint64_t counter = ctx->nonce_counter++;
  size_t counter_size = ctx->nonce_size - 16;
  SAFE_MEMCPY(nonce_out + 16, counter_size, &counter,
              (counter_size < sizeof(counter)) ? counter_size : sizeof(counter));
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
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p", ctx);
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

  // Set default algorithm-specific parameters (can be overridden during handshake)
  ctx->nonce_size = XSALSA20_NONCE_SIZE;
  ctx->mac_size = POLY1305_MAC_SIZE;
  ctx->hmac_size = HMAC_SHA256_SIZE;
  ctx->encryption_key_size = SECRETBOX_KEY_SIZE;
  ctx->public_key_size = X25519_KEY_SIZE;
  ctx->private_key_size = X25519_KEY_SIZE;
  ctx->shared_key_size = X25519_KEY_SIZE;
  ctx->salt_size = ARGON2ID_SALT_SIZE;
  ctx->signature_size = ED25519_SIGNATURE_SIZE;

  // Generate unique session ID to prevent replay attacks across connections
  randombytes_buf(ctx->session_id, sizeof(ctx->session_id));

  log_info("Crypto context initialized with X25519 key exchange");
  return CRYPTO_OK;
}

crypto_result_t crypto_init_with_password(crypto_context_t *ctx, const char *password) {
  if (!ctx || !password) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ctx=%p, password=%p", ctx, password);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (strlen(password) == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Password cannot be empty");
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
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_generate_keypair: NULL context");
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Generate X25519 key pair for key exchange
  if (crypto_box_keypair(ctx->public_key, ctx->private_key) != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to generate X25519 key pair");
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
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_get_public_key: Invalid parameters (ctx=%p, initialized=%d, public_key_out=%p)", ctx,
              ctx ? ctx->initialized : 0, public_key_out);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Bounds check to prevent buffer overflow
  size_t copy_size = (ctx->public_key_size <= X25519_KEY_SIZE) ? ctx->public_key_size : X25519_KEY_SIZE;
  SAFE_MEMCPY(public_key_out, copy_size, ctx->public_key, copy_size);
  return CRYPTO_OK;
}

crypto_result_t crypto_set_peer_public_key(crypto_context_t *ctx, const uint8_t *peer_public_key) {
  if (!ctx || !ctx->initialized || !peer_public_key) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_set_peer_public_key: Invalid parameters (ctx=%p, initialized=%d, peer_public_key=%p)", ctx,
              ctx ? ctx->initialized : 0, peer_public_key);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Store peer's public key
  // Bounds check to prevent buffer overflow
  size_t copy_size = (ctx->public_key_size <= X25519_KEY_SIZE) ? ctx->public_key_size : X25519_KEY_SIZE;
  SAFE_MEMCPY(ctx->peer_public_key, copy_size, peer_public_key, copy_size);
  ctx->peer_key_received = true;

  // Compute shared secret using X25519
  if (crypto_box_beforenm(ctx->shared_key, peer_public_key, ctx->private_key) != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Failed to compute shared secret from peer public key");
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

crypto_result_t crypto_validate_password(const char *password) {
  if (!password) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_validate_password: Password is NULL");
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t password_len = strlen(password);

  if (password_len < MIN_PASSWORD_LENGTH) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Password too short (minimum %d characters, got %zu)", MIN_PASSWORD_LENGTH,
              password_len);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (password_len > MAX_PASSWORD_LENGTH) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Password too long (maximum %d characters, got %zu)", MAX_PASSWORD_LENGTH,
              password_len);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  return CRYPTO_OK;
}

crypto_result_t crypto_derive_password_key(crypto_context_t *ctx, const char *password) {
  if (!ctx || !ctx->initialized || !password) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_derive_password_key: Invalid parameters (ctx=%p, initialized=%d, password=%p)", ctx,
              ctx ? ctx->initialized : 0, password);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Validate password length requirements
  crypto_result_t validation_result = crypto_validate_password(password);
  if (validation_result != CRYPTO_OK) {
    return validation_result;
  }

  // Use deterministic salt for consistent key derivation across client/server
  // This ensures the same password produces the same key on both sides
  // Salt must be exactly ARGON2ID_SALT_SIZE (32) bytes
  const char *deterministic_salt = "ascii-chat-password-salt-v1";
  size_t salt_str_len = strlen(deterministic_salt);

  // Zero-initialize the salt buffer first
  memset(ctx->password_salt, 0, ctx->salt_size);

  // Copy the salt string (will be padded with zeros to ctx->salt_size)
  memcpy(ctx->password_salt, deterministic_salt, (salt_str_len < ctx->salt_size) ? salt_str_len : ctx->salt_size);

  // Derive key using Argon2id (memory-hard, secure against GPU attacks)
  if (crypto_pwhash(ctx->password_key, ctx->encryption_key_size, password, strlen(password), ctx->password_salt,
                    crypto_pwhash_OPSLIMIT_INTERACTIVE, // ~0.1 seconds
                    crypto_pwhash_MEMLIMIT_INTERACTIVE, // ~64MB
                    crypto_pwhash_ALG_DEFAULT) != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Password key derivation failed - possibly out of memory");
    return CRYPTO_ERROR_PASSWORD_DERIVATION;
  }

  log_debug("Password key derived successfully using Argon2id with deterministic salt");
  return CRYPTO_OK;
}

bool crypto_verify_password(const crypto_context_t *ctx, const char *password) {
  if (!ctx || !ctx->initialized || !ctx->has_password || !password) {
    return false;
  }

  uint8_t test_key[SECRETBOX_KEY_SIZE]; // Use maximum size for buffer

  // Use the same deterministic salt for verification
  // Salt must be exactly ARGON2ID_SALT_SIZE (32) bytes
  const char *deterministic_salt = "ascii-chat-password-salt-v1";
  uint8_t salt[ARGON2ID_SALT_SIZE]; // Use maximum size for buffer
  size_t salt_str_len = strlen(deterministic_salt);

  // Zero-initialize the salt buffer first
  memset(salt, 0, ARGON2ID_SALT_SIZE);

  // Copy the salt string (will be padded with zeros to ctx->salt_size)
  memcpy(salt, deterministic_salt, (salt_str_len < ctx->salt_size) ? salt_str_len : ctx->salt_size);

  // Derive key with same salt
  if (crypto_pwhash(test_key, ctx->encryption_key_size, password, strlen(password), salt,
                    crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                    crypto_pwhash_ALG_DEFAULT) != 0) {
    secure_memzero(test_key, sizeof(test_key));
    return false;
  }

  // Constant-time comparison
  bool match = (sodium_memcmp(test_key, ctx->password_key, ctx->encryption_key_size) == 0);

  secure_memzero(test_key, sizeof(test_key));
  return match;
}

// Derive a deterministic encryption key from password for handshake
crypto_result_t crypto_derive_password_encryption_key(const char *password,
                                                      uint8_t encryption_key[SECRETBOX_KEY_SIZE]) {
  if (!password || !encryption_key) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_derive_password_encryption_key: Invalid parameters (password=%p, encryption_key=%p)", password,
              encryption_key);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Validate password length requirements
  crypto_result_t validation_result = crypto_validate_password(password);
  if (validation_result != CRYPTO_OK) {
    return validation_result;
  }

  // Use deterministic salt for consistent key derivation across client/server
  // Salt must be exactly ARGON2ID_SALT_SIZE (32) bytes
  const char *deterministic_salt = "ascii-chat-password-salt-v1";
  uint8_t salt[ARGON2ID_SALT_SIZE]; // Use maximum size for buffer
  size_t salt_str_len = strlen(deterministic_salt);

  // Zero-initialize the salt buffer first
  memset(salt, 0, ARGON2ID_SALT_SIZE);

  // Copy the salt string (will be padded with zeros to ARGON2ID_SALT_SIZE)
  memcpy(salt, deterministic_salt, (salt_str_len < ARGON2ID_SALT_SIZE) ? salt_str_len : ARGON2ID_SALT_SIZE);

  // Derive key using Argon2id (memory-hard, secure against GPU attacks)
  if (crypto_pwhash(encryption_key, SECRETBOX_KEY_SIZE, password, strlen(password), salt,
                    crypto_pwhash_OPSLIMIT_INTERACTIVE, // ~0.1 seconds
                    crypto_pwhash_MEMLIMIT_INTERACTIVE, // ~64MB
                    crypto_pwhash_ALG_DEFAULT) != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Password encryption key derivation failed - possibly out of memory");
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
    SET_ERRNO(ERROR_INVALID_PARAM,
              "Invalid parameters: ctx=%p, initialized=%d, plaintext=%p, ciphertext_out=%p, ciphertext_len_out=%p", ctx,
              ctx ? ctx->initialized : 0, plaintext, ciphertext_out, ciphertext_len_out);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (plaintext_len == 0 || plaintext_len > CRYPTO_MAX_PLAINTEXT_SIZE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid plaintext length: %zu (max: %d)", plaintext_len, CRYPTO_MAX_PLAINTEXT_SIZE);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (!crypto_is_ready(ctx)) {
    SET_ERRNO(ERROR_CRYPTO, "Crypto context not ready for encryption");
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  // Check output buffer size
  size_t required_size = plaintext_len + ctx->nonce_size + ctx->mac_size;
  if (ciphertext_out_size < required_size) {
    SET_ERRNO(ERROR_BUFFER, "Ciphertext buffer too small: %zu < %zu", ciphertext_out_size, required_size);
    return CRYPTO_ERROR_BUFFER_TOO_SMALL;
  }

  // Check for nonce counter exhaustion (extremely unlikely)
  if (ctx->nonce_counter == 0 || ctx->nonce_counter == UINT64_MAX) {
    SET_ERRNO(ERROR_CRYPTO, "Nonce counter exhausted - key rotation required");
    return CRYPTO_ERROR_NONCE_EXHAUSTED;
  }

  // Generate nonce and place at beginning of ciphertext
  uint8_t nonce[XSALSA20_NONCE_SIZE]; // Use maximum nonce size for buffer
  generate_nonce(ctx, nonce);
  SAFE_MEMCPY(ciphertext_out, ctx->nonce_size, nonce, ctx->nonce_size);

  // Choose encryption key (prefer shared key over password key)
  const uint8_t *encryption_key = NULL;
  if (ctx->key_exchange_complete) {
    encryption_key = ctx->shared_key;
  } else if (ctx->has_password) {
    encryption_key = ctx->password_key;
  } else {
    SET_ERRNO(ERROR_CRYPTO, "No encryption key available");
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  // Encrypt using NaCl secretbox (XSalsa20 + Poly1305)
  if (crypto_secretbox_easy(ciphertext_out + ctx->nonce_size, plaintext, plaintext_len, nonce, encryption_key) != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Encryption failed");
    return CRYPTO_ERROR_ENCRYPTION;
  }

  *ciphertext_len_out = required_size;
  ctx->bytes_encrypted += plaintext_len;

  return CRYPTO_OK;
}

crypto_result_t crypto_decrypt(crypto_context_t *ctx, const uint8_t *ciphertext, size_t ciphertext_len,
                               uint8_t *plaintext_out, size_t plaintext_out_size, size_t *plaintext_len_out) {
  if (!ctx || !ctx->initialized || !ciphertext || !plaintext_out || !plaintext_len_out) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "Invalid parameters: ctx=%p, initialized=%d, ciphertext=%p, plaintext_out=%p, plaintext_len_out=%p", ctx,
              ctx ? ctx->initialized : 0, ciphertext, plaintext_out, plaintext_len_out);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (!crypto_is_ready(ctx)) {
    SET_ERRNO(ERROR_CRYPTO, "Crypto context not ready for decryption");
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  // Check minimum ciphertext size (nonce + MAC)
  size_t min_ciphertext_size = ctx->nonce_size + ctx->mac_size;
  if (ciphertext_len < min_ciphertext_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Ciphertext too small: %zu < %zu", ciphertext_len, min_ciphertext_size);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t plaintext_len = ciphertext_len - ctx->nonce_size - ctx->mac_size;
  if (plaintext_out_size < plaintext_len) {
    SET_ERRNO(ERROR_BUFFER, "Plaintext buffer too small: %zu < %zu", plaintext_out_size, plaintext_len);
    return CRYPTO_ERROR_BUFFER_TOO_SMALL;
  }

  // Extract nonce from beginning of ciphertext
  const uint8_t *nonce = ciphertext;
  const uint8_t *encrypted_data = ciphertext + ctx->nonce_size;

  // Choose decryption key (prefer shared key over password key)
  const uint8_t *decryption_key = NULL;
  if (ctx->key_exchange_complete) {
    decryption_key = ctx->shared_key;
  } else if (ctx->has_password) {
    decryption_key = ctx->password_key;
  } else {
    SET_ERRNO(ERROR_CRYPTO, "No decryption key available");
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  // Decrypt using NaCl secretbox (XSalsa20 + Poly1305)
  if (crypto_secretbox_open_easy(plaintext_out, encrypted_data, ciphertext_len - ctx->nonce_size, nonce,
                                 decryption_key) != 0) {
    SET_ERRNO(ERROR_CRYPTO, "Decryption failed - invalid MAC or corrupted data");
    return CRYPTO_ERROR_INVALID_MAC;
  }

  *plaintext_len_out = plaintext_len;
  ctx->bytes_decrypted += plaintext_len;

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
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_random_bytes: Invalid parameters (buffer=%p, len=%zu)", buffer, len);
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
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_create_public_key_packet: Invalid parameters (ctx=%p, initialized=%d, packet_out=%p, "
              "packet_len_out=%p)",
              ctx, ctx ? ctx->initialized : 0, packet_out, packet_len_out);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t required_size = sizeof(uint32_t) + ctx->public_key_size; // type + key
  if (packet_size < required_size) {
    SET_ERRNO(ERROR_BUFFER, "crypto_create_public_key_packet: Buffer too small (size=%zu, required=%zu)", packet_size,
              required_size);
    return CRYPTO_ERROR_BUFFER_TOO_SMALL;
  }

  // Pack packet: [type:4][public_key:32]
  uint32_t packet_type = CRYPTO_PACKET_PUBLIC_KEY;
  SAFE_MEMCPY(packet_out, sizeof(packet_type), &packet_type, sizeof(packet_type));
  // Bounds check to prevent buffer overflow
  size_t copy_size = (ctx->public_key_size <= X25519_KEY_SIZE) ? ctx->public_key_size : X25519_KEY_SIZE;
  SAFE_MEMCPY(packet_out + sizeof(packet_type), copy_size, ctx->public_key, copy_size);

  *packet_len_out = required_size;
  return CRYPTO_OK;
}

crypto_result_t crypto_process_public_key_packet(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len) {
  if (!ctx || !ctx->initialized || !packet) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_process_public_key_packet: Invalid parameters (ctx=%p, initialized=%d, packet=%p)", ctx,
              ctx ? ctx->initialized : 0, packet);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t expected_size = sizeof(uint32_t) + ctx->public_key_size;
  if (packet_len != expected_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_public_key_packet: Invalid packet size (expected=%zu, got=%zu)",
              expected_size, packet_len);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Unpack packet: [type:4][public_key:32]
  uint32_t packet_type;
  SAFE_MEMCPY(&packet_type, sizeof(packet_type), packet, sizeof(packet_type));

  if (packet_type != CRYPTO_PACKET_PUBLIC_KEY) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_public_key_packet: Invalid packet type (expected=%u, got=%u)",
              CRYPTO_PACKET_PUBLIC_KEY, packet_type);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  const uint8_t *peer_public_key = packet + sizeof(packet_type);
  return crypto_set_peer_public_key(ctx, peer_public_key);
}

crypto_result_t crypto_create_encrypted_packet(crypto_context_t *ctx, const uint8_t *data, size_t data_len,
                                               uint8_t *packet_out, size_t packet_size, size_t *packet_len_out) {
  if (!ctx || !data || !packet_out || !packet_len_out) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_create_encrypted_packet: Invalid parameters (ctx=%p, data=%p, packet_out=%p, packet_len_out=%p)",
              ctx, data, packet_out, packet_len_out);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (!crypto_is_ready(ctx)) {
    SET_ERRNO(ERROR_CRYPTO, "crypto_create_encrypted_packet: Crypto context not ready");
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  size_t encrypted_size = data_len + ctx->nonce_size + ctx->mac_size;
  size_t required_size = sizeof(uint32_t) + sizeof(uint32_t) + encrypted_size; // type + len + encrypted_data

  if (packet_size < required_size) {
    SET_ERRNO(ERROR_BUFFER, "crypto_create_encrypted_packet: Buffer too small (size=%zu, required=%zu)", packet_size,
              required_size);
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
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_process_encrypted_packet: Invalid parameters (ctx=%p, packet=%p, data_out=%p, data_len_out=%p)",
              ctx, packet, data_out, data_len_out);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (!crypto_is_ready(ctx)) {
    SET_ERRNO(ERROR_CRYPTO, "crypto_process_encrypted_packet: Crypto context not ready");
    return CRYPTO_ERROR_KEY_EXCHANGE_INCOMPLETE;
  }

  if (packet_len < sizeof(uint32_t) + sizeof(uint32_t)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_encrypted_packet: Packet too small (size=%zu)", packet_len);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Unpack packet: [type:4][length:4][encrypted_data:var]
  uint32_t packet_type;
  uint32_t data_length;
  SAFE_MEMCPY(&packet_type, sizeof(packet_type), packet, sizeof(packet_type));
  SAFE_MEMCPY(&data_length, sizeof(data_length), packet + sizeof(packet_type), sizeof(data_length));

  if (packet_type != CRYPTO_PACKET_ENCRYPTED_DATA) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_encrypted_packet: Invalid packet type (expected=%u, got=%u)",
              CRYPTO_PACKET_ENCRYPTED_DATA, packet_type);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  if (packet_len != sizeof(uint32_t) + sizeof(uint32_t) + data_length) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_encrypted_packet: Packet length mismatch (expected=%zu, got=%zu)",
              sizeof(uint32_t) + sizeof(uint32_t) + data_length, packet_len);
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
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_generate_nonce: NULL nonce buffer");
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  crypto_result_t result = init_libsodium();
  if (result != CRYPTO_OK) {
    return result;
  }

  randombytes_buf(nonce, 32);
  return CRYPTO_OK;
}

crypto_result_t crypto_compute_hmac(crypto_context_t *ctx, const uint8_t key[32], const uint8_t data[32],
                                    uint8_t hmac[32]) {
  if (!ctx || !key || !data || !hmac) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "crypto_compute_hmac: Invalid parameters (ctx=%p, key=%p, data=%p, hmac=%p)",
                     ctx, key, data, hmac);
  }

  crypto_result_t result = init_libsodium();
  if (result != CRYPTO_OK) {
    return result;
  }

  crypto_auth_hmacsha256(hmac, data, 32, key);
  return CRYPTO_OK;
}

crypto_result_t crypto_compute_hmac_ex(const crypto_context_t *ctx, const uint8_t key[32], const uint8_t *data,
                                       size_t data_len, uint8_t hmac[32]) {
  if (!ctx || !key || !data || !hmac || data_len == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM,
                     "crypto_compute_hmac_ex: Invalid parameters (ctx=%p, key=%p, data=%p, data_len=%zu, hmac=%p)", ctx,
                     key, data, data_len, hmac);
  }

  crypto_result_t result = init_libsodium();
  if (result != CRYPTO_OK) {
    return result;
  }

  crypto_auth_hmacsha256(hmac, data, data_len, key);
  return CRYPTO_OK;
}

bool crypto_verify_hmac(const uint8_t key[32], const uint8_t data[32], const uint8_t expected_hmac[32]) {
  if (!key || !data || !expected_hmac) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_verify_hmac: Invalid parameters (key=%p, data=%p, expected_hmac=%p)", key,
              data, expected_hmac);
    return false;
  }

  uint8_t computed_hmac[32];
  if (crypto_auth_hmacsha256(computed_hmac, data, 32, key) != 0) {
    return false;
  }

  return sodium_memcmp(computed_hmac, expected_hmac, 32) == 0;
}

bool crypto_verify_hmac_ex(const uint8_t key[32], const uint8_t *data, size_t data_len,
                           const uint8_t expected_hmac[32]) {
  if (!key || !data || !expected_hmac || data_len == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_verify_hmac_ex: Invalid parameters (key=%p, data=%p, data_len=%zu, expected_hmac=%p)", key, data,
              data_len, expected_hmac);
    return false;
  }

  uint8_t computed_hmac[32];
  if (crypto_auth_hmacsha256(computed_hmac, data, data_len, key) != 0) {
    return false;
  }

  return sodium_memcmp(computed_hmac, expected_hmac, 32) == 0;
}

// =============================================================================
// High-level authentication helpers (shared between client and server)
// =============================================================================

crypto_result_t crypto_compute_auth_response(const crypto_context_t *ctx, const uint8_t nonce[32],
                                             uint8_t hmac_out[32]) {
  if (!ctx || !nonce || !hmac_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_compute_auth_response: Invalid parameters (ctx=%p, nonce=%p, hmac_out=%p)",
              ctx, nonce, hmac_out);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Bind password HMAC to DH shared_secret to prevent MITM
  // Combined data: nonce || shared_secret
  uint8_t combined_data[64];
  memcpy(combined_data, nonce, 32);
  memcpy(combined_data + 32, ctx->shared_key, 32);

  // Use password_key if available, otherwise use shared_key
  const uint8_t *auth_key = ctx->has_password ? ctx->password_key : ctx->shared_key;

  return crypto_compute_hmac_ex(ctx, auth_key, combined_data, 64, hmac_out);
}

bool crypto_verify_auth_response(const crypto_context_t *ctx, const uint8_t nonce[32],
                                 const uint8_t expected_hmac[32]) {
  if (!ctx || !nonce || !expected_hmac) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_verify_auth_response: Invalid parameters (ctx=%p, nonce=%p, expected_hmac=%p)", ctx, nonce,
              expected_hmac);
    return false;
  }

  // Bind password HMAC to DH shared_secret to prevent MITM
  // Combined data: nonce || shared_secret
  uint8_t combined_data[64];
  memcpy(combined_data, nonce, 32);
  memcpy(combined_data + 32, ctx->shared_key, 32);

  // Use password_key if available, otherwise use shared_key
  const uint8_t *auth_key = ctx->has_password ? ctx->password_key : ctx->shared_key;

  return crypto_verify_hmac_ex(auth_key, combined_data, 64, expected_hmac);
}

crypto_result_t crypto_create_auth_challenge(const crypto_context_t *ctx, uint8_t *packet_out, size_t packet_size,
                                             size_t *packet_len_out) {
  if (!ctx || !ctx->initialized || !packet_out || !packet_len_out) {
    SET_ERRNO(
        ERROR_INVALID_PARAM,
        "crypto_create_auth_challenge: Invalid parameters (ctx=%p, initialized=%d, packet_out=%p, packet_len_out=%p)",
        ctx, ctx ? ctx->initialized : 0, packet_out, packet_len_out);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t required_size = sizeof(uint32_t) + 32; // type + nonce
  if (packet_size < required_size) {
    SET_ERRNO(ERROR_BUFFER, "crypto_create_auth_challenge: Buffer too small (size=%zu, required=%zu)", packet_size,
              required_size);
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
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_process_auth_challenge: Invalid parameters (ctx=%p, initialized=%d, packet=%p)", ctx,
              ctx ? ctx->initialized : 0, packet);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t expected_size = sizeof(uint32_t) + 32; // type + nonce
  if (packet_len != expected_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_auth_challenge: Invalid packet size (expected=%zu, got=%zu)",
              expected_size, packet_len);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Unpack packet: [type:4][nonce:32]
  uint32_t packet_type;
  SAFE_MEMCPY(&packet_type, sizeof(packet_type), packet, sizeof(packet_type));

  if (packet_type != CRYPTO_PACKET_AUTH_CHALLENGE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_auth_challenge: Invalid packet type (expected=%u, got=%u)",
              CRYPTO_PACKET_AUTH_CHALLENGE, packet_type);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Store the nonce for HMAC computation
  SAFE_MEMCPY(ctx->auth_nonce, 32, packet + sizeof(packet_type), 32);

  log_debug("Auth challenge received and processed");
  return CRYPTO_OK;
}

crypto_result_t crypto_process_auth_response(crypto_context_t *ctx, const uint8_t *packet, size_t packet_len) {
  if (!ctx || !ctx->initialized || !packet) {
    SET_ERRNO(ERROR_INVALID_PARAM,
              "crypto_process_auth_response: Invalid context or packet (ctx=%p, initialized=%d, packet=%p)", ctx,
              ctx ? ctx->initialized : 0, packet);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  size_t expected_size = sizeof(uint32_t) + 32; // type + hmac
  if (packet_len != expected_size) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_auth_response: Invalid packet size (expected=%zu, got=%zu)",
              expected_size, packet_len);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  // Unpack packet: [type:4][hmac:32]
  uint32_t packet_type;
  SAFE_MEMCPY(&packet_type, sizeof(packet_type), packet, sizeof(packet_type));

  if (packet_type != CRYPTO_PACKET_AUTH_RESPONSE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "crypto_process_auth_response: Invalid packet type (expected=0x%x, got=0x%x)",
              CRYPTO_PACKET_AUTH_RESPONSE, packet_type);
    return CRYPTO_ERROR_INVALID_PARAMS;
  }

  const uint8_t *received_hmac = packet + sizeof(packet_type);

  // Verify HMAC using shared secret
  if (!crypto_verify_hmac(ctx->shared_key, ctx->auth_nonce, received_hmac)) {
    SET_ERRNO(ERROR_CRYPTO, "crypto_process_auth_response: HMAC verification failed");
    return CRYPTO_ERROR_INVALID_MAC;
  }

  ctx->handshake_complete = true;
  log_debug("Authentication successful - handshake complete");
  return CRYPTO_OK;
}

// =============================================================================
// Shared Cryptographic Operations
// =============================================================================

asciichat_error_t crypto_compute_password_hmac(crypto_context_t *ctx, const uint8_t *password_key, const uint8_t *nonce,
                                               const uint8_t *shared_secret, uint8_t *hmac_out) {
  if (!ctx || !password_key || !nonce || !shared_secret || !hmac_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM,
                     "Invalid parameters: ctx=%p, password_key=%p, nonce=%p, shared_secret=%p, hmac_out=%p", ctx,
                     password_key, nonce, shared_secret, hmac_out);
  }

  // Combine nonce and shared_secret for HMAC computation
  // This binds the password to the DH shared secret, preventing MITM attacks
  uint8_t combined_data[64];
  memcpy(combined_data, nonce, 32);
  memcpy(combined_data + 32, shared_secret, 32);

  // Compute HMAC using the password-derived key
  if (crypto_compute_hmac_ex(ctx, password_key, combined_data, 64, hmac_out) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to compute password HMAC");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t crypto_verify_peer_signature(const uint8_t *peer_public_key, const uint8_t *ephemeral_key,
                                               size_t ephemeral_key_size, const uint8_t *signature) {
  if (!peer_public_key || !ephemeral_key || !signature) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: peer_public_key=%p, ephemeral_key=%p, signature=%p",
                     peer_public_key, ephemeral_key, signature);
  }

  if (ephemeral_key_size == 0) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ephemeral key size: %zu", ephemeral_key_size);
  }

  // Verify the signature using Ed25519
  if (crypto_sign_verify_detached(signature, ephemeral_key, ephemeral_key_size, peer_public_key) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Peer signature verification failed");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t crypto_sign_ephemeral_key(const private_key_t *private_key, const uint8_t *ephemeral_key,
                                            size_t ephemeral_key_size, uint8_t *signature_out) {
  if (!private_key || !ephemeral_key || !signature_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: private_key=%p, ephemeral_key=%p, signature_out=%p",
                     private_key, ephemeral_key, signature_out);
  }

  if (ephemeral_key_size == 0) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid ephemeral key size: %zu", ephemeral_key_size);
    return ERROR_INVALID_PARAM;
  }

  // Sign the ephemeral key with our Ed25519 private key
  if (private_key->type == KEY_TYPE_ED25519) {
    if (crypto_sign_detached(signature_out, NULL, ephemeral_key, ephemeral_key_size, private_key->key.ed25519) != 0) {
      return SET_ERRNO(ERROR_CRYPTO, "Failed to sign ephemeral key");
    }
  } else {
    return SET_ERRNO(ERROR_CRYPTO, "Unsupported private key type for signing");
  }

  return ASCIICHAT_OK;
}

void crypto_combine_auth_data(const uint8_t *hmac, const uint8_t *challenge_nonce, uint8_t *combined_out) {
  if (!hmac || !challenge_nonce || !combined_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: hmac=%p, challenge_nonce=%p, combined_out=%p", hmac,
              challenge_nonce, combined_out);
    return;
  }

  // Combine HMAC (32 bytes) + challenge nonce (32 bytes) = 64 bytes total
  memcpy(combined_out, hmac, 32);
  memcpy(combined_out + 32, challenge_nonce, 32);
}

void crypto_extract_auth_data(const uint8_t *combined_data, uint8_t *hmac_out, uint8_t *challenge_out) {
  if (!combined_data || !hmac_out || !challenge_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: combined_data=%p, hmac_out=%p, challenge_out=%p", combined_data,
              hmac_out, challenge_out);
    return;
  }

  // Extract HMAC (first 32 bytes) and challenge nonce (last 32 bytes)
  memcpy(hmac_out, combined_data, 32);
  memcpy(challenge_out, combined_data + 32, 32);
}
