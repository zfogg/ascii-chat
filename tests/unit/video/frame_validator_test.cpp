#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/video/ascii/frame_validator.h>
#include <stdint.h>
#include <string.h>

TEST_CASE("ascii_frame_validation/accepts_bounded_nonterminated_frames_and_uses_last_reset", "[ascii_frame_validation]") {
  const char raw[] = {'X', '\033', '[', '0', 'm'};
  ASCIICHAT_REQUIRE(frame_validate_integrity(raw, sizeof(raw)));
  ASCIICHAT_REQUIRE_EQ(frame_get_valid_end(raw, sizeof(raw)), sizeof(raw));
  const char colored[] = "\033[0mred\033[31mword\033[0m";
  ASCIICHAT_REQUIRE(frame_validate_integrity(colored, sizeof(colored) - 1));
  ASCIICHAT_REQUIRE_EQ(frame_get_valid_end(colored, sizeof(colored) - 1), sizeof(colored) - 1);
  ASCIICHAT_REQUIRE_NOT(frame_validate_integrity(colored, sizeof(colored)), "terminator is not part of the wire frame");
}

TEST_CASE("ascii_frame_validation/rejects_missing_or_truncated_resets_and_trailing_garbage", "[ascii_frame_validation]") {
  ASCIICHAT_REQUIRE_NOT(frame_validate_integrity(NULL, 10));
  ASCIICHAT_REQUIRE_NOT(frame_validate_integrity("", 0));
  ASCIICHAT_REQUIRE_NOT(frame_validate_integrity("abc", 3));
  ASCIICHAT_REQUIRE_NOT(frame_validate_integrity("abcdef", 6));
  ASCIICHAT_REQUIRE_NOT(frame_validate_integrity("x\033[0", 4));
  ASCIICHAT_REQUIRE_EQ(frame_get_valid_end(NULL, 10), 10);
  ASCIICHAT_REQUIRE_EQ(frame_get_valid_end("ab", 2), 2);
  ASCIICHAT_REQUIRE_EQ(frame_get_valid_end("abcdef", 6), 6);
  const char with_garbage[] = "text\033[0mBAD!";
  for (size_t extra = 1; extra <= 4; ++extra) {
    ASCIICHAT_REQUIRE_NOT(frame_validate_integrity(with_garbage, 8 + extra));
    ASCIICHAT_REQUIRE_EQ(frame_get_valid_end(with_garbage, 8 + extra), 8);
  }
}
