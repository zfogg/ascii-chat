/**
 * @file crypto/keys/ssh_keys.c
 * @ingroup keys
 * @brief 🔐 SSH key parsing and management for RSA, ECDSA, and Ed25519 key types
 */

#include <ascii-chat/ui/notice.h>

#include <ascii-chat/crypto/crypto.h> // Includes <sodium.h>
#include <ascii-chat/crypto/ssh/ssh_keys.h>
#include <ascii-chat/crypto/regex.h>
#include <ascii-chat/common.h>
#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/util/password.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/bytes.h>
#include <ascii-chat/crypto/ssh/ssh_agent.h>
#include <ascii-chat/crypto/gpg/gpg.h> // For GPG agent signing support
#include <ascii-chat-deps/bearssl/inc/bearssl.h>
#include <ascii-chat-deps/libsodium-bcrypt-pbkdf/include/sodium_bcrypt_pbkdf.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <sys/stat.h>

// =============================================================================
// Helper Functions
// =============================================================================

// Forward declarations
static asciichat_error_t base64_decode_ssh_key(const char *base64, size_t base64_len, uint8_t **blob_out,
                                               size_t *blob_len);
#ifndef EMSCRIPTEN_BUILD
static asciichat_error_t decrypt_openssh_private_key(const uint8_t *encrypted_blob, size_t blob_len,
                                                     const char *passphrase, const uint8_t *salt, size_t salt_len,
                                                     uint32_t rounds, const char *cipher_name, uint8_t **decrypted_out,
                                                     size_t *decrypted_len);

/**
 * @brief Decrypt OpenSSH encrypted private key using AES-CTR or AES-CBC
 *
 * Uses BearSSL for AES decryption and sodium_bcrypt_pbkdf for key derivation.
 *
 * @param encrypted_blob Encrypted data (including IV)
 * @param blob_len Length of encrypted data
 * @param passphrase User's passphrase
 * @param salt bcrypt salt from KDF options
 * @param salt_len Salt length (typically 16 bytes)
 * @param rounds Number of bcrypt rounds
 * @param cipher_name Cipher algorithm ("aes256-ctr" or "aes256-cbc")
 * @param iv Initialization vector (16 bytes)
 * @param decrypted_out Output buffer for decrypted data
 * @param decrypted_len Output length of decrypted data
 * @return ASCIICHAT_OK on success, error code on failure
 */
static asciichat_error_t decrypt_openssh_private_key(const uint8_t *encrypted_blob, size_t blob_len,
                                                     const char *passphrase, const uint8_t *salt, size_t salt_len,
                                                     uint32_t rounds, const char *cipher_name, uint8_t **decrypted_out,
                                                     size_t *decrypted_len) {
  // OpenSSH uses: key_len + iv_len derived from bcrypt_pbkdf
  // For AES-256: key=32 bytes, iv=16 bytes = 48 bytes total
  const size_t key_size = 32;
  const size_t iv_size = 16;
  const size_t derived_size = key_size + iv_size;

  uint8_t *derived = SAFE_MALLOC(derived_size, uint8_t *);
  if (!derived) {
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for key derivation");
  }

  // Use libsodium-bcrypt-pbkdf (OpenBSD implementation)
  if (sodium_bcrypt_pbkdf(passphrase, strlen(passphrase), salt, salt_len, derived, derived_size, rounds) != 0) {
    SAFE_FREE(derived);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to derive decryption key with sodium_bcrypt_pbkdf");
  }

  const uint8_t *key = derived;
  const uint8_t *derived_iv = derived + key_size;

  // Decrypt using BearSSL
  uint8_t *decrypted = SAFE_MALLOC(blob_len, uint8_t *);
  if (!decrypted) {
    sodium_memzero(derived, derived_size);
    SAFE_FREE(derived);
    return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for decryption");
  }

  if (strcmp(cipher_name, "aes256-ctr") == 0) {
    // AES-256-CTR decryption using BearSSL
    // OpenSSH derives a full 16-byte IV from bcrypt_pbkdf
    // BearSSL expects: 12-byte nonce + 4-byte counter (big-endian uint32)
    // We split the 16-byte IV: first 12 bytes as nonce, last 4 bytes as initial counter
    br_aes_ct_ctr_keys aes_ctx;
    br_aes_ct_ctr_init(&aes_ctx, key, key_size);

    // Extract initial counter value from bytes 12-15 of derived IV (big-endian)
    uint32_t initial_counter = ((uint32_t)derived_iv[12] << 24) | ((uint32_t)derived_iv[13] << 16) |
                               ((uint32_t)derived_iv[14] << 8) | ((uint32_t)derived_iv[15]);

    // Decrypt in-place
    memcpy(decrypted, encrypted_blob, blob_len);
    br_aes_ct_ctr_run(&aes_ctx, derived_iv, initial_counter, decrypted, blob_len);
  } else if (strcmp(cipher_name, "aes256-cbc") == 0) {
    // AES-256-CBC decryption using BearSSL
    br_aes_ct_cbcdec_keys aes_ctx;
    br_aes_ct_cbcdec_init(&aes_ctx, key, key_size);

    // Copy IV to working buffer
    uint8_t working_iv[16];
    memcpy(working_iv, derived_iv, 16);

    // Decrypt in-place
    memcpy(decrypted, encrypted_blob, blob_len);
    br_aes_ct_cbcdec_run(&aes_ctx, working_iv, decrypted, blob_len);
  } else {
    sodium_memzero(derived, derived_size);
    SAFE_FREE(derived);
    SAFE_FREE(decrypted);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported cipher: %s", cipher_name);
  }

  // Clean up sensitive data
  sodium_memzero(derived, derived_size);
  SAFE_FREE(derived);

  *decrypted_out = decrypted;
  *decrypted_len = blob_len;

  return ASCIICHAT_OK;
}
#endif

