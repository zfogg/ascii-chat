#include <ascii-chat/ui/notice.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/ui/prompt.h>
#include <ascii-chat/ui/controller.h>
#include <ascii-chat/ui/frame_buffer.h>
#include <ascii-chat/common.h>
#include <ascii-chat/util/utf8.h>
#include <string.h>
#include <ascii-chat/ui/invitation.h>
#include <ascii-chat/util/time.h>

typedef struct {
  size_t prompt_bytes;
  size_t cursor;
  ui_notice_severity_t severity;
  bool color;
  bool unicode;
  char text[];
} prompt_snapshot_t;

static void media_center_line(frame_buffer_t *buffer, const char *text, int row, int cols) {
  int width = utf8_display_width(text);
  int col = (cols - width) / 2 + 1;
  frame_buffer_printf(buffer, "\033[%d;%dH", row, col > 0 ? col : 1);
  int used = 0;
  for (const char *p = text; *p;) {
    int bytes = utf8_next_char_bytes(p, strlen(p));
    if (bytes <= 0)
      break;
    int cells = utf8_display_width_n(p, (size_t)bytes);
    if (used + cells > cols - 2)
      break;
    frame_buffer_append(buffer, p, (size_t)bytes);
    used += cells;
    p += bytes;
  }
}

void ui_media_panel_render(frame_buffer_t *buffer, terminal_size_t size, const char *title, const char *instruction,
                           const char *text, size_t cursor) {
  bool compact = size.cols < 52;
  int logo_rows = compact ? 1 : 4;
  int lines = 1;
  size_t cursor_line = 0;
  if (text) {
    for (size_t i = 0; text[i]; ++i) {
      if (text[i] == '\n') {
        ++lines;
        if (i < cursor) {
          ++cursor_line;
        }
      }
    }
  }
  int available = size.rows - logo_rows - 5;
  if (available < 1)
    available = 1;
  int shown = text ? (lines < available ? lines : available) : 0;
  int height = logo_rows + 3 + (text ? 1 + shown : 0);
  int top = (size.rows - height) / 2 + 1;
  if (top < 1)
    top = 1;
  bool colors = !GET_OPTION(strip_ansi) && terminal_should_color_output(frame_buffer_get_screen_output_fd());
  frame_buffer_append(buffer, "\033[0m\033[H\033[2J\033[?25l", 17);
  frame_buffer_printf(buffer, "\033[%d;1H", top);
  const animation_sample_t *sample = ui_controller_animation_sample();
  int tick = sample ? (int)(sample->seconds * 60) : (int)((time_get_ns() / NS_PER_MS_INT / 16) % 100000);
  invitation_render_logo(buffer, size.cols, tick, colors, compact);
  media_center_line(buffer, title, top + logo_rows + 1, size.cols);
  media_center_line(buffer, instruction, top + logo_rows + 2, size.cols);
  if (!text)
    return;
  int width = size.cols - 6;
  if (width > 76)
    width = 76;
  int longest = 20;
  const char *line = text;
  for (const char *p = text;; ++p) {
    if (!*p || *p == '\n') {
      int n = utf8_display_width_n(line, (size_t)(p - line));
      if (n > longest)
        longest = n;
      if (!*p)
        break;
      line = p + 1;
    }
  }
  if (longest < width)
    width = longest;
  int col = (size.cols - (width + 2)) / 2 + 1;
  int first = cursor_line >= (size_t)shown ? (int)cursor_line - shown + 1 : 0;
  line = text;
  int number = 0, cursor_col = col + 2;
  while (line && number < first + shown) {
    const char *end = strchr(line, '\n');
    if (!end)
      end = line + strlen(line);
    if (number >= first) {
      const char *start = line;
      bool active_line = (size_t)number == cursor_line;
      bool quoted = end - line >= 2 && (*line == '"' || *line == '\'') && end[-1] == *line;
      const char *anchor = active_line ? text + cursor : end;
      int prefix = 0;
      if (quoted && utf8_display_width_n(line, (size_t)(anchor - line)) > width - 1) {
        // Keep the opening quote visible while scrolling the path's interior.
        // The tail includes the closing quote when the cursor is at the end.
        start = line + 1;
        prefix = 4;
        while (start < anchor && utf8_display_width_n(start, (size_t)(anchor - start)) > width - prefix - 1)
          start += utf8_next_char_bytes(start, (size_t)(end - start));
      } else if (active_line) {
        while (start < anchor && utf8_display_width_n(start, (size_t)(anchor - start)) > width - 1)
          start += utf8_next_char_bytes(start, (size_t)(end - start));
      }
      if (active_line)
        cursor_col = col + 2 + prefix + utf8_display_width_n(start, (size_t)(anchor - start));
      frame_buffer_printf(buffer, "\033[%d;%dH%s", top + logo_rows + 4 + number - first, col,
                          active_line ? "> " : "  ");
      if (prefix)
        frame_buffer_printf(buffer, "%c...", *line);
      int used = prefix;
      while (start < end) {
        int bytes = utf8_next_char_bytes(start, (size_t)(end - start));
        if (bytes <= 0)
          break;
        int cells = utf8_display_width_n(start, (size_t)bytes);
        if (used + cells > width)
          break;
        frame_buffer_append(buffer, start, (size_t)bytes);
        used += cells;
        start += bytes;
      }
    }
    ++number;
    line = *end ? end + 1 : NULL;
  }
  frame_buffer_printf(buffer, "\033[%d;%dH\033[?25h", top + logo_rows + 4 + (int)cursor_line - first, cursor_col);
}

static void render_prompt(terminal_size_t size, const void *data) {
  const prompt_snapshot_t *snapshot = data;
  const char *visible = snapshot->text + snapshot->prompt_bytes;
  frame_buffer_t *buffer = frame_buffer_create(size.rows, size.cols);
  if (!buffer)
    return;
  if (strncmp(snapshot->text, "Open media:", 11) == 0) {
    ui_media_panel_render(buffer, size, "Open media:", "Drop files / paste paths or URLs; Enter plays; Esc cancels",
                          visible, snapshot->cursor);
    ui_controller_write(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
    frame_buffer_destroy(buffer);
    return;
  }
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
  ui_controller_write(STDERR_FILENO, frame_buffer_get_content(buffer), frame_buffer_get_length(buffer));
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
  bool media = strncmp(prompt, "Open media:", 11) == 0;
  snapshot->color = !media && !GET_OPTION(strip_ansi) && terminal_should_color_output(prompt_fd());
  snapshot->unicode = !media && terminal_supports_utf8();
  memcpy(snapshot->text, prompt, prompt_bytes);
  memcpy(snapshot->text + prompt_bytes, visible, visible_bytes);
  // Height is measured at the minimum width, so every larger terminal fits.
  terminal_size_t minimum = {.cols = 40,
                             .rows = ui_notice_render(NULL, prompt, snapshot->severity, 40, false, false) + 3};
  if (media)
    minimum.rows = 8;
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
  if (strncmp(snapshot->text, "Open media:", 11) == 0)
    return cols < 52 ? 8 : 11;
  return ui_notice_render(NULL, snapshot->text, snapshot->severity, cols, false, false) + 3;
}
