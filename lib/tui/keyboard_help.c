#include <stdatomic.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/tui/keyboard_help.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/common.h>
#include <options/options.h>
#include <options/runtime.h>
#include "session/display.h"
#include <string.h>

static atomic_int g_help_parent = -1;
static atomic_int g_help_previous = -1;
static char *g_help_text;
static terminal_size_t g_help_size;
static terminal_size_t g_help_minimum;
static int g_help_cached_parent = -1;
static uint64_t g_help_revision;

static char *keyboard_help_text(terminal_size_t size, terminal_size_t *minimum) {
  int parent = keyboard_help_parent_screen();
  uint64_t revision = options_revision();
  if (!g_help_text || g_help_size.cols != size.cols || g_help_size.rows != size.rows || g_help_cached_parent != parent || g_help_revision != revision) {
    terminal_size_t rendered_minimum;
    char *text = ui_session_context_help_render(size, (ui_screen_t)parent, &rendered_minimum);
    if (!text)
      return NULL;
    SAFE_FREE(g_help_text);
    g_help_text = text;
    g_help_size = size;
    g_help_minimum = rendered_minimum;
    g_help_cached_parent = parent;
    g_help_revision = revision;
  }
  if (minimum)
    *minimum = g_help_minimum;
  return g_help_text;
}

static void render_help_snapshot(terminal_size_t size, const void *data) {
  int fd = *(const int *)data;
  char *text = keyboard_help_text(size, NULL);
  if (text)
    ui_controller_write(fd, text, strlen(text));
}
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

void keyboard_help_toggle_terminal(int fd) {
  int parent = ui_controller_current_screen() == UI_SCREEN_HELP ? keyboard_help_parent_screen() : ui_controller_current_screen();
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
  terminal_size_t minimum;
  char *text = keyboard_help_text(ui_controller_size(), &minimum);
  if (!text)
    return;
  if (!GET_OPTION(snapshot_mode))
    (void)ui_controller_submit(UI_SCREEN_HELP, fd, minimum, render_help_snapshot, &fd, sizeof(fd));
}

void keyboard_help_toggle(session_display_ctx_t *ctx) {
  if (ctx)
    keyboard_help_toggle_terminal(STDOUT_FILENO);
}

void keyboard_help_render(session_display_ctx_t *ctx) {
  if (ctx && keyboard_help_is_active(ctx))
    ui_controller_redraw();
}