// Base64 decode SSH key blob
static asciichat_error_t base64_decode_ssh_key(const char *base64, size_t base64_len, uint8_t **blob_out,
                                               size_t *blob_len) {
  if (!base64 || !blob_out || !blob_len) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for base64 decode");
  }

  // Allocate max possible size
  *blob_out = SAFE_MALLOC(base64_len, uint8_t *);

  const char *end;
  int result = sodium_base642bin(*blob_out, base64_len, base64, base64_len,
                                 NULL, // ignore chars
                                 blob_len, &end, sodium_base64_VARIANT_ORIGINAL);

  if (result != 0) {
    SAFE_FREE(*blob_out);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to decode base64 SSH key data");
  }

  return ASCIICHAT_OK;
}

// =============================================================================
// SSH Key Parsing Implementation
// =============================================================================

asciichat_error_t parse_ssh_ed25519_line(const char *line, uint8_t ed25519_pk[32]) {
  if (!line || !ed25519_pk) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: line=%p, ed25519_pk=%p", line, ed25519_pk);
  }

  // Use regex to parse and extract SSH public key components
  char *base64_key = NULL, *comment = NULL;
  if (!crypto_regex_match_public_key(line, &base64_key, &comment)) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "SSH key line does not match expected format");
  }

  // Base64 decode
  uint8_t *blob;
  size_t blob_len;
  if (base64_decode_ssh_key(base64_key, strlen(base64_key), &blob, &blob_len) != 0) {
    SAFE_FREE(base64_key);
    SAFE_FREE(comment);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to decode SSH key base64 data");
  }

  // Clean up regex-extracted strings
  SAFE_FREE(base64_key);
  SAFE_FREE(comment);

  // Parse SSH key blob structure:
  // [4 bytes: length of "ssh-ed25519"]
  // [11 bytes: "ssh-ed25519"]
  // [4 bytes: length of public key (32)]
  // [32 bytes: Ed25519 public key]

  if (blob_len < SSH_KEY_HEADER_SIZE) {
    SAFE_FREE(blob);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "SSH key blob too small: %zu bytes (expected at least %d)", blob_len,
                     SSH_KEY_HEADER_SIZE);
  }

  // Extract Ed25519 public key (last 32 bytes)
  memcpy(ed25519_pk, blob + blob_len - ED25519_PUBLIC_KEY_SIZE, ED25519_PUBLIC_KEY_SIZE);
  SAFE_FREE(blob);

  return ASCIICHAT_OK;
}

typedef struct {
  char *file_content;
  uint8_t *key_blob;
  size_t key_blob_len;
  size_t offset;
  uint32_t ciphername_len;
  size_t ciphername_pos;
  bool is_encrypted;
  uint32_t kdfname_len;
  size_t kdfname_pos;
  uint32_t kdfoptions_len;
  uint32_t bcrypt_rounds;
  uint32_t salt_len;
  char *password;
  uint8_t *decrypted_blob;
  size_t decrypted_blob_len;
  size_t dec_offset;
  char ciphername[32];
  uint8_t bcrypt_salt[16];
  uint8_t ed25519_pubkey[32];
} ssh_private_key_parse_t;

#ifndef EMSCRIPTEN_BUILD
static bool ssh_private_key_from_agent(const char *key_path, private_key_t *key_out) {
  // First, check if we can get the key from ssh-agent (password-free)
  // This requires reading the public key from the .pub file
  char pub_key_path[BUFFER_SIZE_LARGE];
  safe_snprintf(pub_key_path, sizeof(pub_key_path), "%s.pub", key_path);

  FILE *pub_f = platform_fopen("file_stream", pub_key_path, "r");
  if (pub_f) {
    char pub_line[BUFFER_SIZE_LARGE];
    if (fgets(pub_line, sizeof(pub_line), pub_f)) {
      public_key_t pub_key = {0};
      pub_key.type = KEY_TYPE_ED25519;

      if (parse_ssh_ed25519_line(pub_line, pub_key.key) == ASCIICHAT_OK) {
        // Check if this key is in ssh-agent
        if (ssh_agent_has_key(&pub_key)) {
          fclose(pub_f);
          log_dev("Key found in ssh-agent - using cached key (no password required)");
          // Key is in agent, we can use it
          memset(key_out, 0, sizeof(*key_out));
          key_out->type = KEY_TYPE_ED25519;
          key_out->use_ssh_agent = true;
          memcpy(key_out->key.ed25519 + 32, pub_key.key, 32); // Copy public key to second half
          memcpy(key_out->public_key, pub_key.key, 32);       // Also store in public_key field
          // Note: We don't have the private key seed, but ssh-agent will sign for us
          return true;
        } else {
          log_debug("Key not found in ssh-agent - will decrypt from file");
        }
      }
    }
    fclose(pub_f);
  }
  return false;
}

#endif

static asciichat_error_t ssh_private_key_read_file(ssh_private_key_parse_t *state, const char *key_path) {
  // Validate the SSH key file first
  asciichat_error_t validation_result = validate_ssh_key_file(key_path);
  if (validation_result != ASCIICHAT_OK) {
    return validation_result;
  }

  // Read the private key file
  FILE *f = platform_fopen("file_stream", key_path, "r");
  if (!f) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot read private key file: %s", key_path);
  }

  // Read the entire file
  state->file_content = NULL;
  size_t file_size = 0;
  char buffer[BUFFER_SIZE_XXLARGE];
  size_t bytes_read;

  while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
    state->file_content = SAFE_REALLOC(state->file_content, file_size + bytes_read + 1, char *);
    if (!state->file_content) {
      (void)fclose(f);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Out of memory reading private key file");
    }
    memcpy(state->file_content + file_size, buffer, bytes_read);
    file_size += bytes_read;
  }
  (void)fclose(f);

  if (file_size == 0) {
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key file is empty: %s", key_path);
  }

  state->file_content[file_size] = '\0';
  return ASCIICHAT_OK;
}

