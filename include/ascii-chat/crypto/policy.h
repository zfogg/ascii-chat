#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <ascii-chat/asciichat_errno.h>

/** Transport-independent inputs shared by native, WASM, and mobile callers.
 * @brief The useful part of crypto policy is in `transport_secure`, `encrypt`, `no_encrypt`, `no_auth`: together they describe what is pending, who owns it, and how much data is valid. Keep the object with its parent operation and use the parent cleanup path.
 * TLS/DTLS protect the transport. Keys request authentication, not encryption.
 * Explicit no-encrypt wins over the password default, enabling auth-only.
 */
typedef struct {
  bool transport_secure;
  bool encrypt;
  bool no_encrypt;
  bool no_auth;
  bool has_password;
  bool has_identity;
  bool has_verification_key;
} crypto_policy_t;

/** Validate a peer proposal against server transport and explicit encryption settings. */
asciichat_error_t crypto_policy_check_mode(bool transport_secure, bool encrypt, bool no_encrypt, bool require_auth,
                                           uint8_t mode);

/** Resolve to the ACIP_CRYPTO_* wire bitmask; reject contradictory requests. */
asciichat_error_t crypto_policy_resolve(const crypto_policy_t *policy, uint8_t *mode);
