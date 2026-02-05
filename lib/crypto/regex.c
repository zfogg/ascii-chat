/**
 * @file crypto/regex.c
 * @brief PCRE2-based regex patterns for cryptographic parsing
 * @ingroup crypto
 *
 * Implements regex-based parsing for SSH formats using PCRE2 with JIT compilation.
 * Global singleton with thread-safe pthread_once initialization.
 */

#include "ascii-chat/asciichat_errno.h"
#include <ascii-chat/crypto/regex.h>
#include <ascii-chat/common.h>
#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

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

/* ═══════════════════════════════════════════════════════════════════════════
 * PCRE2 REGEX VALIDATOR STATE
 *
 * Global singleton with lazy initialization to avoid thread safety issues.
 * Compiled regex is read-only after initialization, safe for concurrent reads.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
  pcre2_code *known_hosts_regex;    /* Compiled regex for known_hosts format */
  pcre2_code *ssh_public_key_regex; /* Compiled regex for SSH public key format */
  pcre2_code *openssh_pem_regex;    /* Compiled regex for OpenSSH PEM format */
  pcre2_jit_stack *jit_stack;       /* JIT stack for performance */
  bool initialized;                 /* Whether validator is initialized */
} crypto_regex_validator_t;

static crypto_regex_validator_t g_validator = {0};
static pthread_once_t g_validator_once = PTHREAD_ONCE_INIT;

/**
 * Initialize global crypto regex validator with PCRE2 compiled patterns
 * Called once per process via pthread_once
 */
static void crypto_regex_init(void) {
  int errornumber;
  PCRE2_SIZE erroroffset;

  /* ─── Compile known_hosts regex ─── */
  g_validator.known_hosts_regex =
      pcre2_compile((PCRE2_SPTR)KNOWN_HOSTS_REGEX_PATTERN, PCRE2_ZERO_TERMINATED,
                    PCRE2_MULTILINE | PCRE2_UCP | PCRE2_UTF, &errornumber, &erroroffset, NULL);

  if (!g_validator.known_hosts_regex) {
    PCRE2_UCHAR error_buffer[256];
    pcre2_get_error_message(errornumber, error_buffer, sizeof(error_buffer));
    log_fatal("Failed to compile known_hosts regex at offset %zu: %s", erroroffset, (const char *)error_buffer);
    return;
  }

  /* ─── Compile SSH public key regex ─── */
  g_validator.ssh_public_key_regex =
      pcre2_compile((PCRE2_SPTR)SSH_PUBLIC_KEY_REGEX_PATTERN, PCRE2_ZERO_TERMINATED,
                    PCRE2_CASELESS | PCRE2_UCP | PCRE2_UTF, &errornumber, &erroroffset, NULL);

  if (!g_validator.ssh_public_key_regex) {
    PCRE2_UCHAR error_buffer[256];
    pcre2_get_error_message(errornumber, error_buffer, sizeof(error_buffer));
    log_fatal("Failed to compile SSH public key regex at offset %zu: %s", erroroffset, (const char *)error_buffer);
    pcre2_code_free(g_validator.known_hosts_regex);
    return;
  }

  /* ─── Compile OpenSSH PEM regex ─── */
  g_validator.openssh_pem_regex =
      pcre2_compile((PCRE2_SPTR)OPENSSH_PEM_REGEX_PATTERN, PCRE2_ZERO_TERMINATED,
                    PCRE2_MULTILINE | PCRE2_DOTALL | PCRE2_UCP | PCRE2_UTF, &errornumber, &erroroffset, NULL);

  if (!g_validator.openssh_pem_regex) {
    PCRE2_UCHAR error_buffer[256];
    pcre2_get_error_message(errornumber, error_buffer, sizeof(error_buffer));
    log_fatal("Failed to compile OpenSSH PEM regex at offset %zu: %s", erroroffset, (const char *)error_buffer);
    pcre2_code_free(g_validator.known_hosts_regex);
    pcre2_code_free(g_validator.ssh_public_key_regex);
    return;
  }

  /* ─── Compile JIT for all patterns ─── */
  for (int i = 0; i < 3; i++) {
    pcre2_code *regex = (i == 0)   ? g_validator.known_hosts_regex
                        : (i == 1) ? g_validator.ssh_public_key_regex
                                   : g_validator.openssh_pem_regex;
    int jit_rc = pcre2_jit_compile(regex, PCRE2_JIT_COMPLETE);
    if (jit_rc < 0) {
      log_warn("PCRE2 JIT compilation failed for pattern %d (code %d), using interpreted mode", i, jit_rc);
      /* Fall through - interpreted mode still works, just slower */
    }
  }

  /* ─── Allocate JIT stack ─── */
  g_validator.jit_stack = pcre2_jit_stack_create(32 * 1024, 512 * 1024, NULL);

  g_validator.initialized = true;
}

/**
 * Get initialized validator (lazy initialization via pthread_once)
 */
static crypto_regex_validator_t *crypto_regex_get(void) {
  pthread_once(&g_validator_once, crypto_regex_init);
  return g_validator.initialized ? &g_validator : NULL;
}

