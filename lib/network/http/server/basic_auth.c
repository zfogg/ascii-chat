#include <ascii-chat/network/http/server/basic_auth.h>
#include <ascii-chat/network/http/server/server.h>
#include <ascii-chat/stats/runtime.h>

#include <libplatform/util/string.h>
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
    ++authorization;

  static const char scheme[] = "Basic";
  if (platform_strncasecmp(authorization, scheme, sizeof(scheme) - 1) != 0 ||
      (authorization[sizeof(scheme) - 1] != ' ' && authorization[sizeof(scheme) - 1] != '\t'))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Authorization is not Basic authentication");

  const char *encoded = authorization + sizeof(scheme) - 1;
  while (*encoded == ' ' || *encoded == '\t')
    ++encoded;
  size_t encoded_length = strlen(encoded);
  if (!encoded_length || encoded_length > sizeof(*out) * 4 || encoded_length % 4 != 0)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid Basic authorization value");

  uint8_t decoded[sizeof(*out) + 1];
  size_t decoded_length = 0;
  if (sodium_base642bin(decoded, sizeof(decoded) - 1, encoded, encoded_length, NULL, &decoded_length, NULL,
                        sodium_base64_VARIANT_ORIGINAL) != 0 ||
      decoded_length == 0) {
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
      decoded_length - (size_t)(separator - (char *)decoded) - 1 == 0 ||
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
  if (!provided || !username || !password) {
    (void)SET_ERRNO(ERROR_INVALID_PARAM, "Basic credential comparison arguments are required");
    return false;
  }

  uint8_t expected_username[HTTP_BASIC_AUTH_VALUE_CAPACITY] = {0};
  uint8_t expected_password[HTTP_BASIC_AUTH_VALUE_CAPACITY] = {0};
  bool expected_username_terminated = false, expected_password_terminated = false;
  for (size_t i = 0; i < HTTP_BASIC_AUTH_VALUE_CAPACITY; ++i) {
    if (!expected_username_terminated) {
      expected_username[i] = (uint8_t)username[i];
      if (!username[i])
        expected_username_terminated = true;
    }
    if (!expected_password_terminated) {
      expected_password[i] = (uint8_t)password[i];
      if (!password[i])
        expected_password_terminated = true;
    }
  }
  bool provided_username_terminated = false, provided_password_terminated = false;
  for (size_t i = 0; i < HTTP_BASIC_AUTH_VALUE_CAPACITY; ++i) {
    if (!provided_username_terminated && !provided->username[i])
      provided_username_terminated = true;
    if (!provided_password_terminated && !provided->password[i])
      provided_password_terminated = true;
  }
  int username_comparison = sodium_memcmp(provided->username, expected_username, HTTP_BASIC_AUTH_VALUE_CAPACITY);
  int password_comparison = sodium_memcmp(provided->password, expected_password, HTTP_BASIC_AUTH_VALUE_CAPACITY);
  bool username_match = expected_username_terminated && provided_username_terminated && username_comparison == 0;
  bool password_match = expected_password_terminated && provided_password_terminated && password_comparison == 0;
  sodium_memzero(expected_username, sizeof(expected_username));
  sodium_memzero(expected_password, sizeof(expected_password));
  return username_match && password_match;
}

bool http_basic_auth_authorize(const http_basic_auth_config_t *config, const char *authorization) {
  if (!config) {
    (void)SET_ERRNO(ERROR_INVALID_PARAM, "Missing Basic authentication configuration");
    return false;
  }
  if (!config->enabled)
    return true;
  if (!config->username || !config->password || !config->username[0] || !config->password[0]) {
    (void)SET_ERRNO(ERROR_INVALID_PARAM, "Basic authentication requires non-empty credentials");
    return false;
  }
  http_basic_credentials_t credentials;
  bool authorized = authorization && http_basic_auth_parse(authorization, &credentials) == ASCIICHAT_OK &&
                    http_basic_auth_matches(&credentials, config->username, config->password);
  http_basic_credentials_clear(&credentials);
  return authorized;
}

bool http_server_authorize(const http_basic_auth_config_t *config, const struct http_request_info *request,
                           struct http_response *response) {
  if (!config || !request || !response) {
    (void)SET_ERRNO(ERROR_INVALID_PARAM, "Basic authorization request arguments are required");
    return false;
  }
  if (!config->enabled)
    return true;
  const char *authorization = NULL;
  for (size_t i = 0; i < request->header_count; ++i) {
    if (!platform_strcasecmp(request->headers[i].name, "authorization")) {
      authorization = request->headers[i].value;
      break;
    }
  }
  if (http_basic_auth_authorize(config, authorization))
    return true;
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
  if (http_response_set_status(response, 401) != ASCIICHAT_OK)
    return false;
  const char *realm = config->realm && config->realm[0] ? config->realm : "ascii-chat";
  char challenge[256];
  if (safe_snprintf(challenge, sizeof(challenge), "Basic realm=\"%s\"", realm) < 0 ||
      http_response_set_header(response, "WWW-Authenticate", challenge) != ASCIICHAT_OK)
    return false;
  (void)http_response_finish(response);
  return false;
}
