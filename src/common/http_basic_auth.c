#include "http_basic_auth.h"

#include <ascii-chat/platform/memory.h>
#include <ascii-chat/platform/string.h>
#include <sodium.h>
#include <string.h>

void http_basic_credentials_clear(http_basic_credentials_t *credentials) {
  if (credentials)
    sodium_memzero(credentials, sizeof(*credentials));
}

asciichat_error_t http_basic_auth_parse(const char *authorization, http_basic_credentials_t *out) {
  if (!authorization || !out)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Basic authorization arguments are required");
  memset(out, 0, sizeof(*out));
  while (*authorization == ' ' || *authorization == '\t')
    authorization++;
  const char scheme[] = "Basic";
  if (platform_strncasecmp(authorization, scheme, sizeof(scheme) - 1) != 0 ||
      (authorization[sizeof(scheme) - 1] != ' ' && authorization[sizeof(scheme) - 1] != '\t'))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Authorization is not Basic authentication");
  const char *encoded = authorization + sizeof(scheme) - 1;
  while (*encoded == ' ' || *encoded == '\t')
    encoded++;
  size_t encoded_length = strlen(encoded);
  if (!encoded_length || encoded_length > (sizeof(*out) * 4) || encoded_length % 4 != 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid Basic authorization value");
  uint8_t decoded[sizeof(*out) + 1];
  size_t decoded_length = 0;
  if (sodium_base642bin(decoded, sizeof(decoded) - 1, encoded, encoded_length, NULL, &decoded_length, NULL,
                        sodium_base64_VARIANT_ORIGINAL) != 0 || decoded_length == 0) {
    sodium_memzero(decoded, sizeof(decoded));
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid Basic authorization encoding");
  }
  for (size_t i = 0; i < decoded_length; ++i) {
    if (decoded[i] == '\0') {
      sodium_memzero(decoded, sizeof(decoded));
      return SET_ERRNO(ERROR_INVALID_PARAM, "Basic authorization contains an embedded NUL");
    }
  }
  char *separator = memchr(decoded, ':', decoded_length);
  if (!separator || separator == (char *)decoded || (size_t)(separator - (char *)decoded) >= sizeof(out->username) ||
      decoded_length - (size_t)(separator - (char *)decoded) - 1 >= sizeof(out->password)) {
    sodium_memzero(decoded, sizeof(decoded));
    return SET_ERRNO(ERROR_INVALID_PARAM, "Basic authorization must contain username and password");
  }
  size_t username_length = (size_t)(separator - (char *)decoded);
  size_t password_length = decoded_length - username_length - 1;
  memcpy(out->username, decoded, username_length);
  memcpy(out->password, separator + 1, password_length);
  sodium_memzero(decoded, sizeof(decoded));
  return ASCIICHAT_OK;
}

bool http_basic_auth_matches(const http_basic_credentials_t *provided, const char *username, const char *password) {
  if (!provided || !username || !password)
    return false;
  size_t provided_username_length = strlen(provided->username), expected_username_length = strlen(username);
  size_t provided_password_length = strlen(provided->password), expected_password_length = strlen(password);
  bool username_match = provided_username_length == expected_username_length &&
                        sodium_memcmp(provided->username, username, expected_username_length) == 0;
  bool password_match = provided_password_length == expected_password_length &&
                        sodium_memcmp(provided->password, password, expected_password_length) == 0;
  return username_match && password_match;
}
