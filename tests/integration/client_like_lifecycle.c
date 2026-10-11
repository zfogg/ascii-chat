/**
 * Native lifecycle regression probe, with no webcam, microphone, or network.
 * Built by the test-client-like-lifecycle target and run through the Python
 * driver by CTest. Checks remain active in Debug and Release builds.
 *
 * Application callbacks record retry sleeps without waiting in real time.
 * Session capture/display creation and teardown use the actual library code.
 */
#include "session/client_like.h"
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/common.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                                                               \
  do {                                                                                                                 \
    if (!(condition)) {                                                                                                \
      log_set_terminal_output(true);                                                                                   \
      log_error("Lifecycle check failed: %s (%s:%d)", #condition, __FILE__, __LINE__);                                 \
      abort();                                                                                                         \
    }                                                                                                                  \
  } while (0)

static int calls, prepares, stops, retries;
static unsigned int slept;
static bool exiting;
static bool custom_exit;
static int custom_exit_calls;
static bool (*saved_exit_adapter)(void *);
static bool client_like_custom_exit(void *data) {
  CHECK(data == &custom_exit_calls);
  custom_exit_calls++;
  return custom_exit;
}
static const char *scenario;
static uint32_t expected_fps = 60;
static bool client_like_should_exit(void) {
  return exiting;
}
static void client_like_stop_audio(void) {
  stops++;
}
static void client_like_sleep_ms(unsigned int ms) {
  CHECK(ms <= 100);
  slept += ms;
  if (!strcmp(scenario, "interrupt"))
    exiting = true;
}
static asciichat_error_t client_like_prepare(void *data) {
  CHECK(data == &calls);
  prepares++;
  if (!strcmp(scenario, "shutdown"))
    exiting = true;
  return !strcmp(scenario, "prepare-error") ? ERROR_INVALID_STATE : ASCIICHAT_OK;
}
static bool client_like_retry(asciichat_error_t error, int attempt, void *data) {
  CHECK(error != ASCIICHAT_OK && attempt == calls && data == &retries);
  retries++;
  return strcmp(scenario, "veto") != 0;
}
static asciichat_error_t client_like_run(session_capture_ctx_t *capture, session_display_ctx_t *display, void *data) {
  CHECK(capture && display && data == &calls);
  CHECK(session_capture_get_target_fps(capture) == expected_fps);
  calls++;
  saved_exit_adapter = session_client_like_get_render_should_exit();
  CHECK(saved_exit_adapter);
  if (!exiting) {
    int calls_before_adapter_check = custom_exit_calls;
    CHECK(!saved_exit_adapter(NULL));
    custom_exit = true;
    CHECK(saved_exit_adapter(NULL));
    custom_exit = false;
    CHECK(custom_exit_calls == calls_before_adapter_check + 2);
  }
  if (!strcmp(scenario, "discovery-destroy")) {
    session_display_destroy(display);
    session_display_set_global_context_public(NULL);
    return ASCIICHAT_OK;
  }
  if (!strncmp(scenario, "fps-", 4) || !strcmp(scenario, "success") || !strcmp(scenario, "mirror") ||
      (!strcmp(scenario, "retry-success") && calls == 3) || (!strcmp(scenario, "backoff") && calls == 2))
    return ASCIICHAT_OK;
  return !strcmp(scenario, "backoff") ? ERROR_RATE_LIMITED : ERROR_NETWORK;
}
int main(int argc, char **argv) {
  CHECK(argc == 2 || argc == 3);
  scenario = argv[1];
  CHECK(asciichat_shared_init(NULL, false, true) == ASCIICHAT_OK);
  CHECK(options_state_init() == ASCIICHAT_OK);
  options_t opts = options_t_new();
  if (!strncmp(scenario, "fps-", 4)) {
    CHECK(argc == 3);
    bool explicit_fps = strstr(scenario, "override") != NULL;
    char *fps_value = strstr(scenario, "60") ? "60" : "12";
    char *mode = strstr(scenario, "mirror") ? "mirror" : "client";
    char *fps_args[] = {"ascii-chat", mode, "--file", argv[2], "--fps", fps_value, NULL};
    CHECK(options_init(explicit_fps ? 6 : 4, fps_args) == ASCIICHAT_OK);
    opts = *options_get();
    expected_fps = explicit_fps ? (strstr(scenario, "60") ? 60 : 12) : 24;
  }
  opts.test_pattern = opts.media_file[0] == '\0';
  opts.audio_enabled = false;
  opts.snapshot_mode = true;
  opts.snapshot_delay = 0;
  opts.splash_screen = false;
  opts.disable_keepawake = true;
  opts.width = 40;
  opts.height = 12;
  CHECK(options_state_set(&opts) == ASCIICHAT_OK);
  app_callbacks_t cb = {.should_exit = client_like_should_exit,
                        .platform_sleep_ms = client_like_sleep_ms,
                        .audio_stop_thread = client_like_stop_audio};
  app_callbacks_register(&cb);
  session_client_like_config_t config = {.run_fn = client_like_run,
                                         .custom_should_exit = client_like_custom_exit,
                                         .exit_user_data = &custom_exit_calls,
                                         .prepare_fn = client_like_prepare,
                                         .run_user_data = &calls,
                                         .kind = SESSION_CLIENT_LIKE_KIND_CLIENT,
                                         .max_reconnect_attempts = 3,
                                         .should_reconnect_callback = client_like_retry,
                                         .reconnect_user_data = &retries,
                                         .reconnect_delay_ms = 250};
  if (!strcmp(scenario, "no-retry"))
    config.max_reconnect_attempts = 0;
  if (strstr(scenario, "mirror"))
    config.kind = SESSION_CLIENT_LIKE_KIND_MIRROR;
  if (!strcmp(scenario, "discovery-destroy")) {
    config.kind = SESSION_CLIENT_LIKE_KIND_DISCOVERY;
    config.discovery = &calls;
  }
  asciichat_error_t result = session_client_like_run(&config);
  CHECK(prepares == 1 && stops == 1);
  CHECK(!session_client_like_get_render_should_exit());
  if (saved_exit_adapter) {
    int calls_before_cleanup_check = custom_exit_calls;
    (void)saved_exit_adapter(NULL);
    CHECK(custom_exit_calls == calls_before_cleanup_check);
  }
  CHECK(!session_client_like_get_stdin_reader());
  if (!strcmp(scenario, "prepare-error"))
    CHECK(result == ERROR_INVALID_STATE && calls == 0);
  else if (!strcmp(scenario, "shutdown"))
    CHECK(result == ASCIICHAT_OK && calls == 0);
  else if (!strncmp(scenario, "fps-", 4) || !strcmp(scenario, "success") || !strcmp(scenario, "mirror") ||
           !strcmp(scenario, "discovery-destroy"))
    CHECK(result == ASCIICHAT_OK && calls == 1);
  else if (!strcmp(scenario, "retry-success"))
    CHECK(result == ASCIICHAT_OK && calls == 3 && retries == 2 && slept == 500);
  else if (!strcmp(scenario, "backoff"))
    CHECK(result == ASCIICHAT_OK && calls == 2 && slept == 2000);
  else if (!strcmp(scenario, "no-retry"))
    CHECK(result == ERROR_NETWORK && calls == 1 && retries == 0 && slept == 0);
  else if (!strcmp(scenario, "veto"))
    CHECK(result == ERROR_NETWORK && calls == 1 && retries == 1 && slept == 0);
  else if (!strcmp(scenario, "interrupt"))
    CHECK(result == ERROR_NETWORK && slept == 100);
  else
    CHECK(result == ERROR_NETWORK && calls == 3 && retries == 3 && slept == 500);
  return 0;
}
