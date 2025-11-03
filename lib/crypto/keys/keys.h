#pragma once

/**
 * @file crypto/keys/keys.h
 * @defgroup keys Key Management
 * @ingroup keys
 * @brief SSH key, GPG key, and key validation APIs
 *
 * This header provides a unified interface for parsing SSH, GPG, and X25519 keys
 * from various sources including files, URLs, and raw formats.
 *
 * Supported input formats:
 * - SSH Ed25519 keys (ssh-ed25519 format)
 * - GitHub/GitLab SSH keys (fetched via HTTPS)
 * - GPG keys (from GitHub/GitLab or local keyring)
 * - X25519 keys (raw hex or base64)
 * - File paths (reads first line and parses)
 *
 * @note All keys are normalized to 32-byte X25519 format for key exchange.
 * @note GPG support: Code exists but is currently disabled.
 * @note GitHub/GitLab fetching: Uses BearSSL for HTTPS requests.
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date October 2025
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../../common.h" // For asciichat_error_t
#include "types.h"        // Include the key type definitions

// Include specialized key modules
#include "ssh_keys.h"
#include "gpg_keys.h"
#include "https_keys.h"
#include "validation.h"

/**
 * @name Public Key Parsing
 * @{
 */

/**
 * @brief Parse SSH/GPG public key from any format
 * @param input Key input in various formats (see below)
 * @param key_out Output structure for parsed public key (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses public key from various input formats:
 *
 * Supported formats:
 * - SSH Ed25519: "ssh-ed25519 AAAAC3... comment" (direct key string)
 * - GitHub SSH: "github:username" (fetches from https://github.com/username.keys, uses first Ed25519 key)
 * - GitLab SSH: "gitlab:username" (fetches from https://gitlab.com/username.keys, uses first Ed25519 key)
 * - GitHub GPG: "github:username.gpg" (fetches GPG key from https://github.com/username.gpg)
 * - GitLab GPG: "gitlab:username.gpg" (fetches GPG key from https://gitlab.com/username.gpg)
 * - GPG keyring: "gpg:0xKEYID" (shells out to `gpg --export KEYID`)
 * - File path: Path to `.pub` file or any file containing key (reads first line)
 * - Raw hex: 64 hex chars for X25519 key
 *
 * **File support**:
 * When a file path is provided, the file is read and the first line is parsed
 * as an SSH public key. Common formats:
 * - `.pub` file: Standard SSH public key file (one key per file, reads first line)
 * - Any text file: First line containing SSH key format is used
 *
 * @note Key normalization: All keys are converted to X25519 format for key exchange.
 *       Ed25519 keys are converted using libsodium conversion function.
 *
 * @note GPG support: GPG key parsing code exists but is currently disabled.
 *       Format "gpg:0xKEYID" may not work until GPG support is re-enabled.
 *
 * @note GitHub/GitLab fetching: Uses BearSSL for HTTPS requests. Requires network connectivity.
 *       Only first Ed25519 key is used for SSH format (multiple keys may be returned).
 *
 * @note File path: Reads first line of file and parses as SSH key format.
 *       File must exist and be readable. For files with multiple keys (one per line),
 *       use `parse_keys_from_file()` instead.
 *
 * @warning Network operations: GitHub/GitLab fetching requires network connectivity.
 *          May fail if network is unavailable or endpoints are blocked.
 *
 * @warning GPG support: GPG key parsing may not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t parse_public_key(const char *input, public_key_t *key_out);

/** @} */

/**
 * @name Private Key Parsing
 * @{
 */

/**
 * @brief Parse SSH private key from file
 * @param path Path to private key file
 * @param key_out Output structure for parsed private key (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses private key from file. Supports:
 * - OpenSSH Ed25519: ~/.ssh/id_ed25519 (OpenSSH Ed25519 format)
 * - Raw hex file: File containing 64 hex chars for X25519 key
 *
 * @note Encrypted keys: If key is encrypted (has password), prompts for password
 *       and decrypts it using platform-specific password prompt.
 *
 * @note SSH agent detection: If use_ssh_agent is true, key is loaded from SSH agent
 *       instead of file. Key stays in agent (not loaded into memory).
 *
 * @note File permissions: Validates file permissions before parsing.
 *       Warns if file has overly permissive permissions (world-readable).
 *
 * @note Key format: Currently only supports Ed25519 keys (OpenSSH format).
 *       RSA/ECDSA keys are NOT supported.
 *
 * @warning File permissions: Private key files should have restrictive permissions (0600).
 *          Function warns but does not fail on overly permissive permissions.
 *
 * @ingroup keys
 */
