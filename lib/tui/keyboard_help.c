#include <stdatomic.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include "session/display.h"
#include <string.h>

static atomic_int g_help_parent = -1;
int keyboard_help_parent_screen(void) {
  return atomic_load(&g_help_parent);
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
  if (ui_controller_current_screen() == UI_SCREEN_HELP) {
    ui_controller_remove(UI_SCREEN_HELP);
    ui_controller_select_interactive(keyboard_help_parent_screen() >= UI_SCREEN_LOGS ? keyboard_help_parent_screen()
                                                                                     : -1);
    return;
  }
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
