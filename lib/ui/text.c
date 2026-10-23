#include <ascii-chat/ui/text.h>
#include <ascii-chat/util/string.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/display.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *ui_text_color_prefix(log_color_t color) {
  static _Thread_local char buffers[LOG_COLOR_WHITE + 1][128];
  if (color < LOG_COLOR_DEV || color > LOG_COLOR_WHITE) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid UI text color");
    return "";
  }
  char *out = buffers[color];
  const char *styled = colored_string(color, "!");
  const char *marker = strchr(styled, '!');
  size_t length = marker ? (size_t)(marker - styled) : 0;
  if (length >= sizeof(buffers[0]))
    length = sizeof(buffers[0]) - 1;
  memcpy(out, styled, length);
  out[length] = '\0';
  return out;
}

static void append(char *out, size_t cap, size_t *used, const char *text, size_t length) {
  if (!cap || *used >= cap - 1)
    return;
  size_t room = cap - 1 - *used;
  if (length > room) {
    SET_ERRNO(ERROR_BUFFER_OVERFLOW, "UI text buffer needs %zu additional bytes", length - room);
    return;
  }
  memcpy(out + *used, text, length);
  *used += length;
  out[*used] = '\0';
}

size_t ui_text_cell(char *out, size_t cap, const char *text, int width, ui_alignment_t align) {
  if (!out || !cap) {
    SET_ERRNO(ERROR_INVALID_PARAM, "UI text requires output storage");
    return 0;
  }
  out[0] = '\0';
  if (width <= 0)
    return 0;
  if (!text)
    text = "";
  int columns = 0;
  const char *end = text;
  while (*end) {
    if (end[0] == '\033' && end[1] == '[') {
      const char *escape = end + 2;
      while (*escape && !(*escape >= '@' && *escape <= '~'))
        ++escape;
      if (!*escape)
        break;
      end = escape + 1;
      continue;
    }
    if ((unsigned char)*end < 32)
      break;
    int bytes = utf8_next_char_bytes(end, strlen(end));
    if (bytes <= 0)
      break;
    int n = utf8_display_width_n(end, (size_t)bytes);
    if (n < 0 || columns + n > width)
      break;
    columns += n;
    end += bytes;
  }
  size_t used = 0;
  int pad = width - columns;
  int left = align == UI_ALIGN_RIGHT ? pad : align == UI_ALIGN_CENTER ? pad / 2 : 0;
  for (int i = 0; i < left; ++i)
    append(out, cap, &used, " ", 1);
  append(out, cap, &used, text, (size_t)(end - text));
  // A clipped styled cell must not bleed into the following column.
  if (memchr(text, '\033', (size_t)(end - text)))
    append(out, cap, &used, "\033[0m", 4);
  for (int i = left; i < pad; ++i)
    append(out, cap, &used, " ", 1);
  return used;
}

void ui_text_values_style(char *out, size_t cap, const char *text, log_color_t zero_color) {
  if (!out || !cap) {
    SET_ERRNO(ERROR_INVALID_PARAM, "UI text requires output storage");
    return;
  }
  size_t used = 0;
  out[0] = '\0';
  if (!text)
    return;
  for (const char *p = text; *p;) {
    if (p[0] == '\033' && p[1] == '[') {
      const char *end = p + 2;
      while (*end && !(*end >= '@' && *end <= '~'))
        ++end;
      if (*end)
        ++end;
      append(out, cap, &used, p, (size_t)(end - p));
      p = end;
    } else if (isdigit((unsigned char)*p)) {
      char *end;
      double value = p[0] == '0' && p[1] == 'x' ? (double)strtoull(p, &end, 16) : strtod(p, &end);
      char number[96];
      size_t length = (size_t)(end - p);
      if (length >= sizeof(number))
        length = sizeof(number) - 1;
      memcpy(number, p, length);
      number[length] = '\0';
      const char *styled = colored_string(value == 0 ? zero_color : LOG_COLOR_FATAL, number);
      append(out, cap, &used, styled, strlen(styled));
      p = end;
    } else if ((p == text || !isalnum((unsigned char)p[-1])) &&
               ((!strncmp(p, "enabled", 7) && !isalnum((unsigned char)p[7])) ||
                (!strncmp(p, "disabled", 8) && !isalnum((unsigned char)p[8])))) {
      bool enabled = *p == 'e';
      const char *styled = colored_string(enabled ? LOG_COLOR_INFO : LOG_COLOR_ERROR, enabled ? "enabled" : "disabled");
      append(out, cap, &used, styled, strlen(styled));
      p += enabled ? 7 : 8;
    } else {
      int bytes = utf8_next_char_bytes(p, strlen(p));
      if (bytes <= 0)
        break;
      append(out, cap, &used, p, (size_t)bytes);
      p += bytes;
    }
  }
}

void ui_text_values(char *out, size_t cap, const char *text) {
  ui_text_values_style(out, cap, text, LOG_COLOR_ERROR);
}

void ui_text_render_values(frame_buffer_t *out, const char *text) {
  char styled[4096];
  ui_text_values_style(styled, sizeof(styled), text, LOG_COLOR_FATAL);
  frame_buffer_append(out, styled, strlen(styled));
}
