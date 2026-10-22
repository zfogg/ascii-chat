/**
 * @file ui/update_banner.c
 * @brief Update available prompt screen implementation
 * @ingroup session
 *
 * Renders a centered box-drawing prompt after splash ends, showing version
 * info and upgrade instructions. Blocks for user input (Y/Enter or N/Esc).
 */

#include <ascii-chat/ui/input.h>
#include <ascii-chat/common/shutdown.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/ui/overlay_box.h>
#include <ascii-chat/ui/update_banner.h>
#include <ascii-chat/ui/splash.h>
#include "session/display.h"
#include <ascii-chat/common.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/platform/thread.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/util/display.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/util/lifecycle.h>

#include <stdio.h>
#include <string.h>

// Thread-safe storage for the update check result
static update_check_result_t g_update_result;
static mutex_t g_update_result_mutex;
static bool g_update_result_set = false;
static lifecycle_t g_update_result_lifecycle = LIFECYCLE_INIT_MUTEX(&g_update_result_mutex);

// Background update check thread
static asciichat_thread_t g_update_thread;
static bool g_update_thread_started = false;
static bool g_update_checking = false;
static bool g_update_finished = false;
static bool g_update_overlay = false;

static bool update_banner_copy_result(update_check_result_t *result) {
  if (!result || !lifecycle_is_initialized(&g_update_result_lifecycle)) {
    return false;
  }
  mutex_lock(&g_update_result_mutex);
  bool set = g_update_result_set;
  if (set) {
    *result = g_update_result;
  }
  mutex_unlock(&g_update_result_mutex);
  return set;
}

void update_banner_set_result(const update_check_result_t *result) {
  if (!result) {
    return;
  }

  if (lifecycle_init_once(&g_update_result_lifecycle)) {
    mutex_init(&g_update_result_mutex, "update_banner_result");
    lifecycle_init_commit(&g_update_result_lifecycle);
  }

  mutex_lock(&g_update_result_mutex);
  memcpy(&g_update_result, result, sizeof(g_update_result));
  g_update_result_set = true;
  mutex_unlock(&g_update_result_mutex);
}

bool update_banner_has_update(void) {
  if (!g_update_result_set) {
    return false;
  }

  if (lifecycle_init_once(&g_update_result_lifecycle)) {
    mutex_init(&g_update_result_mutex, "update_banner_result");
    lifecycle_init_commit(&g_update_result_lifecycle);
  }

  mutex_lock(&g_update_result_mutex);
  bool available = g_update_result_set && g_update_result.update_available;
  mutex_unlock(&g_update_result_mutex);
  return available;
}

// Build a box line: "║  <content><padding>║"
// Content may contain ANSI escape sequences — truncate_with_ellipsis handles them.
static void build_box_line(char *output, size_t output_size, const char *content, int box_width) {
  if (!output || output_size < 256 || !content || box_width < 6) {
    return;
  }

  int content_available = box_width - 4; // "║  " + "║"
  if (content_available < 1) {
    content_available = 1;
  }

  char truncated[512];
  truncate_with_ellipsis(content, truncated, sizeof(truncated), content_available);

  int content_width = display_width(truncated);
  int padding = content_available - content_width;
  if (padding < 0) {
    padding = 0;
  }

  char *pos = output;
  int remaining = (int)output_size;

  int n = snprintf(pos, remaining, "║  %s", truncated);
  if (n > 0) {
    pos += n;
    remaining -= n;
  }

  for (int i = 0; i < padding && remaining > 1; i++) {
    *pos++ = ' ';
    remaining--;
  }

  if (remaining > 3) {
    snprintf(pos, remaining, "║");
  }
}

