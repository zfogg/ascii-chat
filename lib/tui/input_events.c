#include <ascii-chat/tui/input_events.h>
#include <ascii-chat/platform/keyboard_text.h>
#include <string.h>

keyboard_key_t tui_input_events_read(tui_input_events_t *s, keyboard_key_t (*read_key)(void),
                                     bool (*classify_paste)(const char *)) {
  if (s->ready)
    return KEY_NONE;
  for (unsigned i = 0; i < 1024; ++i) {
    bool buffered = s->count != 0;
    keyboard_key_t key = buffered ? s->keys[s->head++] : read_key();
    if (buffered)
      --s->count;
    if (key == KEY_NONE)
      return KEY_NONE;
    if (key == KEY_PASTE_BEGIN) {
      s->pasting = true;
      s->invalid = false;
      s->length = 0;
      continue;
    }
    if (key == KEY_PASTE_END) {
      if (!s->pasting)
        continue;
      s->pasting = false;
      s->paste[s->length] = '\0';
      s->ready = true;
      return KEY_PASTE;
    }
    char text[5];
    size_t bytes = keyboard_text_encode(key, text);
    if (s->pasting) {
      // POSIX paste payloads are raw bytes; Windows console text is normalized.
      if (key >= 128 && key <= 255) {
        text[0] = (char)key;
        bytes = 1;
      }
      if (!bytes || (key < 32 && key != '\r' && key != '\n' && key != '\t') || key == 127 ||
          bytes >= sizeof(s->paste) - s->length)
        s->invalid = true;
      else {
        memcpy(s->paste + s->length, text, bytes);
        s->length += bytes;
      }
      continue;
    }
    if (buffered || !bytes || key < 32 || key == 127)
      return key;
    s->head = 0;
    s->keys[0] = key;
    size_t count = 1;
    keyboard_key_t next;
    while (count < 8192 && (next = read_key()) != KEY_NONE) {
      s->keys[count++] = next;
      if (next == KEY_PASTE_BEGIN || next == KEY_PASTE_END)
        break;
    }
    size_t text_count = count;
    while (text_count && (s->keys[text_count - 1] == '\r' || s->keys[text_count - 1] == '\n'))
      --text_count;
    char burst[8192];
    size_t length = 0;
    bool valid = true;
    for (size_t n = 0; n < text_count; ++n) {
      bytes = keyboard_text_encode(s->keys[n], text);
      if (!bytes || s->keys[n] < 32 || s->keys[n] == 127 || bytes >= sizeof(burst) - length) {
        valid = false;
        break;
      }
      memcpy(burst + length, text, bytes);
      length += bytes;
    }
    burst[length] = '\0';
    if (text_count > 1 && classify_paste && classify_paste(burst)) {
      s->invalid = !valid;
      s->length = valid ? length : 0;
      memcpy(s->paste, burst, s->length);
      s->paste[s->length] = '\0';
      s->ready = true;
      return KEY_PASTE;
    }
    s->head = 1;
    s->count = count - 1;
    return key;
  }
  return KEY_NONE;
}
