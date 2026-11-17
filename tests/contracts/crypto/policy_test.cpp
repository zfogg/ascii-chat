#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/common/protocol_constants.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/endian.h>

TEST_CASE("crypto_policy/resolves_supported_combinations", "[crypto_policy]") {
  struct {
    crypto_policy_t input;
    uint8_t expected;
  } cases[] = {
      {{0}, ACIP_CRYPTO_ENCRYPT},
      {{.transport_secure = true}, ACIP_CRYPTO_NONE},
      {{.transport_secure = true, .has_password = true}, ACIP_CRYPTO_FULL},
      {{.transport_secure = true, .encrypt = true}, ACIP_CRYPTO_ENCRYPT},
      {{.has_identity = true}, ACIP_CRYPTO_FULL},
      {{.transport_secure = true, .has_identity = true}, ACIP_CRYPTO_AUTH},
      {{.transport_secure = true, .has_verification_key = true}, ACIP_CRYPTO_AUTH},
      {{.has_identity = true, .no_encrypt = true}, ACIP_CRYPTO_AUTH},
      {{.has_verification_key = true, .no_encrypt = true}, ACIP_CRYPTO_AUTH},
      {{.has_password = true, .no_encrypt = true}, ACIP_CRYPTO_AUTH},
      {{.no_auth = true}, ACIP_CRYPTO_ENCRYPT},
      {{.no_auth = true, .encrypt = true}, ACIP_CRYPTO_ENCRYPT},
      {{.no_auth = true, .no_encrypt = true}, ACIP_CRYPTO_NONE},
      {{.no_encrypt = true}, ACIP_CRYPTO_NONE},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    uint8_t mode = 255;
    REQUIRE((crypto_policy_resolve(&cases[i].input, &mode)) == (ASCIICHAT_OK));
    REQUIRE((mode) == (cases[i].expected));
  }
}

TEST_CASE("crypto_policy/rejects_conflicts_without_changing_output", "[crypto_policy]") {
  crypto_policy_t invalid[] = {
      {.encrypt = true, .no_encrypt = true},
      {.no_auth = true, .has_identity = true},
      {.no_auth = true, .has_password = true},
      {.no_auth = true, .has_verification_key = true},
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    uint8_t mode = 255;
    REQUIRE((crypto_policy_resolve(&invalid[i], &mode)) != (ASCIICHAT_OK));
    REQUIRE((mode) == (255));
    CLEAR_ERRNO();
  }
}

TEST_CASE("crypto_policy/checks_all_mode_constraints", "[crypto_policy]") {
  for (unsigned secure = 0; secure < 2; ++secure) {
    for (unsigned encrypt = 0; encrypt < 2; ++encrypt) {
      for (unsigned no_encrypt = 0; no_encrypt < 2; ++no_encrypt) {
        for (unsigned require_auth = 0; require_auth < 2; ++require_auth) {
          for (uint8_t mode = 0; mode <= ACIP_CRYPTO_FULL; ++mode) {
            bool accepted = !(encrypt && no_encrypt) && (!require_auth || ACIP_CRYPTO_HAS_AUTH(mode)) && (!(encrypt || (!secure && !no_encrypt)) || ACIP_CRYPTO_HAS_ENCRYPT(mode)) &&
                            (!no_encrypt || !ACIP_CRYPTO_HAS_ENCRYPT(mode));
            REQUIRE(((crypto_policy_check_mode(secure, encrypt, no_encrypt, require_auth, mode) == ASCIICHAT_OK)) == (accepted));
            CLEAR_ERRNO();
          }
        }
      }
    }
  }
}

TEST_CASE("crypto_policy/rejects_missing_arguments_without_writing_output", "[crypto_policy]") {
  crypto_policy_t policy = {0};
  uint8_t mode = 0xa5;
  REQUIRE((crypto_policy_resolve(NULL, &mode)) == (ERROR_INVALID_PARAM));
  REQUIRE((mode) == (0xa5));
  CLEAR_ERRNO();
  REQUIRE((crypto_policy_resolve(&policy, NULL)) == (ERROR_INVALID_PARAM));
  CLEAR_ERRNO();
}

TEST_CASE("crypto_policy/rejects_unknown_wire_modes", "[crypto_policy]") {
  for (unsigned mode = ACIP_CRYPTO_FULL + 1; mode <= UINT8_MAX; ++mode) {
    REQUIRE((crypto_policy_check_mode(true, false, false, false, (uint8_t)mode)) == (ERROR_CRYPTO));
    CLEAR_ERRNO();
  }
}

TEST_CASE("crypto_policy/protocol_version_packet_matches_authoritative_constants", "[crypto_policy]") {
  protocol_version_packet_t version;
  protocol_version_packet_init(&version, ACIP_CRYPTO_ENCRYPT);
  REQUIRE((NET_TO_HOST_U16(version.protocol_version)) == (PROTOCOL_VERSION_MAJOR));
  REQUIRE((NET_TO_HOST_U16(version.protocol_revision)) == (PROTOCOL_VERSION_MINOR));
  REQUIRE((NET_TO_HOST_U16(version.protocol_patch)) == (PROTOCOL_VERSION_PATCH));
}
