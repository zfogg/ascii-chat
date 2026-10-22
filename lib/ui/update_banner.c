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
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
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
#include <stdatomic.h>
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
static _Atomic bool g_update_overlay = false;
static _Atomic bool g_update_install_failed = false;
static _Atomic int g_update_selected_action = 0;
static _Atomic uint64_t g_update_visible_until_ns = 0;
static void update_banner_open_release(void);

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
  update_check_result_t result = {0};
  return update_banner_copy_result(&result) && result.update_available;
}

static void update_center_line(frame_buffer_t *out, int row, int left, int width, const char *text, log_color_t color) {
  char clipped[1024];
  truncate_with_ellipsis(text, clipped, sizeof(clipped), width);
  int padding = (width - display_width(clipped)) / 2;
  frame_buffer_printf(out, "\033[%d;%dH%s", row, left + padding, colored_string(color, clipped));
}

static void update_border(frame_buffer_t *out, int row, int left, int width, const char *first, const char *horizontal,
                          const char *last, log_color_t color) {
  frame_buffer_printf(out, "\033[%d;%dH%s", row, left, colored_string(color, first));
  int edges = (first[0] ? 1 : 0) + (last[0] ? 1 : 0);
  const char *stroke = colored_string(color, horizontal);
  for (int col = 0; col < width - edges; ++col)
    frame_buffer_append(out, stroke, strlen(stroke));
  frame_buffer_printf(out, "%s", colored_string(color, last));
}

static int update_action_count(bool startup, bool checking, bool available) {
  return startup ? 3 : checking ? 1 : available ? 4 : 2;
}

static bool update_select_key(int key, int count) {
  int direction = key == KEY_LEFT || key == KEY_UP ? -1 : key == KEY_RIGHT || key == KEY_DOWN || key == '\t' ? 1 : 0;
  if (!direction)
    return false;
  int selected = atomic_load(&g_update_selected_action);
  atomic_store(&g_update_selected_action, (selected + direction + count) % count);
  ui_controller_redraw();
  return true;
}

