#include "crypto/crypto.h"
#include "ssh_keys.h"
#include "common.h"
#include "asciichat_errno.h"
#include "platform/password.h"
#include "platform/internal.h"
#include <sodium.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#define unlink _unlink
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

// =============================================================================
// Helper Functions
// =============================================================================

// Forward declarations
static asciichat_error_t base64_decode_ssh_key(const char *base64, size_t base64_len, uint8_t **blob_out,
                                               size_t *blob_len);
static asciichat_error_t parse_ssh_private_key_structure(const uint8_t *key_blob, size_t key_blob_len,
                                                         private_key_t *key_out);

// Parse OpenSSH key file format
static asciichat_error_t parse_openssh_key_file(const char *key_content, size_t key_size, private_key_t *key_out) {
  (void)key_size; // Suppress unused parameter warning
  if (!key_content || !key_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_openssh_key_file");
  }

  // Find the base64 data
  const char *base64_start = strstr(key_content, "-----BEGIN OPENSSH PRIVATE KEY-----");
  if (!base64_start) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH key format - missing header");
  }

  const char *base64_end = strstr(base64_start, "-----END OPENSSH PRIVATE KEY-----");
  if (!base64_end) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH key format - missing footer");
  }

  // Extract base64 data (remove newlines)
  base64_start = strchr(base64_start, '\n') + 1; // Skip header line
  if (!base64_start || base64_start >= base64_end) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH key format - no base64 data");
  }

  // Remove newlines from base64 data
  char *clean_base64 = SAFE_MALLOC(base64_end - base64_start + 1, char *);
  if (!clean_base64) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to allocate memory for base64");
  }

  char *dest = clean_base64;
  for (const char *src = base64_start; src < base64_end; src++) {
    if (*src != '\n' && *src != '\r') {
      *dest++ = *src;
    }
  }
  *dest = '\0';

  size_t base64_len = dest - clean_base64;

  // Decode the base64 data
  uint8_t *key_blob;
  size_t key_blob_len;
  if (base64_decode_ssh_key(clean_base64, base64_len, &key_blob, &key_blob_len) != 0) {
    SAFE_FREE(clean_base64);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to decode OpenSSH key base64");
  }

  SAFE_FREE(clean_base64);

  // Parse the binary blob (same as encrypted key parsing)
  asciichat_error_t result = parse_ssh_private_key_structure(key_blob, key_blob_len, key_out);

  SAFE_FREE(key_blob);
  return result;
}

