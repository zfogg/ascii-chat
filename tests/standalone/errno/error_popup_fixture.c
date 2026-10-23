#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/common.h>
#include <ascii-chat/debug/named.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/input.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/tui/prompt.h>
#include <ascii-chat/tui/error_popup.h>
#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/tui/errno_stacks.h>
#include <ascii-chat/tui/update_banner.h>
#include <ascii-chat/ui/popup.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/util/string.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct {
  atomic_int request, completed;
  atomic_bool stop;
} worker_t;

static void *produce_errors(void *opaque) {
  worker_t *state = opaque;
  NAMED_REGISTER_ID(thread_current_id(), "popup-media-worker", "thread", "0x%tx", NULL);
  while (!atomic_load(&state->stop)) {
    int request = atomic_load(&state->request);
    if (request != atomic_load(&state->completed)) {
      if (request == 1)
        SET_ERRNO(ERROR_MEDIA_OPEN, "FIRST_ERROR: Cannot open /videos/example.mp4");
      else if (request == 2)
        SET_ERRNO(ERROR_NETWORK_TIMEOUT, "SECOND_ERROR: Timed out connecting to localhost:27224");
      else if (request == 3)
        SET_ERRNO(ERROR_CRYPTO_AUTH, "THIRD_ERROR: The peer rejected authentication");
      else if (request == 4) {
        char message[2048];
        size_t used = 0;
        for (int i = 0; i < 30; ++i)
          used += safe_snprintf(message + used, sizeof(message) - used,
                                "LONG_ERROR line %02d: Unicode caf\xc3\xa9; /a/very/long/media/path.mp4\n", i);
        SET_ERRNO(ERROR_MEDIA_DECODE, "%s", message);
      } else if (request == 5) {
        SET_ERRNO(ERROR_GENERAL, "CONTROL_ERROR: untrusted \033[2J payload");
      } else if (request == 6) {
        bool previous = errno_notifications_suppress(true);
        SET_ERRNO(ERROR_MEMORY, "SUPPRESSED_RENDER_ERROR");
        errno_notifications_suppress(previous);
      } else if (request == 7) {
        for (int i = 0; i < 140; ++i)
          SET_ERRNO(ERROR_NETWORK, "BURST_ERROR_%03d", i);
      } else {
        SET_ERRNO(ERROR_AUDIO, "NEXT_BATCH_ERROR_%d", request);
      }
      atomic_store(&state->completed, request);
    }
    platform_sleep_ms(5);
  }
  NAMED_UNREGISTER_ID(thread_current_id());
  return NULL;
}

