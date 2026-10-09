/** Deterministic native UI entry points for Docker/tmux integration tests. */
#include <ascii-chat/common.h>
#include <ascii-chat/log/io.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/question.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/util/password.h>
#include <ascii-chat/crypto/known_hosts.h>
#include <ascii-chat/crypto/discovery_keys.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/mdns.h>
#include <ascii-chat/ui/splash.h>
#include <ascii-chat/ui/update_banner.h>
#include <ascii-chat/ui/keyboard_help.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/audio/visualization.h>
#include "session/display.h"
#include <string.h>
#include <math.h>

int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  const char *kind = argv[1];
  if (asciichat_shared_init(argv[2], true, true) != ASCIICHAT_OK)
    return 3;
  bool signal_fixture = !strncmp(kind, "signal-", 7);
  char *options[] = {"ui-probe",
                     "--no-check-update",
                     "--log-file",
                     argv[2],
                     "mirror",
                     "--splash-screen=true",
                     strstr(kind, "fft") ? "--fft" : "--waveform",
                     "--audio-source",
                     strstr(kind, "mic") ? "mic" : "call",
                     NULL};
  if (options_init(signal_fixture ? 9 : 6, options) != ASCIICHAT_OK)
    return 4;
  keyboard_init();
  session_display_config_t config = {.color_mode = TERM_COLOR_TRUECOLOR, .palette_type = PALETTE_STANDARD};
  session_display_ctx_t *display = session_display_create(&config);
  if (!display)
    return 5;
  char answer[128] = {0};
  bool ok = false;
  if (!strcmp(kind, "capture-render")) {
    log_io_t capture = log_io_start();
    const char frame[] = "CAPTURE FRAME";
    ok = ui_controller_present(UI_SCREEN_MEDIA, STDOUT_FILENO, (terminal_size_t){.cols = 20, .rows = 10},
                               frame, sizeof(frame) - 1) == ASCIICHAT_OK;
    ui_controller_redraw();
    platform_sleep_ns(5 * NS_PER_SEC_INT);
    log_io_stop(capture, "capture-render");
  } else if (signal_fixture) {
    float samples[4800];
    for (size_t i = 0; i < 4800; ++i)
      samples[i] = 0.7f * sinf((float)i * 6.2831853f * 440.0f / 48000.0f);
    for (int i = 0; i < 80; ++i) {
      audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_MIC, samples, 4800);
      audio_visualization_submit(AUDIO_VISUALIZATION_SOURCE_REMOTE, samples, 4800);
      session_display_write_ascii(display, "fixture");
      platform_sleep_ns(100 * NS_PER_MS_INT);
    }
    ok = true;
  } else if (!strcmp(kind, "text")) {
    ok = platform_prompt_question("Account name", answer, sizeof(answer), PROMPT_OPTS_DEFAULT) == 0 &&
         !strcmp(answer, "alice");
  } else if (!strcmp(kind, "password")) {
    ok = prompt_password("Server password required - please enter password:", answer, sizeof(answer)) == 0 &&
         !strcmp(answer, "s3cret");
  } else if (!strcmp(kind, "ssh-password")) {
    ok = prompt_password_simple("Encrypted SSH key - enter passphrase", answer, sizeof(answer)) == 0 &&
         !strcmp(answer, "s3cret");
  } else if (!strcmp(kind, "gpg-password")) {
    ok = platform_prompt_question("Enter passphrase for GPG key", answer, sizeof(answer), PROMPT_OPTS_PASSWORD) == 0 &&
         !strcmp(answer, "s3cret");
  } else if (!strcmp(kind, "hidden-password")) {
    prompt_opts_t opts = {.echo = false, .same_line = true, .mask_char = 0};
    ok = platform_prompt_question("Hidden password", answer, sizeof(answer), opts) == 0 && !strcmp(answer, "s3cret");
  } else if (!strcmp(kind, "yes-default") || !strcmp(kind, "no-default")) {
    bool expected = !strcmp(kind, "yes-default");
    ok = platform_prompt_yes_no("Default answer test", expected) == expected;
  } else if (!strcmp(kind, "cancel")) {
    ok = platform_prompt_question("Cancellation test", answer, sizeof(answer), PROMPT_OPTS_INLINE) != 0;
  } else if (!strcmp(kind, "unknown-host")) {
    uint8_t key[32] = {1};
    ok = !prompt_unknown_host("192.0.2.1", 27224, key);
  } else if (!strcmp(kind, "unverified-host")) {
    ok = !prompt_unknown_host_no_identity("192.0.2.1", 27224);
  } else if (!strcmp(kind, "acds-key")) {
    uint8_t old_key[32] = {1}, new_key[32] = {2};
    ok = discovery_keys_verify_change("fixture.invalid", old_key, new_key) != ASCIICHAT_OK;
  } else if (!strcmp(kind, "update-yes") || !strcmp(kind, "update-no")) {
    update_check_result_t update = {.update_available = true,
                                    .latest_version = "v99.0.0",
                                    .current_version = "v1.0.0",
                                    .release_url = "https://example.invalid/releases/test"};
    update_banner_set_result(&update);
    bool expected = !strcmp(kind, "update-yes");
    ok = update_banner_show_prompt(display) == expected;
    if (expected)
      update_banner_print_instructions();
  } else if (!strcmp(kind, "mdns") || !strcmp(kind, "mdns-cancel")) {
    ui_mdns_server_t servers[] = {{.name = "fixture-one", .address = "127.0.0.1", .ipv4 = "127.0.0.1", .port = 27224},
                                  {.name = "fixture-two", .address = "127.0.0.1", .ipv4 = "127.0.0.1", .port = 27225}};
    bool logging = !strcmp(kind, "mdns");
    log_set_terminal_output(logging);
    ok = ui_mdns_select(servers, 2) == (!strcmp(kind, "mdns") ? 1 : -1);
    // Give the presentation thread time to retire the selection screen.
    platform_sleep_ns(100 * NS_PER_MS_INT);
    ok = ok && log_get_terminal_output() == logging;
  } else if (!strcmp(kind, "splash")) {
    splash_intro_start(display);
    platform_sleep_ns(8 * NS_PER_SEC_INT);
    splash_intro_done();
    splash_wait_for_animation();
    ok = true;
  }
  memset(answer, 0, sizeof(answer));
  session_display_destroy(display);
  ui_input_shutdown();
  ui_controller_shutdown();
  keyboard_destroy();
  ui_controller_printf(STDOUT_FILENO, "\nPROBE %s\n", ok ? "PASSED" : "FAILED");
  asciichat_shared_destroy();
  return ok ? 0 : 1;
}
