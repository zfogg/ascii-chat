#pragma once
#include <ascii-chat/network/acip/acds.h>
#include <ascii-chat/common/limits.h>

/** Connection-local ordered Ed25519 create sequence. Never persists partial sets.
 * request and reserved_string are adjacent to preserve the legacy trailing name. */
typedef struct {
  acip_session_create_t request;
  char reserved_string[ACIP_MAX_SESSION_STRING_LEN];
  uint8_t keys[MAX_SESSION_KEYS][ED25519_PUBLIC_KEY_SIZE];
  size_t count;
} acds_create_key_set_t;

/** Validate order, bounds, deduplication and individual possession proofs.
 * Errors clear the sequence. complete is true only when every key is present. */
asciichat_error_t acds_create_key_set_collect(acds_create_key_set_t *set, const acip_session_create_t *req,
                                              bool require_identity, bool *complete);