// Append a positioned box line to the buffer
static void append_line(char *buffer, size_t *buf_pos, size_t buf_size, int start_row, int *current_row, int start_col,
                        int box_width, const char *content) {
  if (!buffer || *buf_pos >= buf_size || !content) {
    return;
  }

  char line_buf[256];
  int written =
      snprintf(buffer + *buf_pos, buf_size - *buf_pos, "\033[%d;%dH", start_row + *current_row, start_col + 1);
  if (written > 0) {
    *buf_pos += (size_t)written;
  }

  build_box_line(line_buf, sizeof(line_buf), content, box_width);
  written = snprintf(buffer + *buf_pos, buf_size - *buf_pos, "%s", line_buf);
  if (written > 0) {
    *buf_pos += (size_t)written;
  }

  (*current_row)++;
}

// Build a horizontal border line (top: ╔═╗, middle: ╠═╣, bottom: ╚═╝)
static void append_border(char *buffer, size_t *buf_pos, size_t buf_size, int start_row, int *current_row,
                          int start_col, int box_width, const char *left, const char *right) {
  if (!buffer || *buf_pos >= buf_size) {
    return;
  }

  int written =
      snprintf(buffer + *buf_pos, buf_size - *buf_pos, "\033[%d;%dH", start_row + *current_row, start_col + 1);
  if (written > 0) {
    *buf_pos += (size_t)written;
  }

  char border[256];
  int bpos = 0;
  int r = snprintf(border + bpos, sizeof(border) - bpos, "%s", left);
  if (r > 0) {
    bpos += r;
  }
  for (int i = 1; i < box_width - 1; i++) {
    r = snprintf(border + bpos, sizeof(border) - bpos, "═");
    if (r > 0) {
      bpos += r;
    }
  }
  snprintf(border + bpos, sizeof(border) - bpos, "%s", right);

  written = snprintf(buffer + *buf_pos, buf_size - *buf_pos, "%s", border);
  if (written > 0) {
    *buf_pos += (size_t)written;
  }

  (*current_row)++;
}

static void render_update_prompt(terminal_size_t size, const void *data) {
  update_check_result_t result = *(const update_check_result_t *)data;

  // Get upgrade suggestion
  install_method_t method = update_check_detect_install_method();
  char suggestion[512];
  update_check_get_upgrade_suggestion(method, result.latest_version, suggestion, sizeof(suggestion));

  // Terminal dimensions
  int term_width = size.cols;
  int term_height = size.rows;

  // Box sizing
  int box_width = 52;
  if (box_width > term_width - 2) {
    box_width = term_width - 2;
  }
  if (box_width < 30) {
    box_width = 30;
  }

  int box_height = 16;
  int start_col = (term_width - box_width) / 2;
  if (start_col < 0) {
    start_col = 0;
  }
  int start_row = (term_height - box_height) / 2;
  if (start_row < 0) {
    start_row = 0;
  }

  // Build the screen
  const size_t BUF_SIZE = 4096;
  char *buffer = SAFE_MALLOC(BUF_SIZE, char *);
  size_t buf_pos = 0;

#define APPEND(fmt, ...)                                                                                               \
  do {                                                                                                                 \
    int _w = snprintf(buffer + buf_pos, BUF_SIZE - buf_pos, fmt, ##__VA_ARGS__);                                       \
    if (_w > 0) {                                                                                                      \
      buf_pos += (size_t)_w;                                                                                           \
    }                                                                                                                  \
  } while (0)

  // Clear screen
  APPEND("\033[2J\033[H");

  int row = 1;

  // Top border
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╔", "╗");

  // Title (bold yellow)
  {
    char title_content[256];
    snprintf(title_content, sizeof(title_content), "\033[1m%sUpdate Available\033[0m", log_level_color(LOG_COLOR_WARN));
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, title_content);
  }

  // Separator
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╠", "╣");

  // Blank line
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "");

  // Current version
  {
    char line[256];
    snprintf(line, sizeof(line), "Current : %s (%.8s)", result.current_version, result.current_sha);
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Latest version (green)
  {
    char line[256];
    snprintf(line, sizeof(line), "Latest  : %s%s\033[0m", log_level_color(LOG_COLOR_INFO), result.latest_version);
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Exact release source on GitHub.
  {
    char line[512];
    snprintf(line, sizeof(line), "Release : %s", result.release_url);
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Blank line
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "");

  // Separator
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╠", "╣");

  // Upgrade instructions
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "To upgrade:");
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "");

  // Upgrade command (bold white)
  {
    char line[256];
    snprintf(line, sizeof(line), "  \033[1m%s\033[0m", suggestion);
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Blank line
  append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "");

  // Separator
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╠", "╣");

  // Prompt (grey)
  {
    char line[256];
    snprintf(line, sizeof(line), "%s[Y] update [N] continue\033[0m", log_level_color(LOG_COLOR_GREY));
    append_line(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, line);
  }

  // Bottom border
  append_border(buffer, &buf_pos, BUF_SIZE, start_row, &row, start_col, box_width, "╚", "╝");

