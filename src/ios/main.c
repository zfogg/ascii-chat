#include "main.h"
#include <stddef.h>

extern int asciichat_main(int argc, char **argv);

int asciichat_ios_main(asciichat_ios_mode_t mode) {
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
  char *argv[] = {program, mode_name, splash, status, color, NULL};
  return asciichat_main(5, argv);
}
