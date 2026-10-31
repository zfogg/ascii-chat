#include <ascii-chat/platform/keyboard.h>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/platform/system.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat-deps/utf8proc/utf8proc.h>
#include <string.h>
#include <stdlib.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/log/log.h>

keyboard_key_t keyboard_read_with_timeout(uint32_t timeout_ms) {
  uint64_t deadline = time_get_ns() + (uint64_t)timeout_ms * NS_PER_MS_INT;
  do {
    keyboard_key_t key = keyboard_read_nonblocking();
    if (key != KEY_NONE || timeout_ms == 0)
      return key;
    platform_sleep_ns(NS_PER_MS_INT);
  } while (time_get_ns() < deadline);
  return KEY_NONE;
}

keyboard_line_edit_result_t keyboard_read_line_interactive(keyboard_line_edit_opts_t *opts) {
  return keyboard_apply_key(opts);
}

keyboard_key_t keyboard_text_decode(keyboard_text_decoder_t *s, int input) {
  if (input == 0)
    return KEY_NONE;
  if (s->length && input >= 0x80 && input <= 0xbf) {
    s->bytes[s->length++] = (uint8_t)input;
    if (s->length < s->expected)
      return KEY_NONE;
    s->length = 0;
    utf8proc_int32_t cp;
    if (utf8proc_iterate(s->bytes, s->expected, &cp) < 0)
      return KEY_NONE;
    return (keyboard_key_t)(KEY_UNICODE_BASE + cp);
  }
  s->length = 0;
  if (input < 0x80 || input > 255)
    return (keyboard_key_t)input;
  s->expected = (unsigned)utf8proc_utf8class[input];
  if (s->expected > 1) {
    s->bytes[0] = (uint8_t)input;
    s->length = 1;
  }
  return KEY_NONE;
}

size_t keyboard_text_encode(keyboard_key_t key, char text[5]) {
  utf8proc_int32_t cp;
  if (key > 0 && key < 128)
    cp = (utf8proc_int32_t)key;
  else if (key >= KEY_UNICODE_BASE + 0x80 && key <= KEY_UNICODE_BASE + 0x10ffff)
    cp = (utf8proc_int32_t)key - KEY_UNICODE_BASE;
  else
    return 0;
  if (!utf8proc_codepoint_valid(cp))
    return 0;
  size_t bytes = (size_t)utf8proc_encode_char(cp, (utf8proc_uint8_t *)text);
  text[bytes] = '\0';
  return bytes;
}

keyboard_key_t keyboard_text_utf16(uint16_t *pending_high, uint16_t unit) {
  uint16_t high = *pending_high;
  *pending_high = 0;
  if (unit >= 0xd800 && unit <= 0xdbff) {
    *pending_high = unit;
    return KEY_NONE;
  }
  if (unit >= 0xdc00 && unit <= 0xdfff) {
    if (!high)
      return KEY_NONE;
    return (keyboard_key_t)(KEY_UNICODE_BASE + 0x10000 + ((high - 0xd800) << 10) + unit - 0xdc00);
  }
  return unit < 128 ? (keyboard_key_t)unit : (keyboard_key_t)(KEY_UNICODE_BASE + unit);
}

static atomic_t parser_guard = {0};
static atomic_t background_rgb = {0};
static atomic_t background_ns = {0};
static keyboard_key_t deferred[1024];
static unsigned deferred_head, deferred_count;
static bool querying_background;

static bool parser_try_lock(void) {
  int expected = 0;
  return atomic_cas_int_impl(&parser_guard, &expected, 1);
}

static void parser_unlock(void) {
  atomic_store_int_impl(&parser_guard, 0);
}

