#pragma once

typedef enum {
  ASCIICHAT_IOS_MODE_MIRROR = 0,
  ASCIICHAT_IOS_MODE_CLIENT,
  ASCIICHAT_IOS_MODE_DISCOVERY,
} asciichat_ios_mode_t;

int asciichat_ios_main(asciichat_ios_mode_t mode);
int asciichat_ios_mode_available(asciichat_ios_mode_t mode);
