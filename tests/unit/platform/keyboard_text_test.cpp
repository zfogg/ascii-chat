#include <catch2/catch_test_macros.hpp>
#include <libplatform/keyboard.h>
#include <string.h>

TEST_CASE("keyboard_text/every_unicode_scalar_round_trips", "[keyboard_text] [unit]") {
  for (uint32_t cp = 1; cp <= 0x10ffff; ++cp) {
    if (cp >= 0xd800 && cp <= 0xdfff)
      continue;
    keyboard_key_t expected = (keyboard_key_t)cp;
    char text[5] = {0};
    size_t bytes = keyboard_text_encode(expected, text);
    REQUIRE((bytes) == (cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4));
    REQUIRE((strlen(text)) == (bytes));
    keyboard_text_decoder_t decoder = {0};
    for (size_t i = 0; i < bytes; ++i) {
      REQUIRE((keyboard_text_decode(&decoder, 0)) == (KEY_NONE));
      REQUIRE((keyboard_text_decode(&decoder, (unsigned char)text[i])) == (i + 1 == bytes ? expected : KEY_NONE));
    }
    REQUIRE((decoder.length) == (0));
  }
}

TEST_CASE("keyboard_text/rejects_non_text_and_invalid_scalar_keys", "[keyboard_text] [unit]") {
  const int keys[] = {0, -1, KEY_UP, KEY_SHIFT_ENTER, 0xd800, 0xdfff, 0x110000, 0x11007f, 0x220000};
  for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); ++i) {
    char text[5] = "keep";
    REQUIRE((keyboard_text_encode((keyboard_key_t)keys[i], text)) == (0));
    REQUIRE(std::strcmp((text), ("keep")) == 0);
  }
}

TEST_CASE("keyboard_text/malformed_sequences_are_discarded_without_losing_next_key", "[keyboard_text] [unit]") {
  const char *invalid[] = {"\x80", "\xbf", "\xc0\xaf", "\xc1\xbf", "\xe0\x80\xaf", "\xed\xa0\x80", "\xed\xbf\xbf", "\xf0\x80\x80\xaf", "\xf4\x90\x80\x80", "\xf5\x80\x80\x80", "\xff"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    keyboard_text_decoder_t decoder = {0};
    for (const unsigned char *p = (const unsigned char *)invalid[i]; *p; ++p)
      REQUIRE((keyboard_text_decode(&decoder, *p)) == (KEY_NONE));
    REQUIRE((keyboard_text_decode(&decoder, 'x')) == ('x'));
  }
  keyboard_text_decoder_t decoder = {0};
  keyboard_text_decode(&decoder, 0xe2);
  REQUIRE((keyboard_text_decode(&decoder, KEY_LEFT)) == (KEY_LEFT));
  REQUIRE((keyboard_text_decode(&decoder, 0x82)) == (KEY_NONE));
  keyboard_text_decode(&decoder, 0xe2);
  REQUIRE((keyboard_text_decode(&decoder, 'x')) == ('x'));
  keyboard_text_decode(&decoder, 0xf0);
  keyboard_text_decode(&decoder, 0xc3);
  REQUIRE((keyboard_text_decode(&decoder, 0xa9)) == (0xe9));
}

TEST_CASE("keyboard_text/utf16_surrogates_and_recovery", "[keyboard_text] [unit]") {
  uint16_t high = 0;
  REQUIRE((keyboard_text_utf16(&high, 'x')) == ('x'));
  REQUIRE((keyboard_text_utf16(&high, 0x754c)) == (0x754c));
  REQUIRE((keyboard_text_utf16(&high, 0xdc00)) == (KEY_NONE));
  REQUIRE((keyboard_text_utf16(&high, 0xd83d)) == (KEY_NONE));
  REQUIRE((keyboard_text_utf16(&high, 0xde00)) == (0x1f600));
  REQUIRE((high) == (0));
  keyboard_text_utf16(&high, 0xd800);
  REQUIRE((keyboard_text_utf16(&high, 'q')) == ('q'));
  keyboard_text_utf16(&high, 0xd800);
  keyboard_text_utf16(&high, 0xdbff);
  REQUIRE((keyboard_text_utf16(&high, 0xdfff)) == (0x10ffff));
  REQUIRE((keyboard_text_utf16(&high, 0xffff)) == (0xffff));
  REQUIRE((keyboard_text_utf16(&high, 0)) == (KEY_NONE));
}