static asciichat_error_t ssh_private_key_decode_pem(ssh_private_key_parse_t *state, const char *key_path) {
  // Use regex to extract base64 data from OpenSSH PEM format
  char *pem_base64_data = NULL;
  if (!crypto_regex_extract_pem_base64(state->file_content, &pem_base64_data)) {
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH private key format: %s", key_path);
  }

  // Remove any whitespace/newlines from the base64 data
  char *clean_base64 = SAFE_MALLOC(strlen(pem_base64_data) + 1, char *);
  char *clean_ptr = clean_base64;
  for (const char *p = pem_base64_data; *p; p++) {
    if (*p != '\n' && *p != '\r' && *p != ' ' && *p != '\t') {
      *clean_ptr++ = *p;
    }
  }
  *clean_ptr = '\0';
  SAFE_FREE(pem_base64_data);

  // Decode the base64 data

  asciichat_error_t decode_result =
      base64_decode_ssh_key(clean_base64, strlen(clean_base64), &state->key_blob, &state->key_blob_len);
  SAFE_FREE(clean_base64);

  if (decode_result != ASCIICHAT_OK) {
    SAFE_FREE(state->file_content);
    return decode_result;
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t ssh_private_key_read_header(ssh_private_key_parse_t *state, const char *key_path) {
  // Parse the OpenSSH private key structure
  // Format: [4 bytes: magic] [4 bytes: ciphername length] [ciphername] [4 bytes: kdfname length] [kdfname]
  //         [4 bytes: kdfoptions length] [kdfoptions] [4 bytes: num keys] [4 bytes: pubkey length] [pubkey]
  //         [4 bytes: privkey length] [privkey]

  if (state->key_blob_len < 4) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key blob too small: %s", key_path);
  }

  // Check magic number (should be "openssh-key-v1\0")
  if (memcmp(state->key_blob, "openssh-key-v1\0", 15) != 0) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH private key magic: %s", key_path);
  }

  state->offset = 15; // Skip magic

  // Read ciphername
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at ciphername: %s", key_path);
  }

  state->ciphername_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;

  if (state->offset + state->ciphername_len > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at ciphername data: %s", key_path);
  }

  // Store the position of ciphername for later use
  state->ciphername_pos = state->offset;

  // Check if key is encrypted
  state->is_encrypted = (state->ciphername_len > 0 && memcmp(state->key_blob + state->offset, "none", 4) != 0);

  state->offset += state->ciphername_len;

  // Skip kdfname and kdfoptions (we don't support encryption)
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at kdfname: %s", key_path);
  }

  state->kdfname_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;

  // Store the position of kdfname for later use
  state->kdfname_pos = state->offset;

  state->offset += state->kdfname_len;

  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at kdfoptions: %s", key_path);
  }

  state->kdfoptions_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4 + state->kdfoptions_len;
  return ASCIICHAT_OK;
}

#ifndef EMSCRIPTEN_BUILD
static asciichat_error_t ssh_private_key_read_encryption(ssh_private_key_parse_t *state, const char *key_path) {
  // Parse the cipher name from the stored position

  if (state->ciphername_len > 0 && state->ciphername_len < sizeof(state->ciphername)) {
    memcpy(state->ciphername, state->key_blob + state->ciphername_pos, state->ciphername_len);
  }

  // Parse the KDF name from the stored position
  char kdfname[32] = {0};
  if (state->kdfname_len > 0 && state->kdfname_len < sizeof(kdfname)) {
    memcpy(kdfname, state->key_blob + state->kdfname_pos, state->kdfname_len);
  }

  log_debug("Cipher: %s, KDF: %s", state->ciphername, kdfname);

  // Check if we support this encryption method
  if (strcmp(state->ciphername, "aes256-ctr") != 0 && strcmp(state->ciphername, "aes256-cbc") != 0) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY,
                     "Unsupported cipher '%s' for encrypted SSH key: %s\n"
                     "Supported ciphers: aes256-ctr, aes256-cbc",
                     state->ciphername, key_path);
  }

  if (strcmp(kdfname, "bcrypt") != 0) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY,
                     "Unsupported KDF '%s' for encrypted SSH key: %s\n"
                     "Only bcrypt KDF is supported",
                     kdfname, key_path);
  }

  // Parse KDF options (bcrypt salt and rounds)
  if (state->kdfoptions_len < 8) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid KDF options length: %s", key_path);
  }

  // Parse KDF options: [salt_length:4][salt:N][rounds:4]
  size_t kdf_opt_offset = state->offset - state->kdfoptions_len;

  // Read salt length
  if (kdf_opt_offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "KDF options truncated at salt length: %s", key_path);
  }
  state->salt_len = read_u32_be(&state->key_blob[kdf_opt_offset]);
  kdf_opt_offset += 4;

  // Read salt
  if (state->salt_len != 16) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unexpected bcrypt salt length %u (expected 16): %s", state->salt_len, key_path);
  }
  if (kdf_opt_offset + state->salt_len > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "KDF options truncated at salt data: %s", key_path);
  }

  memcpy(state->bcrypt_salt, state->key_blob + kdf_opt_offset, state->salt_len);
  kdf_opt_offset += state->salt_len;

  // Read rounds
  if (kdf_opt_offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "KDF options truncated at rounds: %s", key_path);
  }
  state->bcrypt_rounds = read_u32_be(&state->key_blob[kdf_opt_offset]);
  return ASCIICHAT_OK;
}

