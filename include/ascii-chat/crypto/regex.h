/**
 * @file crypto/regex.h
 * @brief PCRE2-based regex patterns for cryptographic parsing
 * @ingroup crypto
 *
 * Unified module for regex-based parsing of SSH-related formats:
 * - SSH known_hosts file entries
 * - SSH Ed25519 public key lines
 * - OpenSSH private key PEM format
 *
 * Uses PCRE2
 */

#pragma once

#include <stdbool.h>

/**
 * Match SSH known_hosts line and extract components
 *
 * Format: <IP:port> <keytype> <hex_key> [comment]
 * Example: 192.0.2.1:8080 x25519 0123456789abcdef... ascii-chat
 *
 * @param line Input line from known_hosts file
 * @param ip_port_out Output: allocated string with IP:port (e.g., "192.0.2.1:8080")
 * @param key_type_out Output: allocated string with key type (e.g., "x25519", "no-identity")
 * @param hex_key_out Output: allocated string with 64-char hex key (may be NULL for no-identity)
 * @param comment_out Output: allocated string with comment (may be NULL)
 *
 * @return true if line matches known_hosts format, false otherwise
 *
 * @note Caller must free all non-NULL output strings with SAFE_FREE()
 * @note Returns false if any output pointer is NULL
 */
bool crypto_regex_match_known_hosts(const char *line, char **ip_port_out, char **key_type_out, char **hex_key_out,
                                    char **comment_out);

/**
 * Match SSH Ed25519 public key line and extract base64
 *
 * Format: ssh-ed25519 <base64_key> [comment]
 * Example: ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAI... test-key
 *
 * @param line Input line from SSH public key file
 * @param base64_key_out Output: allocated string with base64-encoded key
 * @param comment_out Output: allocated string with comment (may be NULL)
 *
 * @return true if line matches SSH Ed25519 format, false otherwise
 *
 * @note Caller must free output strings with SAFE_FREE()
 * @note Case-insensitive: matches "SSH-ED25519" or "ssh-ed25519"
 */
bool crypto_regex_match_public_key(const char *line, char **base64_key_out, char **comment_out);

/**
 * Extract base64 data from OpenSSH private key PEM format
 *
 * Format:
 *   -----BEGIN OPENSSH PRIVATE KEY-----
 *   <multiline base64 data>
 *   -----END OPENSSH PRIVATE KEY-----
 *
 * @param file_content Full contents of PEM key file
 * @param base64_data_out Output: allocated string with base64 data (includes whitespace)
 *
 * @return true if PEM structure found and extracted, false otherwise
 *
 * @note Caller must free output string with SAFE_FREE()
 * @note Output includes newlines and whitespace (caller should remove as needed)
 */
bool crypto_regex_extract_pem_base64(const char *file_content, char **base64_data_out);

/**
 * Extract GPG keygrip from colon-delimited GPG output
 *
 * Format: grp:::::::::D52FF935FBA59609EE65E1685287828242A1EA1A:
 * Where the keygrip is a 40-character hexadecimal string after 8 colon-delimited empty fields
 *
 * @param line Input line from GPG output (must start with "grp:")
 * @param keygrip_out Output: allocated string with 40-char hex keygrip
 *
 * @return true if line matches GPG keygrip format, false otherwise
 *
 * @note Caller must free output string with SAFE_FREE()
 * @note Returns false if regex is not available (caller should use fallback manual parsing)
 */
bool crypto_regex_extract_gpg_keygrip(const char *line, char **keygrip_out);
