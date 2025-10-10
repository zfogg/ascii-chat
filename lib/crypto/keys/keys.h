#ifndef SSH_KEYS_H
#define SSH_KEYS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../common.h" // For asciichat_error_t
#include "types.h"  // Include the key type definitions

// Include specialized key modules
#include "ssh_keys.h"
#include "gpg_keys.h"
#include "https_keys.h"
#include "validation.h"

// Key types are now defined in keys/types.h

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
asciichat_error_t parse_public_key(const char *input, public_key_t *key_out);

// Parse SSH private key from file
// Supports:
//   - ~/.ssh/id_ed25519 (OpenSSH Ed25519 format)
//   - Raw hex file (64 chars for X25519)
// If the key is encrypted, prompts for password and decrypts it.
// Returns: ASCIICHAT_OK on success, error code on failure
asciichat_error_t parse_private_key(const char *path, private_key_t *key_out);

// Convert public key to X25519 for DH
// Ed25519 → X25519 conversion, X25519 passthrough, GPG already derived
// Returns: 0 on success, -1 on failure
asciichat_error_t public_key_to_x25519(const public_key_t *key, uint8_t x25519_pk[32]);

// Convert private key to X25519 for DH
// Returns: 0 on success, -1 on failure
asciichat_error_t private_key_to_x25519(const private_key_t *key, uint8_t x25519_sk[32]);

// Sign a message with Ed25519 (uses SSH agent if available, otherwise in-memory key)
// This is the main signing function that abstracts SSH agent vs in-memory signing
// Returns: ASCIICHAT_OK on success, error code on failure
asciichat_error_t ed25519_sign_message(const private_key_t *key, const uint8_t *message, size_t message_len, uint8_t signature[64]);

// Verify an Ed25519 signature
// Returns: ASCIICHAT_OK on success, error code on failure
asciichat_error_t ed25519_verify_signature(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                             const uint8_t signature[64]);

// Fetch SSH/GPG keys from GitHub using BearSSL
// GET https://github.com/username.keys (SSH) or https://github.com/username.gpg (GPG)
// Returns array of key strings (caller must free)
asciichat_error_t fetch_github_keys(const char *username, char ***keys_out, size_t *num_keys, bool use_gpg);

// Fetch SSH/GPG keys from GitLab using BearSSL
// GET https://gitlab.com/username.keys (SSH) or https://gitlab.com/username.gpg (GPG)
asciichat_error_t fetch_gitlab_keys(const char *username, char ***keys_out, size_t *num_keys, bool use_gpg);

// Fetch GPG keys from GitHub using BearSSL
// GET https://github.com/username.gpg
asciichat_error_t fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

// Fetch GPG keys from GitLab using BearSSL
// GET https://gitlab.com/username.gpg
asciichat_error_t fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

// Parse Ed25519 public key from PGP armored format
// Extracts Ed25519 public key from PGP packet structure
asciichat_error_t parse_gpg_key(const char *gpg_key_text, public_key_t *key_out);

// Parse SSH keys from file (supports authorized_keys and known_hosts formats)
// Returns array of public keys (Ed25519 or X25519 only)
asciichat_error_t parse_keys_from_file(const char *path, public_key_t *keys, size_t *num_keys, size_t max_keys);

// Parse client keys from file, comma-separated list, or single key
// Supports multiple formats: authorized_keys, known_hosts, bare base64
asciichat_error_t parse_client_keys(const char *keys_file, public_key_t *keys_out, size_t *num_keys_out, size_t max_keys);

// Convert public key to display format (ssh-ed25519 or x25519 hex)
void format_public_key(const public_key_t *key, char *output, size_t output_size);

// Decode hex string to binary (utility function for testing)
asciichat_error_t hex_decode(const char *hex, uint8_t *output, size_t output_len);

// Forward declaration (full definition in handshake.h)
// Note: This avoids circular includes (handshake.h includes keys.h)
typedef struct crypto_handshake_context_t crypto_handshake_context_t;


/**
 * Validate SSH key file before parsing
 *
 * Checks:
 * - File exists and is readable
 * - File has valid SSH private key header
 * - File permissions are appropriate (warns if overly permissive)
 *
 * @param key_path Path to SSH key file
 * @return 0 if valid, -1 if invalid (logs specific errors)
 */
asciichat_error_t validate_ssh_key_file(const char *key_path);

#endif