#undef APPEND

  // Write to terminal
  ui_controller_write(STDOUT_FILENO, buffer, buf_pos);
  terminal_flush(STDOUT_FILENO);
  SAFE_FREE(buffer);
}

static void render_update_overlay(terminal_size_t size, const void *data) {
  (void)data;
  update_check_result_t result = {0};
  bool checking;
  mutex_lock(&g_update_result_mutex);
  result = g_update_result;
  checking = g_update_checking;
  mutex_unlock(&g_update_result_mutex);
  const char *title = checking ? "Check for updates" : result.update_available ? "Update available" :
                    result.check_succeeded ? "You're up to date" : "Update check unavailable";
  char command[512] = {0};
  if (result.update_available)
    update_check_get_upgrade_suggestion(update_check_detect_install_method(), result.latest_version, command, sizeof(command));
  char installed[128] = {0}, latest[128] = {0}, checking_line[64] = {0};
  const char *lines[5] = {0};
  size_t line_count = 0;
  if (checking) {
    static const char spin[] = "|/-\\";
    snprintf(checking_line, sizeof(checking_line), "%c  Checking for updates...",
             spin[(time_get_ns() / (150 * NS_PER_MS_INT)) % 4]);
    lines[line_count++] = checking_line;
  } else if (result.check_succeeded) {
    snprintf(installed, sizeof(installed), "Installed  %s", result.current_version);
    snprintf(latest, sizeof(latest), "Latest     %s", result.latest_version);
    lines[line_count++] = installed;
    lines[line_count++] = latest;
    if (result.update_available) {
      lines[line_count++] = command;
      lines[line_count++] = "Binaries for every OS with release notes in the GitHub release:";
      lines[line_count++] = result.release_url;
    } else {
      lines[line_count++] = "Checked just now";
    }
  } else {
    lines[line_count++] = "No network result is available. Press R to retry.";
  }
  const char *footer = result.update_available ? "Enter update now  O open GitHub  R retry  F12/Esc close"
                                                : "R retry  F12/Esc close";
  frame_buffer_t *out = frame_buffer_create((size_t)size.rows, (size_t)size.cols);
  if (!out)
    return;
  ui_overlay_box_t box = {.title = title, .lines = lines, .line_count = line_count, .footer = footer, .minimum_width = 66};
  (void)ui_overlay_box_render(out, size, &box);
  ui_controller_write(STDOUT_FILENO, frame_buffer_get_content(out), frame_buffer_get_length(out));
  frame_buffer_destroy(out);
}

bool update_banner_show_prompt(session_display_ctx_t *ctx) {
  if (!ctx || !terminal_can_prompt_user())
    return false;
  update_check_result_t result;
  mutex_lock(&g_update_result_mutex);
  result = g_update_result;
  mutex_unlock(&g_update_result_mutex);
  if (update_check_is_dismissed(&result)) {
    return false;
  }
  if (ui_controller_submit(UI_SCREEN_UPDATE, STDOUT_FILENO, (terminal_size_t){.cols = 32, .rows = 16},
                           render_update_prompt, &result, sizeof(result)) != ASCIICHAT_OK)
    return false;
  bool update = false;
  while (!shutdown_is_requested()) {
    keyboard_key_t key = ui_input_wait_key(UI_SCREEN_UPDATE, 100);
    if (key == 'y' || key == 'Y' || key == '\r' || key == '\n') {
      update = true;
      break;
    }
    if (key == 'n' || key == 'N' || key == KEY_ESCAPE)
      break;
  }
  ui_controller_remove(UI_SCREEN_UPDATE);
  if (!update) {
    update_check_dismiss_for_a_week(&result);
    update_banner_set_result(&result);
  }
  return update;
}

