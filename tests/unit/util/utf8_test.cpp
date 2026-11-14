#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/string.h>
#include <string.h>

TEST_CASE("utf8/prefix_columns", "[utf8]") {
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("abc", 3, 2), 2);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("é中😀x", 10, 3), 5);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("é中😀x", 10, 4), 5);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("é中😀x", 10, 5), 9);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("e\xcc\x81x", 4, 1), 3);
  ASCIICHAT_REQUIRE_EQ(utf8_display_width("é中😀"), 5);
}

TEST_CASE("utf8/bounded_and_invalid_input", "[utf8]") {
  const char slice[] = {'A', (char)0xe4, (char)0xb8, (char)0xad, 'B'};
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width(slice, sizeof(slice), 3), 4);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width(slice, 3, 3), 1);
  ASCIICHAT_REQUIRE_EQ(utf8_display_width_n(slice, 3), 1);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("a\xff", 2, 5), 1);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width(NULL, 5, 5), 0);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("abc", 0, 5), 0);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("abc", 3, 0), 0);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("abc", 3, -1), 0);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("a\0b", 3, 5), 1);
}

TEST_CASE("utf8/complete_csi_and_trailing_marks", "[utf8]") {
  const char *text = "\033[31me\xcc\x81\033[0m\033[2Kx";
  size_t prefix = strlen(text) - 1;
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width(text, strlen(text), 1), prefix);
  ASCIICHAT_REQUIRE_EQ(utf8_display_width_n(text, prefix), 1);
  ASCIICHAT_REQUIRE_EQ(utf8_display_width(text), 2);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("\033[31", 4, 1), 0);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("a\033", 2, 5), 1);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("a\033[", 3, 5), 1);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("a\033[31\0x", 8, 5), 1);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("\033[0mx", 5, 0), 4);
}

TEST_CASE("utf8/ellipsis_width_and_byte_limits", "[utf8]") {
  char output[64];
  truncate_with_ellipsis_mode("é中😀x", output, sizeof(output), 4, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "é中\033[0m…");
  truncate_with_ellipsis_mode("e\xcc\x81x", output, sizeof(output), 1, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "\033[0m…");
  truncate_with_ellipsis_mode("é中", output, sizeof(output), 3, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "é中");
  truncate_with_ellipsis_mode("abc", output, sizeof(output), 0, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "");
  truncate_with_ellipsis_mode("abc", output, sizeof(output), -1, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "");
  truncate_with_ellipsis_mode("a\033[31", output, sizeof(output), 20, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "a\033[0m…");
  truncate_with_ellipsis_mode("中中中中", output, 10, 20, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "\033[0m…");
  truncate_with_ellipsis_mode("\033[31mabcdef", output, 11, 20, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "\033[0m…");
}

TEST_CASE("utf8/ellipsis_uses_ascii_when_utf8_is_disabled", "[utf8]") {
  char output[64];
  truncate_with_ellipsis_mode("abcdef", output, sizeof(output), 4, false);
  ASCIICHAT_REQUIRE_STR_EQ(output, "a\033[0m...");
  ASCIICHAT_REQUIRE_EQ(utf8_display_width(output), 4);
  truncate_with_ellipsis_mode("abcdef", output, sizeof(output), 4, true);
  ASCIICHAT_REQUIRE_STR_EQ(output, "abc\033[0m…");
  ASCIICHAT_REQUIRE_EQ(utf8_display_width(output), 4);
  truncate_with_ellipsis_mode("abcdef", output, sizeof(output), 2, false);
  ASCIICHAT_REQUIRE_STR_EQ(output, "");
}

TEST_CASE("utf8/ellipsis_small_buffers", "[utf8]") {
  unsigned char guarded[18];
  for (size_t size = 0; size <= 16; ++size) {
    memset(guarded, 0xa5, sizeof(guarded));
    char *output = (char *)guarded + 1;
    truncate_with_ellipsis_mode("é中😀abcdef", output, size, 3, true);
    ASCIICHAT_REQUIRE_EQ(guarded[0], 0xa5);
    ASCIICHAT_REQUIRE_EQ(guarded[size + 1], 0xa5);
    if (size > 0) {
      ASCIICHAT_REQUIRE_NOT_NULL(memchr(output, 0, size));
      ASCIICHAT_REQUIRE(utf8_is_valid(output));
      ASCIICHAT_REQUIRE_LE(utf8_display_width(output), 3);
      if (size < 8) {
        ASCIICHAT_REQUIRE_STR_EQ(output, "");
      }
    }
  }
  char tiny[3];
  truncate_with_ellipsis_mode("é", tiny, sizeof(tiny), 1, true);
  ASCIICHAT_REQUIRE_STR_EQ(tiny, "é");
}

TEST_CASE("utf8/repeat_sequences_count_cells_without_splitting", "[utf8]") {
  const char *text = "é\033[3bX";
  ASCIICHAT_REQUIRE_EQ(utf8_display_width(text), 5);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width(text, strlen(text), 4), strlen(text) - 1);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width(text, strlen(text), 3), strlen("é"));
  ASCIICHAT_REQUIRE_EQ(utf8_display_width("界\033[2b"), 6);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("界\033[2b", strlen("界\033[2b"), 5), strlen("界"));
  ASCIICHAT_REQUIRE_EQ(utf8_display_width("x\033[b"), 2);
  ASCIICHAT_REQUIRE_EQ(utf8_display_width("x\033[0b"), 2);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("x\033[42949672960b", strlen("x\033[42949672960b"), 20), 1);
  ASCIICHAT_REQUIRE_EQ(utf8_prefix_bytes_for_width("x\033[1;2b", strlen("x\033[1;2b"), 20), 1);
}

TEST_CASE("utf8/invalid_scalar_encodings_and_truncated_strings_are_rejected", "[utf8]") {
  const char *invalid[] = {"\xc0\x80",
                           "\xc1\xbf",
                           "\xe0\x80\xaf",
                           "\xed\xa0\x80",
                           "\xf0\x80\x80\xaf",
                           "\xf4\x90\x80\x80",
                           "\xf5\x80\x80\x80",
                           "\xc3",
                           "\xe2\x82",
                           "\xf0\x9f\x98",
                           "\x80"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    uint32_t cp = 42;
    ASCIICHAT_REQUIRE_EQ(utf8_decode((const uint8_t *)invalid[i], &cp), -1);
    ASCIICHAT_REQUIRE_EQ(cp, 42);
    ASCIICHAT_REQUIRE_NOT(utf8_is_valid(invalid[i]));
  }
}
