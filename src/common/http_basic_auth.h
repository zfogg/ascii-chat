#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/options/options.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char username[OPTIONS_BUFF_SIZE];
  char password[OPTIONS_BUFF_SIZE];
} http_basic_credentials_t;

asciichat_error_t http_basic_auth_parse(const char *authorization,
                                        http_basic_credentials_t *out);
bool http_basic_auth_matches(const http_basic_credentials_t *provided, const char *username, const char *password);
void http_basic_credentials_clear(http_basic_credentials_t *credentials);