asciichat_error_t parse_private_key(const char *path, private_key_t *key_out);

/** @} */

/**
 * @name Key Conversion
 * @{
 */

/**
 * @brief Convert public key to X25519 for Diffie-Hellman key exchange
 * @param key Public key to convert (must not be NULL)
 * @param x25519_pk Output buffer for X25519 public key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Converts public key to X25519 format for key exchange:
 * - Ed25519 → X25519: Uses libsodium conversion function (crypto_sign_ed25519_pk_to_curve25519)
 * - X25519: Passthrough (key is already in X25519 format)
 * - GPG: Already derived to X25519 during GPG parsing
 *
 * @note Conversion: Ed25519 keys are converted using libsodium's conversion function.
 *       Conversion is mathematically safe (same curve, different representation).
 *
 * @note X25519 passthrough: If key is already X25519, key is copied directly.
 *
 * @note GPG keys: GPG keys are already converted to X25519 during parsing.
 *       No additional conversion needed.
 *
 * @warning All keys must be 32 bytes. Function validates key size before conversion.
 *
 * @ingroup keys
 */
asciichat_error_t public_key_to_x25519(const public_key_t *key, uint8_t x25519_pk[32]);

/**
 * @brief Convert private key to X25519 for Diffie-Hellman key exchange
 * @param key Private key to convert (must not be NULL)
 * @param x25519_sk Output buffer for X25519 private key (32 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Converts private key to X25519 format for key exchange:
 * - Ed25519 → X25519: Uses libsodium conversion function (crypto_sign_ed25519_sk_to_curve25519)
 * - X25519: Passthrough (key is already in X25519 format)
 *
 * @note Conversion: Ed25519 keys are converted using libsodium's conversion function.
 *       Conversion is mathematically safe (same curve, different representation).
 *
 * @note Ed25519 keys: Extracts 32-byte seed from Ed25519 key (first 32 bytes of ed25519 union).
 *       Converts seed to X25519 scalar using libsodium.
 *
 * @note X25519 passthrough: If key is already X25519, key is copied directly.
 *
 * @warning All keys must be 32 bytes. Function validates key size before conversion.
 *
 * @ingroup keys
 */
asciichat_error_t private_key_to_x25519(const private_key_t *key, uint8_t x25519_sk[32]);

/** @} */

/**
 * @name Ed25519 Signing and Verification
 * @{
 */

/**
 * @brief Sign a message with Ed25519 (uses SSH agent if available, otherwise in-memory key)
 * @param key Private key for signing (must not be NULL)
 * @param message Message to sign (must not be NULL)
 * @param message_len Length of message to sign
 * @param signature Output buffer for Ed25519 signature (64 bytes)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Signs a message using Ed25519. This is the main signing function that abstracts
 * SSH agent vs in-memory signing.
 *
 * @note Agent support: If use_ssh_agent is true, uses SSH agent for signing.
 *       Key stays in agent (not loaded into memory). Signing happens via agent protocol.
 *
 * @note In-memory signing: If use_ssh_agent is false, uses in-memory key for signing.
 *       Key must be loaded into memory (via parse_private_key()).
 *
 * @note GPG agent: use_gpg_agent flag exists but GPG agent support is currently disabled.
 *       Setting use_gpg_agent=true will not work until GPG support is re-enabled.
 *
 * @note Signature format: Ed25519 signature is always 64 bytes (R || S format).
 *
 * @warning Agent availability: If use_ssh_agent is true but SSH agent is not available,
 *          function returns error. Check SSH agent availability before using.
 *
 * @warning GPG agent: GPG agent support is currently disabled. use_gpg_agent flag
 *          exists but will not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t ed25519_sign_message(const private_key_t *key, const uint8_t *message, size_t message_len,
                                       uint8_t signature[64]);

/**
 * @brief Verify an Ed25519 signature
 * @param public_key Ed25519 public key (32 bytes)
 * @param message Message that was signed (must not be NULL)
 * @param message_len Length of message that was signed
 * @param signature Ed25519 signature to verify (64 bytes)
 * @return ASCIICHAT_OK if signature is valid, error code on failure
 *
 * Verifies an Ed25519 signature using libsodium's verification function.
 *
 * @note Signature format: Ed25519 signature is always 64 bytes (R || S format).
 *
 * @note Verification: Uses crypto_sign_ed25519_verify_detached() from libsodium.
 *       Returns error if signature is invalid or message was tampered with.
 *
 * @note Constant-time: Verification uses constant-time comparison to prevent timing attacks.
 *
 * @warning Always check return value. Error indicates invalid signature or tampering.
 *
 * @ingroup keys
 */
