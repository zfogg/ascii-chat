#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/terminal.h>
#include <ascii-chat/platform/keyboard.h>
#include <stddef.h>

/** Screen priority, from background to modal. Only the active screen receives input. */
typedef enum {
  UI_SCREEN_STATUS,
  UI_SCREEN_MEDIA,
  UI_SCREEN_SPLASH,
  UI_SCREEN_HELP,
  UI_SCREEN_MDNS,
  UI_SCREEN_UPDATE,
  UI_SCREEN_COUNT
} ui_screen_t;

typedef void (*ui_render_fn)(terminal_size_t size, const void *snapshot);

/** Copies snapshot before returning. Interactive callbacks run on the presentation thread; batch output renders
 * synchronously. Pointer members must remain valid until ui_controller_remove() returns. Callbacks may remove
 * themselves, but must not submit screens or take a lock held by their producer.
 */
asciichat_error_t ui_controller_submit(ui_screen_t screen, int fd, terminal_size_t minimum, ui_render_fn render,
                                       const void *snapshot, size_t bytes);
void ui_controller_remove(ui_screen_t screen);
void ui_controller_shutdown(void);
void ui_controller_redraw(void);
/** Atomic query, safe for interrupt handling. */
bool ui_controller_is_blocked(void);
bool ui_controller_is_owner(void);
terminal_size_t ui_controller_size(void);
keyboard_key_t ui_controller_read_key(ui_screen_t screen);
keyboard_key_t ui_controller_wait_key(ui_screen_t screen, unsigned timeout_ms);

/** Publish an owned copy of an already rendered frame. */
asciichat_error_t ui_controller_present(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data,
                                        size_t len);
