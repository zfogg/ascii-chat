#pragma once
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/platform/keyboard.h>
typedef struct {
  const char *label;
  keyboard_key_t shortcut;
  bool disabled;
  log_color_t color; /**< Unselected foreground; zero retains the default button color. */
/**
 */
} ui_button_t;
/**
 * @brief When code needs to work with ui button, it keeps `label`, `shortcut`, `disabled`, `color` here instead of scattering it across globals. Initialization establishes the handles and sizes; teardown must release any allocation, lock, thread, or callback context recorded in those fields.
 */
typedef struct {
  size_t selected;
/**
 * @brief ui buttons is intentionally a small, concrete record. Fields such as `selected` give callers the identity and current value they need. It is normally borrowed from, or owned by, the surrounding subsystem—not independently managed.
 */
} ui_buttons_t;
/** Returns the activated index or -1. Disabled buttons cannot activate. */
int ui_buttons_key(ui_buttons_t *state, const ui_button_t *buttons, size_t count, keyboard_key_t key);
void ui_buttons_render(frame_buffer_t *out, int row, int left, int width, const ui_button_t *buttons, size_t count,
                       size_t selected);
