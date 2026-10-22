#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/log/log.h>
#include <string.h>
#include <ascii-chat/util/password.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/platform/keyboard.h>

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
