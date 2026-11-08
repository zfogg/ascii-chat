#include "main.h"
#include <ascii-chat/platform/ios/stdin_bridge.h>
#include <stddef.h>
#include <stdatomic.h>

extern int asciichat_main(int argc, char **argv);

static _Atomic int g_ios_running;
static _Atomic int g_ios_last_result;

int asciichat_ios_main(asciichat_ios_mode_t mode) {
  if (!asciichat_ios_mode_available(mode)) {
    g_ios_last_result = -1;
    return -1;
  }
  static char program[] = "ascii-chat-ios";
  static char mirror[] = "mirror";
  static char client[] = "client";
  static char discovery[] = "discovery";
  static char splash[] = "--splash-screen=false";
  static char color[] = "--color-mode=none";
  static char test_pattern[] = "--test-pattern=false";
  char *mode_name = mode == ASCIICHAT_IOS_MODE_CLIENT      ? client
                    : mode == ASCIICHAT_IOS_MODE_DISCOVERY ? discovery
                                                           : mirror;
  /* Keep only options accepted by mirror; status-screen is server-only. */
  char *argv[] = {program, splash, color, test_pattern, mode_name, NULL};
  ios_stdin_bridge_reset();
  atomic_store_explicit(&g_ios_running, 1, memory_order_release);
  atomic_store_explicit(&g_ios_last_result, 0, memory_order_release);
  int result = asciichat_main(5, argv);
  atomic_store_explicit(&g_ios_last_result, result, memory_order_release);
  atomic_store_explicit(&g_ios_running, 0, memory_order_release);
  ios_stdin_bridge_reset();
  return result;
}

int asciichat_ios_mode_available(asciichat_ios_mode_t mode) {
  return mode == ASCIICHAT_IOS_MODE_MIRROR;
}

const char *asciichat_ios_mode_name(asciichat_ios_mode_t mode) {
  switch (mode) {
  case ASCIICHAT_IOS_MODE_MIRROR:
    return "Mirror";
  case ASCIICHAT_IOS_MODE_CLIENT:
    return "Client";
  case ASCIICHAT_IOS_MODE_DISCOVERY:
    return "Discovery";
  default:
    return "Unknown";
  }
}

int asciichat_ios_is_running(void) {
  return atomic_load_explicit(&g_ios_running, memory_order_acquire);
}
int asciichat_ios_last_result(void) {
  return atomic_load_explicit(&g_ios_last_result, memory_order_acquire);
}