// Parse SSH private key structure from binary blob
static asciichat_error_t parse_ssh_private_key_structure(const uint8_t *key_blob, size_t key_blob_len,
                                                         private_key_t *key_out) {
  if (!key_blob || !key_out || key_blob_len < 15) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters for parse_ssh_private_key_structure");
  }

  // Check magic number
  if (memcmp(key_blob, "openssh-key-v1\0", 15) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH private key magic");
  }

  size_t offset = 15; // Skip magic

  // Read ciphername (should be "none" for decrypted keys)
  if (offset + 4 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at ciphername");
  }

  uint32_t ciphername_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  if (offset + ciphername_len > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at ciphername data");
  }

  // Debug: print ciphername
  char ciphername[64] = {0};
  size_t copy_len = (ciphername_len < 63) ? ciphername_len : 63;
  memcpy(ciphername, key_blob + offset, copy_len);
  log_debug("DEBUG: Decrypted key ciphername: '%s' (len=%u)", ciphername, ciphername_len);

  offset += ciphername_len;

  // Skip kdfname and kdfoptions (should be empty for decrypted keys)
  if (offset + 4 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at kdfname");
  }

  uint32_t kdfname_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;
  offset += kdfname_len;

  if (offset + 4 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at kdfoptions");
  }

  uint32_t kdfoptions_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;
  offset += kdfoptions_len;

  // Read number of keys
  if (offset + 4 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at num_keys");
  }

  uint32_t num_keys =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  log_debug("DEBUG: Decrypted key has %u keys", num_keys);

  if (num_keys != 1) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key contains %u keys (expected 1)", num_keys);
  }

  // Read public key
  if (offset + 4 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at pubkey length");
  }

  uint32_t pubkey_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  if (offset + pubkey_len > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at pubkey data");
  }

  // Parse the public key to extract the Ed25519 public key
  if (pubkey_len < 4) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH public key too small");
  }

  log_debug("DEBUG: Public key length: %u, offset: %zu", pubkey_len, offset);

  uint32_t key_type_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  log_debug("DEBUG: Key type length: %u", key_type_len);

  if (key_type_len != 11 || memcmp(key_blob + offset, "ssh-ed25519", 11) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key is not Ed25519");
  }

  offset += key_type_len; // Skip the key type string

  // Read the public key length
  if (offset + 4 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at public key length");
  }

  uint32_t pubkey_data_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  if (pubkey_data_len != 32) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH public key data length is %u (expected 32 for Ed25519)",
                     pubkey_data_len);
  }

  if (offset + 32 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at public key data");
  }

  // Extract Ed25519 public key
  memcpy(key_out->public_key, key_blob + offset, 32);
  offset += 32;

  // Read private key
  if (offset + 4 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at private key length");
  }

  uint32_t private_key_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  if (offset + private_key_len > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at private key data");
  }

  // The private key data contains the actual private key material plus metadata
  // For Ed25519, this should be at least 32 bytes of private key data
  if (private_key_len < 32) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key data length is %u (expected at least 32 for Ed25519)",
                     private_key_len);
  }

  if (offset + 32 > key_blob_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at private key data");
  }

  // The private key section contains:
  // [checkint1:4][checkint2:4][keytype_len:4][keytype][pubkey_len:4][pubkey:32][privkey_len:4][privkey_data:64][comment_len:4][comment][padding]

  // Parse the private key section structure
  size_t privkey_section_start = offset;

  // Verify we have enough data for checkints
  if (private_key_len < 8) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key section too small: %u bytes", private_key_len);
  }

  // Read and verify checkints
  uint32_t checkint1 =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  uint32_t checkint2 =
      (key_blob[offset + 4] << 24) | (key_blob[offset + 5] << 16) | (key_blob[offset + 6] << 8) | key_blob[offset + 7];
  offset += 8;

  if (checkint1 != checkint2) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key checkints don't match");
  }

  log_debug("DEBUG: Checkints verified: %u", checkint1);

  // Skip key type
  if (offset + 4 - privkey_section_start > private_key_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key section truncated at keytype length");
  }

  uint32_t privkey_keytype_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  if (offset + privkey_keytype_len - privkey_section_start > private_key_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key section truncated at keytype");
  }

  offset += privkey_keytype_len; // Skip keytype string

  log_debug("DEBUG: Skipped keytype (%u bytes)", privkey_keytype_len);

  // Skip public key
  if (offset + 4 - privkey_section_start > private_key_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key section truncated at pubkey length");
  }

  uint32_t privkey_pubkey_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  if (offset + privkey_pubkey_len - privkey_section_start > private_key_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key section truncated at pubkey");
  }

  offset += privkey_pubkey_len; // Skip pubkey

  log_debug("DEBUG: Skipped pubkey (%u bytes)", privkey_pubkey_len);

  // Read private key data length
  if (offset + 4 - privkey_section_start > private_key_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key section truncated at privkey length");
  }

  uint32_t privkey_data_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  log_debug("DEBUG: Private key data length: %u", privkey_data_len);

  // For Ed25519, the private key data should be exactly 64 bytes
  if (privkey_data_len != 64) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Ed25519 private key data length is %u (expected 64)", privkey_data_len);
  }

  if (offset + 64 - privkey_section_start > private_key_len) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key section truncated at privkey data");
  }

  // Extract the full 64-byte Ed25519 private key (32-byte seed + 32-byte public key)
  memcpy(key_out->key.ed25519, key_blob + offset, 32);           // Seed (first 32 bytes)
  memcpy(key_out->key.ed25519 + 32, key_blob + offset + 32, 32); // Public key (next 32 bytes)

  // Also save public key separately for easy access
  memcpy(key_out->public_key, key_blob + offset + 32, 32);

  log_debug("DEBUG: Extracted 64-byte Ed25519 key (32-byte seed + 32-byte pubkey)");

  // Set the key type
  key_out->type = KEY_TYPE_ED25519;

  // Set a default comment
  SAFE_STRNCPY(key_out->key_comment, "decrypted", sizeof(key_out->key_comment) - 1);

  log_debug("DEBUG: Decrypted key parsed successfully - seed+pubkey extracted (64 bytes total)");

  return ASCIICHAT_OK;
}

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

  // Find "ssh-ed25519 "
  const char *type_start = strstr(line, "ssh-ed25519");
  if (!type_start) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "SSH key line does not contain 'ssh-ed25519'");
  }

  // Skip to base64 part
  const char *base64_start = type_start + 11; // strlen("ssh-ed25519")
  while (*base64_start == ' ' || *base64_start == '\t') {
    base64_start++;
  }

  // Find end of base64 (space, newline, or end of string)
  const char *base64_end = base64_start;
  while (*base64_end && *base64_end != ' ' && *base64_end != '\t' && *base64_end != '\n' && *base64_end != '\r') {
    base64_end++;
  }

  size_t base64_len = base64_end - base64_start;

  // Base64 decode
  uint8_t *blob;
  size_t blob_len;
  if (base64_decode_ssh_key(base64_start, base64_len, &blob, &blob_len) != 0) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to decode SSH key base64 data");
  }

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

