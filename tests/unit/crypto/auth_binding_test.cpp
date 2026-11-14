#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/crypto/crypto.h>
#include <ascii-chat/crypto/keys.h>
#include <ascii-chat/log/log.h>
#include <sodium.h>
#include <string.h>

static crypto_context_t sender, receiver;
static void setup(void) {
  log_init(NULL, LOG_FATAL, false, false);
  ASCIICHAT_REQUIRE_EQ(crypto_init(&sender), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_init(&receiver), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_generate_keypair(&sender), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_generate_keypair(&receiver), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&sender, receiver.public_key), CRYPTO_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_set_peer_public_key(&receiver, sender.public_key), CRYPTO_OK);
  ASCIICHAT_REQUIRE_ARRAY_EQ(sender.shared_key, receiver.shared_key, 32);
}
static void teardown(void) {
  crypto_destroy(&sender);
  crypto_destroy(&receiver);
  log_destroy();
}
TEST_CASE("auth_binding/authentication_mac_binds_nonce_password_and_dh_secret", "[auth_binding]") {
  uint8_t nonce[32], actual[32], expected[32], input[64];
  for (unsigned i = 0; i < 32; ++i)
    nonce[i] = (uint8_t)i;
  for (unsigned password = 0; password < 2; ++password) {
    sender.has_password = receiver.has_password = password != 0;
    memset(sender.password_key, 42, 32);
    memset(receiver.password_key, 42, 32);
    ASCIICHAT_REQUIRE_EQ(crypto_compute_auth_response(&sender, nonce, actual), CRYPTO_OK);
    memcpy(input, nonce, 32);
    memcpy(input + 32, sender.shared_key, 32);
    ASCIICHAT_REQUIRE_EQ(
        crypto_auth_hmacsha256(expected, input, sizeof(input), password ? sender.password_key : sender.shared_key), 0);
    ASCIICHAT_REQUIRE_ARRAY_EQ(actual, expected, 32);
    ASCIICHAT_REQUIRE(crypto_verify_auth_response(&receiver, nonce, actual));
    nonce[0] ^= 1;
    ASCIICHAT_REQUIRE_NOT(crypto_verify_auth_response(&receiver, nonce, actual));
    nonce[0] ^= 1;
    receiver.shared_key[0] ^= 1;
    ASCIICHAT_REQUIRE_NOT(crypto_verify_auth_response(&receiver, nonce, actual));
    receiver.shared_key[0] ^= 1;
    actual[31] ^= 1;
    ASCIICHAT_REQUIRE_NOT(crypto_verify_auth_response(&receiver, nonce, actual));
    actual[31] ^= 1;
    if (password) {
      receiver.password_key[0] ^= 1;
      ASCIICHAT_REQUIRE_NOT(crypto_verify_auth_response(&receiver, nonce, actual));
    }
  }
  ASCIICHAT_REQUIRE_EQ(crypto_compute_password_hmac(&sender, sender.password_key, nonce, sender.shared_key, actual),
               ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_auth_hmacsha256(expected, input, sizeof(input), sender.password_key), 0);
  ASCIICHAT_REQUIRE_ARRAY_EQ(actual, expected, 32);
  receiver.key_exchange_complete = false;
  ASCIICHAT_REQUIRE_NOT(crypto_verify_auth_response(&receiver, nonce, actual));
  ASCIICHAT_REQUIRE_NE(crypto_compute_auth_response(&receiver, nonce, actual), CRYPTO_OK);
  ASCIICHAT_REQUIRE_NE(crypto_compute_auth_response(NULL, nonce, actual), CRYPTO_OK);
  ASCIICHAT_REQUIRE_NE(crypto_compute_auth_response(&sender, NULL, actual), CRYPTO_OK);
  ASCIICHAT_REQUIRE_NE(crypto_compute_auth_response(&sender, nonce, NULL), CRYPTO_OK);
  ASCIICHAT_REQUIRE_NOT(crypto_verify_auth_response(NULL, nonce, actual));
  ASCIICHAT_REQUIRE_NOT(crypto_verify_auth_response(&sender, NULL, actual));
  ASCIICHAT_REQUIRE_NOT(crypto_verify_auth_response(&sender, nonce, NULL));
  ASCIICHAT_REQUIRE_EQ(crypto_compute_password_hmac(NULL, nonce, nonce, nonce, actual), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_compute_password_hmac(&sender, NULL, nonce, nonce, actual), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_compute_password_hmac(&sender, nonce, NULL, nonce, actual), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_compute_password_hmac(&sender, nonce, nonce, NULL, actual), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_compute_password_hmac(&sender, nonce, nonce, nonce, NULL), ERROR_INVALID_PARAM);
}
TEST_CASE("auth_binding/ephemeral_signatures_match_independent_ed25519_and_detect_tampering", "[auth_binding]") {
  private_key_t key = {.type = KEY_TYPE_ED25519};
  uint8_t seed[32] = {7}, public_key[32], signature[64], reference[64];
  ASCIICHAT_REQUIRE_EQ(crypto_sign_seed_keypair(public_key, key.key.ed25519, seed), 0);
  ASCIICHAT_REQUIRE_EQ(crypto_sign_ephemeral_key(&key, sender.public_key, 32, signature), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(crypto_sign_detached(reference, NULL, sender.public_key, 32, key.key.ed25519), 0);
  ASCIICHAT_REQUIRE_ARRAY_EQ(signature, reference, 64);
  ASCIICHAT_REQUIRE_EQ(crypto_verify_peer_signature(public_key, sender.public_key, 32, signature), ASCIICHAT_OK);
  signature[0] ^= 1;
  ASCIICHAT_REQUIRE_EQ(crypto_verify_peer_signature(public_key, sender.public_key, 32, signature), ERROR_CRYPTO);
  signature[0] ^= 1;
  ASCIICHAT_REQUIRE_EQ(crypto_verify_peer_signature(public_key, receiver.public_key, 32, signature), ERROR_CRYPTO);
  ASCIICHAT_REQUIRE_EQ(crypto_sign_ephemeral_key(NULL, seed, 32, signature), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_sign_ephemeral_key(&key, NULL, 32, signature), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_sign_ephemeral_key(&key, seed, 32, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_sign_ephemeral_key(&key, seed, 0, signature), ERROR_INVALID_PARAM);
  key.type = KEY_TYPE_UNKNOWN;
  ASCIICHAT_REQUIRE_EQ(crypto_sign_ephemeral_key(&key, seed, 32, signature), ERROR_CRYPTO);
  ASCIICHAT_REQUIRE_EQ(crypto_verify_peer_signature(NULL, seed, 32, signature), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_verify_peer_signature(public_key, NULL, 32, signature), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_verify_peer_signature(public_key, seed, 32, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(crypto_verify_peer_signature(public_key, seed, 0, signature), ERROR_INVALID_PARAM);
  sodium_memzero(&key, sizeof(key));
}
TEST_CASE("auth_binding/auth_fields_are_packed_in_wire_order_and_invalid_destinations_are_untouched", "[auth_binding]") {
  uint8_t mac[32], nonce[32], combined[64], out_mac[32], out_nonce[32];
  memset(mac, 0x55, sizeof(mac));
  memset(nonce, 0xaa, sizeof(nonce));
  crypto_combine_auth_data(mac, nonce, combined);
  ASCIICHAT_REQUIRE_ARRAY_EQ(combined, mac, 32);
  ASCIICHAT_REQUIRE_ARRAY_EQ(combined + 32, nonce, 32);
  crypto_extract_auth_data(combined, out_mac, out_nonce);
  ASCIICHAT_REQUIRE_ARRAY_EQ(mac, out_mac, 32);
  ASCIICHAT_REQUIRE_ARRAY_EQ(nonce, out_nonce, 32);
  crypto_combine_auth_data(NULL, nonce, combined);
  ASCIICHAT_REQUIRE_ARRAY_EQ(combined, mac, 32);
  crypto_combine_auth_data(mac, NULL, combined);
  ASCIICHAT_REQUIRE_ARRAY_EQ(combined + 32, nonce, 32);
  crypto_combine_auth_data(mac, nonce, NULL);
  crypto_extract_auth_data(NULL, out_mac, out_nonce);
  ASCIICHAT_REQUIRE_ARRAY_EQ(out_mac, mac, 32);
  crypto_extract_auth_data(combined, NULL, out_nonce);
  ASCIICHAT_REQUIRE_ARRAY_EQ(out_nonce, nonce, 32);
  crypto_extract_auth_data(combined, out_mac, NULL);
  ASCIICHAT_REQUIRE_ARRAY_EQ(out_mac, mac, 32);
}
