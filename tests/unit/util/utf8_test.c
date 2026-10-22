#include <criterion/criterion.h>
#include <ascii-chat/util/utf8.h>
#include <ascii-chat/util/string.h>
#include <string.h>

Test(utf8, prefix_columns) {
  cr_assert_eq(utf8_prefix_bytes_for_width("abc", 3, 2), 2);
  cr_assert_eq(utf8_prefix_bytes_for_width("é中😀x", 10, 3), 5);
  cr_assert_eq(utf8_prefix_bytes_for_width("é中😀x", 10, 4), 5);
  cr_assert_eq(utf8_prefix_bytes_for_width("é中😀x", 10, 5), 9);
  cr_assert_eq(utf8_prefix_bytes_for_width("e\xcc\x81x", 4, 1), 3);
  cr_assert_eq(utf8_display_width("é中😀"), 5);
}

Test(utf8, bounded_and_invalid_input) {
  const char slice[] = {'A', (char)0xe4, (char)0xb8, (char)0xad, 'B'};
  cr_assert_eq(utf8_prefix_bytes_for_width(slice, sizeof(slice), 3), 4);
  cr_assert_eq(utf8_prefix_bytes_for_width(slice, 3, 3), 1);
  cr_assert_eq(utf8_display_width_n(slice, 3), 1);
  cr_assert_eq(utf8_prefix_bytes_for_width("a\xff", 2, 5), 1);
  cr_assert_eq(utf8_prefix_bytes_for_width(NULL, 5, 5), 0);
  cr_assert_eq(utf8_prefix_bytes_for_width("abc", 0, 5), 0);
  cr_assert_eq(utf8_prefix_bytes_for_width("abc", 3, 0), 0);
  cr_assert_eq(utf8_prefix_bytes_for_width("abc", 3, -1), 0);
  cr_assert_eq(utf8_prefix_bytes_for_width("a\0b", 3, 5), 1);
}

Test(utf8, complete_csi_and_trailing_marks) {
  const char *text = "\033[31me\xcc\x81\033[0m\033[2Kx";
  size_t prefix = strlen(text) - 1;
  cr_assert_eq(utf8_prefix_bytes_for_width(text, strlen(text), 1), prefix);
  cr_assert_eq(utf8_display_width_n(text, prefix), 1);
  cr_assert_eq(utf8_display_width(text), 2);
  cr_assert_eq(utf8_prefix_bytes_for_width("\033[31", 4, 1), 0);
  cr_assert_eq(utf8_prefix_bytes_for_width("a\033", 2, 5), 1);
  cr_assert_eq(utf8_prefix_bytes_for_width("a\033[", 3, 5), 1);
  cr_assert_eq(utf8_prefix_bytes_for_width("a\033[31\0x", 8, 5), 1);
  cr_assert_eq(utf8_prefix_bytes_for_width("\033[0mx", 5, 0), 4);
}

Test(utf8, ellipsis_width_and_byte_limits) {
  char output[64];
  truncate_with_ellipsis_mode("é中😀x", output, sizeof(output), 4, true);
  cr_assert_str_eq(output, "é中\033[0m…");
  truncate_with_ellipsis_mode("e\xcc\x81x", output, sizeof(output), 1, true);
  cr_assert_str_eq(output, "\033[0m…");
  truncate_with_ellipsis_mode("é中", output, sizeof(output), 3, true);
  cr_assert_str_eq(output, "é中");
  truncate_with_ellipsis_mode("abc", output, sizeof(output), 0, true);
  cr_assert_str_eq(output, "");
  truncate_with_ellipsis_mode("abc", output, sizeof(output), -1, true);
  cr_assert_str_eq(output, "");
  truncate_with_ellipsis_mode("a\033[31", output, sizeof(output), 20, true);
  cr_assert_str_eq(output, "a\033[0m…");
  truncate_with_ellipsis_mode("中中中中", output, 10, 20, true);
  cr_assert_str_eq(output, "\033[0m…");
  truncate_with_ellipsis_mode("\033[31mabcdef", output, 11, 20, true);
  cr_assert_str_eq(output, "\033[0m…");
}

Test(utf8, ellipsis_uses_ascii_when_utf8_is_disabled) {
  char output[64];
  truncate_with_ellipsis_mode("abcdef", output, sizeof(output), 4, false);
  cr_assert_str_eq(output, "a\033[0m...");
  cr_assert_eq(utf8_display_width(output), 4);
  truncate_with_ellipsis_mode("abcdef", output, sizeof(output), 4, true);
  cr_assert_str_eq(output, "abc\033[0m…");
  cr_assert_eq(utf8_display_width(output), 4);
  truncate_with_ellipsis_mode("abcdef", output, sizeof(output), 2, false);
  cr_assert_str_eq(output, "");
}

Test(utf8, ellipsis_small_buffers) {
  unsigned char guarded[18];
  for (size_t size = 0; size <= 16; ++size) {
    memset(guarded, 0xa5, sizeof(guarded));
    char *output = (char *)guarded + 1;
    truncate_with_ellipsis_mode("é中😀abcdef", output, size, 3, true);
    cr_assert_eq(guarded[0], 0xa5);
    cr_assert_eq(guarded[size + 1], 0xa5);
    if (size > 0) {
      cr_assert_not_null(memchr(output, 0, size));
      cr_assert(utf8_is_valid(output));
      cr_assert_leq(utf8_display_width(output), 3);
      if (size < 8) {
        cr_assert_str_eq(output, "");
      }
    }
  }
  char tiny[3];
  truncate_with_ellipsis_mode("é", tiny, sizeof(tiny), 1, true);
  cr_assert_str_eq(tiny, "é");
}

Test(utf8, repeat_sequences_count_cells_without_splitting) {
  const char *text = "é\033[3bX";
  cr_assert_eq(utf8_display_width(text), 5);
  cr_assert_eq(utf8_prefix_bytes_for_width(text, strlen(text), 4), strlen(text) - 1);
  cr_assert_eq(utf8_prefix_bytes_for_width(text, strlen(text), 3), strlen("é"));
  cr_assert_eq(utf8_display_width("界\033[2b"), 6);
  cr_assert_eq(utf8_prefix_bytes_for_width("界\033[2b", strlen("界\033[2b"), 5), strlen("界"));
  cr_assert_eq(utf8_display_width("x\033[b"), 2);
  cr_assert_eq(utf8_display_width("x\033[0b"), 2);
  cr_assert_eq(utf8_prefix_bytes_for_width("x\033[42949672960b", strlen("x\033[42949672960b"), 20), 1);
  cr_assert_eq(utf8_prefix_bytes_for_width("x\033[1;2b", strlen("x\033[1;2b"), 20), 1);
}
