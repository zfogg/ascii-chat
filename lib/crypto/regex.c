/**
 * @file crypto/regex.c
 * @brief PCRE2-based regex patterns for cryptographic parsing
 * @ingroup crypto
 *
 * Implements regex-based parsing for SSH formats using PCRE2 with JIT compilation.
 * Uses centralized PCRE2 singleton module for thread-safe lazy initialization.
 */

#include "ascii-chat/asciichat_errno.h"
#include <ascii-chat/crypto/regex.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/pcre2.h>
#include <pcre2.h>
#include <string.h>
#include <stdlib.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * PCRE2 REGEX PATTERNS
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * SSH known_hosts line format:
 *   <IP:port> <keytype> <hex_key> [comment]
 *
 * Captures:
 *   - ip_port: IP:port string (handles IPv4 and [IPv6]:port)
 *   - key_type: Key type identifier (x25519, no-identity, etc.)
 *   - hex_key: 64-character hex key (optional)
 *   - comment: Trailing comment (optional)
 */
static const char *KNOWN_HOSTS_REGEX_PATTERN = "^(?<ip_port>\\S+)"            // IP:port (non-whitespace)
                                               "\\s+"                         // Whitespace separator
                                               "(?<key_type>\\S+)"            // Key type (x25519 or no-identity)
                                               "(?:"                          // Optional key and comment group
                                               "\\s+"                         // Whitespace separator
                                               "(?<hex_key>[0-9a-fA-F]{64})?" // Optional 64-char hex key
                                               "(?:\\s+(?<comment>.*))?)"     // Optional comment
                                               "\\s*$";                       // Optional trailing whitespace

/**
 * SSH Ed25519 public key format:
 *   ssh-ed25519 <base64_key> [comment]
 *
 * Captures:
 *   - base64_key: Base64-encoded SSH key
 *   - comment: Optional comment after key
 */
static const char *SSH_PUBLIC_KEY_REGEX_PATTERN = "ssh-ed25519"                     // Literal prefix
                                                  "\\s+"                            // Whitespace separator
                                                  "(?<base64_key>[A-Za-z0-9+/]+=*)" // Base64 key with optional padding
                                                  "(?:\\s+(?<comment>.*))?";        // Optional comment

/**
 * OpenSSH private key PEM format:
 *   -----BEGIN OPENSSH PRIVATE KEY-----
 *   <multiline base64 data>
 *   -----END OPENSSH PRIVATE KEY-----
 *
 * Captures:
 *   - base64_data: Multiline base64 content (including newlines/whitespace)
 */
static const char *OPENSSH_PEM_REGEX_PATTERN =
    "-----BEGIN OPENSSH PRIVATE KEY-----\\s*" // Header with optional whitespace
    "(?<base64_data>[A-Za-z0-9+/=\\s]+?)"     // Multiline base64 (lazy match)
    "\\s*-----END OPENSSH PRIVATE KEY-----";  // Footer with optional whitespace

/**
 * GPG keygrip extraction format:
 *   grp:::::::::D52FF935FBA59609EE65E1685287828242A1EA1A:
 *
 * Captures:
 *   - keygrip: 40-character hexadecimal keygrip (skips 8 colon-delimited empty fields)
 */
static const char *GPG_KEYGRIP_REGEX_PATTERN = "^grp:(?:[^:]*:){8}(?<keygrip>[A-Fa-f0-9]{40}):"; // GPG keygrip format

/* ═══════════════════════════════════════════════════════════════════════════
 * PCRE2 REGEX SINGLETONS
 *
 * Individual lazy-initialized singletons for each regex pattern.
 * Compiled regex is read-only after initialization, safe for concurrent reads.
 * ═══════════════════════════════════════════════════════════════════════════ */

static pcre2_singleton_t *g_known_hosts_regex = NULL;
static pcre2_singleton_t *g_ssh_public_key_regex = NULL;
static pcre2_singleton_t *g_openssh_pem_regex = NULL;
static pcre2_singleton_t *g_gpg_keygrip_regex = NULL;

/**
 * Get compiled known_hosts regex (lazy initialization)
 */
static pcre2_code *crypto_regex_get_known_hosts(void) {
  if (g_known_hosts_regex == NULL) {
    g_known_hosts_regex =
        asciichat_pcre2_singleton_compile(KNOWN_HOSTS_REGEX_PATTERN, PCRE2_MULTILINE | PCRE2_UCP | PCRE2_UTF);
  }
  return asciichat_pcre2_singleton_get_code(g_known_hosts_regex);
}

/**
 * Get compiled SSH public key regex (lazy initialization)
 */
static pcre2_code *crypto_regex_get_ssh_public_key(void) {
  if (g_ssh_public_key_regex == NULL) {
    g_ssh_public_key_regex =
        asciichat_pcre2_singleton_compile(SSH_PUBLIC_KEY_REGEX_PATTERN, PCRE2_CASELESS | PCRE2_UCP | PCRE2_UTF);
  }
  return asciichat_pcre2_singleton_get_code(g_ssh_public_key_regex);
}

/**
 * Get compiled OpenSSH PEM regex (lazy initialization)
 */
