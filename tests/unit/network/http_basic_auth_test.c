#include <criterion/criterion.h>
#include <sodium.h>

#include <ascii-chat/network/http/server/basic_auth.h>

Test(http_basic_auth, parses_and_matches_credentials) {
  http_basic_credentials_t credentials;
  cr_assert(sodium_init() >= 0);
  cr_assert_eq(http_basic_auth_parse("Basic dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
  cr_assert_str_eq(credentials.username, "user");
  cr_assert_str_eq(credentials.password, "pass");
  cr_assert(http_basic_auth_matches(&credentials, "user", "pass"));
  http_basic_credentials_clear(&credentials);
  cr_assert_eq(credentials.username[0], '\0');
}

Test(http_basic_auth, accepts_case_insensitive_scheme) {
  http_basic_credentials_t credentials;
  cr_assert_eq(http_basic_auth_parse("bAsIc dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
}

Test(http_basic_auth, rejects_invalid_credentials) {
  http_basic_credentials_t credentials;
  cr_assert_neq(http_basic_auth_parse("Bearer dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
  cr_assert_neq(http_basic_auth_parse("Basic bm8tc2VwYXJhdG9y", &credentials), ASCIICHAT_OK);
  cr_assert_neq(http_basic_auth_parse("Basic dXNlcgA6cGFzcw==", &credentials), ASCIICHAT_OK);
  cr_assert_neq(http_basic_auth_parse("Basic dXNlcjo=", &credentials), ASCIICHAT_OK);
}
