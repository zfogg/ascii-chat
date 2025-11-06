/**
 * @file crypto/keys/gpg_stubs.c
 * @brief Stub implementations for GPG functions when GPG support is disabled
 *
 * This file provides stub implementations that return errors when GPG features
 * are disabled at compile time.
 */

#include "gpg_keys.h"
#include "../../asciichat_errno.h"
#include "../../logging.h"

// =============================================================================
// GPG Key Parsing Stubs
// =============================================================================
// NOTE: fetch_github_gpg_keys() and fetch_gitlab_gpg_keys() are implemented
// in https_keys.c and do not depend on the full GPG implementation.
// We only need to stub parse_gpg_key() which is called from keys.c.

asciichat_error_t parse_gpg_key(const char *gpg_key_text, public_key_t *key_out) {
  (void)gpg_key_text;
  (void)key_out;
  return SET_ERRNO(ERROR_CRYPTO_KEY, "GPG support is currently disabled");
}