static bool parse_background(const char *text, uint32_t *rgb) {
  uint32_t value = 0;
  for (unsigned channel = 0; channel < 3; ++channel) {
    unsigned component = 0, digits = 0;
    while (*text) {
      unsigned digit;
      if (*text >= '0' && *text <= '9')
        digit = (unsigned)(*text - '0');
      else if (*text >= 'a' && *text <= 'f')
        digit = (unsigned)(*text - 'a' + 10);
      else if (*text >= 'A' && *text <= 'F')
        digit = (unsigned)(*text - 'A' + 10);
      else
        break;
      if (++digits > 4)
        return false;
      component = component * 16 + digit;
      ++text;
    }
    if (!digits)
      return false;
    if (channel < 2) {
      if (*text++ != '/')
        return false;
    } else if (*text != 7 && !(*text == 27 && text[1] == '\\'))
      return false;
    if (digits == 1)
      component *= 17;
    else if (digits > 2)
      component >>= 4 * (digits - 2);
    value = (value << 8) | component;
  }
  *rgb = value;
  return true;
}

static char sequence[128];
static atomic_t terminal_reply_ns = {0};

uint64_t keyboard_stream_terminal_reply_ns(void) {
  return atomic_load_u64(&terminal_reply_ns);
}
static size_t length;
static uint64_t started;
static bool pasting;
static keyboard_text_decoder_t text_decoder;
static bool pending_escape;
static bool discarding_osc;
static bool osc_escape;
static keyboard_key_t queued_key;

void keyboard_stream_reset(void) {
  while (!parser_try_lock())
    platform_sleep_ms(1);
  deferred_head = deferred_count = 0;
  length = 0;
  pasting = false;
  memset(&text_decoder, 0, sizeof(text_decoder));
  pending_escape = discarding_osc = osc_escape = false;
  queued_key = KEY_NONE;
  parser_unlock();
}