#define CHECK(expression)                                                                                              \
  do {                                                                                                                 \
    if (!(expression)) {                                                                                               \
      log_error("Notification contract failed at line %d: %s", __LINE__, #expression);                                 \
      return 1;                                                                                                        \
    }                                                                                                                  \
  } while (0)

static int journal_contract(void) {
  asciichat_error_context_t entries[ASCIICHAT_ERRNO_HISTORY_SIZE];
  uint64_t dropped;
  SET_ERRNO(ERROR_MEDIA_OPEN, "Before TUI");
  CHECK(!errno_notifications_pending());
  errno_notifications_enable(true);
  SET_ERRNO(ERROR_NETWORK_TIMEOUT, "First batch");
  CHECK(errno_notifications_snapshot(entries, ARRAY_SIZE(entries), &dropped) == 1);
  uint64_t cutoff = entries[0].error_id;
  // This models a producer publishing between the UI snapshot and acknowledgment.
  SET_ERRNO(ERROR_CRYPTO_AUTH, "Concurrent arrival");
  errno_notifications_dismiss(cutoff);
  CHECK(errno_notifications_snapshot(entries, ARRAY_SIZE(entries), &dropped) == 1);
  CHECK(entries[0].code == ERROR_CRYPTO_AUTH);
  CHECK(HAS_ERRNO_CODE(ERROR_NETWORK_TIMEOUT));
  errno_notifications_dismiss(entries[0].error_id);
  CHECK(!errno_notifications_pending());
  CHECK(HAS_ERRNO_CODE(ERROR_CRYPTO_AUTH));
  SET_ERRNO(ERROR_AUDIO, "Cleared by caller");
  CLEAR_ERRNO();
  CHECK(errno_notifications_snapshot(entries, ARRAY_SIZE(entries), &dropped) == 1);
  CHECK(entries[0].code == ERROR_AUDIO);
  errno_notifications_enable(false);
  CHECK(!errno_notifications_pending());
  SET_ERRNO(ERROR_GENERAL, "After TUI");
  CHECK(!errno_notifications_pending());
  CLEAR_ERRNO_ALL();
  return 0;
}

static void background(ui_screen_t screen) {
  const char *label = screen == UI_SCREEN_STATUS   ? "STATUS_BACKGROUND"
                      : screen == UI_SCREEN_SPLASH ? "SPLASH_BACKGROUND"
                                                   : "MEDIA_BACKGROUND";
  char text[512];
  int length = safe_snprintf(text, sizeof(text), "\033[H\033[2J%s\nProduction controller PTY fixture\n", label);
  (void)ui_controller_present(screen, STDOUT_FILENO, (terminal_size_t){20, 5}, text, (size_t)length);
}

static void dialog(terminal_size_t size, const void *unused) {
  (void)unused;
  frame_buffer_t *out = frame_buffer_create(size.rows, size.cols);
  if (!out)
    return;
  ui_popup_frame(out, 3, 3, size.cols - 6, size.rows - 5, LOG_COLOR_RESET);
  frame_buffer_printf(out, "\033[4;5HUPDATE_DIALOG_BACKGROUND");
  ui_controller_write(STDOUT_FILENO, frame_buffer_get_content(out), frame_buffer_get_length(out));
  frame_buffer_destroy(out);
}

int main(int argc, char **argv) {
  if (asciichat_shared_init(NULL, false, true) != ASCIICHAT_OK)
    return 2;
  if (argc == 2 && !strcmp(argv[1], "--contract")) {
    int result = journal_contract();
    asciichat_shared_destroy();
    return result;
  }
  if (argc < 2)
    return 2;
  const char *control_path = argv[1];
  char *options[24] = {"popup-fixture"};
  size_t option_count = 1;
  for (int i = 2; i < argc && option_count + 3 < ARRAY_SIZE(options); ++i)
    options[option_count++] = argv[i];
  options[option_count++] = "mirror";
  options[option_count++] = "--audio=false";
  options[option_count++] = "--splash-screen=false";
  if (options_init((int)option_count, options) != ASCIICHAT_OK || keyboard_init() != ASCIICHAT_OK) {
    log_set_terminal_output(true);
    LOG_ERRNO_IF_SET("Cannot initialize popup fixture");
    return 3;
  }
  char log_path[2048];
  safe_snprintf(log_path, sizeof(log_path), "%s.log", control_path);
  log_init(log_path, LOG_DEBUG, false, false);
  worker_t state = {0};
  asciichat_thread_t worker;
  if (acthread_create(&worker, "popup-media-worker", produce_errors, &state) != ASCIICHAT_OK)
    return 4;
  if (stats_runtime_start("mirror", 0) != ASCIICHAT_OK)
    return 5;
  background(UI_SCREEN_MEDIA);
  unsigned last_sequence = 0;
  bool stop = false;
  while (!stop) {
    char command[128] = {0}, action[32] = {0};
    unsigned sequence = 0;
    int number = 0;
    int fd = platform_open("popup-test-control", control_path, PLATFORM_O_RDONLY);
    if (fd >= 0) {
      ssize_t bytes = platform_read(fd, command, sizeof(command) - 1);
      platform_close(fd);
      if (bytes > 0 && sscanf(command, "%u %31s %d", &sequence, action, &number) >= 2 && sequence != last_sequence) {
        last_sequence = sequence;
        if (!strcmp(action, "error"))
          atomic_store(&state.request, number);
        else if (!strcmp(action, "status")) {
          ui_controller_remove(UI_SCREEN_MEDIA);
          ui_controller_remove(UI_SCREEN_SPLASH);
          background(UI_SCREEN_STATUS);
        } else if (!strcmp(action, "splash")) {
          ui_controller_remove(UI_SCREEN_MEDIA);
          ui_controller_remove(UI_SCREEN_STATUS);
          background(UI_SCREEN_SPLASH);
        } else if (!strcmp(action, "prompt")) {
          (void)ui_input_media_begin("/saved/prompt.mp4");
        } else if (!strcmp(action, "stats")) {
          stats_runtime_toggle();
        } else if (!strcmp(action, "logs")) {
          ui_controller_toggle_logs();
        } else if (!strcmp(action, "errors")) {
          ui_errno_stacks_toggle();
        } else if (!strcmp(action, "help")) {
          keyboard_help_toggle_terminal(ui_controller_active_fd());
        } else if (!strcmp(action, "dialog")) {
          (void)ui_controller_submit(UI_SCREEN_UPDATE, STDOUT_FILENO, (terminal_size_t){40, 10}, dialog, &number,
                                     sizeof(number));
        } else if (!strcmp(action, "stop")) {
          stop = true;
        }
      }
    }
    keyboard_key_t key = ui_input_read_key(UI_SCREEN_MEDIA);
    if (ui_controller_current_screen() == UI_SCREEN_LOGS)
      (void)ui_session_logs_key(key);
    if (key == 'q' || key == 3)
      stop = true;
    platform_sleep_ms(10);
  }
  atomic_store(&state.stop, true);
  thread_join(&worker, NULL);
  stats_runtime_stop();
  ui_controller_shutdown();
  keyboard_destroy();
  asciichat_shared_destroy();
  return 0;
}