static void render_update_dialog(terminal_size_t size, const update_check_result_t *result, bool startup,
                                 bool checking) {
  bool available = !checking && result->update_available;
  bool unicode = GET_OPTION(force_utf8) == UTF8_SETTING_TRUE ||
                 (GET_OPTION(force_utf8) == UTF8_SETTING_AUTO && terminal_supports_utf8());
  log_color_t cyan = LOG_COLOR_DEBUG;
  log_color_t magenta = LOG_COLOR_FATAL;
  log_color_t muted = LOG_COLOR_GREY;
  log_color_t green = LOG_COLOR_INFO;
  log_color_t yellow = LOG_COLOR_WARN;
  const char *horizontal = unicode ? "─" : "-";
  const char *vertical = unicode ? "│" : "|";
  const char *labels[4] = {0};
  int count = update_action_count(startup, checking, available);
  if (startup) {
    labels[0] = " Enter  Update now ";
    labels[1] = " O  GitHub ";
    labels[2] = " Esc  Continue ";
  } else if (available) {
    labels[0] = " Enter  Update now ";
    labels[1] = " O  GitHub ";
    labels[2] = " Esc / F12 Close ";
    labels[3] = " R  Check again ";
  } else {
    labels[0] = " Esc / F12 Close ";
    labels[1] = " R  Check again ";
  }
  int buttons_width = (count - 1) * 3;
  for (int i = 0; i < count; ++i)
    buttons_width += display_width(labels[i]);
  int width = size.cols - 4;
  int preferred_width = available ? 78 : checking ? 62 : 58;
  if (width > preferred_width)
    width = preferred_width;
  bool stacked = buttons_width > width - 6;
  int height = (available ? 18 : checking ? 15 : 13) + (stacked ? count - 1 : 0);
  bool failed = atomic_load(&g_update_install_failed);
  if (width < 24 || height > size.rows - 2)
    return;
  int left = (size.cols - width) / 2 + 1;
  int top = (size.rows - height) / 2 + 1;
  frame_buffer_t *out = frame_buffer_create(size.rows, size.cols);
  if (!out)
    return;
  frame_buffer_printf(out, startup ? "\033[0m\033[2J\033[H" : "\033[0m");
  update_border(out, top, left, width, unicode ? "┌" : "+", horizontal, unicode ? "┐" : "+", cyan);
  for (int row = 1; row < height - 1; ++row)
    frame_buffer_printf(out, "\033[%d;%dH%s%*s%s", top + row, left, colored_string(cyan, vertical), width - 2, "",
                        colored_string(cyan, vertical));
  update_border(out, top + height - 1, left, width, unicode ? "└" : "+", horizontal, unicode ? "┘" : "+", cyan);
  const char *title = checking                  ? "Check for updates"
                      : available               ? "Update available"
                      : result->check_succeeded ? "You're up to date"
                                                : "Update check unavailable";
  char heading[160];
  snprintf(heading, sizeof(heading), "\033[1m%s", title);
  update_center_line(out, top + (checking ? 1 : 2), left + 2, width - 4, heading,
                     checking                  ? cyan
                     : available               ? yellow
                     : result->check_succeeded ? green
                                               : yellow);
  if (checking) {
    static const int dot_x[] = {3, 5, 6, 5, 3, 1, 0, 1};
    static const int dot_y[] = {0, 0, 1, 2, 2, 2, 1, 0};
    unsigned phase = (unsigned)((time_get_ns() / (100 * NS_PER_MS_INT)) % 8);
    int spinner_left = left + (width - 7) / 2;
    for (unsigned dot = 0; dot < 8; ++dot) {
      bool lit = (phase + 8 - dot) % 8 < 3;
      frame_buffer_printf(out, "\033[%d;%dH%s", top + 3 + dot_y[dot], spinner_left + dot_x[dot],
                          colored_string(lit ? yellow : muted, unicode ? "●"
                                                               : lit   ? "o"
                                                                       : "."));
    }
    update_center_line(out, top + 7, left + 2, width - 4, "Contacting GitHub Releases...", LOG_COLOR_RESET);
    update_center_line(out, top + 9, left + 2, width - 4, "Video chat in your terminal", muted);
  } else if (result->check_succeeded) {
    char installed[160], latest[160], installed_version[128];
    snprintf(installed_version, sizeof(installed_version), "%s%s", result->current_version[0] == 'v' ? "" : "v",
             result->current_version);
    snprintf(installed, sizeof(installed), "Installed  %s",
             colored_string(available ? yellow : green, installed_version));
    snprintf(latest, sizeof(latest), "Latest     %s", colored_string(green, result->latest_version));
    update_center_line(out, top + 4, left + 2, width - 4, installed, LOG_COLOR_RESET);
    update_center_line(out, top + 5, left + 2, width - 4, latest, LOG_COLOR_RESET);
    if (available) {
      update_center_line(out, top + 6, left + 2, width - 4, "A new version of ascii-chat is available.", muted);
      char command[512];
      update_check_get_upgrade_suggestion(update_check_detect_install_method(), result->latest_version, command,
                                          sizeof(command));
      int command_width = display_width(command) + 6;
      if (command_width > width - 6)
        command_width = width - 6;
      int command_left = left + (width - command_width) / 2;
      update_border(out, top + 7, command_left, command_width, unicode ? "┌" : "+", horizontal, unicode ? "┐" : "+",
                    cyan);
      frame_buffer_printf(out, "\033[%d;%dH%s%*s%s", top + 8, command_left, colored_string(cyan, vertical),
                          command_width - 2, "", colored_string(cyan, vertical));
      update_center_line(out, top + 8, command_left + 2, command_width - 4, command, cyan);
      update_border(out, top + 9, command_left, command_width, unicode ? "└" : "+", horizontal, unicode ? "┘" : "+",
                    cyan);
      char instructions[128];
      truncate_with_ellipsis("Full instructions on ascii-chat.com", instructions, sizeof(instructions), width - 4);
      int instructions_left = left + 2 + (width - 4 - display_width(instructions)) / 2;
      char domain_link[128], styled_instructions[256];
      (void)terminal_format_hyperlink(domain_link, sizeof(domain_link), "https://ascii-chat.com", "ascii-chat.com",
                                      !GET_OPTION(strip_ansi));
      char *domain = strstr(instructions, "ascii-chat.com");
      if (domain) {
        *domain = '\0';
        snprintf(styled_instructions, sizeof(styled_instructions), "%s%s", instructions, domain_link);
      } else {
        snprintf(styled_instructions, sizeof(styled_instructions), "%s", instructions);
      }
      frame_buffer_printf(out, "\033[%d;%dH%s", top + 10, instructions_left,
                          colored_string(LOG_COLOR_WHITE, styled_instructions));
      update_center_line(out, top + 11, left + 2, width - 4,
                         "Binaries for every OS with release notes on GitHub:", muted);
      update_center_line(out, top + 12, left + 2, width - 4, result->release_url, cyan);
    } else {
      update_center_line(out, top + 7, left + 2, width - 4, "Checked just now", muted);
    }
  } else {
    update_center_line(out, top + 6, left + 2, width - 4, "No network result is available.", muted);
    update_center_line(out, top + 7, left + 2, width - 4, "Press R to retry.", muted);
  }
  int button_row = top + (available ? 16 : checking ? 13 : 11);
  if (failed) {
    update_center_line(out, button_row - 3, left + 2, width - 4, "Update command failed. See the logs and retry.",
                       yellow);
  }
  update_border(out, button_row - 2, left + 2, width - 4, "", horizontal, "", cyan);
  int selected = atomic_load(&g_update_selected_action) % count;
  int col = left + (width - buttons_width) / 2;
  for (int i = 0; i < count; ++i) {
    int label_width = display_width(labels[i]);
    int button_col = stacked ? left + (width - label_width) / 2 : col;
    char button[96];
    snprintf(button, sizeof(button), "%s%s", i == selected ? "\033[40;7m" : "", labels[i]);
    frame_buffer_printf(out, "\033[%d;%dH%s\033[0m", button_row + (stacked ? i : 0), button_col,
                        colored_string(magenta, button));
    col += label_width + 3;
  }
  ui_controller_write(STDOUT_FILENO, frame_buffer_get_content(out), frame_buffer_get_length(out));
  frame_buffer_destroy(out);
}

