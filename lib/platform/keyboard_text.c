#include <ascii-chat/platform/keyboard_text.h>
#include <ascii-chat-deps/utf8proc/utf8proc.h>

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
