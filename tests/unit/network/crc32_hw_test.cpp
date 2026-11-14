#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <ascii-chat/network/crc32.h>
/* ============================================================================
 * Basic CRC32 Computation Tests
 * ============================================================================ */

TEST_CASE("crc32_hw/empty_data", "[crc32_hw]") {
  // CRC32 of empty data should be a specific value
  uint32_t crc_hw = asciichat_crc32_hw(NULL, 0);
  uint32_t crc_sw = asciichat_crc32_sw(NULL, 0);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of empty data should match");
  ASCIICHAT_REQUIRE_EQ(crc_hw, 0, "CRC32 of empty data should be 0");
}

TEST_CASE("crc32_hw/single_byte", "[crc32_hw]") {
  uint8_t data = 0x42;
  uint32_t crc_hw = asciichat_crc32_hw(&data, 1);
  uint32_t crc_sw = asciichat_crc32_sw(&data, 1);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of single byte should match");
  ASCIICHAT_REQUIRE_NE(crc_hw, 0, "CRC32 of single byte should not be 0");
}

TEST_CASE("crc32_hw/simple_string", "[crc32_hw]") {
  const char *test_str = "Hello, World!";
  uint32_t crc_hw = asciichat_crc32_hw(test_str, strlen(test_str));
  uint32_t crc_sw = asciichat_crc32_sw(test_str, strlen(test_str));

  log_debug("CRC32 test for 'Hello, World!' (len=%zu):", strlen(test_str));
  log_debug("  Hardware CRC: 0x%08x", crc_hw);
  log_debug("  Software CRC: 0x%08x", crc_sw);
  log_debug("  HW available: %s", crc32_hw_is_available() ? "YES" : "NO");

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 'Hello, World!' should match");

  // Known CRC32-C (Castagnoli) value for "Hello, World!"
  // Note: This uses the CRC32-C polynomial (0x1EDC6F41), not IEEE 802.3
  // This matches hardware implementations (__crc32* on ARM, _mm_crc32_* on x86)
  uint32_t expected = 0x4d551068;
  log_debug("  Expected CRC: 0x%08x", expected);
  ASCIICHAT_REQUIRE_EQ(crc_hw, expected, "CRC32-C of 'Hello, World!' should be 0x%08x", expected);
}

TEST_CASE("crc32_hw/ascii_chat_string", "[crc32_hw]") {
  const char *test_str = "ascii-chat";
  uint32_t crc_hw = asciichat_crc32_hw(test_str, strlen(test_str));
  uint32_t crc_sw = asciichat_crc32_sw(test_str, strlen(test_str));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 should match for 'ascii-chat'");
}

