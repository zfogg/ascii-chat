#include <ascii-chat/crypto/policy.h>
#include <ascii-chat/network/packet/packet.h>
#include <ascii-chat/common.h>

#define CHECK(expr)                                                                                                    \
  do {                                                                                                                 \
    if (!(expr))                                                                                                       \
      FATAL(ERROR_ASSERTION_FAILED, "line %d: %s", __LINE__, #expr);                                                   \
  } while (0)

int main(void) {
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
    CHECK(crypto_policy_resolve(&cases[i].input, &mode) == ASCIICHAT_OK);
    CHECK(mode == cases[i].expected);
  }
  crypto_policy_t invalid[] = {
      {.encrypt = true, .no_encrypt = true},
      {.no_auth = true, .has_identity = true},
      {.no_auth = true, .has_password = true},
      {.no_auth = true, .has_verification_key = true},
  };
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    uint8_t mode = 255;
    CHECK(crypto_policy_resolve(&invalid[i], &mode) != ASCIICHAT_OK);
    CHECK(mode == 255);
    CLEAR_ERRNO();
  }
  return 0;
}
