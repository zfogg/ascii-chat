#include <criterion/criterion.h>
#include <ascii-chat/common.h>
#include <ascii-chat/video/ascii/zalgo.h>
#include <string.h>
#include <ascii-chat/ui/controller.h>

Test(zalgo, respects_output_budget) {
  char *output = NULL;
  cr_assert_eq(zalgo_apply("@", 1, 10, SIZE_MAX, &output), ASCIICHAT_OK);
  size_t exact_size = strlen(output);
  SAFE_FREE(output);
  cr_assert_eq(zalgo_apply("@", 1, 10, exact_size, &output), ASCIICHAT_OK);
  SAFE_FREE(output);
  cr_assert_neq(zalgo_apply("@", 1, 10, exact_size - 1, &output), ASCIICHAT_OK);
  cr_assert_null(output);
  cr_assert_neq(zalgo_apply("abc", 3, 1, 2, &output), ASCIICHAT_OK);
  cr_assert_null(output);
}

Test(zalgo, rejects_dense_frame_exceeding_presenter_budget) {
  size_t length = 1024 * 1024 - 1;
  char *input = SAFE_MALLOC(length + 1, char *);
  memset(input, '@', length);
  input[length] = '\0';
  char *output = NULL;
  cr_assert_neq(zalgo_apply(input, length, 10, UI_CONTROLLER_MAX_FRAME_BYTES - 7, &output), ASCIICHAT_OK);
  cr_assert_null(output);
  SAFE_FREE(input);
}

Test(zalgo, preserves_whitespace_unicode_and_controls) {
  const char *source = " \t\r\n\xc3\xa9\x1b[31m\x1b]0;title\a\x1b[0m";
  char *output = NULL;
  cr_assert_eq(zalgo_apply(source, strlen(source), 10, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_str_eq(output, source);
  SAFE_FREE(output);
}

Test(zalgo, expands_rep_before_decoration) {
  char *compressed = NULL, *plain = NULL;
  const char *source = "A\x1b[19b\n";
  const char *expanded = "AAAAAAAAAAAAAAAAAAAA\n";
  cr_assert_eq(zalgo_apply(source, strlen(source), 10, SIZE_MAX, &compressed), ASCIICHAT_OK);
  cr_assert_eq(zalgo_apply(expanded, strlen(expanded), 10, SIZE_MAX, &plain), ASCIICHAT_OK);
  cr_assert_str_eq(compressed, plain);
  SAFE_FREE(compressed);
  SAFE_FREE(plain);
}

Test(zalgo, rejects_invalid_levels) {
  char *output = NULL;
  cr_assert_neq(zalgo_apply("abc", 3, 0, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_null(output);
  cr_assert_neq(zalgo_apply("abc", 3, 11, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_null(output);
}

Test(zalgo, rejects_unbounded_rep) {
  const char *source = "A\x1b[4294967295b";
  char *output = NULL;
  cr_assert_neq(zalgo_apply(source, strlen(source), 10, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_null(output);
  source = "A\x1b[999999999999999999999b";
  cr_assert_neq(zalgo_apply(source, strlen(source), 10, SIZE_MAX, &output), ASCIICHAT_OK);
  cr_assert_null(output);
}

Test(zalgo, deterministic_bounded_and_increasing) {
  char input[1001];
  memset(input, '@', 1000);
  input[1000] = '\0';
  size_t previous = 1000;
  for (int level = 1; level <= 10; level++) {
    char *first = NULL, *second = NULL;
    cr_assert_eq(zalgo_apply(input, 1000, level, SIZE_MAX, &first), ASCIICHAT_OK);
    cr_assert_eq(zalgo_apply(input, 1000, level, SIZE_MAX, &second), ASCIICHAT_OK);
    cr_assert_str_eq(first, second);
    size_t length = strlen(first);
    cr_assert_geq(length, previous);
    cr_assert_leq(length, 21000);
    previous = length;
    size_t bases = 0, marks = 0;
    for (size_t i = 0; i < length; i++) {
      if (first[i] == '@') {
        cr_assert_leq(marks, 10);
        bases++;
        marks = 0;
      } else {
        cr_assert_eq((unsigned char)first[i], 0xcc);
        cr_assert_lt(++i, length);
        cr_assert_geq((unsigned char)first[i], 0x80);
        cr_assert_leq((unsigned char)first[i], 0xbf);
        marks++;
      }
    }
    cr_assert_eq(bases, 1000);
    cr_assert_leq(marks, 10);
    SAFE_FREE(first);
    SAFE_FREE(second);
  }
}