TEST_CASE("crc32_hw/binary_data", "[crc32_hw]") {
  uint8_t data[256];
  for (int i = 0; i < 256; i++) {
    data[i] = (uint8_t)i;
  }

  uint32_t crc_hw = asciichat_crc32_hw(data, sizeof(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, sizeof(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of binary data should match");
}

TEST_CASE("crc32_hw/all_zeros", "[crc32_hw]") {
  uint8_t data[128];
  memset(data, 0, sizeof(data));

  uint32_t crc_hw = asciichat_crc32_hw(data, sizeof(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, sizeof(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of all zeros should match");
}

TEST_CASE("crc32_hw/all_ones", "[crc32_hw]") {
  uint8_t data[128];
  memset(data, 0xFF, sizeof(data));

  uint32_t crc_hw = asciichat_crc32_hw(data, sizeof(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, sizeof(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of all ones should match");
}

/* ============================================================================
 * Size Alignment Tests
 * ============================================================================ */

TEST_CASE("crc32_hw/size_7_bytes", "[crc32_hw]") {
  // Test odd size (not aligned to 8 bytes)
  const char *data = "1234567";
  uint32_t crc_hw = asciichat_crc32_hw(data, 7);
  uint32_t crc_sw = asciichat_crc32_sw(data, 7);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 7 bytes should match");
}

TEST_CASE("crc32_hw/size_8_bytes", "[crc32_hw]") {
  // Test 8-byte aligned size
  const char *data = "12345678";
  uint32_t crc_hw = asciichat_crc32_hw(data, 8);
  uint32_t crc_sw = asciichat_crc32_sw(data, 8);

  log_debug("CRC32 test for 8-byte aligned data:");
  log_debug("  Data: '%s'", data);
  log_debug("  Hardware CRC: 0x%08x", crc_hw);
  log_debug("  Software CRC: 0x%08x", crc_sw);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 8 bytes should match");
}

TEST_CASE("crc32_hw/size_9_bytes", "[crc32_hw]") {
  // Test 8 bytes + 1
  const char *data = "123456789";
  uint32_t crc_hw = asciichat_crc32_hw(data, 9);
  uint32_t crc_sw = asciichat_crc32_sw(data, 9);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 9 bytes should match");
}

TEST_CASE("crc32_hw/size_16_bytes", "[crc32_hw]") {
  // Test 2x 8-byte aligned
  const char *data = "0123456789ABCDEF";
  uint32_t crc_hw = asciichat_crc32_hw(data, 16);
  uint32_t crc_sw = asciichat_crc32_sw(data, 16);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 16 bytes should match");
}

TEST_CASE("crc32_hw/size_17_bytes", "[crc32_hw]") {
  // Test 2x 8 bytes + 1
  const char *data = "0123456789ABCDEFG";
  uint32_t crc_hw = asciichat_crc32_hw(data, 17);
  uint32_t crc_sw = asciichat_crc32_sw(data, 17);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 17 bytes should match");
}

TEST_CASE("crc32_hw/size_1_byte", "[crc32_hw]") {
  const char *data = "A";
  uint32_t crc_hw = asciichat_crc32_hw(data, 1);
  uint32_t crc_sw = asciichat_crc32_sw(data, 1);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 1 byte should match");
}

TEST_CASE("crc32_hw/size_2_bytes", "[crc32_hw]") {
  const char *data = "AB";
  uint32_t crc_hw = asciichat_crc32_hw(data, 2);
  uint32_t crc_sw = asciichat_crc32_sw(data, 2);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 2 bytes should match");
}

TEST_CASE("crc32_hw/size_3_bytes", "[crc32_hw]") {
  const char *data = "ABC";
  uint32_t crc_hw = asciichat_crc32_hw(data, 3);
  uint32_t crc_sw = asciichat_crc32_sw(data, 3);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 3 bytes should match");
}

TEST_CASE("crc32_hw/size_4_bytes", "[crc32_hw]") {
  const char *data = "ABCD";
  uint32_t crc_hw = asciichat_crc32_hw(data, 4);
  uint32_t crc_sw = asciichat_crc32_sw(data, 4);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 4 bytes should match");
}

TEST_CASE("crc32_hw/size_5_bytes", "[crc32_hw]") {
  const char *data = "ABCDE";
  uint32_t crc_hw = asciichat_crc32_hw(data, 5);
  uint32_t crc_sw = asciichat_crc32_sw(data, 5);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 5 bytes should match");
}

TEST_CASE("crc32_hw/size_6_bytes", "[crc32_hw]") {
  const char *data = "ABCDEF";
  uint32_t crc_hw = asciichat_crc32_hw(data, 6);
  uint32_t crc_sw = asciichat_crc32_sw(data, 6);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 6 bytes should match");
}

/* ============================================================================
 * Large Data Tests
 * ============================================================================ */

TEST_CASE("crc32_hw/large_buffer_1kb", "[crc32_hw]") {
  uint8_t data[1024];
  for (size_t i = 0; i < sizeof(data); i++) {
    data[i] = (uint8_t)(i & 0xFF);
  }

  uint32_t crc_hw = asciichat_crc32_hw(data, sizeof(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, sizeof(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 1KB should match");
}

TEST_CASE("crc32_hw/large_buffer_4kb", "[crc32_hw]") {
  uint8_t *data;
  data = SAFE_MALLOC(4096, uint8_t *);
  for (size_t i = 0; i < 4096; i++) {
    data[i] = (uint8_t)(i & 0xFF);
  }

  uint32_t crc_hw = asciichat_crc32_hw(data, 4096);
  uint32_t crc_sw = asciichat_crc32_sw(data, 4096);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 4KB should match");

  SAFE_FREE(data);
}

TEST_CASE("crc32_hw/large_buffer_64kb", "[crc32_hw]") {
  size_t size = 65536;
  uint8_t *data;
  data = SAFE_MALLOC(size, uint8_t *);
  for (size_t i = 0; i < size; i++) {
    data[i] = (uint8_t)(i & 0xFF);
  }

  uint32_t crc_hw = asciichat_crc32_hw(data, size);
  uint32_t crc_sw = asciichat_crc32_sw(data, size);

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of 64KB should match");

  SAFE_FREE(data);
}

/* ============================================================================
 * Data Variation Tests
 * ============================================================================ */

TEST_CASE("crc32_hw/different_data_different_crc", "[crc32_hw]") {
  const char *data1 = "Hello, World!";
  const char *data2 = "Hello, World?";

  uint32_t crc1 = asciichat_crc32_hw(data1, strlen(data1));
  uint32_t crc2 = asciichat_crc32_hw(data2, strlen(data2));

  ASCIICHAT_REQUIRE_NE(crc1, crc2, "Different data should produce different CRC32");
}

TEST_CASE("crc32_hw/same_data_same_crc", "[crc32_hw]") {
  const char *data1 = "Testing CRC32";
  const char *data2 = "Testing CRC32";

  uint32_t crc1 = asciichat_crc32_hw(data1, strlen(data1));
  uint32_t crc2 = asciichat_crc32_hw(data2, strlen(data2));

  ASCIICHAT_REQUIRE_EQ(crc1, crc2, "Same data should produce same CRC32");
}

TEST_CASE("crc32_hw/order_matters", "[crc32_hw]") {
  uint8_t data1[] = {0x01, 0x02, 0x03, 0x04};
  uint8_t data2[] = {0x04, 0x03, 0x02, 0x01};

  uint32_t crc1 = asciichat_crc32_hw(data1, sizeof(data1));
  uint32_t crc2 = asciichat_crc32_hw(data2, sizeof(data2));

  ASCIICHAT_REQUIRE_NE(crc1, crc2, "Byte order should affect CRC32");
}

/* ============================================================================
 * Hardware Availability Tests
 * ============================================================================ */

TEST_CASE("crc32_hw/hardware_availability_check", "[crc32_hw]") {
  bool hw_available = crc32_hw_is_available();

  // Should not crash
  ASCIICHAT_REQUIRE(true, "Hardware availability check should not crash");

  // Log the result for debugging
  if (hw_available) {
    ASCIICHAT_INFO("CRC32 hardware acceleration is available");
  } else {
    ASCIICHAT_INFO("CRC32 hardware acceleration is NOT available - using software fallback");
  }
}

TEST_CASE("crc32_hw/repeated_hw_check", "[crc32_hw]") {
  // Check multiple times - should return consistent result
  bool hw1 = crc32_hw_is_available();
  bool hw2 = crc32_hw_is_available();
  bool hw3 = crc32_hw_is_available();

  ASCIICHAT_REQUIRE_EQ(hw1, hw2, "Hardware availability should be consistent");
  ASCIICHAT_REQUIRE_EQ(hw2, hw3, "Hardware availability should be consistent");
}

/* ============================================================================
 * Macro Tests
 * ============================================================================ */

TEST_CASE("crc32_hw/macro_dispatches_correctly", "[crc32_hw]") {
  const char *test_str = "Macro test";
  uint32_t crc_macro = asciichat_crc32(test_str, strlen(test_str));
  uint32_t crc_hw = asciichat_crc32_hw(test_str, strlen(test_str));

  ASCIICHAT_REQUIRE_EQ(crc_macro, crc_hw, "Macro should dispatch to hardware function");
}

/* ============================================================================
 * Edge Cases
 * ============================================================================ */

TEST_CASE("crc32_hw/special_characters", "[crc32_hw]") {
  const char *data = "!@#$%^&*()_+-=[]{}|;':\",./<>?";
  uint32_t crc_hw = asciichat_crc32_hw(data, strlen(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, strlen(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of special chars should match");
}

TEST_CASE("crc32_hw/unicode_data", "[crc32_hw]") {
  const char *data = "café résumé naïve";
  uint32_t crc_hw = asciichat_crc32_hw(data, strlen(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, strlen(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of unicode should match");
}

TEST_CASE("crc32_hw/null_bytes_in_data", "[crc32_hw]") {
  uint8_t data[] = {0x01, 0x00, 0x02, 0x00, 0x03, 0x00, 0x04};
  uint32_t crc_hw = asciichat_crc32_hw(data, sizeof(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, sizeof(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 with null bytes should match");
}

TEST_CASE("crc32_hw/repeating_pattern", "[crc32_hw]") {
  uint8_t data[256];
  for (size_t i = 0; i < sizeof(data); i++) {
    data[i] = 0xAA;
  }

  uint32_t crc_hw = asciichat_crc32_hw(data, sizeof(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, sizeof(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of repeating pattern should match");
}

TEST_CASE("crc32_hw/alternating_pattern", "[crc32_hw]") {
  uint8_t data[256];
  for (size_t i = 0; i < sizeof(data); i++) {
    data[i] = (i % 2) ? 0xFF : 0x00;
  }

  uint32_t crc_hw = asciichat_crc32_hw(data, sizeof(data));
  uint32_t crc_sw = asciichat_crc32_sw(data, sizeof(data));

  ASCIICHAT_REQUIRE_EQ(crc_hw, crc_sw, "Hardware and software CRC32 of alternating pattern should match");
}

/* ============================================================================
 * Consistency Tests - Multiple Calls
 * ============================================================================ */

TEST_CASE("crc32_hw/consistent_results", "[crc32_hw]") {
  const char *data = "Consistency test";

  uint32_t crc1 = asciichat_crc32_hw(data, strlen(data));
  uint32_t crc2 = asciichat_crc32_hw(data, strlen(data));
  uint32_t crc3 = asciichat_crc32_hw(data, strlen(data));

  ASCIICHAT_REQUIRE_EQ(crc1, crc2, "Multiple CRC32 calls should return same result");
  ASCIICHAT_REQUIRE_EQ(crc2, crc3, "Multiple CRC32 calls should return same result");
}

TEST_CASE("crc32_hw/sw_consistent_results", "[crc32_hw]") {
  const char *data = "Software consistency test";

  uint32_t crc1 = asciichat_crc32_sw(data, strlen(data));
  uint32_t crc2 = asciichat_crc32_sw(data, strlen(data));
  uint32_t crc3 = asciichat_crc32_sw(data, strlen(data));

  ASCIICHAT_REQUIRE_EQ(crc1, crc2, "Multiple software CRC32 calls should return same result");
  ASCIICHAT_REQUIRE_EQ(crc2, crc3, "Multiple software CRC32 calls should return same result");
}
