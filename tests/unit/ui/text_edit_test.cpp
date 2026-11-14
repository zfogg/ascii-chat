#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/ui/input.h>
#include <ascii-chat/platform/keyboard.h>
#include <string.h>
#include <locale.h>

TEST_CASE("text_edit/insertion_is_atomic_and_keeps_valid_utf8", "[text_edit]") {
  char text[16] = "a界b";
  size_t length = strlen(text), cursor = 1;
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, "é😀"), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(text, "aé😀界b");
  ASCIICHAT_REQUIRE_EQ(cursor, 7);
  ASCIICHAT_REQUIRE_EQ(length, 11);
  const char *bad[] = {"\x80", "\xc3", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
    ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, bad[i]), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_STR_EQ(text, "aé😀界b");
    ASCIICHAT_REQUIRE_EQ(length, 11);
    ASCIICHAT_REQUIRE_EQ(cursor, 7);
  }
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, 12, &length, &cursor, "é"), ERROR_BUFFER_OVERFLOW);
  ASCIICHAT_REQUIRE_STR_EQ(text, "aé😀界b");
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, ""), ASCIICHAT_OK);
}

TEST_CASE("text_edit/invalid_storage_and_mid_character_cursors_are_rejected", "[text_edit]") {
  char text[8] = "é";
  size_t length = 2, cursor = 1;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_DELETE);
  ASCIICHAT_REQUIRE_STR_EQ(text, "é");
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  cursor = 2;
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(NULL, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), NULL, &cursor, "x"), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, NULL, "x"), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, NULL), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, 2, &length, &cursor, "x"), ERROR_INVALID_PARAM);
  cursor = 3;
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  cursor = 0;
  length = 1;
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  length = 2;
  text[0] = (char)0xc0;
  ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, "x"), ERROR_INVALID_PARAM);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'x');
  text[0] = (char)0xc3;
  ui_input_edit_key(NULL, sizeof(text), &length, &cursor, 'x');
  ui_input_edit_key(text, 0, &length, &cursor, 'x');
  ui_input_edit_key(text, sizeof(text), NULL, &cursor, 'x');
  ui_input_edit_key(text, sizeof(text), &length, NULL, 'x');
  ui_input_edit_key(text, 2, &length, &cursor, 'x');
  cursor = 3;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'x');
  cursor = 0;
  length = 1;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 'x');
  ASCIICHAT_REQUIRE_STR_EQ(text, "é");
}

TEST_CASE("text_edit/boundaries_word_operations_and_ignored_keys", "[text_edit]") {
  char text[64] = "é  界 😀";
  size_t length = strlen(text), cursor = length;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_LEFT);
  ASCIICHAT_REQUIRE_EQ(cursor, 8);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_RIGHT);
  ASCIICHAT_REQUIRE_EQ(cursor, length);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_RIGHT);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_DELETE);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_HOME);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_LEFT);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 8);
  ASCIICHAT_REQUIRE_EQ(cursor, 0);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 23);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_CTRL_DELETE);
  ASCIICHAT_REQUIRE_STR_EQ(text, "界 😀");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_END);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_CTRL_DELETE);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 23);
  ASCIICHAT_REQUIRE_STR_EQ(text, "界 ");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 23);
  ASCIICHAT_REQUIRE_STR_EQ(text, "");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_DELETE);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_RIGHT);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_UP);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 1);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, (keyboard_key_t)0x110000);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, (keyboard_key_t)(0x1f600));
  ASCIICHAT_REQUIRE_STR_EQ(text, "😀");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, '\t');
  ASCIICHAT_REQUIRE_STR_EQ(text, "😀\t");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 8);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ASCIICHAT_REQUIRE_STR_EQ(text, "");
}

TEST_CASE("text_edit/combining_marks_and_emoji_sequences_preserve_codepoint_boundaries", "[text_edit]") {
  char text[64] = "e\xcc\x81👩‍💻";
  size_t length = strlen(text), cursor = length;
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ASCIICHAT_REQUIRE_STR_EQ(text, "e\xcc\x81👩‍");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ASCIICHAT_REQUIRE_STR_EQ(text, "e\xcc\x81👩");
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ui_input_edit_key(text, sizeof(text), &length, &cursor, 127);
  ASCIICHAT_REQUIRE_STR_EQ(text, "e");
}

TEST_CASE("text_edit/word_deletion_uses_ascii_delimiters_in_every_available_locale", "[text_edit]") {
  const char *locales[] = {"C", "C.UTF-8", "en_US.ISO8859-1", "fr_FR.ISO8859-1"};
  char original[128];
  SAFE_STRNCPY(original, setlocale(LC_CTYPE, NULL), sizeof(original));
  for (size_t i = 0; i < sizeof(locales) / sizeof(*locales); ++i) {
    if (!setlocale(LC_CTYPE, locales[i]))
      continue;
    char text[64] = "abcàdef tail";
    size_t length = strlen(text), cursor = length;
    ui_input_edit_key(text, sizeof(text), &length, &cursor, 23);
    ASCIICHAT_REQUIRE_STR_EQ(text, "abcàdef ");
    ui_input_edit_key(text, sizeof(text), &length, &cursor, 23);
    ASCIICHAT_REQUIRE_STR_EQ(text, "");
    ASCIICHAT_REQUIRE_EQ(ui_input_insert(text, sizeof(text), &length, &cursor, "abcàdef\t\n\r\v\f tail"), ASCIICHAT_OK);
    cursor = 0;
    ui_input_edit_key(text, sizeof(text), &length, &cursor, KEY_CTRL_DELETE);
    ASCIICHAT_REQUIRE_STR_EQ(text, "tail");
    ASCIICHAT_REQUIRE_EQ(cursor, 0);
  }
  ASCIICHAT_REQUIRE_NOT_NULL(setlocale(LC_CTYPE, original));
}
