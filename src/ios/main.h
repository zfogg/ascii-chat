#pragma once

typedef enum {
  ASCIICHAT_IOS_MODE_MIRROR = 0,
  ASCIICHAT_IOS_MODE_CLIENT,
  ASCIICHAT_IOS_MODE_DISCOVERY,
} asciichat_ios_mode_t;

int asciichat_ios_main(asciichat_ios_mode_t mode);
void asciichat_ios_set_session_string(const char *session_string);
void asciichat_ios_set_server_address(const char *address);
void asciichat_ios_set_password(const char *password);
int asciichat_ios_mode_available(asciichat_ios_mode_t mode);
const char *asciichat_ios_mode_name(asciichat_ios_mode_t mode);
int asciichat_ios_is_running(void);
int asciichat_ios_last_result(void);
void asciichat_ios_request_stop(void);
