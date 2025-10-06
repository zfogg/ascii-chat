#ifndef SSH_KEYS_H
#define SSH_KEYS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Key type enumeration (Ed25519 and X25519 only - no RSA!)
typedef enum {
  KEY_TYPE_UNKNOWN = 0,
  KEY_TYPE_ED25519, // ssh-ed25519 (converts to X25519)
  KEY_TYPE_X25519,  // Native X25519 (raw hex or base64)
  KEY_TYPE_GPG      // GPG key (Ed25519 variant, derived to X25519)
} key_type_t;

// Public key structure (simple - just 32 bytes!)
typedef struct {
  key_type_t type;
  uint8_t key[32];   // Always 32 bytes (Ed25519, X25519, or GPG-derived)
  char comment[256]; // Key comment/label
} public_key_t;

// Private key structure (for server --ssh-key)
typedef struct {
  key_type_t type;
  union {
    uint8_t ed25519[64]; // Ed25519 seed (32) + public key (32) = 64 bytes
    uint8_t x25519[32];  // X25519 private key (32 bytes)
  } key;
} private_key_t;

// Parse SSH/GPG public key from any format
// Formats:
//   - "ssh-ed25519 AAAAC3... comment" (SSH Ed25519)
//   - "github:username" (fetches from GitHub .keys, uses first Ed25519 key)
//   - "gitlab:username" (fetches from GitLab .keys, uses first Ed25519 key)
//   - "github:username.gpg" (fetches GPG key from GitHub)
//   - "gitlab:username.gpg" (fetches GPG key from GitLab)
//   - "gpg:0xKEYID" (shells out to `gpg --export KEYID`)
//   - File path (reads first line and parses)
//   - Raw hex (64 chars for X25519)
// Returns: 0 on success, -1 on failure
int parse_public_key(const char *input, public_key_t *key_out);

// Parse SSH private key from file
// Supports:
//   - ~/.ssh/id_ed25519 (OpenSSH Ed25519 format)
//   - Raw hex file (64 chars for X25519)
// Returns: 0 on success, -1 on failure
int parse_private_key(const char *path, private_key_t *key_out);

// Convert public key to X25519 for DH
// Ed25519 → X25519 conversion, X25519 passthrough, GPG already derived
// Returns: 0 on success, -1 on failure
int public_key_to_x25519(const public_key_t *key, uint8_t x25519_pk[32]);

// Convert private key to X25519 for DH
// Returns: 0 on success, -1 on failure
int private_key_to_x25519(const private_key_t *key, uint8_t x25519_sk[32]);

// Fetch SSH keys from GitHub using BearSSL
// GET https://github.com/username.keys
// Returns array of SSH public key strings (caller must free)
int fetch_github_keys(const char *username, char ***keys_out, size_t *num_keys);

// Fetch SSH keys from GitLab using BearSSL
// GET https://gitlab.com/username.keys
int fetch_gitlab_keys(const char *username, char ***keys_out, size_t *num_keys);

// Fetch GPG keys from GitHub using BearSSL
// GET https://github.com/username.gpg
int fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

// Fetch GPG keys from GitLab using BearSSL
// GET https://gitlab.com/username.gpg
int fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

// Parse SSH authorized_keys file
// Returns array of public keys (Ed25519 or X25519 only)
int parse_authorized_keys(const char *path, public_key_t *keys, size_t *num_keys, size_t max_keys);

// Convert public key to display format (ssh-ed25519 or x25519 hex)
void format_public_key(const public_key_t *key, char *output, size_t output_size);

#endif
