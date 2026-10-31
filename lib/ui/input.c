#include <ascii-chat/ui/input.h>
#include <ascii-chat/asciichat_errno.h>
#include <string.h>
#include <ascii-chat/util/utf8.h>
asciichat_error_t ui_input_render(frame_buffer_t *buffer, int row, int col, int width, int height, const char *text,
                                  size_t cursor, bool preserve_quotes) {
  if (!buffer || !text || row < 1 || col < 1 || width < 5 || height < 1 || cursor > strlen(text) ||
      !utf8_is_valid(text) || (cursor && ((unsigned char)text[cursor] & 0xc0) == 0x80))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid input field text, cursor or geometry");
  int lines = 1;
  size_t cursor_line = 0;
  for (size_t i = 0; text[i]; ++i)
    if (text[i] == '\n') {
      ++lines;
      if (i < cursor)
        ++cursor_line;
    }
  int shown = lines < height ? lines : height;
  int first = cursor_line >= (size_t)shown ? (int)cursor_line - shown + 1 : 0;
  const char *line = text;
  int number = 0, cursor_col = col + 2;
  while (line && number < first + shown) {
    const char *end = strchr(line, '\n');
    if (!end)
      end = line + strlen(line);
    if (number >= first) {
      const char *start = line;
      bool active_line = (size_t)number == cursor_line;
      bool quoted = preserve_quotes && end - line >= 2 && (*line == '"' || *line == '\'') && end[-1] == *line;
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
      frame_buffer_printf(buffer, "\033[%d;%dH%s", row + number - first, col, active_line ? "> " : "  ");
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
      frame_buffer_printf(buffer, "%*s", width - used, "");
    }
    ++number;
    line = *end ? end + 1 : NULL;
  }
  frame_buffer_printf(buffer, "\033[%d;%dH\033[?25h", row + (int)cursor_line - first, cursor_col);
  return ASCIICHAT_OK;
}