static asciichat_error_t ssh_private_key_read_password(ssh_private_key_parse_t *state, const char *key_path) {
  // Check for password in environment variable first
  const char *env_password = platform_getenv("ASCII_CHAT_KEY_PASSWORD");
  state->password = NULL;
  if (env_password && strlen(env_password) > 0) {
    // Use password from environment variable
    state->password = platform_strdup(env_password);
    if (!state->password) {
      SAFE_FREE(state->key_blob);
      SAFE_FREE(state->file_content);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for password");
    }
  } else {
    // Prompt for password interactively - allocate buffer for input
    state->password = SAFE_MALLOC(1024, char *);
    if (!state->password) {
      SAFE_FREE(state->key_blob);
      SAFE_FREE(state->file_content);
      return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for password");
    }
    if (prompt_password_simple("Encrypted SSH key - enter passphrase", state->password, 1024) != 0) {
      SAFE_FREE(state->key_blob);
      SAFE_FREE(state->file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to read passphrase for encrypted key: %s", key_path);
    }
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t ssh_private_key_decrypt(ssh_private_key_parse_t *state, const char *key_path) {
  // Native OpenSSH key decryption using bcrypt_pbkdf + BearSSL AES

  // Skip past the unencrypted public key section to get to encrypted private keys
  // Format: [num_keys:4][pubkey_len:4][pubkey:N]...[encrypted_len:4][encrypted:N]

  // Read num_keys
  if (state->offset + 4 > state->key_blob_len) {
    sodium_memzero(state->password, strlen(state->password));
    SAFE_FREE(state->password);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Encrypted key truncated at num_keys: %s", key_path);
  }
  uint32_t num_keys = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;
  log_debug("num_keys=%u", num_keys);

  // Skip all public keys
  for (uint32_t i = 0; i < num_keys; i++) {
    if (state->offset + 4 > state->key_blob_len) {
      sodium_memzero(state->password, strlen(state->password));
      SAFE_FREE(state->password);
      SAFE_FREE(state->key_blob);
      SAFE_FREE(state->file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Encrypted key truncated at pubkey %u length: %s", i, key_path);
    }
    uint32_t pubkey_len = read_u32_be(&state->key_blob[state->offset]);
    state->offset += 4;
    log_debug("Skipping public key %u: %u bytes", i, pubkey_len);

    if (state->offset + pubkey_len > state->key_blob_len) {
      sodium_memzero(state->password, strlen(state->password));
      SAFE_FREE(state->password);
      SAFE_FREE(state->key_blob);
      SAFE_FREE(state->file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Encrypted key truncated at pubkey %u data: %s", i, key_path);
    }
    state->offset += pubkey_len;
  }

  // Read encrypted private keys length
  if (state->offset + 4 > state->key_blob_len) {
    sodium_memzero(state->password, strlen(state->password));
    SAFE_FREE(state->password);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Encrypted key truncated at encrypted_len: %s", key_path);
  }
  uint32_t encrypted_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;

  // Now offset points to the actual encrypted data
  size_t encrypted_data_start = state->offset;
  size_t encrypted_data_len = encrypted_len;

  if (encrypted_data_len < 16) {
    sodium_memzero(state->password, strlen(state->password));
    SAFE_FREE(state->password);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Encrypted data too small: %s", key_path);
  }

  // Extract encrypted blob (includes everything from offset onwards)
  const uint8_t *encrypted_blob = state->key_blob + encrypted_data_start;

  // Call native decryption function
  state->decrypted_blob = NULL;
  state->decrypted_blob_len = 0;

  // Note: decrypt_openssh_private_key derives IV from bcrypt_pbkdf, not from encrypted data
  asciichat_error_t decrypt_result = decrypt_openssh_private_key(
      encrypted_blob, encrypted_data_len, state->password, state->bcrypt_salt, state->salt_len, state->bcrypt_rounds,
      state->ciphername, &state->decrypted_blob, &state->decrypted_blob_len);

  // Clean up password immediately after use
  sodium_memzero(state->password, strlen(state->password));
  SAFE_FREE(state->password);

  if (decrypt_result != ASCIICHAT_OK) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return decrypt_result;
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t ssh_private_key_read_decrypted_type(ssh_private_key_parse_t *state, const char *key_path) {
  // Parse the decrypted private key structure
  // OpenSSH format (decrypted):
  //   [checkint1:4][checkint2:4][keytype:string][pubkey:string][privkey:string][comment:string][padding:N]

  if (state->decrypted_blob_len < 8) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Decrypted data too small (no checkints): %s", key_path);
  }

  // Verify checkints (first 8 bytes should be two identical 32-bit values)
  uint32_t checkint1 = read_u32_be(&state->decrypted_blob[0]);
  uint32_t checkint2 = read_u32_be(&state->decrypted_blob[4]);
  if (checkint1 != checkint2) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY,
                     "Incorrect passphrase or corrupted key (checkint mismatch): %s\n"
                     "Expected matching checkints, got 0x%08x != 0x%08x",
                     key_path, checkint1, checkint2);
  }

  // Parse the decrypted private key structure manually
  // Format after checkints: [keytype:string][pubkey:string][privkey:string][comment:string][padding:N]
  state->dec_offset = 8; // Skip checkints

  // Read keytype length
  if (state->dec_offset + 4 > state->decrypted_blob_len) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Decrypted key truncated at keytype length: %s", key_path);
  }
  uint32_t keytype_len = read_u32_be(&state->decrypted_blob[state->dec_offset]);
  state->dec_offset += 4;

  // Read keytype
  if (state->dec_offset + keytype_len > state->decrypted_blob_len) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Decrypted key truncated at keytype data: %s", key_path);
  }
  char keytype[BUFFER_SIZE_SMALL / 4] = {0}; // SSH key type strings are short
  if (keytype_len > 0 && keytype_len < sizeof(keytype)) {
    memcpy(keytype, state->decrypted_blob + state->dec_offset, keytype_len);
  }
  state->dec_offset += keytype_len;

  // Check if it's Ed25519
  if (strcmp(keytype, "ssh-ed25519") != 0) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported key type after decryption: '%s'", keytype);
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t ssh_private_key_read_decrypted_material(ssh_private_key_parse_t *state, const char *key_path,
                                                                 private_key_t *key_out) {
  // Read public key length
  if (state->dec_offset + 4 > state->decrypted_blob_len) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Decrypted key truncated at pubkey length: %s", key_path);
  }
  uint32_t pubkey_data_len = read_u32_be(&state->decrypted_blob[state->dec_offset]);
  state->dec_offset += 4;

  // Read public key (32 bytes for Ed25519)
  if (pubkey_data_len != 32) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid Ed25519 public key length: %u (expected 32)", pubkey_data_len);
  }
  if (state->dec_offset + pubkey_data_len > state->decrypted_blob_len) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Decrypted key truncated at pubkey data: %s", key_path);
  }
  uint8_t ed25519_pk[32];
  memcpy(ed25519_pk, state->decrypted_blob + state->dec_offset, 32);
  state->dec_offset += 32;

  // Read private key length
  if (state->dec_offset + 4 > state->decrypted_blob_len) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Decrypted key truncated at privkey length: %s", key_path);
  }
  uint32_t privkey_data_len = read_u32_be(&state->decrypted_blob[state->dec_offset]);
  state->dec_offset += 4;

  // Read private key (64 bytes for Ed25519: 32-byte seed + 32-byte public key)
  if (privkey_data_len != 64) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid Ed25519 private key length: %u (expected 64)", privkey_data_len);
  }
  if (state->dec_offset + privkey_data_len > state->decrypted_blob_len) {
    SAFE_FREE(state->decrypted_blob);
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Decrypted key truncated at privkey data: %s", key_path);
  }
  uint8_t ed25519_sk[64];
  memcpy(ed25519_sk, state->decrypted_blob + state->dec_offset, 64);
  state->dec_offset += 64;

  // Populate key_out (ed25519_sk contains: 32-byte seed + 32-byte public key)
  memset(key_out, 0, sizeof(*key_out));
  key_out->type = KEY_TYPE_ED25519;
  memcpy(key_out->key.ed25519, ed25519_sk, 32);           // Seed (first 32 bytes)
  memcpy(key_out->key.ed25519 + 32, ed25519_sk + 32, 32); // Public key (next 32 bytes)
  memcpy(key_out->public_key, ed25519_pk, sizeof(ed25519_pk));
  SAFE_STRNCPY(key_out->key_comment, "ssh-ed25519", sizeof(key_out->key_comment) - 1);
  sodium_memzero(ed25519_sk, sizeof(ed25519_sk));

  // Clean up decrypted data (sensitive!)
  sodium_memzero(state->decrypted_blob, state->decrypted_blob_len);
  SAFE_FREE(state->decrypted_blob);
  SAFE_FREE(state->key_blob);
  SAFE_FREE(state->file_content);

  log_debug("Successfully parsed decrypted Ed25519 key");
  return ASCIICHAT_OK;
}

