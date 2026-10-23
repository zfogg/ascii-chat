#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/log.h>
#include <string.h>
#include <ascii-chat/util/password.h>
#include <ascii-chat/tui/errno_stacks.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/tui/update_banner.h>
#include <ascii-chat/tui/render_progress.h>
#include "../../../src/common/session/display.h"

#define REQUIRE(condition)                                                                                             \
  do {                                                                                                                 \
    if (!(condition))                                                                                                  \
      return SET_ERRNO(ERROR_INVALID_STATE, "Terminal contract failed: %s", #condition);                               \
  } while (0)
static asciichat_error_t probe(const char *mode) {
  if (strcmp(mode, "size") == 0) {
    terminal_size_t size = {0};
    REQUIRE(platform_isatty(0));
    REQUIRE(platform_ttyname(0) != NULL);
    REQUIRE(is_valid_tty_path(platform_ttyname(0)));
    REQUIRE(!is_valid_tty_path("/tmp/not-a-terminal"));
    REQUIRE(terminal_get_size_fd(0, &size) == ASCIICHAT_OK);
    REQUIRE(size.cols == 91 && size.rows == 37);
    size = (terminal_size_t){0};
    REQUIRE(terminal_get_size(&size) == ASCIICHAT_OK);
    REQUIRE(size.cols == 91 && size.rows == 37);
    unsigned short width = 0, height = 0;
    REQUIRE(get_terminal_size(&width, &height) == ASCIICHAT_OK);
    REQUIRE(width == 91 && height == 37);
    REQUIRE(terminal_get_size_fd(0, NULL) == ERROR_INVALID_PARAM);
    REQUIRE(terminal_get_size_fd(-1, &size) == ERROR_TERMINAL);
    REQUIRE(size.cols == 0 && size.rows == 0);
  } else if (strcmp(mode, "controls") == 0) {
    REQUIRE(terminal_clear_screen() == ASCIICHAT_OK);
    REQUIRE(terminal_move_cursor(2, 4) == ASCIICHAT_OK);
    REQUIRE(terminal_move_cursor_relative(3) == ASCIICHAT_OK);
    REQUIRE(terminal_move_cursor_relative(-2) == ASCIICHAT_OK);
    REQUIRE(terminal_move_cursor_relative(0) == ASCIICHAT_OK);
    REQUIRE(terminal_cursor_hide() == ASCIICHAT_OK);
    REQUIRE(terminal_cursor_show() == ASCIICHAT_OK);
    REQUIRE(terminal_cursor_home(1) == ASCIICHAT_OK);
    REQUIRE(terminal_clear_scrollback(1) == ASCIICHAT_OK);
    REQUIRE(terminal_reset(1) == ASCIICHAT_OK);
    REQUIRE(terminal_flush(1) == ASCIICHAT_OK);
  } else if (strcmp(mode, "raw") == 0) {
    REQUIRE(terminal_set_raw_mode(true) == ASCIICHAT_OK);
    REQUIRE(terminal_set_echo(false) == ASCIICHAT_OK);
    REQUIRE(platform_write_all(1, "READY", 5) == 5);
    char acknowledgement = 0;
    REQUIRE(platform_read(0, &acknowledgement, 1) == 1 && acknowledgement == 'x');
    REQUIRE(terminal_set_raw_mode(false) == ASCIICHAT_OK);
    REQUIRE(terminal_set_echo(true) == ASCIICHAT_OK);
    REQUIRE(platform_write_all(1, "RESTORED", 8) == 8);
    REQUIRE(platform_read(0, &acknowledgement, 1) == 1 && acknowledgement == 'y');
  } else if (strcmp(mode, "password") == 0 || strcmp(mode, "password-cancel") == 0 ||
             strcmp(mode, "password-box") == 0) {
    char password[32];
    int result = strcmp(mode, "password-box") == 0
                     ? prompt_password("Test passphrase", password, sizeof(password))
                     : prompt_password_simple("Test passphrase", password, sizeof(password));
    if (strcmp(mode, "password-cancel") != 0) {
      REQUIRE(result == 0);
      REQUIRE(strcmp(password, "sécret") == 0);
    } else {
      REQUIRE(result == -1);
    }
    REQUIRE(prompt_password(NULL, password, sizeof(password)) == -1);
    REQUIRE(prompt_password("Prompt", NULL, sizeof(password)) == -1);
    REQUIRE(prompt_password("Prompt", password, 1) == -1);
    REQUIRE(prompt_password_simple(NULL, password, sizeof(password)) == -1);
    REQUIRE(prompt_password_simple("Prompt", NULL, sizeof(password)) == -1);
    REQUIRE(prompt_password_simple("Prompt", password, 1) == -1);
    ui_input_shutdown();
    ui_controller_shutdown();
    keyboard_destroy();
  } else if (strncmp(mode, "update-", 7) == 0) {
    REQUIRE(!update_banner_has_update());
    update_banner_set_result(NULL);
    update_check_result_t ignored = {0};
    update_banner_set_result(&ignored);
    REQUIRE(!update_banner_has_update());
    REQUIRE(!update_banner_show_prompt(NULL));
    update_banner_wait_for_check();
    REQUIRE(options_set_bool("strip_ansi", true) == ASCIICHAT_OK);
    update_check_result_t result = {.update_available = true,
                                    .current_version = "v7.1.0",
                                    .latest_version = "v8.2.0",
                                    .current_sha = "1234567890abcdef",
                                    .release_url = "https://example.invalid/releases/v8.2.0"};
    update_banner_set_result(&result);
    REQUIRE(update_banner_has_update());
    memset(&result, 0, sizeof(result));
    update_banner_set_result(&result);
    REQUIRE(update_banner_has_update());
    session_display_config_t config = {0};
    session_display_ctx_t *display = session_display_create(&config);
    REQUIRE(display != NULL);
    REQUIRE(keyboard_init() == ASCIICHAT_OK);
    bool wants_update = update_banner_show_prompt(display);
    bool expected = strcmp(mode, "update-y") == 0 || strcmp(mode, "update-enter") == 0;
    REQUIRE(wants_update == expected);
    if (wants_update)
      update_banner_print_instructions();
    session_display_destroy(display);
    ui_input_shutdown();
    ui_controller_shutdown();
    keyboard_destroy();
  } else if (strncmp(mode, "progress-", 9) == 0) {
    REQUIRE(render_progress_create(false, "unused") == NULL);
    render_progress_frame(NULL, NULL, false);
    render_progress_begin(NULL, 0, false);
    render_progress_finalize(NULL);
    render_progress_destroy(NULL);
    REQUIRE(terminal_set_raw_mode(true) == ASCIICHAT_OK);
    render_progress_t *progress = render_progress_create(true, "/tmp/output\033[2J\n.mp4");
    REQUIRE(progress != NULL);
    char preview[] = "FIRST-PREVIEW\nsecond-line";
    render_progress_frame(progress, preview, true);
    memset(preview, 'Z', sizeof(preview) - 1);
    render_progress_begin(progress, 3, strcmp(mode, "progress-known") == 0);
    char acknowledgement = 0;
    REQUIRE(platform_read(0, &acknowledgement, 1) == 1 && acknowledgement == 'f');
    render_progress_finalize(progress);
    REQUIRE(platform_read(0, &acknowledgement, 1) == 1 && acknowledgement == 'e');
    render_progress_frame(progress, "FAILED-PREVIEW", false);
    REQUIRE(platform_read(0, &acknowledgement, 1) == 1 && acknowledgement == 'o');
    size_t oversized_length = 1024 * 1024 + 1;
    char *oversized = SAFE_MALLOC(oversized_length + 1, char *);
    memset(oversized, 'X', oversized_length);
    oversized[oversized_length] = '\0';
    render_progress_frame(progress, oversized, true);
    SAFE_FREE(oversized);
    REQUIRE(platform_read(0, &acknowledgement, 1) == 1 && acknowledgement == 'q');
    render_progress_destroy(progress);
    ui_controller_shutdown();
    REQUIRE(terminal_set_raw_mode(false) == ASCIICHAT_OK);
  } else if (strcmp(mode, "errno-ui") == 0) {
#ifdef ASCIICHAT_DIAGNOSTIC_UI
    REQUIRE(options_set_bool("strip_ansi", true) == ASCIICHAT_OK);
    REQUIRE(keyboard_init() == ASCIICHAT_OK);
    CLEAR_ERRNO_ALL();
    (void)SET_ERRNO(ERROR_NETWORK, "ERR-ALPHA");
    (void)SET_ERRNO(ERROR_NETWORK, "ERR-BRAVO");
    (void)SET_ERRNO(ERROR_NETWORK, "ERR-CHARLIE");
    ui_errno_stacks_toggle();
    REQUIRE(ui_errno_stacks_visible());
    for (;;) {
      char key = 0;
      REQUIRE(platform_read(0, &key, 1) == 1);
      if (key == '\033') {
        REQUIRE(ui_errno_stacks_handle_key(KEY_ESCAPE));
        break;
      }
      REQUIRE(ui_errno_stacks_handle_key((keyboard_key_t)key));
    }
    REQUIRE(!ui_errno_stacks_visible());
    REQUIRE(!ui_errno_stacks_handle_key('j'));
    asciichat_error_context_t frames[8];
    size_t count = errno_snapshot(thread_current_id(), frames, 8);
    REQUIRE(count == 2);
    for (size_t i = 0; i < count; ++i)
      REQUIRE(strcmp(frames[i].context_message, "ERR-BRAVO") != 0);
    ui_errno_stacks_hide();
    ui_controller_shutdown();
    CLEAR_ERRNO_ALL();
    keyboard_destroy();
#else
    REQUIRE(platform_write_all(1, "DIAGNOSTIC_DISABLED", 19) == 19);
#endif
  } else if (strcmp(mode, "dark") == 0 || strcmp(mode, "light") == 0) {
    REQUIRE(terminal_has_dark_background() == (strcmp(mode, "dark") == 0));
  } else if (strcmp(mode, "background") == 0 || strcmp(mode, "bad-background") == 0) {
    uint8_t r = 17, g = 34, b = 51;
    bool result = terminal_query_background_color(&r, &g, &b);
    if (strcmp(mode, "background") == 0) {
      REQUIRE(result);
      REQUIRE(r == 0x12 && g == 0x34 && b == 0x56);
    } else {
      REQUIRE(!result);
      REQUIRE(r == 17 && g == 34 && b == 51);
    }
  } else
    return SET_ERRNO(ERROR_INVALID_PARAM, "Unknown terminal probe");
  return ASCIICHAT_OK;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  log_init(NULL, LOG_FATAL, false, false);
  log_set_terminal_output(false);
  if (options_state_init() != ASCIICHAT_OK)
    return 2;
  asciichat_error_t result = probe(argv[1]);
  if (result != ASCIICHAT_OK)
    log_error("%s", asciichat_error_format(NULL, result));
  options_state_destroy();
  log_destroy();
  return result == ASCIICHAT_OK ? 0 : 1;
}
