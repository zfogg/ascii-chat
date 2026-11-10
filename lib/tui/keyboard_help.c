#include <stdatomic.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include "session/display.h"
#include <string.h>

static atomic_int g_help_parent = -1;
static atomic_int g_help_previous = -1;
int keyboard_help_previous_interactive(void) {
  return atomic_load(&g_help_previous);
}
int keyboard_help_parent_screen(void) {
  return atomic_load(&g_help_parent);
}

const char *keyboard_help_screen_name(int parent) {
  switch (parent) {
  case UI_SCREEN_STATS:
    return "stats_help";
  case UI_SCREEN_SYNC:
    return "sync_help";
  case UI_SCREEN_ERRNO:
    return "errno_help";
  case UI_SCREEN_PROMPT:
    return "media_input_help";
  case UI_SCREEN_MDNS:
    return "mdns_help";
  case UI_SCREEN_UPDATE:
    return "update_help";
  case UI_SCREEN_ERROR:
    return "error_help";
  default:
    return "help";
  }
}

static void render_help_snapshot(terminal_size_t size, const void *data) {
  int fd = *(const int *)data;
  terminal_size_t minimum;
  char *text = ui_session_context_help_render(size, (ui_screen_t)keyboard_help_parent_screen(), &minimum);
  if (text) {
    ui_controller_write(fd, text, strlen(text));
    SAFE_FREE(text);
  }
}

static asciichat_error_t present_help_snapshot(int fd, terminal_size_t minimum, const char *text, size_t length) {
#ifdef PLATFORM_IOS
  (void)text;
  (void)length;
  static const char ios_help[] =
      "\033[2J\033[H"
      "\033[1;38;2;34;211;238mKEYBOARD SHORTCUTS\033[0m\n"
      "\033[38;2;120;120;140m------------------\033[0m\n"
      "\033[38;2;34;211;238m?\033[0m       Open this help\n"
      "\033[38;2;34;211;238mEsc\033[0m     Close the active TUI\n"
      "\033[38;2;34;211;238m0\033[0m       Toggle sync state\n"
      "\033[38;2;34;211;238mF12\033[0m     Open update checker\n"
      "\033[38;2;34;211;238mTab\033[0m     Move selection\n"
      "\033[38;2;34;211;238mUp/Down\033[0m Select row\n"
      "\033[38;2;34;211;238mLeft/Right\033[0m Change page\n"
      "\n"
      "\033[38;2;180;180;200mA hardware keyboard uses the same shortcuts.\033[0m\n";
  terminal_size_t native_minimum = {.cols = 40, .rows = 11};
  return ui_controller_present(UI_SCREEN_HELP, fd, native_minimum, ios_help, sizeof(ios_help) - 1);
#else
  (void)text;
  (void)length;
  return ui_controller_submit(UI_SCREEN_HELP, fd, minimum, render_help_snapshot, &fd, sizeof(fd));
#endif
}

void keyboard_help_toggle_terminal(int fd) {
  int parent =
      ui_controller_current_screen() == UI_SCREEN_HELP ? keyboard_help_parent_screen() : ui_controller_current_screen();
  ascii_otel_ui_usage(keyboard_help_screen_name(parent), "shortcut", 0);
  if (ui_controller_current_screen() == UI_SCREEN_HELP) {
    // Keep the underlying worker active throughout dismissal. An intermediate
    // empty selection could make it retire its screen after we restore it.
    ui_controller_dismiss(UI_SCREEN_HELP, keyboard_help_previous_interactive());
    return;
  }
  if (parent == UI_SCREEN_ERROR)
    ascii_otel_ui_usage("error", "help_viewed", 0);
  atomic_store(&g_help_previous, ui_controller_interactive_screen());
  atomic_store(&g_help_parent, ui_controller_current_screen());
  terminal_size_t render_size = ui_controller_size();
  if (render_size.cols < 80 || render_size.rows < 24) {
    render_size.cols = 80;
    render_size.rows = 24;
  }
  terminal_size_t minimum;
  char *text = ui_session_context_help_render(render_size, (ui_screen_t)keyboard_help_parent_screen(), &minimum);
  if (!text)
    return;
  if (!GET_OPTION(snapshot_mode))
    (void)present_help_snapshot(fd, minimum, text, strlen(text));
  SAFE_FREE(text);
  ui_controller_select_interactive(UI_SCREEN_HELP);
  return;
}

void keyboard_help_toggle(session_display_ctx_t *ctx) {
  if (ctx)
    keyboard_help_toggle_terminal(STDOUT_FILENO);
}

void keyboard_help_render(session_display_ctx_t *ctx) {
  if (ctx && keyboard_help_is_active(ctx))
    ui_controller_redraw();
}