static void ssh_private_key_cache_in_agent(const char *key_path, const private_key_t *key_out) {
  // Attempt to add the decrypted key to ssh-agent for future password-free use
  log_debug("Attempting to add decrypted key to ssh-agent");
  asciichat_error_t agent_result = ssh_agent_add_key(key_out, key_path);
  if (agent_result == ASCIICHAT_OK) {
    log_debug("Successfully added key to ssh-agent - password will not be required on next run");
  } else {
    // Non-fatal: key is already decrypted and loaded, just won't be cached in agent
    log_warn("Failed to add key to ssh-agent (non-fatal): %s", asciichat_error_format(NULL, agent_result));
    log_warn("You can manually add it with: ssh-add %s", key_path);
  }
}

static asciichat_error_t ssh_private_key_parse_encrypted(ssh_private_key_parse_t *state, const char *key_path,
                                                         private_key_t *key_out) {
  asciichat_error_t result = ssh_private_key_read_encryption(state, key_path);
  if (result != ASCIICHAT_OK)
    return result;
  result = ssh_private_key_read_password(state, key_path);
  if (result != ASCIICHAT_OK)
    return result;
  result = ssh_private_key_decrypt(state, key_path);
  if (result != ASCIICHAT_OK)
    return result;
  result = ssh_private_key_read_decrypted_type(state, key_path);
  if (result != ASCIICHAT_OK)
    return result;
  result = ssh_private_key_read_decrypted_material(state, key_path, key_out);
  if (result != ASCIICHAT_OK)
    return result;
  ssh_private_key_cache_in_agent(key_path, key_out);
  return ASCIICHAT_OK;
}
#endif

