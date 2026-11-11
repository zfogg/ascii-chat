#include <criterion/criterion.h>
#include <ascii-chat/crypto/handshake/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/util/endian.h>
#include <sodium.h>
#include <string.h>

static crypto_handshake_context_t first, second;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  cr_assert_eq(crypto_init(&first.crypto_ctx), CRYPTO_OK);
  cr_assert_eq(crypto_init(&second.crypto_ctx), CRYPTO_OK);
  uint8_t a[32], b[32];
  cr_assert_eq(crypto_get_public_key(&first.crypto_ctx, a), CRYPTO_OK);
  cr_assert_eq(crypto_get_public_key(&second.crypto_ctx, b), CRYPTO_OK);
  cr_assert_eq(crypto_set_peer_public_key(&first.crypto_ctx, b), CRYPTO_OK);
  cr_assert_eq(crypto_set_peer_public_key(&second.crypto_ctx, a), CRYPTO_OK);
  first.state = second.state = CRYPTO_HANDSHAKE_READY;
}
static void teardown(void) {
  crypto_handshake_destroy(&first);
  crypto_handshake_destroy(&second);
  CLEAR_ERRNO_ALL();
  log_destroy();
}
TestSuite(handshake_contract, .init = setup, .fini = teardown, .timeout = 10);

Test(handshake_contract, packet_lengths_enforce_negotiated_boundaries_and_reject_unknown_types) {
  first.crypto_ctx.public_key_size = first.crypto_ctx.auth_public_key_size = 32;
  first.crypto_ctx.signature_size = 64;
  first.crypto_ctx.hmac_size = first.crypto_ctx.auth_challenge_size = 32;
  const struct {
    uint16_t type;
    size_t minimum, maximum, alternate;
  } cases[] = {
      {PACKET_TYPE_CRYPTO_CAPABILITIES, sizeof(crypto_capabilities_packet_t), sizeof(crypto_capabilities_packet_t), 0},
      {PACKET_TYPE_CRYPTO_PARAMETERS, sizeof(crypto_parameters_packet_t), sizeof(crypto_parameters_packet_t), 0},
      {PACKET_TYPE_CRYPTO_KEY_EXCHANGE_INIT, 128, 128, 32},
      {PACKET_TYPE_CRYPTO_KEY_EXCHANGE_RESP, 128, 169, 32},
      {PACKET_TYPE_CRYPTO_AUTH_CHALLENGE, AUTH_CHALLENGE_FLAGS_SIZE + 32, AUTH_CHALLENGE_FLAGS_SIZE + 32, 0},
      {PACKET_TYPE_CRYPTO_AUTH_RESPONSE, 64, 105, 0},
      {PACKET_TYPE_CRYPTO_AUTH_FAILED, 0, MAX_AUTH_FAILED_PACKET_SIZE, 0},
      {PACKET_TYPE_CRYPTO_SERVER_AUTH_RESP, 32, 32, 0},
      {PACKET_TYPE_CRYPTO_HANDSHAKE_COMPLETE, 0, 0, 0},
      {PACKET_TYPE_CRYPTO_NO_ENCRYPTION, 0, 0, 0},
      {PACKET_TYPE_ENCRYPTED, 0, MAX_ENCRYPTED_PACKET_SIZE, 0}};
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
    const size_t lengths[] = {0,
                              1,
                              cases[i].minimum ? cases[i].minimum - 1 : 0,
                              cases[i].minimum,
                              cases[i].minimum + 1,
                              cases[i].maximum,
                              cases[i].maximum + 1,
                              cases[i].alternate,
                              SIZE_MAX};
    for (size_t j = 0; j < sizeof(lengths) / sizeof(*lengths); ++j) {
      bool valid = (lengths[j] >= cases[i].minimum && lengths[j] <= cases[i].maximum) ||
                   (cases[i].alternate && lengths[j] == cases[i].alternate);
      cr_assert_eq(crypto_handshake_validate_packet_size(&first, cases[i].type, lengths[j]),
                   valid ? ASCIICHAT_OK : ERROR_NETWORK_PROTOCOL, "type %u size %zu", cases[i].type, lengths[j]);
    }
  }
  cr_assert_eq(crypto_handshake_validate_packet_size(&first, UINT16_MAX, 0), ERROR_NETWORK_PROTOCOL);
  cr_assert_eq(crypto_handshake_validate_packet_size(NULL, 0, 0), ERROR_INVALID_PARAM);
}

Test(handshake_contract, negotiated_sizes_use_host_order_for_server_and_network_order_for_client) {
  for (unsigned server = 0; server < 2; ++server) {
    first.is_server = server;
    crypto_parameters_packet_t parameters = {.selected_cipher = CIPHER_ALGO_NONE,
                                             .verification_enabled = 1,
                                             .nonce_size = 24,
                                             .mac_size = 16,
                                             .hmac_size = 32};
    parameters.kex_public_key_size = server ? 32 : HOST_TO_NET_U16(32);
    parameters.auth_public_key_size = server ? 32 : HOST_TO_NET_U16(32);
    parameters.shared_secret_size = server ? 32 : HOST_TO_NET_U16(32);
    parameters.signature_size = server ? 64 : HOST_TO_NET_U16(64);
    cr_assert_eq(crypto_handshake_set_parameters(&first, &parameters), ASCIICHAT_OK);
    cr_assert_not(first.crypto_ctx.encrypt_data);
    cr_assert_eq(first.crypto_ctx.public_key_size, 32);
    cr_assert_eq(first.crypto_ctx.auth_public_key_size, 32);
    cr_assert_eq(first.crypto_ctx.shared_key_size, 32);
    cr_assert_eq(first.crypto_ctx.signature_size, 64);
    cr_assert_eq(first.crypto_ctx.nonce_size, 24);
    cr_assert_eq(first.crypto_ctx.mac_size, 16);
    cr_assert_eq(first.crypto_ctx.hmac_size, 32);
    cr_assert_eq(first.crypto_ctx.private_key_size, 32);
    if (!server)
      cr_assert(first.require_client_auth);
  }
  cr_assert_eq(crypto_handshake_set_parameters(NULL, NULL), ERROR_INVALID_PARAM);
  cr_assert_eq(crypto_handshake_set_parameters(&first, NULL), ERROR_INVALID_PARAM);
}

Test(handshake_contract, exposes_crypto_only_after_handshake_completion) {
  cr_assert(crypto_handshake_is_ready(&first));
  cr_assert_eq(crypto_handshake_get_context(&first), &first.crypto_ctx);
  cr_assert_null(crypto_handshake_get_context(NULL));
  first.state = CRYPTO_HANDSHAKE_INIT;
  cr_assert_null(crypto_handshake_get_context(&first));
}
