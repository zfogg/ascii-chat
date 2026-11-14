#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tui/prompt.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/filesystem.h>
#include <libplatform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
#include <string>

class stdout_capture final {
 public:
  stdout_capture() : saved_fd_(platform_dup(STDOUT_FILENO)), file_(platform_tmpfile()) {
    REQUIRE(saved_fd_ >= 0);
    REQUIRE(file_ != nullptr);
    fflush(stdout);
    REQUIRE(platform_dup2(platform_fileno(file_), STDOUT_FILENO) == ASCIICHAT_OK);
  }

  ~stdout_capture() {
    if (file_ != nullptr)
      fflush(stdout);
    if (saved_fd_ >= 0) {
      platform_dup2(saved_fd_, STDOUT_FILENO);
      platform_close(saved_fd_);
    }
    if (file_ != nullptr)
      fclose(file_);
  }

  std::string read() {
    fflush(stdout);
    REQUIRE(fseek(file_, 0, SEEK_SET) == 0);
    std::string result;
    char buffer[256];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file_)) != 0)
      result.append(buffer, count);
    return result;
  }

 private:
  int saved_fd_;
  FILE *file_;
};

TEST_CASE("controller_batch/utf8_chunk_trim_does_not_read_past_chunk", "[controller_batch]") {
  char data[32769];
  memset(data, 'A', sizeof(data));
  // This byte belongs to the next chunk and must not affect the boundary.
  data[32768] = (char)0x80;
  ASCIICHAT_REQUIRE_EQ(ui_controller_trim_utf8_chunk(data, 32768), 32768);
}

static void render_snapshot(terminal_size_t size, const void *snapshot) {
  ASCIICHAT_REQUIRE(ui_controller_is_owner());
  ASCIICHAT_REQUIRE_EQ(size.cols, 32);
  ASCIICHAT_REQUIRE_EQ(size.rows, 12);
  ASCIICHAT_REQUIRE_EQ(ui_controller_size().cols, 32);
  ASCIICHAT_REQUIRE_EQ(ui_controller_printf(-1, "%s %d\n", (const char *)snapshot, size.rows), ASCIICHAT_OK);
}

TEST_CASE("controller_batch/redirected_output_is_synchronous_and_restores_ownership", "[controller_batch]") {
  options_t options = *options_get();
  options.width = 32;
  options.height = 12;
  options_state_set(&options);
  char path[1024];
  int fd;
  ASCIICHAT_REQUIRE_EQ(platform_create_temp_file(path, sizeof(path), "ascii-batch", &fd), 0);
  terminal_size_t minimum = {.cols = 8, .rows = 4};
  const char snapshot[] = "rendered";
  ASCIICHAT_REQUIRE_NOT(ui_controller_is_owner());
  ASCIICHAT_REQUIRE_EQ(ui_controller_submit(UI_SCREEN_STATUS, fd, minimum, render_snapshot, snapshot, sizeof(snapshot)),
               ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(ui_controller_is_owner());
  ASCIICHAT_REQUIRE_NOT(ui_controller_is_presenting());
  ASCIICHAT_REQUIRE_EQ(ui_controller_write(fd, "direct\n", 7), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(ui_controller_present(UI_SCREEN_STATUS, fd, minimum, "frame\n", 6), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(ui_controller_present_retained(UI_SCREEN_STATUS, fd, minimum, "retained\n", 9), ASCIICHAT_OK);
  ui_controller_finish(fd, "done\n", 5);
  platform_close(fd);
  FILE *stream = platform_fopen("batch-test", path, "r");
  ASCIICHAT_REQUIRE_NOT_NULL(stream);
  char output[1024] = {0};
  size_t length = fread(output, 1, sizeof(output) - 1, stream);
  ASCIICHAT_REQUIRE_GT(length, 0);
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(output, "rendered 12\n"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(output, "direct\n"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(output, "frame\n"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(output, "retained\n"));
  ASCIICHAT_REQUIRE_NOT_NULL(strstr(output, "done\n"));
  fclose(stream);
  ASCIICHAT_REQUIRE_EQ(platform_delete_temp_file(path), 0);
}

TEST_CASE("controller_batch/invalid_snapshots_and_frames_are_rejected_before_rendering", "[controller_batch]") {
  terminal_size_t minimum = {.cols = 8, .rows = 4};
  const char snapshot[] = "unused";
  ASCIICHAT_REQUIRE_EQ(ui_controller_submit(UI_SCREEN_COUNT, -1, minimum, render_snapshot, snapshot, sizeof(snapshot)),
               ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_controller_submit(UI_SCREEN_STATUS, -1, minimum, NULL, snapshot, sizeof(snapshot)),
               ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_controller_submit(UI_SCREEN_STATUS, -1, minimum, render_snapshot, NULL, sizeof(snapshot)),
               ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_controller_submit(UI_SCREEN_STATUS, -1, minimum, render_snapshot, snapshot, 0), ERROR_INVALID_PARAM);
  minimum.cols = 0;
  ASCIICHAT_REQUIRE_EQ(ui_controller_submit(UI_SCREEN_STATUS, -1, minimum, render_snapshot, snapshot, sizeof(snapshot)),
               ERROR_INVALID_PARAM);
  minimum.cols = 8;
  ASCIICHAT_REQUIRE_EQ(ui_controller_present(UI_SCREEN_STATUS, -1, minimum, NULL, 1), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_controller_present(UI_SCREEN_STATUS, -1, minimum, snapshot, 0), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_controller_present(UI_SCREEN_STATUS, -1, minimum, snapshot, UI_CONTROLLER_MAX_FRAME_BYTES + 1),
               ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_controller_write(-1, NULL, 1), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(ui_controller_write(-1, snapshot, 0), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(ui_controller_is_owner());
}

TEST_CASE("controller_batch/prompt_dismissal_terminates_redirected_output", "[controller_batch]") {
  stdout_capture capture;
  ASCIICHAT_REQUIRE_NOT(ui_controller_is_presenting());
  ui_prompt_dismiss(-1);
  ui_prompt_remove();
  ASCIICHAT_REQUIRE_EQ(capture.read(), std::string("\r\n\r\n"));
}

static void dismiss_over_sync(terminal_size_t size, const void *snapshot) {
  (void)size;
  (void)snapshot;
  ASCIICHAT_REQUIRE(ui_controller_is_owner());
  ASCIICHAT_REQUIRE_EQ(ui_controller_current_screen(), UI_SCREEN_SYNC);
  ASCIICHAT_REQUIRE_EQ(ui_controller_state().screen, -1);
  ui_prompt_dismiss(UI_SCREEN_SYNC);
}

TEST_CASE("controller_batch/sync_mailbox_dismissal_does_not_emit_batch_newline", "[controller_batch]") {
  stdout_capture capture;
  ui_controller_select_interactive(UI_SCREEN_SYNC);
  const bool snapshot = true;
  asciichat_error_t result =
      ui_controller_submit(UI_SCREEN_STATUS, STDOUT_FILENO, (terminal_size_t){.cols = 8, .rows = 4}, dismiss_over_sync,
                           &snapshot, sizeof(snapshot));
  ui_controller_select_interactive(-1);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(ui_controller_write(STDOUT_FILENO, "sentinel", 8), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(capture.read(), std::string("sentinel"));
}
