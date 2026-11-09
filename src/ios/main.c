#include "main.h"

#include <ascii-chat/platform/ios/render_bridge.h>
#include "../main.h"
#include <ascii-chat/platform/ios/stdin_bridge.h>
#include <stddef.h>
#include <stdio.h>
#include <stdatomic.h>
#include <os/log.h>

extern int asciichat_main(int argc, char **argv);

static _Atomic int g_ios_running;
static _Atomic int g_ios_last_result;

int asciichat_ios_main(asciichat_ios_mode_t mode) {
  os_log(OS_LOG_DEFAULT, "iOS native session starting mode=%d", mode);
  if (!asciichat_ios_mode_available(mode)) {
    g_ios_last_result = -1;
    return -1;
  }
  static char program[] = "ascii-chat-ios";
  static char mirror[] = "mirror";
  static char client[] = "client";
  static char discovery[] = "discovery";
  static char splash[] = "--splash-screen=false";
  static char color[] = "--color-mode=truecolor";
  // Zero lets a real camera keep its negotiated delivery rate. Synthetic
  // sources select the 60 FPS default in the shared capture pipeline.
  static char fps[] = "--fps=0";
  int render_cols = 0;
  int render_rows = 0;
  ios_render_bridge_get_dimensions(&render_cols, &render_rows);
  os_log(OS_LOG_DEFAULT, "iOS native dimensions cols=%d rows=%d", render_cols, render_rows);
  if (render_cols <= 0 || render_rows <= 0) {
    g_ios_last_result = -1;
    return -1;
  }
  static char width[32];
  static char height[32];
  snprintf(width, sizeof(width), "--width=%d", render_cols);
  snprintf(height, sizeof(height), "--height=%d", render_rows);
#ifdef ASCIICHAT_IOS_SIMULATOR
  static char test_pattern[] = "--test-pattern=1";
#else
  static char test_pattern[] = "--test-pattern=false";
#endif
  char *mode_name = mode == ASCIICHAT_IOS_MODE_CLIENT      ? client
                    : mode == ASCIICHAT_IOS_MODE_DISCOVERY ? discovery
                                                           : mirror;
  /* Keep only options accepted by mirror; status-screen is server-only. */
  char *argv[] = {program, mode_name, splash, color, fps, width, height, test_pattern, NULL};
  ios_stdin_bridge_reset();
  atomic_store_explicit(&g_ios_running, 1, memory_order_release);
  atomic_store_explicit(&g_ios_last_result, 0, memory_order_release);
  os_log(OS_LOG_DEFAULT, "iOS native entering asciichat_main");
  for (int i = 0; i < 8; i++)
    os_log(OS_LOG_DEFAULT, "iOS argv[%d]=%{public}s", i, argv[i]);
  int result = asciichat_main(8, argv);
  os_log(OS_LOG_DEFAULT, "iOS native session finished result=%d", result);
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

void asciichat_ios_request_stop(void) {
  signal_exit();
}