asciichat_error_t ed25519_verify_signature(const uint8_t public_key[32], const uint8_t *message, size_t message_len,
                                           const uint8_t signature[64]);

/** @} */

/**
 * @name Key Fetching (GitHub/GitLab)
 * @{
 *
 * Fetch SSH/GPG keys from GitHub and GitLab using HTTPS (BearSSL).
 * Requires network connectivity and valid usernames.
 */

/**
 * @brief Fetch SSH/GPG keys from GitHub using BearSSL
 * @param username GitHub username (must not be NULL)
 * @param keys_out Output array of key strings (caller must free each string and array)
 * @param num_keys Output parameter for number of keys fetched
 * @param use_gpg True to fetch GPG keys, false to fetch SSH keys
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Fetches keys from GitHub:
 * - SSH keys: GET https://github.com/username.keys
 * - GPG keys: GET https://github.com/username.gpg
 *
 * @note Network operation: Requires network connectivity and valid GitHub username.
 *       May fail if network is unavailable or username doesn't exist.
 *
 * @note Memory management: Caller must free each key string and the keys array:
 *       ```c
 *       for (size_t i = 0; i < num_keys; i++) {
 *         free(keys_out[i]);
 *       }
 *       free(keys_out);
 *       ```
 *
 * @note GPG support: GPG key fetching code exists but may not work until GPG support is re-enabled.
 *
 * @warning Network dependency: Requires active network connection.
 *          May fail if GitHub is unreachable or username doesn't exist.
 *
 * @ingroup keys
 */
asciichat_error_t fetch_github_keys(const char *username, char ***keys_out, size_t *num_keys, bool use_gpg);

/**
 * @brief Fetch SSH/GPG keys from GitLab using BearSSL
 * @param username GitLab username (must not be NULL)
 * @param keys_out Output array of key strings (caller must free each string and array)
 * @param num_keys Output parameter for number of keys fetched
 * @param use_gpg True to fetch GPG keys, false to fetch SSH keys
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Fetches keys from GitLab:
 * - SSH keys: GET https://gitlab.com/username.keys
 * - GPG keys: GET https://gitlab.com/username.gpg
 *
 * @note Network operation: Requires network connectivity and valid GitLab username.
 *       May fail if network is unavailable or username doesn't exist.
 *
 * @note Memory management: Caller must free each key string and the keys array (same as fetch_github_keys).
 *
 * @note GPG support: GPG key fetching code exists but may not work until GPG support is re-enabled.
 *
 * @warning Network dependency: Requires active network connection.
 *          May fail if GitLab is unreachable or username doesn't exist.
 *
 * @ingroup keys
 */
asciichat_error_t fetch_gitlab_keys(const char *username, char ***keys_out, size_t *num_keys, bool use_gpg);

