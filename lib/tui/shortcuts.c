#include <ascii-chat/tui/shortcuts.h>
#include <ascii-chat/asciichat_errno.h>
typedef struct {
  keyboard_key_t keys[3];
  tui_shortcut_t action;
  const char *label, *description;
  bool native, media;
} binding_t;
static const binding_t bindings[] = {
    {{KEY_QUESTION}, TUI_SHORTCUT_HELP, "?", "Toggle this help screen TUI", false, false},
    {{']'}, TUI_SHORTCUT_LOGS, "]", "Live log viewer/searcher TUI", false, false},
    {{'='}, TUI_SHORTCUT_STATS, "=", "Running app statistics TUI", true, false},
#ifndef NDEBUG
    {{KEY_0}, TUI_SHORTCUT_SYNC, "0", "C code sync primitives TUI", true, false},
#endif
#if defined(ASCIICHAT_DIAGNOSTIC_UI)
    {{KEY_BACKTICK}, TUI_SHORTCUT_ERRORS, "`", "Error stacks diagnostic TUI", true, false},
#endif
    {{KEY_F12}, TUI_SHORTCUT_UPDATE, "F12", "Version update checker TUI", true, false},
    {{'o', 'O', 15}, TUI_SHORTCUT_MEDIA, "o / Ctrl-O", "TUI to open media from path or URL", true, true},
};
tui_shortcut_t tui_shortcut_action(keyboard_key_t key) {
  if (key == KEY_NONE)
    return TUI_SHORTCUT_NONE;
  for (size_t i = 0; i < sizeof(bindings) / sizeof(*bindings); ++i)
    for (size_t alias = 0; alias < 3; ++alias)
      if (bindings[i].keys[alias] == key)
        return bindings[i].action;
  return TUI_SHORTCUT_NONE;
}
size_t tui_shortcut_help(ui_help_entry_t *entries, size_t capacity, bool server, bool browser) {
  if (!entries || !capacity) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Help binding output is required");
    return 0;
  }
  size_t count = 0;
  for (size_t i = 0; i < sizeof(bindings) / sizeof(*bindings); ++i) {
    const binding_t *b = &bindings[i];
    if ((server && b->media) || (browser && b->native))
      continue;
    if (count >= capacity) {
      SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Help binding output is too small");
      return 0;
    }
    entries[count++] = (ui_help_entry_t){.keys = b->label, .description = b->description};
  }
  return count;
}