static asciichat_error_t ssh_private_key_read_public(ssh_private_key_parse_t *state, const char *key_path) {
  // Read number of keys
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at num keys: %s", key_path);
  }

  uint32_t num_keys = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;

  log_debug("num_keys=%u, offset=%zu, key_blob_len=%zu", num_keys, state->offset, state->key_blob_len);
  log_debug("Raw bytes at offset %zu: %02x %02x %02x %02x", state->offset, state->key_blob[state->offset],
            state->key_blob[state->offset + 1], state->key_blob[state->offset + 2], state->key_blob[state->offset + 3]);
  log_debug("After num_keys, offset=%zu", state->offset);

  if (num_keys != 1) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key contains %u keys (expected 1): %s", num_keys, key_path);
  }

  // Read public key
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at pubkey length: %s", key_path);
  }

  log_debug("About to read pubkey_len at offset=%zu, bytes: %02x %02x %02x %02x", state->offset,
            state->key_blob[state->offset], state->key_blob[state->offset + 1], state->key_blob[state->offset + 2],
            state->key_blob[state->offset + 3]);

  uint32_t pubkey_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;

  log_debug("pubkey_len=%u, offset=%zu", pubkey_len, state->offset);

  if (state->offset + pubkey_len > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at pubkey data: %s", key_path);
  }

  // Parse the public key to extract the Ed25519 public key for validation
  if (pubkey_len < 4) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH public key too small: %s", key_path);
  }

  uint32_t key_type_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;

  // Check if it's an Ed25519 key
  if (key_type_len != 11 || memcmp(state->key_blob + state->offset, "ssh-ed25519", 11) != 0) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key is not Ed25519: %s", key_path);
  }

  state->offset += key_type_len; // Skip the key type string
  log_debug("After skipping key type, offset=%zu", state->offset);

  // Read the public key length
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at public key length: %s", key_path);
  }

  uint32_t pubkey_data_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;
  log_debug("Public key data length: %u, offset=%zu", pubkey_data_len, state->offset);

  log_debug("Public key data length: %u (expected 32 for Ed25519)", pubkey_data_len);

  // For Ed25519, the public key should be 32 bytes, but let's be more flexible
  if (pubkey_data_len < 32) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH public key data too small (%u bytes, expected at least 32): %s",
                     pubkey_data_len, key_path);
  }

  // Read Ed25519 public key (first 32 bytes) for validation
  if (state->offset + 32 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at public key: %s", key_path);
  }

  memcpy(state->ed25519_pubkey, state->key_blob + state->offset, 32);
  state->offset += pubkey_data_len; // Skip the entire public key data

  // Skip the rest of the public key data to get to the private key
  // We've already parsed: 4 (key_type_len) + 11 (ssh-ed25519) + 4 (pubkey_data_len) + 32 (key) = 51 bytes
  // So we need to skip the remaining pubkey_len - 51 bytes
  // Check for underflow before subtraction
  if (pubkey_len >= 51) {
    size_t remaining_pubkey = pubkey_len - 51;
    if (remaining_pubkey > 0) {
      state->offset += remaining_pubkey;
    }
  }
  return ASCIICHAT_OK;
}

static asciichat_error_t ssh_private_key_read_private_header(ssh_private_key_parse_t *state, const char *key_path) {
  // Read private key
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at privkey length: %s", key_path);
  }

  uint32_t privkey_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;

  if (state->offset + privkey_len > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at privkey data: %s", key_path);
  }

  // Parse the private key structure
  // Format: [4 bytes: checkint1] [4 bytes: checkint2] [4 bytes: key type length] [key type]
  //         [4 bytes: public key length] [public key] [4 bytes: private key length] [private key]
  //         [4 bytes: comment length] [comment]

  if (privkey_len < 8) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key data too small: %s", key_path);
  }

  // Verify checkints (should be equal)
  uint32_t checkint1 = read_u32_be(&state->key_blob[state->offset]);
  uint32_t checkint2 = read_u32_be(&state->key_blob[state->offset + 4]);
  state->offset += 8;

  if (checkint1 != checkint2) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key checkints don't match: %s", key_path);
  }

  // Skip key type
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at key type length: %s", key_path);
  }

  uint32_t key_type_len_priv = read_u32_be(&state->key_blob[state->offset]);
  // Check for integer overflow before adding to offset
  if (key_type_len_priv > state->key_blob_len || state->offset + 4 + key_type_len_priv > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key type length overflow: %u", key_type_len_priv);
  }
  state->offset += 4 + key_type_len_priv;

  // Skip public key
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at pubkey length: %s", key_path);
  }

  uint32_t pubkey_len_priv = read_u32_be(&state->key_blob[state->offset]);
  // Check for integer overflow before adding to offset
  if (pubkey_len_priv > state->key_blob_len || state->offset + 4 + pubkey_len_priv > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key public key length overflow: %u", pubkey_len_priv);
  }
  state->offset += 4 + pubkey_len_priv;

  return ASCIICHAT_OK;
}

static asciichat_error_t ssh_private_key_read_private(ssh_private_key_parse_t *state, const char *key_path,
                                                      private_key_t *key_out) {
  // Read private key
  if (state->offset + 4 > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at privkey length: %s", key_path);
  }

  uint32_t privkey_data_len = read_u32_be(&state->key_blob[state->offset]);
  state->offset += 4;

  if (state->offset + privkey_data_len > state->key_blob_len) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at privkey data: %s", key_path);
  }

  // The private key data should be at least 64 bytes (32 bytes private key + 32 bytes public key)
  // But OpenSSH format may have additional data
  if (privkey_data_len < 64) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key data length is %u (expected at least 64): %s",
                     privkey_data_len, key_path);
  }

  // Extract the Ed25519 private key (first 32 bytes)
  uint8_t ed25519_privkey[32];
  memcpy(ed25519_privkey, state->key_blob + state->offset, 32);

  // Verify the public key matches
  // The public key in the privkey section is raw Ed25519, while the one in pubkey section is SSH format
  // We need to compare the raw public key from privkey with the raw public key extracted from pubkey
  // Use constant-time comparison for cryptographic material
  if (sodium_memcmp(state->key_blob + state->offset + 32, state->ed25519_pubkey, 32) != 0) {
    SAFE_FREE(state->key_blob);
    SAFE_FREE(state->file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key public key mismatch: %s", key_path);
  }

  // Initialize the private key structure
  memset(key_out, 0, sizeof(private_key_t));
  key_out->type = KEY_TYPE_ED25519;

  // Store the actual private key (seed + public key = 64 bytes)
  // Ed25519 private key format: [32 bytes seed][32 bytes public key]
  memcpy(key_out->key.ed25519, ed25519_privkey, 32);            // Store the seed (first 32 bytes)
  memcpy(key_out->key.ed25519 + 32, state->ed25519_pubkey, 32); // Store the public key (next 32 bytes)

  // Also store the public key in the public_key field for easy access
  memcpy(key_out->public_key, state->ed25519_pubkey, 32);

  // Set a comment
  SAFE_STRNCPY(key_out->key_comment, "ssh-ed25519", sizeof(key_out->key_comment) - 1);

  SAFE_FREE(state->key_blob);
  SAFE_FREE(state->file_content);
  return ASCIICHAT_OK;
}

