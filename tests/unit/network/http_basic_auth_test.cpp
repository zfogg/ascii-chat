#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <sodium.h>

#include <ascii-chat/network/http/server/basic_auth.h>

TEST_CASE("http_basic_auth/parses_and_matches_credentials", "[http_basic_auth]") {
  http_basic_credentials_t credentials;
  ASCIICHAT_REQUIRE(sodium_init() >= 0);
  ASCIICHAT_REQUIRE_EQ(http_basic_auth_parse("Basic dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(credentials.username, "user");
  ASCIICHAT_REQUIRE_STR_EQ(credentials.password, "pass");
  ASCIICHAT_REQUIRE(http_basic_auth_matches(&credentials, "user", "pass"));
  http_basic_credentials_clear(&credentials);
  ASCIICHAT_REQUIRE_EQ(credentials.username[0], '\0');
}

TEST_CASE("http_basic_auth/accepts_case_insensitive_scheme", "[http_basic_auth]") {
  http_basic_credentials_t credentials;
  ASCIICHAT_REQUIRE_EQ(http_basic_auth_parse("bAsIc dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
}

TEST_CASE("http_basic_auth/rejects_invalid_credentials", "[http_basic_auth]") {
  http_basic_credentials_t credentials;
  ASCIICHAT_REQUIRE_NE(http_basic_auth_parse("Bearer dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(http_basic_auth_parse("Basic bm8tc2VwYXJhdG9y", &credentials), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(http_basic_auth_parse("Basic dXNlcgA6cGFzcw==", &credentials), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(http_basic_auth_parse("Basic dXNlcjo=", &credentials), ASCIICHAT_OK);
}
