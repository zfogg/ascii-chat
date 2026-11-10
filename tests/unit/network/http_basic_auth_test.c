#include <criterion/criterion.h>
#include <sodium.h>

#include "../../../src/common/http_basic_auth.h"

Test(http_basic_auth, parses_valid_credentials) {
  http_basic_credentials_t credentials;
  cr_assert_eq(sodium_init(), 0);
  cr_assert_eq(http_basic_auth_parse("Basic dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
  cr_assert_str_eq(credentials.username, "user");
  cr_assert_str_eq(credentials.password, "pass");
  cr_assert(http_basic_auth_matches(&credentials, "user", "pass"));
  http_basic_credentials_clear(&credentials);
  cr_assert_eq(credentials.username[0], '\0');
}

Test(http_basic_auth, scheme_is_case_insensitive) {
  http_basic_credentials_t credentials;
  cr_assert_eq(http_basic_auth_parse("bAsIc dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
}

Test(http_basic_auth, rejects_malformed_or_embedded_nul_credentials) {
  http_basic_credentials_t credentials;
  cr_assert_neq(http_basic_auth_parse("Bearer dXNlcjpwYXNz", &credentials), ASCIICHAT_OK);
  cr_assert_neq(http_basic_auth_parse("Basic bm8tc2VwYXJhdG9y", &credentials), ASCIICHAT_OK);
  cr_assert_neq(http_basic_auth_parse("Basic dXNlcgA6cGFzcw==", &credentials), ASCIICHAT_OK);
}
