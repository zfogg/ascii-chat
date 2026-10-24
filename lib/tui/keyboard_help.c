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

void keyboard_help_toggle_terminal(int fd) {
  int parent = ui_controller_current_screen() == UI_SCREEN_HELP ? keyboard_help_parent_screen()
                                                             : ui_controller_current_screen();
  ascii_otel_ui_usage(keyboard_help_screen_name(parent), "shortcut", 0);
  if (ui_controller_current_screen() == UI_SCREEN_HELP) {
    // Keep the underlying worker active throughout dismissal. An intermediate
    // empty selection could make it retire its screen after we restore it.
    ui_controller_select_interactive(keyboard_help_previous_interactive());
    ui_controller_remove(UI_SCREEN_HELP);
    return;
  }
  if (parent == UI_SCREEN_ERROR)
    ascii_otel_ui_usage("error", "help_viewed", 0);
  atomic_store(&g_help_previous, ui_controller_interactive_screen());
  atomic_store(&g_help_parent, ui_controller_current_screen());
  terminal_size_t minimum;
  char *text =
      ui_session_context_help_render(ui_controller_size(), (ui_screen_t)keyboard_help_parent_screen(), &minimum);
  if (!text)
    return;
  if (!GET_OPTION(snapshot_mode))
    (void)ui_controller_submit(UI_SCREEN_HELP, fd, minimum, render_help_snapshot, &fd, sizeof(fd));
  SAFE_FREE(text);
}

void keyboard_help_toggle(session_display_ctx_t *ctx) {
  if (ctx)
    keyboard_help_toggle_terminal(STDOUT_FILENO);
}

void keyboard_help_render(session_display_ctx_t *ctx) {
  if (ctx && keyboard_help_is_active(ctx))
    ui_controller_redraw();
}
