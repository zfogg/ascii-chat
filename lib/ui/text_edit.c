#include <ascii-chat/ui/input.h>
#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/util/utf8.h>
#include <string.h>
#include <ctype.h>

static size_t previous(const char *text, size_t cursor) {
  if (cursor) {
    do {
      --cursor;
    } while (cursor && ((unsigned char)text[cursor] & 0xc0) == 0x80);
  }
  return cursor;
}

void ui_input_edit_key(char *buffer, size_t capacity, size_t *length, size_t *position, keyboard_key_t key) {
  if (!buffer || !capacity || !length || !position || *length >= capacity || *position > *length ||
      strnlen(buffer, capacity) != *length || !utf8_is_valid(buffer) ||
      (*position && ((unsigned char)buffer[*position] & 0xc0) == 0x80)) {
    SET_ERRNO(ERROR_INVALID_PARAM, "Invalid line editor storage, UTF-8 or cursor");
    return;
  }
  size_t len = *length, cursor = *position, start = cursor, end = cursor;
  if (key == KEY_HOME)
    cursor = 0;
  else if (key == KEY_END)
    cursor = len;
  else if (key == KEY_LEFT)
    cursor = previous(buffer, cursor);
  else if (key == KEY_RIGHT && cursor < len)
    cursor += (size_t)utf8_next_char_bytes(buffer + cursor, len - cursor);
  else if (key == 8 || key == 127)
    start = previous(buffer, cursor);
  else if (key == KEY_DELETE && cursor < len)
    end += (size_t)utf8_next_char_bytes(buffer + cursor, len - cursor);
  else if (key == 23) {
    while (start && isspace((unsigned char)buffer[start - 1]))
      start = previous(buffer, start);
    while (start && !isspace((unsigned char)buffer[start - 1]))
      start = previous(buffer, start);
  } else if (key == KEY_CTRL_DELETE) {
    while (end < len && !isspace((unsigned char)buffer[end]))
      end += (size_t)utf8_next_char_bytes(buffer + end, len - end);
    while (end < len && isspace((unsigned char)buffer[end]))
      ++end;
  } else {
    char text[5];
    if (key == '\t' || (key >= 32 && key != 127)) {
      if (keyboard_text_encode(key, text))
        (void)ui_input_insert(buffer, capacity, length, position, text);
    }
    return;
  }
  if (start != end) {
    memmove(buffer + start, buffer + end, len - end + 1);
    len -= end - start;
    cursor = start;
  }
  *length = len;
  *position = cursor;
}

asciichat_error_t ui_input_insert(char *buffer, size_t capacity, size_t *length, size_t *cursor, const char *text) {
  if (!buffer || !length || !cursor || !text || *length >= capacity || *cursor > *length ||
      strnlen(buffer, capacity) != *length || !utf8_is_valid(text) || !utf8_is_valid(buffer) ||
      (*cursor && ((unsigned char)buffer[*cursor] & 0xc0) == 0x80))
    return SET_ERRNO(ERROR_INVALID_PARAM, "Invalid input insertion storage, text or cursor");
  size_t bytes = strlen(text);
  if (bytes >= capacity - *length)
    return SET_ERRNO(ERROR_BUFFER_OVERFLOW, "Input insertion exceeds field capacity");
  memmove(buffer + *cursor + bytes, buffer + *cursor, *length - *cursor + 1);
  memcpy(buffer + *cursor, text, bytes);
  *cursor += bytes;
  *length += bytes;
  return ASCIICHAT_OK;
}
