#include <criterion/criterion.h>
#include <ascii-chat/platform/keyboard_text.h>
#include <string.h>

Test(keyboard_text, every_unicode_scalar_round_trips) {
  for (uint32_t cp = 1; cp <= 0x10ffff; ++cp) {
    if (cp >= 0xd800 && cp <= 0xdfff)
      continue;
    keyboard_key_t expected = cp < 128 ? (keyboard_key_t)cp : (keyboard_key_t)(KEY_UNICODE_BASE + cp);
    char text[5] = {0};
    size_t bytes = keyboard_text_encode(expected, text);
    cr_assert_eq(bytes, cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4, "U+%x", cp);
    cr_assert_eq(strlen(text), bytes);
    keyboard_text_decoder_t decoder = {0};
    for (size_t i = 0; i < bytes; ++i) {
      cr_assert_eq(keyboard_text_decode(&decoder, 0), KEY_NONE);
      cr_assert_eq(keyboard_text_decode(&decoder, (unsigned char)text[i]), i + 1 == bytes ? expected : KEY_NONE,
                   "U+%x byte %zu", cp, i);
    }
    cr_assert_eq(decoder.length, 0);
  }
}

Test(keyboard_text, rejects_non_text_and_invalid_scalar_keys) {
  const int keys[] = {0,
                      -1,
                      128,
                      255,
                      KEY_UP,
                      KEY_UNICODE_BASE,
                      KEY_UNICODE_BASE + 127,
                      KEY_UNICODE_BASE + 0xd800,
                      KEY_UNICODE_BASE + 0xdfff,
                      KEY_UNICODE_BASE + 0x110000};
  for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); ++i) {
    char text[5] = "keep";
    cr_assert_eq(keyboard_text_encode((keyboard_key_t)keys[i], text), 0);
    cr_assert_str_eq(text, "keep");
  }
}

Test(keyboard_text, malformed_sequences_are_discarded_without_losing_next_key) {
  const char *invalid[] = {"\x80",         "\xbf",         "\xc0\xaf",         "\xc1\xbf",         "\xe0\x80\xaf",
                           "\xed\xa0\x80", "\xed\xbf\xbf", "\xf0\x80\x80\xaf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80",
                           "\xff"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    keyboard_text_decoder_t decoder = {0};
    for (const unsigned char *p = (const unsigned char *)invalid[i]; *p; ++p)
      cr_assert_eq(keyboard_text_decode(&decoder, *p), KEY_NONE);
    cr_assert_eq(keyboard_text_decode(&decoder, 'x'), 'x');
  }
  keyboard_text_decoder_t decoder = {0};
  keyboard_text_decode(&decoder, 0xe2);
  cr_assert_eq(keyboard_text_decode(&decoder, KEY_LEFT), KEY_LEFT);
  cr_assert_eq(keyboard_text_decode(&decoder, 0x82), KEY_NONE);
  keyboard_text_decode(&decoder, 0xe2);
  cr_assert_eq(keyboard_text_decode(&decoder, 'x'), 'x');
  keyboard_text_decode(&decoder, 0xf0);
  keyboard_text_decode(&decoder, 0xc3);
  cr_assert_eq(keyboard_text_decode(&decoder, 0xa9), KEY_UNICODE_BASE + 0xe9);
}

Test(keyboard_text, utf16_surrogates_and_recovery) {
  uint16_t high = 0;
  cr_assert_eq(keyboard_text_utf16(&high, 'x'), 'x');
  cr_assert_eq(keyboard_text_utf16(&high, 0x754c), KEY_UNICODE_BASE + 0x754c);
  cr_assert_eq(keyboard_text_utf16(&high, 0xdc00), KEY_NONE);
  cr_assert_eq(keyboard_text_utf16(&high, 0xd83d), KEY_NONE);
  cr_assert_eq(keyboard_text_utf16(&high, 0xde00), KEY_UNICODE_BASE + 0x1f600);
  cr_assert_eq(high, 0);
  keyboard_text_utf16(&high, 0xd800);
  cr_assert_eq(keyboard_text_utf16(&high, 'q'), 'q');
  keyboard_text_utf16(&high, 0xd800);
  keyboard_text_utf16(&high, 0xdbff);
  cr_assert_eq(keyboard_text_utf16(&high, 0xdfff), KEY_UNICODE_BASE + 0x10ffff);
  cr_assert_eq(keyboard_text_utf16(&high, 0xffff), KEY_UNICODE_BASE + 0xffff);
  cr_assert_eq(keyboard_text_utf16(&high, 0), KEY_NONE);
}