static keyboard_key_t keyboard_stream_read_unlocked(int (*read_byte)(void)) {
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
    if (!length && byte != 27) {
      if (pasting)
        return (keyboard_key_t)byte;
      keyboard_key_t text = keyboard_text_decode(&text_decoder, byte);
      if (text != KEY_NONE)
        return text;
      continue;
    }
    text_decoder.length = 0;
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
        uint32_t rgb;
        if (parse_background(sequence + 9, &rgb)) {
          atomic_store_u64(&background_rgb, rgb);
          atomic_store_u64(&background_ns, completed_ns);
        }
        if (!querying_background)
          log_debug("TERMINAL_REPLY: parse_ms=%.3f bytes=%zu", (double)(completed_ns - started) / NS_PER_MS_INT,
                    length);
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
    else if (!strcmp(sequence, "\033[13;2u") || !strcmp(sequence, "\033[27;2;13~"))
      key = KEY_SHIFT_ENTER;
    else if (!strcmp(sequence, "\033[27u"))
      key = KEY_ESCAPE;
    else if (!strcmp(sequence, "\033[13u"))
      key = (keyboard_key_t)13;
    else if (!strcmp(sequence, "\033[9u"))
      key = (keyboard_key_t)9;
    else if (!strcmp(sequence, "\033[127u"))
      key = (keyboard_key_t)127;
    else if (!strcmp(sequence, "\033[99;5u") || !strcmp(sequence, "\033[27;5;99~"))
      key = (keyboard_key_t)3;
    else if (!strcmp(sequence, "\033[118;5u") || !strcmp(sequence, "\033[27;5;118~"))
      key = (keyboard_key_t)22;
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
    else if (!strcmp(sequence, "\033[5~"))
      key = KEY_PAGEUP;
    else if (!strcmp(sequence, "\033[6~"))
      key = KEY_PAGEDOWN;
    else if (!strcmp(sequence, "\033[3~"))
      key = KEY_DELETE;
    else if (!strcmp(sequence, "\033[3;5~") || !strcmp(sequence, "\033[2~"))
      key = KEY_CTRL_DELETE;
    else if (!strcmp(sequence, "\033[24~"))
      key = KEY_F12;
    else if (sequence[1] == '[' && byte == 'u') {
      char *end;
      unsigned long cp = strtoul(sequence + 2, &end, 10);
      unsigned long modifiers = 1;
      if (*end == ';')
        modifiers = strtoul(end + 1, &end, 10);
      if (end == sequence + length - 1 && cp > 0 && cp <= 0x10ffff && utf8proc_codepoint_valid((utf8proc_int32_t)cp) &&
          !(cp >= 0xe000 && cp <= 0xf8ff) && (modifiers == 1 || modifiers == 2))
        key = cp < 128 ? (keyboard_key_t)cp : (keyboard_key_t)(KEY_UNICODE_BASE + cp);
    }
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

bool keyboard_stream_ready(int (*read_byte)(void)) {
  if (!parser_try_lock())
    return false;
  if (!deferred_count) {
    keyboard_key_t key = keyboard_stream_read_unlocked(read_byte);
    if (key != KEY_NONE)
      deferred[(deferred_head + deferred_count++) % 1024] = key;
  }
  bool ready = deferred_count != 0;
  parser_unlock();
  return ready;
}

keyboard_key_t keyboard_stream_read(int (*read_byte)(void)) {
  if (!parser_try_lock())
    return KEY_NONE;
  keyboard_key_t key;
  if (deferred_count) {
    key = deferred[deferred_head];
    deferred_head = (deferred_head + 1) % 1024;
    --deferred_count;
  } else {
    key = keyboard_stream_read_unlocked(read_byte);
  }
  parser_unlock();
  return key;
}

bool keyboard_stream_query_background(int (*read_byte)(void), bool (*send_query)(void), uint8_t *r, uint8_t *g,
                                      uint8_t *b) {
  if (!r || !g || !b || !read_byte || !send_query)
    return false;
  uint64_t deadline = time_get_ns() + 100 * NS_PER_MS_INT;
  while (!parser_try_lock()) {
    if (time_get_ns() >= deadline)
      return false;
    platform_sleep_ms(1);
  }
  uint64_t before = atomic_load_u64(&background_ns);
  querying_background = true;
  bool received = false;
  if (deferred_count < 1024 && send_query()) {
    do {
      keyboard_key_t key = keyboard_stream_read_unlocked(read_byte);
      if (key != KEY_NONE)
        deferred[(deferred_head + deferred_count++) % 1024] = key;
      if (atomic_load_u64(&background_ns) > before) {
        uint64_t rgb = atomic_load_u64(&background_rgb);
        *r = (uint8_t)(rgb >> 16);
        *g = (uint8_t)(rgb >> 8);
        *b = (uint8_t)rgb;
        received = true;
        break;
      }
      if (key == KEY_NONE)
        platform_sleep_ms(1);
    } while (deferred_count < 1024 && time_get_ns() < deadline);
  }
  querying_background = false;
  parser_unlock();
  return received;
}

bool keyboard_stream_background(uint8_t *r, uint8_t *g, uint8_t *b) {
  if (!atomic_load_u64(&background_ns))
    return false;
  uint64_t rgb = atomic_load_u64(&background_rgb);
  *r = (uint8_t)(rgb >> 16);
  *g = (uint8_t)(rgb >> 8);
  *b = (uint8_t)rgb;
  return true;
}

keyboard_line_edit_result_t keyboard_apply_key(keyboard_line_edit_opts_t *opts) {
  if (!opts || !opts->buffer || !opts->len || !opts->cursor || !opts->max_len || *opts->len >= opts->max_len ||
      *opts->cursor > *opts->len || opts->key == KEY_NONE)
    return LINE_EDIT_NO_INPUT;
  keyboard_key_t key = opts->key;
  if (key == '\r' || key == '\n')
    return LINE_EDIT_ACCEPTED;
  if (key == KEY_ESCAPE || key == 3 || ((key == 8 || key == 127 || key == 23) && !*opts->len))
    return LINE_EDIT_CANCELLED;
  ui_input_edit_key(opts->buffer, opts->max_len, opts->len, opts->cursor, key);
  return LINE_EDIT_CONTINUE;
}
