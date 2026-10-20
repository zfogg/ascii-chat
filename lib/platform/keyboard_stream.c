#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/util/time.h>
#include <string.h>
#include <ascii-chat/atomic.h>

static char sequence[128];
static atomic_t terminal_reply_ns = {0};

uint64_t keyboard_stream_terminal_reply_ns(void) {
  return atomic_load_u64(&terminal_reply_ns);
}
static size_t length;
static uint64_t started;
static bool pasting;

void keyboard_stream_reset(void) {
  length = 0;
  pasting = false;
}

keyboard_key_t keyboard_stream_read(int (*read_byte)(void)) {
  for (unsigned i = 0; i < 64; ++i) {
    int byte = read_byte();
    if (!byte) {
      if (length && !pasting && time_get_ns() - started >= 50 * NS_PER_MS_INT) {
        bool escape = length == 1;
        length = 0;
        return escape ? KEY_ESCAPE : KEY_NONE;
      }
      return KEY_NONE;
    }
    if (!length && byte != 27)
      return (keyboard_key_t)byte;
    if (!length)
      started = time_get_ns();
    if (byte > 255 || length == sizeof(sequence) - 1) {
      length = 0;
      return pasting ? (keyboard_key_t)27 : KEY_NONE;
    }
    sequence[length++] = (char)byte;
    sequence[length] = '\0';
    if (length == 1)
      continue;
    if (length == 2 && (byte == '[' || byte == ']'))
      continue;
    if (length >= 3 && sequence[1] == ']' && !pasting) {
      bool complete = byte == 7 || (byte == '\\' && sequence[length - 2] == 27);
      if (!complete)
        continue;
      if (!strncmp(sequence, "\033]11;rgb:", 9))
        atomic_store_u64(&terminal_reply_ns, time_get_ns());
      length = 0;
      return KEY_NONE;
    }
    if (length >= 3 && ((byte >= '0' && byte <= '9') || byte == ';'))
      continue;
    keyboard_key_t key = KEY_NONE;
    if (!strcmp(sequence, "\033[200~")) {
      pasting = true;
      key = KEY_PASTE_BEGIN;
    } else if (!strcmp(sequence, "\033[201~")) {
      pasting = false;
      key = KEY_PASTE_END;
    } else if (pasting)
      key = (keyboard_key_t)27;
    else if (!strcmp(sequence, "\033[A"))
      key = KEY_UP;
    else if (!strcmp(sequence, "\033[B"))
      key = KEY_DOWN;
    else if (!strcmp(sequence, "\033[C"))
      key = KEY_RIGHT;
    else if (!strcmp(sequence, "\033[D"))
      key = KEY_LEFT;
    else if (!strcmp(sequence, "\033[H") || !strcmp(sequence, "\033[1~"))
      key = KEY_HOME;
    else if (!strcmp(sequence, "\033[F") || !strcmp(sequence, "\033[4~"))
      key = KEY_END;
    else if (!strcmp(sequence, "\033[3~"))
      key = KEY_DELETE;
    else if (!strcmp(sequence, "\033[3;5~") || !strcmp(sequence, "\033[2~"))
      key = KEY_CTRL_DELETE;
    length = 0;
    return key;
  }
  return KEY_NONE;
}
