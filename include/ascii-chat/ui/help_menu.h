#pragma once
#include <ascii-chat/ui/popup.h>
typedef struct {
  const char *keys; /* NULL denotes a section heading. */
  const char *description;
} ui_help_entry_t;
typedef struct {
  const char *title;
  const ui_help_entry_t *entries;
  size_t count;
  const char *const *settings;
  size_t settings_count;
  const char *footer;
  int top_margin;
} ui_help_menu_t;
terminal_size_t ui_help_menu_measure(const ui_help_menu_t *menu);
bool ui_help_menu_render(frame_buffer_t *output, terminal_size_t size, const ui_help_menu_t *menu);
/** Key tokens are yellow; spaces, slashes, parentheses and pipes stay uncolored. */
void ui_help_keys(char *output, size_t capacity, const char *keys);
