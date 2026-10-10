#include <ascii-chat/ui/notice.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/utf8.h>
#include <string.h>

typedef struct {
  size_t prompt_bytes;
  size_t cursor;
  ui_notice_severity_t severity;
  bool color;
  bool unicode;
  char text[];
} prompt_snapshot_t;

static void render_prompt(terminal_size_t size, const void *data) {
  const prompt_snapshot_t *snapshot = data;
  const char *visible = snapshot->text + snapshot->prompt_bytes;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  frame_buffer_append(buffer, "\033[0m\033[H", 7);
  int row =
      ui_notice_render(buffer, snapshot->text, snapshot->severity, size.cols, snapshot->unicode, snapshot->color) + 2;
  frame_buffer_printf(buffer, "\033[%d;1H\033[2K> ", row);
  // Keep the editable line within the terminal and scroll with the cursor.
  size_t start = snapshot->cursor > (size_t)(size.cols - 4) ? snapshot->cursor - (size.cols - 4) : 0;
  while (start > 0 && ((unsigned char)visible[start] & 0xc0) == 0x80)
    --start;
  const char *end = visible + start;
  int width = 0;
  while (*end) {
    int bytes = utf8_next_char_bytes(end, strlen(end));
    if (bytes <= 0)
      break;
    int next_width = utf8_display_width_n(end, (size_t)bytes);
    if (width + next_width > size.cols - 3)
      break;
    frame_buffer_append(buffer, end, (size_t)bytes);
    width += next_width;
    end += bytes;
  }
  int cursor_col = 3 + utf8_display_width_n(visible + start, snapshot->cursor - start);
  frame_buffer_printf(buffer, "\033[%d;%dH\033[?25h", row, cursor_col);
  frame_buffer_flush(buffer);
  frame_buffer_destroy(buffer);
}

static int prompt_fd(void) {
  return platform_isatty(STDERR_FILENO) ? STDERR_FILENO : STDOUT_FILENO;
}

asciichat_error_t ui_prompt_present(const char *prompt, const char *visible, size_t cursor) {
  size_t prompt_bytes = strlen(prompt) + 1;
  size_t visible_bytes = strlen(visible) + 1;
  prompt_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot) + prompt_bytes + visible_bytes, prompt_snapshot_t *);
  if (!snapshot)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate prompt presentation");
  snapshot->prompt_bytes = prompt_bytes;
  snapshot->cursor = cursor;
  snapshot->severity = ui_notice_prompt_severity();
  snapshot->color = !GET_OPTION(strip_ansi) && terminal_should_color_output(prompt_fd());
  snapshot->unicode = terminal_supports_utf8();
  memcpy(snapshot->text, prompt, prompt_bytes);
  memcpy(snapshot->text + prompt_bytes, visible, visible_bytes);
  // Height is measured at the minimum width, so every larger terminal fits.
  terminal_size_t minimum = {.cols = 40,
                             .rows = ui_notice_render(NULL, prompt, snapshot->severity, 40, false, false) + 3};
  asciichat_error_t result = ui_controller_submit(UI_SCREEN_PROMPT, prompt_fd(), minimum, render_prompt, snapshot,
                                                  sizeof(*snapshot) + prompt_bytes + visible_bytes);
  SAFE_FREE(snapshot);
  return result;
}

void ui_prompt_remove(void) {
  ui_controller_remove(UI_SCREEN_PROMPT);
  if (ui_controller_state().screen < 0)
    ui_controller_write(prompt_fd(), "\r\n", 2);
}

int ui_prompt_required_rows(const void *data, int cols) {
  const prompt_snapshot_t *snapshot = data;
  return ui_notice_render(NULL, snapshot->text, snapshot->severity, cols, false, false) + 3;
}
