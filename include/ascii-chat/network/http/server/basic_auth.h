#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <stdbool.h>

struct http_request_info;
struct http_response;

#define HTTP_BASIC_AUTH_VALUE_CAPACITY 1024

typedef struct http_basic_auth_config {
  bool enabled;
  const char *username;
  const char *password;
  const char *realm;
} http_basic_auth_config_t;

typedef struct http_basic_credentials {
  char username[HTTP_BASIC_AUTH_VALUE_CAPACITY];
  char password[HTTP_BASIC_AUTH_VALUE_CAPACITY];
} http_basic_credentials_t;

asciichat_error_t http_basic_auth_parse(const char *authorization, http_basic_credentials_t *out);
bool http_basic_auth_matches(const http_basic_credentials_t *provided, const char *username, const char *password);
bool http_basic_auth_authorize(const http_basic_auth_config_t *config, const char *authorization);
bool http_server_authorize(const http_basic_auth_config_t *config, const struct http_request_info *request,
                           struct http_response *response);
void http_basic_credentials_clear(http_basic_credentials_t *credentials);
