#include <ascii-chat/platform/keyboard_stream.h>
#include <ascii-chat/util/time.h>
#include <string.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/log/log.h>

static char sequence[128];
static atomic_t terminal_reply_ns = {0};

uint64_t keyboard_stream_terminal_reply_ns(void) {
  return atomic_load_u64(&terminal_reply_ns);
}
static size_t length;
static uint64_t started;
static bool pasting;
static bool pending_escape;
static bool discarding_osc;
static bool osc_escape;
static keyboard_key_t queued_key;

void keyboard_stream_reset(void) {
  length = 0;
  pasting = false;
  pending_escape = discarding_osc = osc_escape = false;
  queued_key = KEY_NONE;
}

keyboard_key_t keyboard_stream_read(int (*read_byte)(void)) {
  if (queued_key != KEY_NONE) {
    keyboard_key_t key = queued_key;
    queued_key = KEY_NONE;
    return key;
  }
  for (unsigned i = 0; i < 64; ++i) {
    int byte = read_byte();
    if (!byte) {
      if (length == 1 && !pasting && time_get_ns() - started >= 50 * NS_PER_MS_INT) {
        length = 0;
        pending_escape = false;
        return KEY_ESCAPE;
      }
      return KEY_NONE;
    }
    if (discarding_osc) {
      bool complete = byte == 7 || (osc_escape && byte == '\\');
      osc_escape = byte == 27;
      if (complete) {
        discarding_osc = osc_escape = false;
        bool escape = pending_escape;
        pending_escape = false;
        return escape ? KEY_ESCAPE : KEY_NONE;
      }
      continue;
    }
    if (!pasting && length > 2 && sequence[1] == ']' && sequence[length - 1] == 27 && byte != '\\') {
      // ESC is only part of an OSC reply when followed by the ST terminator.
      // Preserve a physical Escape inserted between reply fragments.
      pending_escape = true;
      --length;
    }
    if (!pasting && byte == 27 && length && !(length > 1 && sequence[1] == ']')) {
      // A physical Escape can precede the ESC that starts a frontend reply.
      // Keep the reply framed; its ']' and color components are never keys.
      pending_escape |= length == 1;
      length = 0;
    }
    if (!length && byte != 27)
      return (keyboard_key_t)byte;
    if (!length)
      started = time_get_ns();
    if (byte > 255 || length == sizeof(sequence) - 1) {
      if (!pasting && length > 1 && sequence[1] == ']') {
        bool complete = byte == 7 || (sequence[length - 1] == 27 && byte == '\\');
        discarding_osc = !complete;
        osc_escape = !complete && byte == 27;
        if (complete && pending_escape) {
          length = 0;
          pending_escape = false;
          return KEY_ESCAPE;
        }
      }
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
      if (!strncmp(sequence, "\033]11;rgb:", 9)) {
        uint64_t completed_ns = time_get_ns();
        atomic_store_u64(&terminal_reply_ns, completed_ns);
        log_debug("TERMINAL_REPLY: parse_ms=%.3f bytes=%zu", (double)(completed_ns - started) / NS_PER_MS_INT, length);
      }
      length = 0;
      bool escape = pending_escape;
      pending_escape = false;
      return escape ? KEY_ESCAPE : KEY_NONE;
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
    if (pending_escape) {
      pending_escape = false;
      queued_key = key;
      return KEY_ESCAPE;
    }
    return key;
  }
  return KEY_NONE;
}
