#pragma once

#include <ascii-chat/asciichat_errno.h>
#include <ascii-chat/platform/terminal.h>
#include <stddef.h>

/** Rendering priority, from background to modal. */
typedef enum {
  UI_SCREEN_STATUS,
  UI_SCREEN_MEDIA,
  UI_SCREEN_SPLASH,
  UI_SCREEN_HELP,
  UI_SCREEN_MDNS,
  UI_SCREEN_UPDATE,
  UI_SCREEN_PROMPT,
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
/** Stop presentation and restore cursor visibility on an interactive terminal. */
void ui_controller_restore_terminal(void);
void ui_controller_redraw(void);
/** Atomic query, safe for interrupt handling. */
bool ui_controller_is_blocked(void);
/** Lock-free query for console logging, including logs from rendering callbacks. */
bool ui_controller_is_presenting(void);
bool ui_controller_is_owner(void);
terminal_size_t ui_controller_size(void);
/** Read-only presentation state. Input policy belongs to ui/input.h. */
typedef struct {
  int screen;
  bool covered;
} ui_presentation_state_t;
ui_presentation_state_t ui_controller_state(void);

/** Final output sink. Live screen writes must originate in a presentation callback.
 * Other terminal text is suppressed while a screen owns the terminal; non-TTY output remains synchronous.
 */
asciichat_error_t ui_controller_write(int fd, const char *data, size_t len);
/** Retire live screens and synchronously render final text before the caller exits. */
void ui_controller_finish(int fd, const char *data, size_t len);

/** Publish an owned copy of an already rendered frame. */
asciichat_error_t ui_controller_present(ui_screen_t screen, int fd, terminal_size_t minimum, const char *data,
                                        size_t len);

asciichat_error_t ui_controller_printf(int fd, const char *format, ...);