static void render_update_prompt(terminal_size_t size, const void *data) {
  render_update_dialog(size, data, true, false);
}

static void render_update_overlay(terminal_size_t size, const void *data) {
  (void)data;
  update_check_result_t result;
  mutex_lock(&g_update_result_mutex);
  result = g_update_result;
  bool checking = g_update_checking;
  mutex_unlock(&g_update_result_mutex);
  render_update_dialog(size, &result, false, checking);
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
  atomic_store(&g_update_selected_action, 0);
  if (ui_controller_submit(UI_SCREEN_UPDATE, STDOUT_FILENO, (terminal_size_t){.cols = 32, .rows = 23},
                           render_update_prompt, &result, sizeof(result)) != ASCIICHAT_OK)
    return false;
  bool update = false;
  while (!shutdown_is_requested()) {
    keyboard_key_t key = ui_input_wait_key(UI_SCREEN_UPDATE, 100);
    if (g_update_overlay)
      return false;
    if (update_select_key(key, 3))
      continue;
    if (key == 'o' || key == 'O' || ((key == '\r' || key == '\n') && atomic_load(&g_update_selected_action) == 1)) {
      update_banner_open_release();
      continue;
    }
    if (key == '\r' || key == '\n') {
      update = atomic_load(&g_update_selected_action) == 0;
      break;
    }
    if (key == 'y' || key == 'Y') {
      update = true;
      break;
    }
    if (key == 'n' || key == 'N' || key == KEY_ESCAPE)
      break;
  }
  if (!g_update_overlay) {
    ui_controller_remove(UI_SCREEN_UPDATE);
  }
  if (!update && !g_update_overlay) {
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

  char domain_link[128], instructions[256];
  (void)terminal_format_hyperlink(domain_link, sizeof(domain_link), "https://ascii-chat.com", "ascii-chat.com",
                                  !GET_OPTION(strip_ansi));
  snprintf(instructions, sizeof(instructions), "Full instructions on %s", domain_link);
  char output[2048];
  int len = snprintf(output, sizeof(output),
                     "\033[0m\033[2J\033[H\033[?25h"
                     "\nUpdated ascii-chat: %s -> %s\n\nRun ascii-chat again to start the new version.\n\n"
                     "Binaries for every OS with release notes on GitHub:\n%s\n\n"
                     "%s\n\n",
                     result.current_version, result.latest_version, result.release_url,
                     colored_string(LOG_COLOR_WHITE, instructions));
  if (len > 0 && len < (int)sizeof(output))
    ui_controller_finish(STDOUT_FILENO, output, (size_t)len);
}

bool update_banner_install_update(void) {
  update_check_result_t result = {0};
  if (!update_banner_copy_result(&result) || !result.update_available) {
    return false;
  }
  g_update_install_failed = false;

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
    // Scoop is a PowerShell script/shim, rather than a native executable.
    const char *argv[] = {"powershell", "-NoProfile", "-Command",
                          "$ErrorActionPreference = 'Stop'; scoop update ascii-chat; if (!$?) { exit 1 }", NULL};
    exit_code = platform_execute_subprocess("powershell", argv, NULL, 0);
  } else if (method == INSTALL_METHOD_ARCH_AUR) {
    const char *helper = update_check_get_aur_helper();
    if (!helper) {
      log_warn("No supported AUR helper is available");
      g_update_install_failed = true;
      ui_controller_redraw();
      return false;
    }
    const char *argv[] = {helper, "-S", "ascii-chat", NULL};
    exit_code = platform_execute_subprocess(helper, argv, NULL, 0);
  } else {
#ifdef _WIN32
    const char *argv[] = {
        "powershell", "-NoProfile", "-ExecutionPolicy",
        "Bypass",     "-Command",   "$ErrorActionPreference = 'Stop'; irm https://ascii-chat.com/install.ps1 | iex",
        NULL};
    exit_code = platform_execute_subprocess("powershell", argv, NULL, 0);
#else
    const char *argv[] = {"bash", "-o", "pipefail", "-c", "curl -fsSL https://ascii-chat.com/install.sh | bash", NULL};
    exit_code = platform_execute_subprocess("bash", argv, NULL, 0);
#endif
  }
  if (exit_code != 0) {
    log_warn("ascii-chat update command exited with status %d", exit_code);
    g_update_install_failed = true;
    ui_controller_redraw();
    return false;
  }
  update_banner_print_instructions();
  shutdown_request();
  APP_CALLBACK_VOID(signal_exit);
  return true;
}