/**
 * Extract named substring from match data
 * Returns allocated string or NULL if group not matched
 * Note: This version requires the original subject string to be passed in
 */
static char *crypto_regex_extract_named_group_with_subject(pcre2_code *regex, pcre2_match_data *match_data,
                                                           const char *group_name, const char *subject) {
  if (!regex || !match_data || !group_name || !subject) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid parameters");
    return NULL;
  }

  int group_number = pcre2_substring_number_from_name(regex, (PCRE2_SPTR)group_name);
  if (group_number < 0) {
    return NULL; /* Group doesn't exist */
  }

  PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
  PCRE2_SIZE start = ovector[2 * group_number];
  PCRE2_SIZE end = ovector[2 * group_number + 1];

  if (start == PCRE2_UNSET || end == PCRE2_UNSET) {
    return NULL; /* Group not matched */
  }

  /* Allocate and copy substring */
  size_t len = end - start;
  char *result = SAFE_MALLOC(len + 1, char *);
  if (!result) {
    return NULL;
  }

  memcpy(result, subject + start, len);
  result[len] = '\0';

  return result;
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

  crypto_regex_validator_t *validator = crypto_regex_get();
  if (!validator || !validator->known_hosts_regex) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid validator state");
    return false;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(validator->known_hosts_regex, NULL);
  if (!match_data) {
    return false;
  }

  /* Perform JIT match if JIT compiled, otherwise interpreted match */
  int rc;
  if (validator->jit_stack) {
    rc = pcre2_jit_match(validator->known_hosts_regex, (PCRE2_SPTR)line, strlen(line), 0, /* startoffset */
                         0,                                                               /* options */
                         match_data, NULL);                                               /* mcontext */
  } else {
    rc = pcre2_match(validator->known_hosts_regex, (PCRE2_SPTR)line, strlen(line), 0, /* startoffset */
                     0,                                                               /* options */
                     match_data, NULL);                                               /* mcontext */
  }

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    return false;
  }

  /* Extract named groups */
  *ip_port_out =
      crypto_regex_extract_named_group_with_subject(validator->known_hosts_regex, match_data, "ip_port", line);
  *key_type_out =
      crypto_regex_extract_named_group_with_subject(validator->known_hosts_regex, match_data, "key_type", line);
  *hex_key_out =
      crypto_regex_extract_named_group_with_subject(validator->known_hosts_regex, match_data, "hex_key", line);
  *comment_out =
      crypto_regex_extract_named_group_with_subject(validator->known_hosts_regex, match_data, "comment", line);

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

  crypto_regex_validator_t *validator = crypto_regex_get();
  if (!validator || !validator->ssh_public_key_regex) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid validator state");
    return false;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(validator->ssh_public_key_regex, NULL);
  if (!match_data) {
    return false;
  }

  /* Perform JIT match if JIT compiled, otherwise interpreted match */
  int rc;
  if (validator->jit_stack) {
    rc = pcre2_jit_match(validator->ssh_public_key_regex, (PCRE2_SPTR)line, strlen(line), 0, /* startoffset */
                         0,                                                                  /* options */
                         match_data, NULL);                                                  /* mcontext */
  } else {
    rc = pcre2_match(validator->ssh_public_key_regex, (PCRE2_SPTR)line, strlen(line), 0, /* startoffset */
                     0,                                                                  /* options */
                     match_data, NULL);                                                  /* mcontext */
  }

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    return false;
  }

  /* Extract named groups */
  *base64_key_out =
      crypto_regex_extract_named_group_with_subject(validator->ssh_public_key_regex, match_data, "base64_key", line);
  *comment_out =
      crypto_regex_extract_named_group_with_subject(validator->ssh_public_key_regex, match_data, "comment", line);

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

  crypto_regex_validator_t *validator = crypto_regex_get();
  if (!validator || !validator->openssh_pem_regex) {
    SET_ERRNO(ERROR_INVALID_STATE, "Invalid validator state");
    return false;
  }

  pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(validator->openssh_pem_regex, NULL);
  if (!match_data) {
    return false;
  }

  /* Perform match */
  int rc;
  if (validator->jit_stack) {
    rc = pcre2_jit_match(validator->openssh_pem_regex, (PCRE2_SPTR)file_content, strlen(file_content),
                         0,                 /* startoffset */
                         0,                 /* options */
                         match_data, NULL); /* mcontext */
  } else {
    rc = pcre2_match(validator->openssh_pem_regex, (PCRE2_SPTR)file_content, strlen(file_content), 0, /* startoffset */
                     0,                                                                               /* options */
                     match_data, NULL);                                                               /* mcontext */
  }

  if (rc < 0) {
    pcre2_match_data_free(match_data);
    return false;
  }

  /* Extract base64 data */
  *base64_data_out = crypto_regex_extract_named_group_with_subject(validator->openssh_pem_regex, match_data,
                                                                   "base64_data", file_content);

  pcre2_match_data_free(match_data);

  /* Verify we got the base64 data */
  if (!*base64_data_out) {
    return false;
  }

  return true;
}