asciichat_error_t parse_ssh_private_key(const char *key_path, private_key_t *key_out) {
  if (!key_path || !key_out) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p, key_out=%p", key_path, key_out);
  }

  // Validate the SSH key file first
  asciichat_error_t validation_result = validate_ssh_key_file(key_path);
  if (validation_result != ASCIICHAT_OK) {
    return validation_result;
  }

  // Read the private key file
  FILE *f = platform_fopen(key_path, "r");
  if (!f) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot read private key file: %s", key_path);
  }

  // Read the entire file
  char *file_content = NULL;
  size_t file_size = 0;
  char buffer[4096];
  size_t bytes_read;

  while ((bytes_read = fread(buffer, 1, sizeof(buffer), f)) > 0) {
    file_content = SAFE_REALLOC(file_content, file_size + bytes_read + 1, char *);
    if (!file_content) {
      (void)fclose(f);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Out of memory reading private key file");
    }
    memcpy(file_content + file_size, buffer, bytes_read);
    file_size += bytes_read;
  }
  (void)fclose(f);

  if (file_size == 0) {
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Private key file is empty: %s", key_path);
  }

  file_content[file_size] = '\0';

  // Check if this is an OpenSSH private key
  if (strstr(file_content, "BEGIN OPENSSH PRIVATE KEY") == NULL) {
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Unsupported private key format (only OpenSSH format supported): %s", key_path);
  }

  // Parse the OpenSSH private key format
  // The format is:
  // -----BEGIN OPENSSH PRIVATE KEY-----
  // [base64 encoded data]
  // -----END OPENSSH PRIVATE KEY-----

  // Find the base64 data between the headers
  const char *base64_start = strstr(file_content, "-----BEGIN OPENSSH PRIVATE KEY-----");
  if (!base64_start) {
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH private key format: %s", key_path);
  }

  // Skip to the end of the header line
  base64_start = strchr(base64_start, '\n');
  if (!base64_start) {
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH private key format: %s", key_path);
  }
  base64_start++; // Skip the newline

  // Find the end of the base64 data
  const char *base64_end = strstr(base64_start, "-----END OPENSSH PRIVATE KEY-----");
  if (!base64_end) {
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH private key format: %s", key_path);
  }

  // Remove any whitespace/newlines from the base64 data
  char *clean_base64 = SAFE_MALLOC(base64_end - base64_start + 1, char *);
  char *clean_ptr = clean_base64;
  for (const char *p = base64_start; p < base64_end; p++) {
    if (*p != '\n' && *p != '\r' && *p != ' ' && *p != '\t') {
      *clean_ptr++ = *p;
    }
  }
  *clean_ptr = '\0';

  // Decode the base64 data
  uint8_t *key_blob;
  size_t key_blob_len;
  asciichat_error_t decode_result = base64_decode_ssh_key(clean_base64, strlen(clean_base64), &key_blob, &key_blob_len);
  SAFE_FREE(clean_base64);

  if (decode_result != ASCIICHAT_OK) {
    SAFE_FREE(file_content);
    return decode_result;
  }

  // Parse the OpenSSH private key structure
  // Format: [4 bytes: magic] [4 bytes: ciphername length] [ciphername] [4 bytes: kdfname length] [kdfname]
  //         [4 bytes: kdfoptions length] [kdfoptions] [4 bytes: num keys] [4 bytes: pubkey length] [pubkey]
  //         [4 bytes: privkey length] [privkey]

  if (key_blob_len < 4) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key blob too small: %s", key_path);
  }

  // Check magic number (should be "openssh-key-v1\0")
  if (memcmp(key_blob, "openssh-key-v1\0", 15) != 0) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid OpenSSH private key magic: %s", key_path);
  }

  size_t offset = 15; // Skip magic

  // Read ciphername
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at ciphername: %s", key_path);
  }

  uint32_t ciphername_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  if (offset + ciphername_len > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at ciphername data: %s", key_path);
  }

  // Store the position of ciphername for later use
  size_t ciphername_pos = offset;

  // Check if key is encrypted
  bool is_encrypted = (ciphername_len > 0 && memcmp(key_blob + offset, "none", 4) != 0);
  if (is_encrypted) {
    log_debug("DEBUG: Encrypted key detected, ciphername_len=%u", ciphername_len);
    // We'll handle encryption below after parsing the structure
  }

  offset += ciphername_len;

  // Skip kdfname and kdfoptions (we don't support encryption)
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at kdfname: %s", key_path);
  }

  uint32_t kdfname_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  // Store the position of kdfname for later use
  size_t kdfname_pos = offset;

  offset += kdfname_len;

  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at kdfoptions: %s", key_path);
  }

  uint32_t kdfoptions_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4 + kdfoptions_len;

  // Handle encrypted keys
  if (is_encrypted) {
    log_debug("DEBUG: Processing encrypted key, ciphername_len=%u, kdfname_len=%u, kdfoptions_len=%u", ciphername_len,
              kdfname_len, kdfoptions_len);

    // Parse the cipher name from the stored position
    char ciphername[32] = {0};
    if (ciphername_len > 0 && ciphername_len < sizeof(ciphername)) {
      memcpy(ciphername, key_blob + ciphername_pos, ciphername_len);
    }

    // Parse the KDF name from the stored position
    char kdfname[32] = {0};
    if (kdfname_len > 0 && kdfname_len < sizeof(kdfname)) {
      memcpy(kdfname, key_blob + kdfname_pos, kdfname_len);
    }

    log_debug("DEBUG: Cipher: %s, KDF: %s", ciphername, kdfname);

    // Check if we support this encryption method
    if (strcmp(ciphername, "aes256-ctr") != 0 && strcmp(ciphername, "aes256-cbc") != 0) {
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY,
                       "Unsupported cipher '%s' for encrypted SSH key: %s\n"
                       "Supported ciphers: aes256-ctr, aes256-cbc",
                       ciphername, key_path);
    }

    if (strcmp(kdfname, "bcrypt") != 0) {
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY,
                       "Unsupported KDF '%s' for encrypted SSH key: %s\n"
                       "Only bcrypt KDF is supported",
                       kdfname, key_path);
    }

    // Parse KDF options (bcrypt salt and rounds)
    if (kdfoptions_len < 8) {
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Invalid KDF options length: %s", key_path);
    }

    // Extract bcrypt salt (16 bytes) and rounds (4 bytes)
    uint8_t bcrypt_salt[16];
    memcpy(bcrypt_salt, key_blob + offset - kdfoptions_len, 16);

    // Check for password in environment variable first
    const char *env_password = platform_getenv("ASCII_CHAT_SSH_PASSWORD");
    char *password = NULL;
    if (env_password && strlen(env_password) > 0) {
      // Use password from environment variable
      password = platform_strdup(env_password);
      if (!password) {
        SAFE_FREE(key_blob);
        SAFE_FREE(file_content);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for password");
      }
    } else {
      // Prompt for password interactively - allocate buffer for input
      password = SAFE_MALLOC(1024, char *);
      if (!password) {
        SAFE_FREE(key_blob);
        SAFE_FREE(file_content);
        return SET_ERRNO(ERROR_MEMORY, "Failed to allocate memory for password");
      }
      if (platform_prompt_password("Encrypted SSH key detected - please enter passphrase:", password, 1024) != 0) {
        SAFE_FREE(key_blob);
        SAFE_FREE(file_content);
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to read passphrase for encrypted key: %s", key_path);
      }
    }

    log_debug("DEBUG: Password entered, length=%zu", strlen(password));

    // Now decrypt the key data
    // The encrypted data starts after the header
    size_t encrypted_data_start = offset;
    size_t encrypted_data_len = key_blob_len - encrypted_data_start;

    if (encrypted_data_len < 16) {
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Encrypted data too small: %s", key_path);
    }

    // For AES-CTR, we need to implement CTR mode decryption
    // We'll use a simplified approach that should work for most cases
    uint8_t *decrypted_data = SAFE_MALLOC(encrypted_data_len, uint8_t *);
    if (!decrypted_data) {
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to allocate memory for decryption: %s", key_path);
    }

    // Extract IV (first 16 bytes)
    uint8_t iv[16];
    memcpy(iv, key_blob + encrypted_data_start, 16);

    // Use libsodium to decrypt the key data directly
    // This is safer than modifying the user's key file
    uint8_t derived_key[32];
    if (crypto_pwhash(derived_key, 32, password, strlen(password), bcrypt_salt, crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_DEFAULT) != 0) {
      sodium_memzero(password, strlen(password));
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to derive key from passphrase: %s", key_path);
    }

    // Use ssh-keygen to decrypt to a temporary file (safer approach)
    char temp_key_path[1024];
    safe_snprintf(temp_key_path, sizeof(temp_key_path), "%s_temp_decrypted", key_path);

    // Copy the encrypted key file to temp location using C file operations (more reliable than batch copy)
    FILE *src_file = platform_fopen(key_path, "rb");
    if (!src_file) {
      sodium_memzero(password, strlen(password));
      SAFE_FREE(decrypted_data);
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to open source key file for copying: %s", key_path);
    }

    FILE *dest_file = platform_fopen(temp_key_path, "wb");
    if (!dest_file) {
      (void)fclose(src_file);
      sodium_memzero(password, strlen(password));
      SAFE_FREE(decrypted_data);
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to create temp key file: %s", temp_key_path);
    }

    // Copy file contents
    char copy_buffer[4096];
    size_t bytes;
    while ((bytes = fread(copy_buffer, 1, sizeof(copy_buffer), src_file)) > 0) {
      if (fwrite(copy_buffer, 1, bytes, dest_file) != bytes) {
        (void)fclose(src_file);
        (void)fclose(dest_file);
        unlink(temp_key_path);
        sodium_memzero(password, strlen(password));
        SAFE_FREE(decrypted_data);
        SAFE_FREE(key_blob);
        SAFE_FREE(file_content);
        return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to copy key file");
      }
    }
    (void)fclose(src_file);
    (void)fclose(dest_file);

    log_debug("DEBUG: Copied encrypted key to temp file: %s", temp_key_path);

    // Create a batch script that decrypts the temporary file
    char script_path[1024];
    safe_snprintf(script_path, sizeof(script_path), "%s_script.bat", key_path);

    FILE *script_file = platform_fopen(script_path, "w");
    if (!script_file) {
      unlink(temp_key_path);
      sodium_memzero(password, strlen(password));
      SAFE_FREE(decrypted_data);
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to create script file: %s", script_path);
    }

    safe_fprintf(script_file, "@echo off\n");
    safe_fprintf(script_file, "ssh-keygen -p -f \"%s\" -N \"\" -P \"%s\"\n", temp_key_path, password);
    (void)fclose(script_file);

    if (strlen(password) > 0) {
      sodium_memzero(password, strlen(password));
    }

    char decrypt_cmd[2048];
    safe_snprintf(decrypt_cmd, sizeof(decrypt_cmd), "call \"%s\"", script_path);

    // Execute ssh-keygen to decrypt the temporary key
    int result = system(decrypt_cmd);
    if (result != 0) {
      unlink(temp_key_path);
      unlink(script_path);
      SAFE_FREE(decrypted_data);
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY,
                       "Failed to decrypt SSH key using ssh-keygen: %s\n"
                       "Make sure ssh-keygen is available and the passphrase is correct",
                       key_path);
    }

    // Read the decrypted temporary key file
    FILE *temp_file = platform_fopen(temp_key_path, "rb");
    if (!temp_file) {
      unlink(temp_key_path);
      unlink(script_path);
      SAFE_FREE(decrypted_data);
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to open decrypted key file: %s", temp_key_path);
    }

    // Read the decrypted key file
    (void)fseek(temp_file, 0, SEEK_END);
    long temp_file_size = ftell(temp_file);
    (void)fseek(temp_file, 0, SEEK_SET);

    char *temp_file_content = malloc(temp_file_size + 1);
    if (!temp_file_content) {
      (void)fclose(temp_file);
      unlink(temp_key_path);
      unlink(script_path);
      SAFE_FREE(decrypted_data);
      SAFE_FREE(key_blob);
      SAFE_FREE(file_content);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "Failed to allocate memory for decrypted key: %s", temp_key_path);
    }

    (void)fread(temp_file_content, 1, temp_file_size, temp_file);
    temp_file_content[temp_file_size] = '\0';
    (void)fclose(temp_file);

    // Clean up temporary files
    unlink(temp_key_path);
    unlink(script_path);

    // The decrypted key file is in OpenSSH format, not binary blob format
    // We need to parse it as a regular OpenSSH key file
    // Parse the OpenSSH key file directly
    asciichat_error_t parse_result = parse_openssh_key_file(temp_file_content, temp_file_size, key_out);

    SAFE_FREE(temp_file_content);
    SAFE_FREE(decrypted_data);
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    if (password) {
      sodium_memzero(password, strlen(password));
      SAFE_FREE(password);
    }
    sodium_memzero(derived_key, sizeof(derived_key));

    return parse_result;

    log_debug("DEBUG: Key decrypted successfully using ssh-keygen (temporary file approach)");
  }

  // Read number of keys
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at num keys: %s", key_path);
  }

  uint32_t num_keys =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  log_debug("DEBUG: num_keys=%u, offset=%zu, key_blob_len=%zu", num_keys, offset, key_blob_len);
  log_debug("DEBUG: Raw bytes at offset %zu: %02x %02x %02x %02x", offset, key_blob[offset], key_blob[offset + 1],
            key_blob[offset + 2], key_blob[offset + 3]);
  log_debug("DEBUG: After num_keys, offset=%zu", offset);

  if (num_keys != 1) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key contains %u keys (expected 1): %s", num_keys, key_path);
  }

  // Read public key
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at pubkey length: %s", key_path);
  }

  log_debug("DEBUG: About to read pubkey_len at offset=%zu, bytes: %02x %02x %02x %02x", offset, key_blob[offset],
            key_blob[offset + 1], key_blob[offset + 2], key_blob[offset + 3]);

  uint32_t pubkey_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  log_debug("DEBUG: pubkey_len=%u, offset=%zu", pubkey_len, offset);

  if (offset + pubkey_len > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at pubkey data: %s", key_path);
  }

  // Parse the public key to extract the Ed25519 public key for validation
  if (pubkey_len < 4) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH public key too small: %s", key_path);
  }

  uint32_t key_type_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  // Check if it's an Ed25519 key
  if (key_type_len != 11 || memcmp(key_blob + offset, "ssh-ed25519", 11) != 0) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key is not Ed25519: %s", key_path);
  }

  offset += key_type_len; // Skip the key type string
  log_debug("DEBUG: After skipping key type, offset=%zu", offset);

  // Read the public key length
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at public key length: %s", key_path);
  }

  uint32_t pubkey_data_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;
  log_debug("DEBUG: Public key data length: %u, offset=%zu", pubkey_data_len, offset);

  log_debug("DEBUG: Public key data length: %u (expected 32 for Ed25519)", pubkey_data_len);

  // For Ed25519, the public key should be 32 bytes, but let's be more flexible
  if (pubkey_data_len < 32) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH public key data too small (%u bytes, expected at least 32): %s",
                     pubkey_data_len, key_path);
  }

  // Read Ed25519 public key (first 32 bytes) for validation
  if (offset + 32 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at public key: %s", key_path);
  }

  uint8_t ed25519_pubkey[32];
  memcpy(ed25519_pubkey, key_blob + offset, 32);
  offset += pubkey_data_len; // Skip the entire public key data

  log_debug("DEBUG: Extracted public key, offset=%zu", offset);
  log_debug("DEBUG: Raw public key bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
            "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
            ed25519_pubkey[0], ed25519_pubkey[1], ed25519_pubkey[2], ed25519_pubkey[3], ed25519_pubkey[4],
            ed25519_pubkey[5], ed25519_pubkey[6], ed25519_pubkey[7], ed25519_pubkey[8], ed25519_pubkey[9],
            ed25519_pubkey[10], ed25519_pubkey[11], ed25519_pubkey[12], ed25519_pubkey[13], ed25519_pubkey[14],
            ed25519_pubkey[15], ed25519_pubkey[16], ed25519_pubkey[17], ed25519_pubkey[18], ed25519_pubkey[19],
            ed25519_pubkey[20], ed25519_pubkey[21], ed25519_pubkey[22], ed25519_pubkey[23], ed25519_pubkey[24],
            ed25519_pubkey[25], ed25519_pubkey[26], ed25519_pubkey[27], ed25519_pubkey[28], ed25519_pubkey[29],
            ed25519_pubkey[30], ed25519_pubkey[31]);
  log_debug("DEBUG: Public key data length: %u", pubkey_data_len);

  // Skip the rest of the public key data to get to the private key
  // We've already parsed: 4 (key_type_len) + 11 (ssh-ed25519) + 4 (pubkey_data_len) + 32 (key) = 51 bytes
  // So we need to skip the remaining pubkey_len - 51 bytes
  size_t remaining_pubkey = pubkey_len - 51;
  if (remaining_pubkey > 0) {
    offset += remaining_pubkey;
  }

  log_debug("DEBUG: After skipping remaining pubkey data, offset=%zu", offset);

  // Read private key
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at privkey length: %s", key_path);
  }

  log_debug("DEBUG: About to read privkey_len at offset=%zu, bytes: %02x %02x %02x %02x", offset, key_blob[offset],
            key_blob[offset + 1], key_blob[offset + 2], key_blob[offset + 3]);

  uint32_t privkey_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  log_debug("DEBUG: privkey_len=%u, offset=%zu, key_blob_len=%zu", privkey_len, offset, key_blob_len);

  if (offset + privkey_len > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at privkey data: %s", key_path);
  }

  // Parse the private key structure
  // Format: [4 bytes: checkint1] [4 bytes: checkint2] [4 bytes: key type length] [key type]
  //         [4 bytes: public key length] [public key] [4 bytes: private key length] [private key]
  //         [4 bytes: comment length] [comment]

  if (privkey_len < 8) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key data too small: %s", key_path);
  }

  // Verify checkints (should be equal)
  uint32_t checkint1 =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  uint32_t checkint2 =
      (key_blob[offset + 4] << 24) | (key_blob[offset + 5] << 16) | (key_blob[offset + 6] << 8) | key_blob[offset + 7];
  offset += 8;

  if (checkint1 != checkint2) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key checkints don't match: %s", key_path);
  }

  // Skip key type
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at key type length: %s", key_path);
  }

  uint32_t key_type_len_priv =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4 + key_type_len_priv;

  // Skip public key
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at pubkey length: %s", key_path);
  }

  uint32_t pubkey_len_priv =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4 + pubkey_len_priv;

  // Read private key
  if (offset + 4 > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at privkey length: %s", key_path);
  }

  uint32_t privkey_data_len =
      (key_blob[offset] << 24) | (key_blob[offset + 1] << 16) | (key_blob[offset + 2] << 8) | key_blob[offset + 3];
  offset += 4;

  if (offset + privkey_data_len > key_blob_len) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key truncated at privkey data: %s", key_path);
  }

  // The private key data should be at least 64 bytes (32 bytes private key + 32 bytes public key)
  // But OpenSSH format may have additional data
  if (privkey_data_len < 64) {
    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key data length is %u (expected at least 64): %s",
                     privkey_data_len, key_path);
  }

  log_debug("DEBUG: Private key data length: %u bytes", privkey_data_len);

  // Extract the Ed25519 private key (first 32 bytes)
  uint8_t ed25519_privkey[32];
  memcpy(ed25519_privkey, key_blob + offset, 32);

  // Verify the public key matches
  // The public key in the privkey section is raw Ed25519, while the one in pubkey section is SSH format
  // We need to compare the raw public key from privkey with the raw public key extracted from pubkey
  log_debug("DEBUG: Comparing public keys - extracted from pubkey: %02x%02x%02x%02x..., stored in privkey: "
            "%02x%02x%02x%02x...",
            ed25519_pubkey[0], ed25519_pubkey[1], ed25519_pubkey[2], ed25519_pubkey[3], key_blob[offset + 32],
            key_blob[offset + 33], key_blob[offset + 34], key_blob[offset + 35]);

  if (memcmp(key_blob + offset + 32, ed25519_pubkey, 32) != 0) {
    // For debugging, let's print the full comparison
    log_debug("DEBUG: Public key mismatch detected");
    log_debug("DEBUG: Extracted pubkey (first 16 bytes): %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x %02x",
              ed25519_pubkey[0], ed25519_pubkey[1], ed25519_pubkey[2], ed25519_pubkey[3], ed25519_pubkey[4],
              ed25519_pubkey[5], ed25519_pubkey[6], ed25519_pubkey[7], ed25519_pubkey[8], ed25519_pubkey[9],
              ed25519_pubkey[10], ed25519_pubkey[11], ed25519_pubkey[12], ed25519_pubkey[13], ed25519_pubkey[14],
              ed25519_pubkey[15]);
    log_debug("DEBUG: Stored pubkey (first 16 bytes): %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x "
              "%02x %02x %02x",
              key_blob[offset + 32], key_blob[offset + 33], key_blob[offset + 34], key_blob[offset + 35],
              key_blob[offset + 36], key_blob[offset + 37], key_blob[offset + 38], key_blob[offset + 39],
              key_blob[offset + 40], key_blob[offset + 41], key_blob[offset + 42], key_blob[offset + 43],
              key_blob[offset + 44], key_blob[offset + 45], key_blob[offset + 46], key_blob[offset + 47]);

    SAFE_FREE(key_blob);
    SAFE_FREE(file_content);
    return SET_ERRNO(ERROR_CRYPTO_KEY, "OpenSSH private key public key mismatch: %s", key_path);
  }

  // Initialize the private key structure
  memset(key_out, 0, sizeof(private_key_t));
  key_out->type = KEY_TYPE_ED25519;

  // Store the actual private key (seed + public key = 64 bytes)
  // Ed25519 private key format: [32 bytes seed][32 bytes public key]
  memcpy(key_out->key.ed25519, ed25519_privkey, 32);     // Store the seed (first 32 bytes)
  memcpy(key_out->key.ed25519 + 32, ed25519_pubkey, 32); // Store the public key (next 32 bytes)

  // Also store the public key in the public_key field for easy access
  memcpy(key_out->public_key, ed25519_pubkey, 32);

  // Set a comment
  SAFE_STRNCPY(key_out->key_comment, "ssh-ed25519", sizeof(key_out->key_comment) - 1);

  SAFE_FREE(key_blob);
  SAFE_FREE(file_content);

  return ASCIICHAT_OK;
}