static void *update_check_thread_func(void *arg) {
  bool force = arg != NULL;
  uint64_t visible_until = time_get_ns() + 2 * NS_PER_SEC_INT;
  update_check_result_t result;
  asciichat_error_t err = force ? update_check_perform(&result) : update_check_startup(&result);
  while (!shutdown_is_requested()) {
    uint64_t overlay_until = atomic_load(&g_update_visible_until_ns);
    if (overlay_until > visible_until)
      visible_until = overlay_until;
    if (time_get_ns() >= visible_until)
      break;
    platform_sleep_ms(10);
  }
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
  } else {
    // A failed retry must not advertise an earlier successful result.
    update_check_result_t unavailable = {0};
    update_banner_set_result(&unavailable);
  }
  mutex_lock(&g_update_result_mutex);
  g_update_checking = false;
  g_update_finished = true;
  mutex_unlock(&g_update_result_mutex);
  ui_controller_redraw();
  return NULL;
}

static void update_banner_start_check_impl(bool force) {
  if (g_update_thread_started)
    return;
  if (lifecycle_init_once(&g_update_result_lifecycle)) {
    mutex_init(&g_update_result_mutex, "update_banner_result");
    lifecycle_init_commit(&g_update_result_lifecycle);
  }
  mutex_lock(&g_update_result_mutex);
  g_update_checking = true;
  g_update_finished = false;
  atomic_store(&g_update_selected_action, 0);
  memset(&g_update_result, 0, sizeof(g_update_result));
  g_update_result_set = false;
  mutex_unlock(&g_update_result_mutex);
  if (acthread_create(&g_update_thread, "update_check", update_check_thread_func, force ? (void *)1 : NULL) == 0) {
    g_update_thread_started = true;
  } else {
    mutex_lock(&g_update_result_mutex);
    g_update_checking = false;
    g_update_finished = true;
    mutex_unlock(&g_update_result_mutex);
  }
  ui_controller_redraw();
}

