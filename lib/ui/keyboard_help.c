#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/keyboard_help.h>
#include <ascii-chat/ui/session.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/options.h>
#include "session/display.h"
#include <string.h>

static void render_help_snapshot(terminal_size_t size, const void *data) {
  session_display_ctx_t *display = *(session_display_ctx_t *const *)data;
  const char *url = GET_OPTION(media_url), *file = GET_OPTION(media_file);
  terminal_size_t minimum;
  char *text = ui_session_help_render(size, (url && *url) || (file && *file), false, &minimum);
  if (text) {
    session_display_write_raw(display, text, strlen(text));
    SAFE_FREE(text);
  }
}

static void show_help(session_display_ctx_t *ctx) {
  if (!ctx)
    return;
  terminal_size_t size = ui_controller_size(), minimum;
  const char *url = GET_OPTION(media_url), *file = GET_OPTION(media_file);
  char *text = ui_session_help_render(size, (url && *url) || (file && *file), false, &minimum);
  if (!text)
    return;
  if (!GET_OPTION(snapshot_mode))
    (void)ui_controller_submit(UI_SCREEN_HELP, STDOUT_FILENO, minimum, render_help_snapshot, &ctx, sizeof(ctx));
  else
    session_display_write_raw(ctx, text, strlen(text));
  SAFE_FREE(text);
}

void keyboard_help_toggle(session_display_ctx_t *ctx) {
  if (!ctx) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Session display context is NULL");
    return;
  }
  if (keyboard_help_is_active(ctx))
    ui_controller_remove(UI_SCREEN_HELP);
  else
    show_help(ctx);
}

void keyboard_help_render(session_display_ctx_t *ctx) {
  // The retained callback reads settings and geometry on each presentation.
  // Refreshing it must not select help again after input switched to another TUI.
  if (ctx && keyboard_help_is_active(ctx))
    ui_controller_redraw();
}
