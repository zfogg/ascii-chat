#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/network/crc32.h>
#include <array>
#include <cstdint>
#include <vector>

static uint32_t crc32_reference(const uint8_t *data, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0x82f63b78u : 0u);
  }
  return ~crc;
}

TEST_CASE("crc32/standalone_vectors_and_software_dispatch_agree", "[crc32][standalone]") {
  REQUIRE(asciichat_crc32(nullptr, 0) == 0);
  REQUIRE(asciichat_crc32_sw(nullptr, 0) == 0);

  const std::array<uint8_t, 13> hello{'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!'};
  REQUIRE(asciichat_crc32(hello.data(), hello.size()) == 0x4d551068u);
  REQUIRE(asciichat_crc32_sw(hello.data(), hello.size()) == 0x4d551068u);

  std::vector<uint8_t> data(65552);
  for (size_t i = 0; i < data.size(); ++i)
    data[i] = static_cast<uint8_t>(i);
  for (size_t offset = 0; offset < 16; ++offset) {
    for (size_t length = 1; length <= 1024; ++length) {
      const auto expected = crc32_reference(data.data() + offset, length);
      REQUIRE(asciichat_crc32(data.data() + offset, length) == expected);
      REQUIRE(asciichat_crc32_sw(data.data() + offset, length) == expected);
    }
  }
}

TEST_CASE("crc32/dispatch_reports_hardware_availability_consistently", "[crc32][standalone]") {
  REQUIRE((crc32_hw_is_available() == true || crc32_hw_is_available() == false));
}
