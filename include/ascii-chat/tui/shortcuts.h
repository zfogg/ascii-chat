#pragma once
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/ui/help_menu.h>
typedef enum {
  TUI_SHORTCUT_NONE,
  TUI_SHORTCUT_HELP,
  TUI_SHORTCUT_LOGS,
  TUI_SHORTCUT_STATS,
  TUI_SHORTCUT_SYNC,
  TUI_SHORTCUT_ERRORS,
  TUI_SHORTCUT_UPDATE,
  TUI_SHORTCUT_MEDIA
} tui_shortcut_t;
tui_shortcut_t tui_shortcut_action(keyboard_key_t key);
/** Shared definitions used by both dispatch and mode help. */
size_t tui_shortcut_help(ui_help_entry_t *entries, size_t capacity, bool server, bool browser);