asciichat_error_t validate_ssh_key_file(const char *key_path) {
  if (!key_path) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: key_path=%p", key_path);
  }

  // Check if file exists and is readable
  FILE *test_file = platform_fopen(key_path, "r");
  if (test_file == NULL) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "Cannot read key file: %s", key_path);
  }

  // Check if this is an SSH key file by looking for the header
  char header[256];
  bool is_ssh_key_file = false;
  if (fgets(header, sizeof(header), test_file) != NULL) {
    if (strstr(header, "BEGIN OPENSSH PRIVATE KEY") != NULL || strstr(header, "BEGIN RSA PRIVATE KEY") != NULL ||
        strstr(header, "BEGIN EC PRIVATE KEY") != NULL) {
      is_ssh_key_file = true;
    }
  }
  (void)fclose(test_file);

  if (!is_ssh_key_file) {
    return SET_ERRNO(ERROR_CRYPTO_KEY, "File is not a valid SSH key: %s", key_path);
  }

  // Check permissions for SSH key files (should be 600 or 400)
#ifndef _WIN32
  struct stat st;
  if (stat(key_path, &st) == 0) {
    if ((st.st_mode & SSH_KEY_PERMISSIONS_MASK) != 0) {
      log_error("SSH key file %s has overly permissive permissions: %o", key_path, st.st_mode & 0777);
      log_error("Run 'chmod 600 %s' to fix this", key_path);
      return SET_ERRNO(ERROR_CRYPTO_KEY, "SSH key file has overly permissive permissions: %s", key_path);
    }
  }
#endif

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

  // Sign the message with Ed25519
  if (crypto_sign_detached(signature, NULL, message, message_len, key->key.ed25519) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Failed to sign message with Ed25519");
  }

  return ASCIICHAT_OK;
}

asciichat_error_t ed25519_verify_signature(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                                           const uint8_t signature[64]) {
  if (!public_key || !message || !signature) {
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters: public_key=%p, message=%p, signature=%p", public_key,
                     message, signature);
  }

  // Verify the Ed25519 signature
  if (crypto_sign_verify_detached(signature, message, message_len, public_key) != 0) {
    return SET_ERRNO(ERROR_CRYPTO, "Ed25519 signature verification failed");
  }

  return ASCIICHAT_OK;
}
