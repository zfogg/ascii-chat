#include "main.h"
#include <stddef.h>

extern int asciichat_main(int argc, char **argv);

static volatile int g_ios_running;
static volatile int g_ios_last_result;

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
  static char status[] = "--status-screen=false";
  static char color[] = "--color-mode=none";
  char *mode_name = mode == ASCIICHAT_IOS_MODE_CLIENT ? client
                    : mode == ASCIICHAT_IOS_MODE_DISCOVERY ? discovery
                                                            : mirror;
  /* Global options precede the positional mode, matching the desktop CLI
   * parser and preserving the regular dispatcher path on iOS. */
  char *argv[] = {program, splash, status, color, mode_name, NULL};
  g_ios_running = 1;
  g_ios_last_result = 0;
  int result = asciichat_main(5, argv);
  g_ios_last_result = result;
  g_ios_running = 0;
  return result;
}

int asciichat_ios_mode_available(asciichat_ios_mode_t mode) {
  return mode == ASCIICHAT_IOS_MODE_MIRROR;
}

const char *asciichat_ios_mode_name(asciichat_ios_mode_t mode) {
  switch (mode) {
  case ASCIICHAT_IOS_MODE_MIRROR: return "Mirror";
  case ASCIICHAT_IOS_MODE_CLIENT: return "Client";
  case ASCIICHAT_IOS_MODE_DISCOVERY: return "Discovery";
  default: return "Unknown";
  }
}

int asciichat_ios_is_running(void) { return g_ios_running; }
int asciichat_ios_last_result(void) { return g_ios_last_result; }