void update_banner_print_instructions(void) {
  mutex_lock(&g_update_result_mutex);
  update_check_result_t result;
  memcpy(&result, &g_update_result, sizeof(result));
  mutex_unlock(&g_update_result_mutex);

  char output[2048];
  int len =
      snprintf(output, sizeof(output),
               "\033[0m\033[2J\033[H\033[?25h"
               "\nUpdated ascii-chat: %s -> %s\n\nRun ascii-chat again to start the new version.\n\n"
               "Binaries for every OS with release notes in the GitHub release:\n%s\n\n",
               result.current_version, result.latest_version, result.release_url);
  if (len > 0 && len < (int)sizeof(output))
    ui_controller_finish(STDOUT_FILENO, output, (size_t)len);
}

bool update_banner_install_update(void) {
  update_check_result_t result = {0};
  if (!update_banner_copy_result(&result) || !result.update_available) {
    return false;
  }

  install_method_t method = update_check_detect_install_method();
  int exit_code = -1;
#ifndef NDEBUG
  const char *test_command = SAFE_GETENV("ASCII_CHAT_TEST_UPDATE_COMMAND");
  if (test_command && test_command[0]) {
#ifdef _WIN32
    const char *argv[] = {"cmd", "/d", "/c", test_command, NULL};
    exit_code = platform_execute_subprocess("cmd", argv, NULL, 0);
#else
    const char *argv[] = {"/bin/sh", "-c", test_command, NULL};
    exit_code = platform_execute_subprocess("/bin/sh", argv, NULL, 0);
#endif
  } else
#endif
  if (method == INSTALL_METHOD_HOMEBREW) {
    const char *argv[] = {"brew", "upgrade", "ascii-chat", NULL};
    exit_code = platform_execute_subprocess("brew", argv, NULL, 0);
  } else if (method == INSTALL_METHOD_SCOOP) {
    const char *argv[] = {"scoop", "update", "ascii-chat", NULL};
    exit_code = platform_execute_subprocess("scoop", argv, NULL, 0);
  } else if (method == INSTALL_METHOD_ARCH_AUR) {
    const char *argv[] = {"paru", "-S", "ascii-chat", NULL};
    exit_code = platform_execute_subprocess("paru", argv, NULL, 0);
  } else {
#ifdef _WIN32
    const char *argv[] = {"powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                          "irm https://ascii-chat.com/install.ps1 | iex", NULL};
    exit_code = platform_execute_subprocess("powershell", argv, NULL, 0);
#else
    const char *argv[] = {"/bin/sh", "-c", "curl -fsSL https://ascii-chat.com/install.sh | bash", NULL};
    exit_code = platform_execute_subprocess("/bin/sh", argv, NULL, 0);
#endif
  }
  if (exit_code != 0) {
    log_warn("ascii-chat update command exited with status %d", exit_code);
    return false;
  }
  update_banner_print_instructions();
  shutdown_request();
  return true;
}

static void *update_check_thread_func(void *arg) {
  bool force = arg != NULL;
  update_check_result_t result;
  asciichat_error_t err = force ? update_check_perform(&result) : update_check_startup(&result);
  if (err == ASCIICHAT_OK) {
    if (result.update_available) {
      char notification[1024];
      update_check_format_notification(&result, notification, sizeof(notification));
      log_info("%s", notification);
      splash_set_update_notification(notification);
    } else {
      log_info("Already on latest version: %s", result.current_version);
    }

    // Store every terminal state for the reusable update overlay.
    update_banner_set_result(&result);
  }
  mutex_lock(&g_update_result_mutex);
  g_update_checking = false;
  g_update_finished = true;
  mutex_unlock(&g_update_result_mutex);
  ui_controller_redraw();
  return NULL;
}

static void update_banner_start_check_impl(bool force) {
  if (g_update_thread_started) return;
  if (lifecycle_init_once(&g_update_result_lifecycle)) { mutex_init(&g_update_result_mutex, "update_banner_result"); lifecycle_init_commit(&g_update_result_lifecycle); }
  mutex_lock(&g_update_result_mutex); g_update_checking = true; mutex_unlock(&g_update_result_mutex);
  mutex_lock(&g_update_result_mutex);
  g_update_finished = false;
  mutex_unlock(&g_update_result_mutex);
  if (acthread_create(&g_update_thread, "update_check", update_check_thread_func, force ? (void *)1 : NULL) == 0) {
    g_update_thread_started = true;
  }
}

void update_banner_start_check(void) {
  update_banner_start_check_impl(false);
}

void update_banner_toggle_overlay(void) {
  if (g_update_overlay) { g_update_overlay = false; ui_controller_remove(UI_SCREEN_UPDATE); return; }
  g_update_overlay = true;
  bool finished = false;
  if (lifecycle_is_initialized(&g_update_result_lifecycle)) {
    mutex_lock(&g_update_result_mutex);
    finished = g_update_finished;
    mutex_unlock(&g_update_result_mutex);
  }
  if (g_update_thread_started && finished)
    update_banner_wait_for_check();
  if (!g_update_thread_started) update_banner_start_check_impl(true);
  const bool snapshot = true;
  if (ui_controller_submit(UI_SCREEN_UPDATE, STDOUT_FILENO, (terminal_size_t){.cols = 40, .rows = 15},
                           render_update_overlay, &snapshot, sizeof(snapshot)) != ASCIICHAT_OK) {
    g_update_overlay = false;
    LOG_ERRNO_IF_SET("Cannot show update checker");
    CLEAR_ERRNO();
  }
}

bool update_banner_is_checking(void) {
  if (!lifecycle_is_initialized(&g_update_result_lifecycle)) return false;
  mutex_lock(&g_update_result_mutex);
  bool checking = g_update_checking;
  mutex_unlock(&g_update_result_mutex);
  return checking;
}

bool update_banner_is_overlay_open(void) {
  return g_update_overlay;
}

static void update_banner_open_release(void) {
  update_check_result_t result;
  mutex_lock(&g_update_result_mutex); result = g_update_result; mutex_unlock(&g_update_result_mutex);
#ifdef _WIN32
  const char *argv[] = {"cmd", "/c", "start", "", result.release_url, NULL};
  (void)platform_execute_subprocess("cmd", argv, NULL, 0);
#elif defined(__APPLE__)
  const char *argv[] = {"open", result.release_url, NULL};
  (void)platform_execute_subprocess("open", argv, NULL, 0);
#else
  const char *argv[] = {"xdg-open", result.release_url, NULL};
  (void)platform_execute_subprocess("xdg-open", argv, NULL, 0);
#endif
}

bool update_banner_handle_key(int key) {
  if (key == KEY_F12) { update_banner_toggle_overlay(); return true; }
  if (!g_update_overlay) return false;
  if (key == KEY_ESCAPE) { update_banner_toggle_overlay(); return true; }
  if (key == 'o' || key == 'O') { update_banner_open_release(); return true; }
  if (key == 'r' || key == 'R') { update_banner_wait_for_check(); update_banner_start_check_impl(true); return true; }
  if (key == '\r' || key == '\n') { (void)update_banner_install_update(); return true; }
  return g_update_overlay;
}

void update_banner_wait_for_check(void) {
  if (!g_update_thread_started) {
    return;
  }
  int ret = thread_join_timeout(&g_update_thread, NULL, 5LL * NS_PER_SEC_INT);
  if (ret != 0) {
    // Timeout — thread is still alive. Do a blocking join to avoid orphaning it.
    // The thread has bounded runtime (DNS + HTTP both have timeouts) so this won't hang.
    log_warn("Update check thread did not finish within 5s, waiting for completion");
    thread_join(&g_update_thread, NULL);
  }
  g_update_thread_started = false;
}
