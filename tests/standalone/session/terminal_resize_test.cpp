#include <catch2/catch_test_macros.hpp>
/* Exercise resize polling against a private Windows console screen buffer. */
#include <libplatform/terminal.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/common.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/options/rcu.h>
#include <libplatform/system.h>

static void render(terminal_size_t size, const void *snapshot) {
  (void)size;
  (void)snapshot;
}
static void await_dimensions(int width, int height) {
  for (int i = 0; i < 200; ++i) {
    if (GET_OPTION(width) == width && GET_OPTION(height) == height)
      return;
    platform_sleep_ms(10);
  }
  REQUIRE(GET_OPTION(width) == width && GET_OPTION(height) == height);
}
TEST_CASE("terminal resize watcher", "[standalone][platform][serial]") {
  log_init(NULL, LOG_WARN, false, false);
  HANDLE buffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                            CONSOLE_TEXTMODE_BUFFER, NULL);
  REQUIRE(buffer != INVALID_HANDLE_VALUE);
  int fd = _open_osfhandle((intptr_t)buffer, _O_RDWR);
  REQUIRE(fd >= 0);
  SMALL_RECT first = {0, 0, 39, 14};
  REQUIRE(SetConsoleWindowInfo(buffer, TRUE, &first));
  terminal_resize_watcher_t watcher = {0}, independent = {0};
  bool changed = false;
  REQUIRE(terminal_resize_poll(&watcher, fd, &changed) == ASCIICHAT_OK && changed);
  REQUIRE(watcher.size.cols == 40 && watcher.size.rows == 15);
  REQUIRE(terminal_resize_poll(&watcher, fd, &changed) == ASCIICHAT_OK && !changed);
  SMALL_RECT second = {0, 0, 49, 19};
  REQUIRE(SetConsoleWindowInfo(buffer, TRUE, &second));
  REQUIRE(terminal_resize_poll(&watcher, fd, &changed) == ASCIICHAT_OK && changed);
  REQUIRE(watcher.size.cols == 50 && watcher.size.rows == 20);
  REQUIRE(terminal_resize_poll(&independent, fd, &changed) == ASCIICHAT_OK && changed);
  REQUIRE(terminal_resize_poll(&watcher, fd, &changed) == ASCIICHAT_OK && !changed);
  int pipe_fds[2];
  REQUIRE(_pipe(pipe_fds, 256, _O_BINARY) == 0);
  REQUIRE(terminal_resize_poll(&watcher, pipe_fds[0], &changed) != ASCIICHAT_OK && !changed);
  REQUIRE(watcher.size.cols == 50 && watcher.size.rows == 20);
  REQUIRE(terminal_resize_poll(NULL, fd, &changed) == ERROR_INVALID_PARAM && !changed);
  REQUIRE(terminal_resize_poll(&watcher, fd, NULL) == ERROR_INVALID_PARAM);
  _close(pipe_fds[0]);
  _close(pipe_fds[1]);
  REQUIRE(options_state_init() == ASCIICHAT_OK);
  REQUIRE(options_set_bool("auto_width", true) == ASCIICHAT_OK);
  REQUIRE(options_set_bool("auto_height", true) == ASCIICHAT_OK);
  REQUIRE(ui_controller_submit(UI_SCREEN_MEDIA, fd, (terminal_size_t){.cols = 1, .rows = 1}, render, "frame", 6) ==
        ASCIICHAT_OK);
  await_dimensions(50, 20);
  REQUIRE(SetConsoleWindowInfo(buffer, TRUE, &first));
  await_dimensions(40, 15);
  REQUIRE(options_set_bool("auto_width", false) == ASCIICHAT_OK);
  REQUIRE(SetConsoleWindowInfo(buffer, TRUE, &second));
  await_dimensions(40, 20);
  ui_controller_remove(UI_SCREEN_MEDIA);
  ui_controller_shutdown();
  options_state_destroy();
  _close(fd);
  log_destroy();
}
