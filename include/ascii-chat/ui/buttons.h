#pragma once
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/keyboard.h>
typedef struct {
  const char *label;
  keyboard_key_t shortcut;
  bool disabled;
  log_color_t color; /**< Unselected foreground; zero retains the default button color. */
} ui_button_t;
typedef struct {
  size_t selected;
} ui_buttons_t;
/** Returns the activated index or -1. Disabled buttons cannot activate. */
int ui_buttons_key(ui_buttons_t *state, const ui_button_t *buttons, size_t count, keyboard_key_t key);
void ui_buttons_render(frame_buffer_t *out, int row, int left, int width, const ui_button_t *buttons, size_t count,
                       size_t selected);
