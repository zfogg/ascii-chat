#include "main.h"

#include <ascii-chat/platform/ios/render_bridge.h>
#include "../main.h"
#include <ascii-chat/platform/ios/stdin_bridge.h>
#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/options/runtime.h>
#include <stddef.h>
#include <stdio.h>
#include <stdatomic.h>
#include <os/log.h>

extern int asciichat_main(int argc, char **argv);

static _Atomic int g_ios_running;
static _Atomic int g_ios_last_result;
static char g_ios_session_string[128];
static char g_ios_server_address[256];
static char g_ios_options_json[32768];

/* Native controls use the same shared help renderer as the terminal shortcut. */
void asciichat_ios_toggle_help(void) {
  int fd = ui_controller_active_fd();
  keyboard_help_toggle_terminal(fd);
}

void asciichat_ios_set_session_string(const char *session_string) {
  if (!session_string) {
    g_ios_session_string[0] = '\0';
    return;
  }
  snprintf(g_ios_session_string, sizeof(g_ios_session_string), "%s", session_string);
}

void asciichat_ios_set_server_address(const char *address) {
  if (!address) {
    g_ios_server_address[0] = '\0';
    return;
  }
  snprintf(g_ios_server_address, sizeof(g_ios_server_address), "%s", address);
}

void asciichat_ios_set_options_json(const char *json) {
  if (!json) {
    g_ios_options_json[0] = '\0';
    return;
  }
  snprintf(g_ios_options_json, sizeof(g_ios_options_json), "%s", json);
}

void asciichat_ios_apply_pending_options(void) {
  if (g_ios_options_json[0] == '\0')
    return;
  asciichat_error_t result = options_apply_json(g_ios_options_json, strlen(g_ios_options_json), OPTIONS_REVISION_ANY,
                                                OPTIONS_ORIGIN_LOCAL, NULL);
  if (result != ASCIICHAT_OK)
    os_log_error(OS_LOG_DEFAULT, "iOS persisted options patch was rejected: %{public}s",
                 asciichat_error_format(NULL, result));
}


int asciichat_ios_main(asciichat_ios_mode_t mode) {
  os_log(OS_LOG_DEFAULT, "iOS native session starting mode=%d", mode);
  if (!asciichat_ios_mode_available(mode)) {
    g_ios_last_result = -1;
    return -1;
  }
  static char program[] = "ascii-chat-ios";
  static char mirror[] = "mirror";
  static char client[] = "client";
  static char splash[] = "--splash-screen=false";
  static char color[] = "--color-mode=truecolor";
  // Zero lets a real camera keep its negotiated delivery rate. Synthetic
  // sources select the 60 FPS default in the shared capture pipeline.
  static char fps[] = "--fps=0";
#ifdef ASCIICHAT_IOS_SIMULATOR
  static char simulator_audio[] = "--audio=false";
  static char simulator_discovery_service[] = "--discovery-service=127.0.0.1";
  static char simulator_discovery_port[] = "--discovery-service-port=27225";
  static char simulator_discovery_insecure[] = "--discovery-insecure=true";
#endif
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
  static char test_pattern[] = "--test-pattern=1";
  char *mode_name = mode == ASCIICHAT_IOS_MODE_CLIENT ? client : mirror;
  /* Synthetic frames are a simulator mirror aid; network clients must use
   * their negotiated remote media instead. */
  char *argv[] = {program, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
  int argc = 1;
  /* Discovery is the binary's default session-string mode. Its positional
   * session must precede options, while client and mirror retain their mode
   * token and accept positional values after the shared options. */
  if (mode == ASCIICHAT_IOS_MODE_DISCOVERY) {
    if (g_ios_session_string[0] != '\0')
      argv[argc++] = g_ios_session_string;
  } else {
    argv[argc++] = mode_name;
  }
  argv[argc++] = splash;
  argv[argc++] = color;
  argv[argc++] = fps;
  argv[argc++] = width;
  argv[argc++] = height;
#ifdef ASCIICHAT_IOS_SIMULATOR
  argv[argc++] = simulator_audio;
  if (mode == ASCIICHAT_IOS_MODE_DISCOVERY) {
    argv[argc++] = simulator_discovery_service;
    argv[argc++] = simulator_discovery_port;
    argv[argc++] = simulator_discovery_insecure;
  }
  if (mode == ASCIICHAT_IOS_MODE_MIRROR || mode == ASCIICHAT_IOS_MODE_CLIENT) {
    argv[argc++] = test_pattern;
  }
#endif
  if (mode == ASCIICHAT_IOS_MODE_CLIENT && g_ios_server_address[0] != '\0') {
    argv[argc++] = g_ios_server_address;
  }
  ios_stdin_bridge_reset();
  atomic_store_explicit(&g_ios_running, 1, memory_order_release);
  atomic_store_explicit(&g_ios_last_result, 0, memory_order_release);
  os_log(OS_LOG_DEFAULT, "iOS native entering asciichat_main");
  for (int i = 0; i < argc; i++)
    os_log(OS_LOG_DEFAULT, "iOS argv[%d]=%{public}s", i, argv[i]);
  int result = asciichat_main(argc, argv);
  os_log(OS_LOG_DEFAULT, "iOS native session finished result=%d", result);
  atomic_store_explicit(&g_ios_last_result, result, memory_order_release);
  atomic_store_explicit(&g_ios_running, 0, memory_order_release);
  ios_stdin_bridge_reset();
  return result;
}

int asciichat_ios_mode_available(asciichat_ios_mode_t mode) {
  return mode == ASCIICHAT_IOS_MODE_MIRROR || mode == ASCIICHAT_IOS_MODE_CLIENT || mode == ASCIICHAT_IOS_MODE_DISCOVERY;
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