asciichat_error_t parse_ssh_private_key(const char *key_path, private_key_t *key_out) {
  if (!key_path || !key_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p, key_out=%p", key_path, key_out);
  }
#ifndef EMSCRIPTEN_BUILD
  if (ssh_private_key_from_agent(key_path, key_out))
    return ASCIICHAT_OK;
#endif

  // Each stage retains the existing failure cleanup; later stages run only on success.
  ssh_private_key_parse_t state = {0};
  asciichat_error_t result = ssh_private_key_read_file(&state, key_path);
  if (result != ASCIICHAT_OK)
    return result;
  result = ssh_private_key_decode_pem(&state, key_path);
  if (result != ASCIICHAT_OK)
    return result;
  result = ssh_private_key_read_header(&state, key_path);
  if (result != ASCIICHAT_OK)
    return result;

  if (state.is_encrypted) {
#ifdef EMSCRIPTEN_BUILD
    SAFE_FREE(state.key_blob);
    SAFE_FREE(state.file_content);
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "Passphrase-protected SSH keys are not supported in the browser");
#else
    return ssh_private_key_parse_encrypted(&state, key_path, key_out);
#endif
  }

  result = ssh_private_key_read_public(&state, key_path);
  if (result != ASCIICHAT_OK)
    return result;
  result = ssh_private_key_read_private_header(&state, key_path);
  if (result != ASCIICHAT_OK)
    return result;
  return ssh_private_key_read_private(&state, key_path, key_out);
}

asciichat_error_t validate_ssh_key_file(const char *key_path) {
  if (!key_path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
  }

#ifdef EMSCRIPTEN_BUILD
  // Browser callers stage imported key text at this private virtual-FS path;
  // native path expansion and whitelist checks do not apply to that sandbox.
  static const char wasm_key_prefix[] = "/tmp/ascii-chat-key-";
  if (strncmp(key_path, wasm_key_prefix, sizeof(wasm_key_prefix) - 1) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid browser SSH key path");
  }
  const char *validated_path = key_path;
  char *normalized_path = NULL;
#else
  if (!path_looks_like_path(key_path)) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid SSH key path: %s", key_path);
  }

  char *normalized_path = NULL;
  asciichat_error_t path_result = path_validate_user_path(key_path, PATH_ROLE_KEY_PRIVATE, &normalized_path);
  if (path_result != ASCIICHAT_OK) {
    SAFE_FREE(normalized_path);
    return path_result;
  }
  const char *validated_path = normalized_path;
#endif

  // Check if file exists and is readable
  FILE *test_file = platform_fopen("file_stream", validated_path, "rb");
  if (test_file == NULL) {
    SAFE_FREE(normalized_path);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot read key file: %s", key_path);
  }

  // Check if this is an SSH key file or GPG armored key by looking for the header
  char header[BUFFER_SIZE_SMALL];
  bool is_valid_key_file = false;
  size_t header_len = fread(header, 1, sizeof(header) - 1, test_file);
  if (header_len > 0) {
    header[header_len] = '\0';
    if (strstr(header, "BEGIN OPENSSH PRIVATE KEY") != NULL || strstr(header, "BEGIN RSA PRIVATE KEY") != NULL ||
        strstr(header, "BEGIN EC PRIVATE KEY") != NULL || strstr(header, "BEGIN PGP PRIVATE KEY") != NULL ||
        strstr(header, "BEGIN PGP SECRET KEY") != NULL) {
      is_valid_key_file = true;
    } else {
      uint8_t first_packet = (uint8_t)header[0];
      unsigned int packet_tag = (first_packet & 0x80u)
                                    ? ((first_packet & 0x40u) ? (first_packet & 0x3fu) : ((first_packet >> 2) & 0x0fu))
                                    : 0u;
      is_valid_key_file = packet_tag == 5u;
    }
  }
  (void)fclose(test_file);

  if (!is_valid_key_file) {
    SAFE_FREE(normalized_path);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "File is not a valid SSH or GPG key: %s", key_path);
  }

  // Check permissions for SSH key files (should be 600 or 400)
#ifndef _WIN32
  struct stat st;
  if (stat(validated_path, &st) == 0) {
    if ((st.st_mode & SSH_KEY_PERMISSIONS_MASK) != 0) {
      NOTICE(DANGER, "UNSAFE PRIVATE KEY PERMISSIONS",
             "SSH key file %s has overly permissive permissions: %o.\nRun chmod 600 on this key file before retrying.",
             key_path, st.st_mode & 0777);
      SAFE_FREE(normalized_path);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "SSH key file has overly permissive permissions: %s", key_path);
    }
  }
#endif

  SAFE_FREE(normalized_path);
  return ASCIICHAT_OK;
}

// =============================================================================
// Key Conversion Functions
// =============================================================================

asciichat_error_t ed25519_to_x25519_public(const uint8_t ed25519_pk[32], uint8_t x25519_pk[32]) {
  if (!ed25519_pk || !x25519_pk) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ed25519_pk=%p, x25519_pk=%p", ed25519_pk, x25519_pk);
  }

  // Convert Ed25519 public key to X25519 public key
  if (crypto_sign_ed25519_pk_to_curve25519(x25519_pk, ed25519_pk) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to convert Ed25519 public key to X25519");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t ed25519_to_x25519_private(const uint8_t ed25519_sk[64], uint8_t x25519_sk[32]) {
  if (!ed25519_sk || !x25519_sk) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: ed25519_sk=%p, x25519_sk=%p", ed25519_sk, x25519_sk);
  }

  // Convert Ed25519 private key to X25519 private key
  if (crypto_sign_ed25519_sk_to_curve25519(x25519_sk, ed25519_sk) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to convert Ed25519 private key to X25519");
  }

  return ASCIICHAT_OK;
}

