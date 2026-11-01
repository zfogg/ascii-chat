#include <ascii-chat/discovery/key_set.h>
#include <ascii-chat/network/acip/acds_client.h>
#include <sodium.h>
#include <string.h>

asciichat_error_t acds_create_key_set_collect(acds_create_key_set_t *set, const acip_session_create_t *req,
                                              bool require_identity, bool *complete) {
  if (!set || !req || !complete)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing create sequence parameters");
  *complete = false;
  size_t total = req->total_keys ? req->total_keys : 1;
  if (total > MAX_IDENTITY_KEYS || req->key_index >= total || req->key_index != set->count ||
      (!req->total_keys && set->count) ||
      (set->count && (req->total_keys != set->request.total_keys || req->capabilities != set->request.capabilities ||
                      req->max_participants != set->request.max_participants)) ||
      req->reserved_string_len >= ACIP_MAX_SESSION_STRING_LEN ||
      !memchr(req->server_address, '\0', sizeof(req->server_address)) ||
      (req->has_password && !memchr(req->password_hash, '\0', sizeof(req->password_hash)))) {
    memset(set, 0, sizeof(*set));
    return SET_ERRNO(ERROR_INVALID_PARAM, "Malformed or out-of-order create sequence");
  }
  for (size_t i = 0; i < set->count; ++i) {
    if (memcmp(set->keys[i], req->identity_pubkey, sizeof(req->identity_pubkey)) == 0) {
      memset(set, 0, sizeof(*set));
      return SET_ERRNO(ERROR_INVALID_PARAM, "Duplicate Ed25519 identity");
    }
  }
  // Every member of a restricted set proves possession. Optional single-key
  // identity keeps its existing service-wide policy.
  if (require_identity || total > 1) {
    if (!acds_validate_timestamp(req->timestamp, 300) ||
        acds_verify_session_create(req->identity_pubkey, req->timestamp, req->capabilities, req->max_participants,
                                   req->signature) != ASCIICHAT_OK) {
      memset(set, 0, sizeof(*set));
      return SET_ERRNO(ERROR_CRYPTO_VERIFICATION, "Invalid create identity proof");
    }
  }
  if (!set->count) {
    memcpy(&set->request, req, sizeof(*req));
    if (req->reserved_string_len)
      memcpy(set->reserved_string, req + 1, (size_t)req->reserved_string_len);
  }
  memcpy(set->keys[set->count++], req->identity_pubkey, sizeof(req->identity_pubkey));
  *complete = set->count == total;
  return ASCIICHAT_OK;
}