void update_banner_start_check(void) {
  update_banner_start_check_impl(false);
}

void update_banner_toggle_overlay(void) {
  if (g_update_overlay) {
    g_update_overlay = false;
    ui_controller_remove(UI_SCREEN_UPDATE);
    return;
  }
  g_update_overlay = true;
  atomic_store(&g_update_visible_until_ns, time_get_ns() + 2 * NS_PER_SEC_INT);
  atomic_store(&g_update_selected_action, 0);
  bool finished = false;
  if (lifecycle_is_initialized(&g_update_result_lifecycle)) {
    mutex_lock(&g_update_result_mutex);
    finished = g_update_finished;
    mutex_unlock(&g_update_result_mutex);
  }
  if (g_update_thread_started && finished)
    update_banner_wait_for_check();
  if (!g_update_thread_started)
    update_banner_start_check_impl(true);
  const bool snapshot = true;
  if (ui_controller_submit(UI_SCREEN_UPDATE, STDOUT_FILENO, (terminal_size_t){.cols = 32, .rows = 24},
                           render_update_overlay, &snapshot, sizeof(snapshot)) != ASCIICHAT_OK) {
    g_update_overlay = false;
    LOG_ERRNO_IF_SET("Cannot show update checker");
    CLEAR_ERRNO();
  }
}

bool update_banner_is_checking(void) {
  if (!lifecycle_is_initialized(&g_update_result_lifecycle))
    return false;
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
  mutex_lock(&g_update_result_mutex);
  result = g_update_result;
  mutex_unlock(&g_update_result_mutex);
  // The URL can originate in the disk cache. Keep it a single HTTPS argument,
  // including when Windows passes it through cmd's start builtin.
  if (!result.check_succeeded || strncmp(result.release_url, "https://", 8) != 0 || !result.release_url[8] ||
      strpbrk(result.release_url, "\"'&|^<>%!`()\\") != NULL)
    return;
  for (const unsigned char *ch = (const unsigned char *)result.release_url; *ch; ++ch) {
    if (*ch <= ' ' || *ch == 127)
      return;
  }
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
  if (key == KEY_F12) {
    update_banner_toggle_overlay();
    return true;
  }
  if (!g_update_overlay)
    return false;
  bool checking = update_banner_is_checking();
  update_check_result_t result = {0};
  bool available = !checking && update_banner_copy_result(&result) && result.update_available;
  int count = update_action_count(false, checking, available);
  if (update_select_key(key, count))
    return true;
  if (key == '\r' || key == '\n') {
    int selected = atomic_load(&g_update_selected_action) % count;
    if ((available && selected == 2) || (!available && selected == 0)) {
      update_banner_toggle_overlay();
      return true;
    }
    if (checking)
      return true;
    if (available && selected == 0) {
      (void)update_banner_install_update();
      return true;
    }
    if (available && selected == 1) {
      update_banner_open_release();
      return true;
    }
    key = 'r';
  }
  if (key == KEY_ESCAPE) {
    update_banner_toggle_overlay();
    return true;
  }
  if (key == 'o' || key == 'O') {
    if (!update_banner_is_checking())
      update_banner_open_release();
    return true;
  }
  if (key == 'r' || key == 'R') {
    if (update_banner_is_checking())
      return true;
    update_banner_wait_for_check();
    update_banner_start_check_impl(true);
    return true;
  }
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
