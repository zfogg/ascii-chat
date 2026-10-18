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

asciichat_error_t crypto_policy_check_mode(bool transport_secure, bool encrypt, bool no_encrypt, bool require_auth,
                                           uint8_t mode) {
  if (mode > ACIP_CRYPTO_FULL || (encrypt && no_encrypt))
    return SET_ERRNO(ERROR_CRYPTO, "Invalid security policy");
  if (require_auth && !ACIP_CRYPTO_HAS_AUTH(mode))
    return SET_ERRNO(ERROR_CRYPTO_AUTH, "Server requires authentication");
  if ((encrypt || (!transport_secure && !no_encrypt)) && !ACIP_CRYPTO_HAS_ENCRYPT(mode))
    return SET_ERRNO(ERROR_CRYPTO, "Server requires ACIP encryption");
  if (no_encrypt && ACIP_CRYPTO_HAS_ENCRYPT(mode))
    return SET_ERRNO(ERROR_CRYPTO, "Server disabled ACIP encryption");
  return ASCIICHAT_OK;
}