/**
 * @brief Fetch GPG keys from GitHub using BearSSL
 * @param username GitHub username (must not be NULL)
 * @param keys_out Output array of GPG key strings (caller must free each string and array)
 * @param num_keys Output parameter for number of GPG keys fetched
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Fetches GPG keys from GitHub: GET https://github.com/username.gpg
 *
 * @note GPG support: Code exists but GPG key parsing may not work until GPG support is re-enabled.
 *
 * @note Memory management: Caller must free each key string and the keys array (same as fetch_github_keys).
 *
 * @warning Network dependency: Requires active network connection.
 * @warning GPG support: GPG key parsing may not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t fetch_github_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

/**
 * @brief Fetch GPG keys from GitLab using BearSSL
 * @param username GitLab username (must not be NULL)
 * @param keys_out Output array of GPG key strings (caller must free each string and array)
 * @param num_keys Output parameter for number of GPG keys fetched
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Fetches GPG keys from GitLab: GET https://gitlab.com/username.gpg
 *
 * @note GPG support: Code exists but GPG key parsing may not work until GPG support is re-enabled.
 *
 * @note Memory management: Caller must free each key string and the keys array (same as fetch_gitlab_keys).
 *
 * @warning Network dependency: Requires active network connection.
 * @warning GPG support: GPG key parsing may not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t fetch_gitlab_gpg_keys(const char *username, char ***keys_out, size_t *num_keys);

/** @} */

/**
 * @name Key Parsing
 * @{
 */

/**
 * @brief Parse Ed25519 public key from PGP armored format
 * @param gpg_key_text GPG key in armored format (must not be NULL)
 * @param key_out Output structure for parsed public key (must not be NULL)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Extracts Ed25519 public key from PGP armored format.
 * Parses GPG key structure and converts to Ed25519/X25519 format.
 *
 * @note GPG support: Code exists but GPG key parsing may not work until GPG support is re-enabled.
 *
 * @note Key format: GPG keys are parsed from armored format (-----BEGIN PGP PUBLIC KEY BLOCK-----).
 *       Extracts Ed25519 subkey and converts to X25519 for key exchange.
 *
 * @warning GPG support: GPG key parsing may not work until GPG support is re-enabled.
 *
 * @ingroup keys
 */
asciichat_error_t parse_gpg_key(const char *gpg_key_text, public_key_t *key_out);

/**
 * @brief Parse SSH keys from file (supports authorized_keys and known_hosts formats)
 * @param path Path to key file (must not be NULL)
 * @param keys Output array for parsed public keys (must not be NULL)
 * @param num_keys Output parameter for number of keys parsed (must not be NULL)
 * @param max_keys Maximum number of keys to parse (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses multiple SSH keys from file. Supports:
 * - **authorized_keys format**: One key per line (ssh-ed25519 AAAAC3... comment)
 * - **.pub file with multiple entries**: File containing multiple SSH public key
 *   entries, one per line (each line in `ssh-ed25519 AAAAC3... comment` format).
 *   This format is similar to `authorized_keys` and can contain any number of keys.
 * - **known_hosts format**: Multiple keys per line (hostname ssh-ed25519 AAAAC3... comment)
 *
 * **File format examples**:
 * ```
 * # authorized_keys format or .pub file with multiple entries
 * ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... alice@example.com
 * ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... bob@example.com
 * ```
 *
 * @note Key filtering: Only Ed25519 and X25519 keys are parsed.
 *       RSA/ECDSA keys are skipped (not supported).
 *
 * @note File format: Automatically detects file format based on content.
 *       Supports both `authorized_keys` format (one key per line) and `known_hosts` format.
 *       Also supports `.pub` files containing multiple key entries (one per line).
 *
 * @note Maximum keys: Stops parsing after max_keys keys are found.
 *       Function returns ASCIICHAT_OK even if more keys exist in file.
 *
 * @warning File must exist and be readable. Returns error if file cannot be opened.
 *
 * @ingroup keys
 */
asciichat_error_t parse_keys_from_file(const char *path, public_key_t *keys, size_t *num_keys, size_t max_keys);

/**
 * @brief Parse client keys from file, comma-separated list, or single key
 * @param keys_file Keys input (file path, comma-separated list, or single key)
 * @param keys_out Output array for parsed public keys (must not be NULL)
 * @param num_keys_out Output parameter for number of keys parsed (must not be NULL)
 * @param max_keys Maximum number of keys to parse (must be > 0)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Parses client keys from various input formats:
 * - **File path**: Path to file containing SSH public keys (one per line)
 * - **Comma-separated list**: "key1,key2,key3" (parses each key)
 * - **Single key**: Direct key string (ssh-ed25519 format or raw hex)
 *
 * **File support**:
 * When a file path is provided, the file is read and parsed for SSH public keys.
 * The file can contain multiple public keys, one per line. Supported file formats:
 * - **authorized_keys format**: Standard SSH authorized_keys file format (one key per line)
 * - **.pub file with multiple entries**: File containing multiple SSH public key
 *   entries, one per line (each line in `ssh-ed25519 AAAAC3... comment` format)
 * - **known_hosts format**: SSH known_hosts file format (all keys from file)
 *
 * @note Supported formats: authorized_keys (one key per line), .pub files with multiple
 *       entries (one per line), known_hosts, bare base64, SSH Ed25519 format
 *
 * @note Comma-separated list: Splits by comma and parses each key separately.
 *       Skips invalid keys and continues parsing.
 *
 * @note File detection: If input looks like a file path (contains '/'), attempts to read from file.
 *       Otherwise, treats as direct key string or comma-separated list.
 *
 * @warning File paths: File must exist and be readable if input is a file path.
 *          Returns error if file cannot be opened.
 *
 * @ingroup keys
 */
