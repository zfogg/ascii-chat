#include <ascii-chat/network/acip/security.h>
#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/options/options.h>
#include <string.h>

asciichat_error_t acip_security_init_options(acip_security_t *security, bool server, bool transport_secure,
                                             const char *password_override) {
  if (!security)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Missing peer security state");
  memset(security, 0, sizeof(*security));
  const options_t *options = options_get();
  if (!options)
    return SET_ERRNO(ERROR_INVALID_STATE, "Peer security requires initialized options");
  const char *password = password_override ? password_override : options->password;
  crypto_policy_t policy = {
      .transport_secure = transport_secure,
      .encrypt = options->encrypt_enabled,
      .no_encrypt = options->no_encrypt,
      .no_auth = options->no_auth,
      .has_password = password[0] != '\0',
      .has_identity = options->encrypt_key[0] != '\0',
      .has_verification_key = server ? options->client_keys[0] != '\0' : options->server_key[0] != '\0',
  };
  asciichat_error_t result = crypto_policy_resolve(&policy, &security->mode);
  security->forbid_encryption = policy.no_encrypt;
  security->require_encryption = policy.encrypt;
  if (result != ASCIICHAT_OK)
    return result;
  crypto_handshake_context_t *ctx = &security->handshake;
  result = policy.has_password ? crypto_handshake_init_with_password("webrtc_peer", ctx, server, password)
                               : crypto_handshake_init("webrtc_peer", ctx, server);
  if (result != ASCIICHAT_OK)
    return result;
  ctx->require_client_auth = server && options->require_server_verify;
  ctx->crypto_ctx.encrypt_data = ACIP_CRYPTO_HAS_ENCRYPT(security->mode);
  if (policy.has_identity) {
    private_key_t *private_key = server ? &ctx->server_private_key : &ctx->client_private_key;
    public_key_t *public_key = server ? &ctx->server_public_key : &ctx->client_public_key;
    result = parse_private_key(options->encrypt_key, private_key);
    if (result != ASCIICHAT_OK)
      goto failed;
    public_key->type = KEY_TYPE_ED25519;
    memcpy(public_key->key, private_key->public_key, ED25519_PUBLIC_KEY_SIZE);
    if (!server && strncmp(options->encrypt_key, "gpg:", 4) == 0)
      SAFE_STRNCPY(ctx->client_gpg_key_id, options->encrypt_key + 4, sizeof(ctx->client_gpg_key_id));
  }
  if (server && policy.has_verification_key) {
    ctx->client_whitelist = SAFE_CALLOC(MAX_CLIENTS, sizeof(public_key_t), public_key_t *);
    if (!ctx->client_whitelist) {
      result = SET_ERRNO(ERROR_MEMORY, "Cannot allocate WebRTC client key list");
      goto failed;
    }
    result = parse_public_keys(options->client_keys, ctx->client_whitelist, &ctx->num_whitelisted_clients, MAX_CLIENTS);
    if (result != ASCIICHAT_OK)
      goto failed;
    if (!ctx->num_whitelisted_clients) {
      result = SET_ERRNO(ERROR_CRYPTO_AUTH, "WebRTC client key list is empty");
      goto failed;
    }
    ctx->require_client_auth = true;
  } else if (!server && policy.has_verification_key) {
    ctx->verify_server_key = true;
    SAFE_STRNCPY(ctx->expected_server_key, options->server_key, sizeof(ctx->expected_server_key));
    SAFE_STRNCPY(ctx->server_hostname, options->session_string, sizeof(ctx->server_hostname));
  }
  return ASCIICHAT_OK;
failed:
  acip_security_cleanup(security);
  return result;
}
