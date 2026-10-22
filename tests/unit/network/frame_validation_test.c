#include <criterion/criterion.h>
#include <ascii-chat/network/frame_validator.h>
#include <string.h>

Test(frame_validation, legacy_lengths_and_overflow_boundaries) {
  cr_assert_eq(frame_validate_legacy(20, 12), ASCIICHAT_OK);
  cr_assert_eq(frame_validate_legacy(7, 12), ERROR_INVALID_FRAME);
  cr_assert_eq(frame_validate_legacy(19, 12), ERROR_INVALID_FRAME);
  cr_assert_eq(frame_validate_legacy(21, 12), ERROR_INVALID_FRAME);
  cr_assert_eq(frame_validate_legacy(20, SIZE_MAX), ERROR_BUFFER_OVERFLOW);
  cr_assert_eq(frame_check_size_overflow(0, SIZE_MAX), ASCIICHAT_OK);
  cr_assert_eq(frame_check_size_overflow(SIZE_MAX, 0), ASCIICHAT_OK);
  cr_assert_eq(frame_check_size_overflow(8, SIZE_MAX - 8), ASCIICHAT_OK);
  cr_assert_eq(frame_check_size_overflow(8, SIZE_MAX - 7), ERROR_BUFFER_OVERFLOW);
}

Test(frame_validation, network_order_fields_work_with_unaligned_headers) {
  // A four-pixel RGB row: width=4, height=1, uncompressed, twelve payload bytes.
  const uint8_t wire[] = {0, 0,  0,   4, 0, 0, 0,   1, 0, 0, 0,   0,   0,   0,
                          0, 12, 255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
  uint8_t unaligned[sizeof(wire) + 1];
  memcpy(unaligned + 1, wire, sizeof(wire));
  uint32_t width = 0, height = 0, size = 0, flag = 99;
  bool compressed = true;
  frame_extract_dimensions(unaligned + 1, &width, &height);
  cr_assert_eq(width, 4);
  cr_assert_eq(height, 1);
  frame_extract_new_header(unaligned + 1, &flag, &size);
  cr_assert_eq(flag, 0);
  cr_assert_eq(size, 12);
  cr_assert_eq(frame_validate_new(unaligned + 1, sizeof(wire), &compressed, &size), ASCIICHAT_OK);
  cr_assert_not(compressed);
  cr_assert_eq(size, 12);
  unaligned[1 + 11] = 1;
  cr_assert_eq(frame_validate_new(unaligned + 1, sizeof(wire), &compressed, &size), ASCIICHAT_OK);
  cr_assert(compressed);
  cr_assert_eq(frame_validate_new(unaligned + 1, sizeof(wire), NULL, &size), ASCIICHAT_OK);
  cr_assert_eq(frame_validate_new(unaligned + 1, sizeof(wire), &compressed, NULL), ASCIICHAT_OK);
  cr_assert_eq(frame_validate_new(unaligned + 1, sizeof(wire), NULL, NULL), ASCIICHAT_OK);
}

Test(frame_validation, rejects_truncated_oversized_and_inconsistent_payloads_without_publishing_outputs) {
  uint8_t header[16] = {0, 0, 0, 4, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 12};
  bool compressed = false;
  uint32_t size = 987;
  for (size_t length = 0; length < sizeof(header); ++length)
    cr_assert_eq(frame_validate_new(header, length, &compressed, &size), ERROR_INVALID_FRAME);
  cr_assert_eq(frame_validate_new(header, 27, &compressed, &size), ERROR_INVALID_FRAME);
  cr_assert_eq(frame_validate_new(header, 29, &compressed, &size), ERROR_INVALID_FRAME);
  memset(header + 12, 0xff, 4);
  cr_assert_eq(frame_validate_new(header, 16, &compressed, &size), ERROR_INVALID_FRAME);
  cr_assert_not(compressed);
  cr_assert_eq(size, 987);
}