asciichat_error_t parse_client_keys(const char *keys_file, public_key_t *keys_out, size_t *num_keys_out,
                                    size_t max_keys);

/** @} */

/**
 * @name Key Formatting and Utilities
 * @{
 */

/**
 * @brief Convert public key to display format (ssh-ed25519 or x25519 hex)
 * @param key Public key to format (must not be NULL)
 * @param output Output buffer for formatted key string (must not be NULL)
 * @param output_size Size of output buffer (must be large enough for formatted key)
 *
 * Formats public key for display:
 * - Ed25519: "ssh-ed25519 AAAAC3..." (base64-encoded key)
 * - X25519: "x25519 <hex>" (hex-encoded key)
 *
 * @note Output format: Depends on key type. Ed25519 uses base64, X25519 uses hex.
 *
 * @note Buffer size: Output buffer should be at least 512 bytes for Ed25519 format.
 *       X25519 format requires at least 65 bytes (64 hex chars + "x25519 " prefix + null).
 *
 * @warning Output buffer must be large enough for formatted key string.
 *          Function does not check buffer size (may overflow).
 *
 * @ingroup keys
 */
void format_public_key(const public_key_t *key, char *output, size_t output_size);

/**
 * @brief Decode hex string to binary (utility function for testing)
 * @param hex Hex string to decode (must not be NULL, must have even length)
 * @param output Output buffer for binary data (must not be NULL)
 * @param output_len Expected output length in bytes (hex string must be output_len * 2 chars)
 * @return ASCIICHAT_OK on success, error code on failure
 *
 * Decodes hex string to binary data. Utility function primarily for testing.
 *
 * @note Hex format: Must contain only valid hex characters (0-9, a-f, A-F).
 *       Must have even length (2 chars per byte).
 *
 * @note Length validation: Hex string must be exactly output_len * 2 characters.
 *       Returns error if length doesn't match.
 *
 * @note Invalid characters: Returns error if hex string contains invalid characters.
 *
 * @warning Hex string must have even length. Odd-length strings return error.
 *
 * @ingroup keys
 */
asciichat_error_t hex_decode(const char *hex, uint8_t *output, size_t output_len);

/**
 * @brief Validate SSH key file before parsing
 * @param key_path Path to SSH key file (must not be NULL)
 * @return ASCIICHAT_OK if valid, error code on failure
 *
 * Validates SSH key file before parsing. Checks:
 * - File exists and is readable
 * - File has valid SSH private key header ("-----BEGIN OPENSSH PRIVATE KEY-----")
 * - File permissions are appropriate (warns if overly permissive)
 *
 * @note Permission checking: Checks file permissions on Unix systems.
 *       Warns if file has world-readable permissions but does not fail validation.
 *
 * @note File header: Validates that file starts with correct SSH private key header.
 *       Returns error if header is missing or invalid.
 *
 * @note Platform-specific: Permission checking is Unix-specific.
 *       Windows does not have Unix-style permissions.
 *
 * @warning File permissions: Private key files should have restrictive permissions (0600).
 *          Function warns but does not fail on overly permissive permissions.
 *
 * @ingroup keys
 */
asciichat_error_t validate_ssh_key_file(const char *key_path);

/** @} */

// Forward declaration (full definition in handshake.h)
// Note: This avoids circular includes (handshake.h includes keys.h)
typedef struct crypto_handshake_context_t crypto_handshake_context_t;