static pcre2_code *crypto_regex_get_openssh_pem(void) {
  if (g_openssh_pem_regex == NULL) {
    g_openssh_pem_regex = asciichat_pcre2_singleton_compile(OPENSSH_PEM_REGEX_PATTERN,
                                                            PCRE2_MULTILINE | PCRE2_DOTALL | PCRE2_UCP | PCRE2_UTF);
  }
  return asciichat_pcre2_singleton_get_code(g_openssh_pem_regex);
}

/**
 * Get compiled GPG keygrip regex (lazy initialization)
 */
static pcre2_code *crypto_regex_get_gpg_keygrip(void) {
  if (g_gpg_keygrip_regex == NULL) {
    g_gpg_keygrip_regex = asciichat_pcre2_singleton_compile(GPG_KEYGRIP_REGEX_PATTERN, PCRE2_UCP | PCRE2_UTF);
  }
  return asciichat_pcre2_singleton_get_code(g_gpg_keygrip_regex);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PUBLIC API IMPLEMENTATION
 * ═══════════════════════════════════════════════════════════════════════════ */

bool crypto_regex_match_known_hosts(const char *line, char **ip_port_out, char **key_type_out, char **hex_key_out,
                                    char **comment_out) {
  if (!line || !ip_port_out || !key_type_out || !hex_key_out || !comment_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return false;
  }

  pcre2_code *regex = crypto_regex_get_known_hosts();
  if (!regex) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid validator state");
    return false;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    return false;
  }

  /* Perform JIT match (falls back to interpreted if JIT unavailable) */
  int rc = pcre2_jit_match(regex, (PCRE2_SPTR)line, strlen(line), 0, /* startoffset */
                           0,                                        /* options */
                           match_data, NULL);                        /* mcontext */

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    return false;
  }

  /* Extract named groups */
  *ip_port_out = asciichat_pcre2_extract_named_group(regex, match_data, "ip_port", line);
  *key_type_out = asciichat_pcre2_extract_named_group(regex, match_data, "key_type", line);
  *hex_key_out = asciichat_pcre2_extract_named_group(regex, match_data, "hex_key", line);
  *comment_out = asciichat_pcre2_extract_named_group(regex, match_data, "comment", line);

  pcre2_match_data_free(match_data);

  /* Verify we got required fields */
  if (!*ip_port_out || !*key_type_out) {
    SAFE_FREE(*ip_port_out);
    SAFE_FREE(*key_type_out);
    SAFE_FREE(*hex_key_out);
    SAFE_FREE(*comment_out);
    return false;
  }

  return true;
}

bool crypto_regex_match_public_key(const char *line, char **base64_key_out, char **comment_out) {
  if (!line || !base64_key_out || !comment_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return false;
  }

  pcre2_code *regex = crypto_regex_get_ssh_public_key();
  if (!regex) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid validator state");
    return false;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    return false;
  }

  /* Perform JIT match (falls back to interpreted if JIT unavailable) */
  int rc = pcre2_jit_match(regex, (PCRE2_SPTR)line, strlen(line), 0, /* startoffset */
                           0,                                        /* options */
                           match_data, NULL);                        /* mcontext */

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    return false;
  }

  /* Extract named groups */
  *base64_key_out = asciichat_pcre2_extract_named_group(regex, match_data, "base64_key", line);
  *comment_out = asciichat_pcre2_extract_named_group(regex, match_data, "comment", line);

  pcre2_match_data_free(match_data);

  /* Verify we got required field */
  if (!*base64_key_out) {
    SAFE_FREE(*base64_key_out);
    SAFE_FREE(*comment_out);
    return false;
  }

  return true;
}

bool crypto_regex_extract_pem_base64(const char *file_content, char **base64_data_out) {
  if (!file_content || !base64_data_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return false;
  }

  pcre2_code *regex = crypto_regex_get_openssh_pem();
  if (!regex) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid validator state");
    return false;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    return false;
  }

  /* Perform JIT match (falls back to interpreted if JIT unavailable) */
  int rc = pcre2_jit_match(regex, (PCRE2_SPTR)file_content, strlen(file_content), 0, /* startoffset */
                           0,                                                        /* options */
                           match_data, NULL);                                        /* mcontext */

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    return false;
  }

  /* Extract base64 data */
  *base64_data_out = asciichat_pcre2_extract_named_group(regex, match_data, "base64_data", file_content);

  pcre2_match_data_free(match_data);

  /* Verify we got the base64 data */
  if (!*base64_data_out) {
    return false;
  }

  return true;
}

bool crypto_regex_extract_gpg_keygrip(const char *line, char **keygrip_out) {
  if (!line || !keygrip_out) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return false;
  }

  pcre2_code *regex = crypto_regex_get_gpg_keygrip();
  if (!regex) {
    /* Regex not available - caller should use fallback manual parsing */
    return false;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(regex, NULL);
  if (!match_data) {
    return false;
  }

  /* Perform JIT match (falls back to interpreted if JIT unavailable) */
  int rc = pcre2_jit_match(regex, (PCRE2_SPTR)line, strlen(line), 0, /* startoffset */
                           0,                                        /* options */
                           match_data, NULL);                        /* mcontext */

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    return false;
  }

  /* Extract keygrip from named group */
  *keygrip_out = asciichat_pcre2_extract_named_group(regex, match_data, "keygrip", line);

  pcre2_match_data_free(match_data);

  /* Verify we got the keygrip */
  if (!*keygrip_out) {
    return false;
  }

  return true;
}
