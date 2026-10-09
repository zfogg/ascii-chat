#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/utf8.h>
#include <string.h>

typedef struct {
  size_t prompt_bytes;
  size_t cursor;
  char text[];
} prompt_snapshot_t;

// The same layout measures the minimum height and renders it. Wrapping keeps
// fingerprints and long questions intact instead of truncating security context.
static int layout(frame_buffer_t *buffer, const char *text, int cols, int row) {
  int col = 1;
  while (*text) {
    int bytes = utf8_next_char_bytes(text, strlen(text));
    if (bytes <= 0)
      bytes = 1;
    int width = utf8_display_width_n(text, (size_t)bytes);
    if (*text == '\n' || col + width > cols) {
      ++row;
      col = 1;
      if (*text == '\n') {
        ++text;
        continue;
      }
    }
    if (buffer) {
      frame_buffer_printf(buffer, "\033[%d;%dH", row, col);
      frame_buffer_append(buffer, text, (size_t)bytes);
    }
    col += width > 0 ? width : 0;
    text += bytes;
  }
  return row;
}

static void render_prompt(terminal_size_t size, const void *data) {
  const prompt_snapshot_t *snapshot = data;
  const char *visible = snapshot->text + snapshot->prompt_bytes;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  frame_buffer_append(buffer, "\033[0m\033[2J\033[H", 11);
  int row = layout(buffer, snapshot->text, size.cols, 1) + 2;
  frame_buffer_printf(buffer, "\033[%d;1H> ", row);
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

asciichat_error_t ui_prompt_present(const char *prompt, const char *visible, size_t cursor) {
  size_t prompt_bytes = strlen(prompt) + 1;
  size_t visible_bytes = strlen(visible) + 1;
  prompt_snapshot_t *snapshot = SAFE_MALLOC(sizeof(*snapshot) + prompt_bytes + visible_bytes, prompt_snapshot_t *);
  if (!snapshot)
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate prompt presentation");
  snapshot->prompt_bytes = prompt_bytes;
  snapshot->cursor = cursor;
  memcpy(snapshot->text, prompt, prompt_bytes);
  memcpy(snapshot->text + prompt_bytes, visible, visible_bytes);
  // Height is measured at the minimum width, so every larger terminal fits.
  terminal_size_t minimum = {.cols = 40, .rows = layout(NULL, prompt, 40, 1) + 3};
  asciichat_error_t result = ui_controller_submit(UI_SCREEN_PROMPT, STDERR_FILENO, minimum, render_prompt, snapshot,
                                                  sizeof(*snapshot) + prompt_bytes + visible_bytes);
  SAFE_FREE(snapshot);
  return result;
}

void ui_prompt_remove(void) {
  ui_controller_remove(UI_SCREEN_PROMPT);
  if (ui_controller_state().screen < 0)
    ui_controller_write(STDERR_FILENO, "\r\n", 2);
}
