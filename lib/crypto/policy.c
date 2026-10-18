#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/network/packet/packet.h>

asciichat_error_t crypto_policy_resolve(const crypto_policy_t *policy, uint8_t *mode) {
  if (!policy || !mode)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing crypto policy or result");
  if (policy->encrypt && policy->no_encrypt)
    return SET_ERRNO(ERROR_INVALID_PARAM, "--encrypt and --no-encrypt are mutually exclusive");
  bool authentication = policy->has_password || policy->has_identity || policy->has_verification_key;
  if (policy->no_auth && authentication)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Authentication material cannot be used with --no-auth");
  bool encryption = !policy->no_encrypt && (policy->encrypt || policy->has_password || !policy->transport_secure);
  *mode = (encryption ? ACIP_CRYPTO_ENCRYPT : 0) | (authentication ? ACIP_CRYPTO_AUTH : 0);
  return ASCIICHAT_OK;
}
