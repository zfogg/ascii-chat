#include <criterion/criterion.h>
#include <ascii-chat/video/ascii/frame_validator.h>
#include <stdint.h>
#include <string.h>

Test(ascii_frame_validation, accepts_bounded_nonterminated_frames_and_uses_last_reset) {
  const char raw[] = {'X', '\033', '[', '0', 'm'};
  cr_assert(frame_validate_integrity(raw, sizeof(raw)));
  cr_assert_eq(frame_get_valid_end(raw, sizeof(raw)), sizeof(raw));
  const char colored[] = "\033[0mred\033[31mword\033[0m";
  cr_assert(frame_validate_integrity(colored, sizeof(colored) - 1));
  cr_assert_eq(frame_get_valid_end(colored, sizeof(colored) - 1), sizeof(colored) - 1);
  cr_assert_not(frame_validate_integrity(colored, sizeof(colored)), "terminator is not part of the wire frame");
}

Test(ascii_frame_validation, rejects_missing_or_truncated_resets_and_trailing_garbage) {
  cr_assert_not(frame_validate_integrity(NULL, 10));
  cr_assert_not(frame_validate_integrity("", 0));
  cr_assert_not(frame_validate_integrity("abc", 3));
  cr_assert_not(frame_validate_integrity("abcdef", 6));
  cr_assert_not(frame_validate_integrity("x\033[0", 4));
  cr_assert_eq(frame_get_valid_end(NULL, 10), 10);
  cr_assert_eq(frame_get_valid_end("ab", 2), 2);
  cr_assert_eq(frame_get_valid_end("abcdef", 6), 6);
  const char with_garbage[] = "text\033[0mBAD!";
  for (size_t extra = 1; extra <= 4; ++extra) {
    cr_assert_not(frame_validate_integrity(with_garbage, 8 + extra));
    cr_assert_eq(frame_get_valid_end(with_garbage, 8 + extra), 8);
  }
}