// =============================================================================
// SSH Key Operations
// =============================================================================

asciichat_error_t ed25519_sign_message(const private_key_t *key, const uint8_t *message, size_t message_len,
                                       uint8_t signature[64]) {
  if (!key || !message || !signature) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key=%p, message=%p, signature=%p", key, message,
                     signature);
  }

  if (key->type != KEY_TYPE_ED25519) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Key is not an Ed25519 key");
  }

#ifndef EMSCRIPTEN_BUILD
  // If using GPG agent, delegate signing to GPG agent
  if (key->use_gpg_agent) {
    log_debug("Using GPG agent for Ed25519 signing (keygrip: %.40s)", key->gpg_keygrip);

    // Connect to GPG agent
    int agent_sock = gpg_agent_connect();
    log_debug("GPG agent connection result: %d", agent_sock);

    if (agent_sock < 0) {
      log_debug("GPG agent not available, falling back to gpg --detach-sign for signing");

      // Extract GPG key ID from key comment ("GPG key <key_id>")
      const char *key_id_prefix = "GPG key ";
      const char *key_id = NULL;
      if (strncmp(key->key_comment, key_id_prefix, strlen(key_id_prefix)) == 0) {
        key_id = key->key_comment + strlen(key_id_prefix);
      }

      log_debug("Extracted GPG key ID: %s (len=%zu)", key_id ? key_id : "NULL", key_id ? strlen(key_id) : 0);

      if (!key_id || strlen(key_id) != 16) {
        return SET_ERRNO(ERROR_CRYPTO, "Cannot extract GPG key ID from comment for fallback signing");
      }

      // Use gpg --detach-sign fallback
      log_debug("Calling gpg_sign_detached_ed25519 with key %s", key_id);
      int result = gpg_sign_detached_ed25519(key_id, message, message_len, signature);
      log_debug("gpg_sign_detached_ed25519 result: %d", result);

      if (result != 0) {
        return SET_ERRNO(ERROR_CRYPTO, "GPG fallback signing failed (both agent and gpg command failed)");
      }

      log_debug("Successfully signed message with gpg --detach-sign fallback (64 bytes)");
      return ASCIICHAT_OK;
    }

    log_debug("Using GPG agent for signing");

    // Sign the message using GPG agent
    size_t sig_len = 0;
    int result = gpg_agent_sign(agent_sock, key->gpg_keygrip, message, message_len, signature, &sig_len);

    // Disconnect from agent
    gpg_agent_disconnect(agent_sock);

    if (result != 0) {
      return SET_ERRNO(ERROR_CRYPTO, "GPG agent signing failed");
    }

    if (sig_len != 64) {
      return SET_ERRNO(ERROR_CRYPTO, "GPG agent returned invalid signature length: %zu (expected 64)", sig_len);
    }

    log_debug("Successfully signed message with GPG agent (64 bytes)");
    return ASCIICHAT_OK;
  }

  // If using SSH agent, delegate signing to SSH agent
  if (key->use_ssh_agent) {
    log_debug("Using SSH agent for Ed25519 signing");

    // Create a public_key_t from the stored public key
    public_key_t pub_key = {0};
    pub_key.type = KEY_TYPE_ED25519;
    memcpy(pub_key.key, key->public_key, 32);

    // Sign using SSH agent
    asciichat_error_t result = ssh_agent_sign(&pub_key, message, message_len, signature);
    if (result != ASCIICHAT_OK) {
      return result; // Error already set by ssh_agent_sign
    }

    log_debug("Successfully signed message with SSH agent (64 bytes)");
    return ASCIICHAT_OK;
  }
#else
  if (key->use_gpg_agent || key->use_ssh_agent) {
    return SET_ERRNO(ERROR_NOT_SUPPORTED, "External SSH and GPG agents are not available in the browser");
  }
#endif

  // Sign the message with Ed25519 (in-memory key)
  if (crypto_sign_detached(signature, NULL, message, message_len, key->key.ed25519) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to sign message with Ed25519");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t ed25519_verify_signature(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                                           const uint8_t signature[64], const char *gpg_key_id) {
  if (!public_key || !message || !signature) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: public_key=%p, message=%p, signature=%p", public_key,
                     message, signature);
  }

  // Try standard Ed25519 verification first
  if (crypto_sign_verify_detached(signature, message, message_len, public_key) == 0) {
    return ASCIICHAT_OK;
  }

#ifndef EMSCRIPTEN_BUILD
  // If standard verification fails, try GPG fallback (for GPG-signed messages)
  // Use provided gpg_key_id if available, otherwise check environment variable (for tests)
  const char *key_id_to_use = gpg_key_id;
  size_t key_id_len = key_id_to_use ? strlen(key_id_to_use) : 0;
  // Accept 8, 16, or 40 character GPG key IDs (short, long, or full fingerprint)
  bool valid_key_id = (key_id_len == 8 || key_id_len == 16 || key_id_len == 40);

  if (!valid_key_id) {
    key_id_to_use = platform_getenv("TEST_GPG_KEY_ID");
    key_id_len = key_id_to_use ? strlen(key_id_to_use) : 0;
    valid_key_id = (key_id_len == 8 || key_id_len == 16 || key_id_len == 40);
  }

  if (valid_key_id) {
    log_debug("Standard Ed25519 verification failed, trying GPG fallback with key %s", key_id_to_use);
    int gpg_result = gpg_verify_detached_ed25519(key_id_to_use, message, message_len, signature);
    if (gpg_result == 0) {
      log_debug("GPG verification succeeded");
      return ASCIICHAT_OK;
    }
    log_debug("GPG verification also failed");
  }
#endif

  return SET_ERRNO(ERROR_CRYPTO, "Ed25519 signature verification failed (tried both libsodium and GPG)");
}
