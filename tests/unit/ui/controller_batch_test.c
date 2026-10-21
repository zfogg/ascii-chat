#include <criterion/criterion.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/rcu.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(controller_batch);

static void render_snapshot(terminal_size_t size, const void *snapshot) {
  cr_assert(ui_controller_is_owner());
  cr_assert_eq(size.cols, 32);
  cr_assert_eq(size.rows, 12);
  cr_assert_eq(ui_controller_size().cols, 32);
  cr_assert_eq(ui_controller_printf(-1, "%s %d\n", (const char *)snapshot, size.rows), ASCIICHAT_OK);
}

Test(controller_batch, redirected_output_is_synchronous_and_restores_ownership) {
  options_t options = *options_get();
  options.width = 32;
  options.height = 12;
  options_state_set(&options);
  char path[1024];
  int fd;
  cr_assert_eq(platform_create_temp_file(path, sizeof(path), "ascii-batch", &fd), 0);
  terminal_size_t minimum = {.cols = 8, .rows = 4};
  const char snapshot[] = "rendered";
  cr_assert_not(ui_controller_is_owner());
  cr_assert_eq(ui_controller_submit(UI_SCREEN_STATUS, fd, minimum, render_snapshot, snapshot, sizeof(snapshot)),
               ASCIICHAT_OK);
  cr_assert_not(ui_controller_is_owner());
  cr_assert_not(ui_controller_is_presenting());
  cr_assert_eq(ui_controller_write(fd, "direct\n", 7), ASCIICHAT_OK);
  cr_assert_eq(ui_controller_present(UI_SCREEN_STATUS, fd, minimum, "frame\n", 6), ASCIICHAT_OK);
  cr_assert_eq(ui_controller_present_retained(UI_SCREEN_STATUS, fd, minimum, "retained\n", 9), ASCIICHAT_OK);
  ui_controller_finish(fd, "done\n", 5);
  platform_close(fd);
  FILE *stream = platform_fopen("batch-test", path, "r");
  cr_assert_not_null(stream);
  char output[1024] = {0};
  size_t length = fread(output, 1, sizeof(output) - 1, stream);
  cr_assert_gt(length, 0);
  cr_assert_not_null(strstr(output, "rendered 12\n"));
  cr_assert_not_null(strstr(output, "direct\n"));
  cr_assert_not_null(strstr(output, "frame\n"));
  cr_assert_not_null(strstr(output, "retained\n"));
  cr_assert_not_null(strstr(output, "done\n"));
  fclose(stream);
  cr_assert_eq(platform_delete_temp_file(path), 0);
}

Test(controller_batch, invalid_snapshots_and_frames_are_rejected_before_rendering) {
  terminal_size_t minimum = {.cols = 8, .rows = 4};
  const char snapshot[] = "unused";
  cr_assert_eq(ui_controller_submit(UI_SCREEN_COUNT, -1, minimum, render_snapshot, snapshot, sizeof(snapshot)),
               ERROR_INVALID_PARAM);
  cr_assert_eq(ui_controller_submit(UI_SCREEN_STATUS, -1, minimum, NULL, snapshot, sizeof(snapshot)),
               ERROR_INVALID_PARAM);
  cr_assert_eq(ui_controller_submit(UI_SCREEN_STATUS, -1, minimum, render_snapshot, NULL, sizeof(snapshot)),
               ERROR_INVALID_PARAM);
  cr_assert_eq(ui_controller_submit(UI_SCREEN_STATUS, -1, minimum, render_snapshot, snapshot, 0), ERROR_INVALID_PARAM);
  minimum.cols = 0;
  cr_assert_eq(ui_controller_submit(UI_SCREEN_STATUS, -1, minimum, render_snapshot, snapshot, sizeof(snapshot)),
               ERROR_INVALID_PARAM);
  minimum.cols = 8;
  cr_assert_eq(ui_controller_present(UI_SCREEN_STATUS, -1, minimum, NULL, 1), ERROR_INVALID_PARAM);
  cr_assert_eq(ui_controller_present(UI_SCREEN_STATUS, -1, minimum, snapshot, 0), ERROR_INVALID_PARAM);
  cr_assert_eq(ui_controller_present(UI_SCREEN_STATUS, -1, minimum, snapshot, UI_CONTROLLER_MAX_FRAME_BYTES + 1),
               ERROR_INVALID_PARAM);
  cr_assert_eq(ui_controller_write(-1, NULL, 1), ASCIICHAT_OK);
  cr_assert_eq(ui_controller_write(-1, snapshot, 0), ASCIICHAT_OK);
  cr_assert_not(ui_controller_is_owner());
}
