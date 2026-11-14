#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/frame_validator.h>
#include <string.h>

TEST_CASE("frame_validation/legacy_lengths_and_overflow_boundaries", "[frame_validation]") {
  ASCIICHAT_REQUIRE_EQ(frame_validate_legacy(20, 12), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(frame_validate_legacy(7, 12), ERROR_INVALID_FRAME);
  ASCIICHAT_REQUIRE_EQ(frame_validate_legacy(19, 12), ERROR_INVALID_FRAME);
  ASCIICHAT_REQUIRE_EQ(frame_validate_legacy(21, 12), ERROR_INVALID_FRAME);
  ASCIICHAT_REQUIRE_EQ(frame_validate_legacy(20, SIZE_MAX), ERROR_BUFFER_OVERFLOW);
  ASCIICHAT_REQUIRE_EQ(frame_check_size_overflow(0, SIZE_MAX), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(frame_check_size_overflow(SIZE_MAX, 0), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(frame_check_size_overflow(8, SIZE_MAX - 8), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(frame_check_size_overflow(8, SIZE_MAX - 7), ERROR_BUFFER_OVERFLOW);
}

TEST_CASE("frame_validation/network_order_fields_work_with_unaligned_headers", "[frame_validation]") {
  // A four-pixel RGB row: width=4, height=1, uncompressed, twelve payload bytes.
  const uint8_t wire[] = {0, 0,  0,   4, 0, 0, 0,   1, 0, 0, 0,   0,   0,   0,
                          0, 12, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
  uint8_t unaligned[sizeof(wire) + 1];
  memcpy(unaligned + 1, wire, sizeof(wire));
  uint32_t width = 0, height = 0, size = 0, flag = 99;
  bool compressed = true;
  frame_extract_dimensions(unaligned + 1, &width, &height);
  ASCIICHAT_REQUIRE_EQ(width, 4);
  ASCIICHAT_REQUIRE_EQ(height, 1);
  frame_extract_new_header(unaligned + 1, &flag, &size);
  ASCIICHAT_REQUIRE_EQ(flag, 0);
  ASCIICHAT_REQUIRE_EQ(size, 12);
  ASCIICHAT_REQUIRE_EQ(frame_validate_new(unaligned + 1, sizeof(wire), &compressed, &size), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(compressed);
  ASCIICHAT_REQUIRE_EQ(size, 12);
  unaligned[1 + 11] = 1;
  ASCIICHAT_REQUIRE_EQ(frame_validate_new(unaligned + 1, sizeof(wire), &compressed, &size), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(compressed);
  ASCIICHAT_REQUIRE_EQ(frame_validate_new(unaligned + 1, sizeof(wire), NULL, &size), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(frame_validate_new(unaligned + 1, sizeof(wire), &compressed, NULL), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(frame_validate_new(unaligned + 1, sizeof(wire), NULL, NULL), ASCIICHAT_OK);
}

TEST_CASE("frame_validation/rejects_truncated_oversized_and_inconsistent_payloads_without_publishing_outputs", "[frame_validation]") {
  uint8_t header[16] = {0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 12};
  bool compressed = false;
  uint32_t size = 987;
  for (size_t length = 0; length < sizeof(header); ++length)
    ASCIICHAT_REQUIRE_EQ(frame_validate_new(header, length, &compressed, &size), ERROR_INVALID_FRAME);
  ASCIICHAT_REQUIRE_EQ(frame_validate_new(header, 27, &compressed, &size), ERROR_INVALID_FRAME);
  ASCIICHAT_REQUIRE_EQ(frame_validate_new(header, 29, &compressed, &size), ERROR_INVALID_FRAME);
  memset(header + 12, 0xff, 4);
  ASCIICHAT_REQUIRE_EQ(frame_validate_new(header, 16, &compressed, &size), ERROR_INVALID_FRAME);
  ASCIICHAT_REQUIRE_NOT(compressed);
  ASCIICHAT_REQUIRE_EQ(size, 987);
}
