#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <ascii-chat/util/ip.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>

// Use quiet logging for tests
TEST_CASE("ip_utils/mapped_addresses_round_trip_and_ipv6_formatting_rejects_invalid_inputs", "[ip_utils]") {
  char mapped[64], ipv4[16], expanded[64], compact[64];
  const char *addresses[] = {"0.0.0.0", "127.0.0.1", "192.0.2.17", "255.255.255.255"};
  for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
    ASCIICHAT_REQUIRE_EQ(ipv4_to_ipv6_mapped(addresses[i], mapped, sizeof(mapped)), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(is_ipv4_mapped_ipv6(mapped));
    ASCIICHAT_REQUIRE_EQ(extract_ipv4_from_mapped_ipv6(mapped, ipv4, sizeof(ipv4)), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(ipv4, addresses[i]);
  }
  ASCIICHAT_REQUIRE_EQ(expand_ipv6("[2001:DB8::1]", expanded, sizeof(expanded)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(expanded, "2001:0db8:0000:0000:0000:0000:0000:0001");
  ASCIICHAT_REQUIRE_EQ(compact_ipv6(expanded, compact, sizeof(compact)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(compact, "2001:db8::1");
  ASCIICHAT_REQUIRE_EQ(canonicalize_ipv6("2001:db8::1", compact, 2), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(extract_ipv4_from_mapped_ipv6("::ffff:192.0.2.17", ipv4, 2), ERROR_NETWORK);
  ASCIICHAT_REQUIRE_EQ(ipv4_to_ipv6_mapped(NULL, mapped, sizeof(mapped)), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(ipv4_to_ipv6_mapped("256.0.0.1", mapped, sizeof(mapped)), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(extract_ipv4_from_mapped_ipv6(NULL, ipv4, sizeof(ipv4)), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(extract_ipv4_from_mapped_ipv6("2001:db8::1", ipv4, sizeof(ipv4)), ERROR_INVALID_PARAM);
  const char *invalid[] = {NULL, "[bad", "not-an-ip"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    ASCIICHAT_REQUIRE_EQ(expand_ipv6(invalid[i], expanded, sizeof(expanded)), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_EQ(canonicalize_ipv6(invalid[i], compact, sizeof(compact)), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_NOT(is_ipv4_mapped_ipv6(invalid[i]));
  }
}

TEST_CASE("ip_utils/discovery_rejects_special_use_addresses_as_public_endpoints", "[ip_utils]") {
  const char *non_public_v4[] = {
      "10.0.0.1",   "172.31.255.255", "192.168.1.1",  "127.0.0.2",       "169.254.1.2", "255.255.255.255",
      "224.0.0.1",  "0.1.2.3",        "100.64.0.1",   "100.127.255.255", "192.0.0.1",   "192.0.2.1",
      "198.18.0.1", "198.19.255.255", "198.51.100.1", "203.0.113.1",     "240.0.0.1",   "invalid"};
  for (size_t i = 0; i < sizeof(non_public_v4) / sizeof(non_public_v4[0]); ++i) {
    ASCIICHAT_REQUIRE_NOT(is_internet_ipv4(non_public_v4[i]), "%s", non_public_v4[i]);
  }
  const char *public_v4[] = {"8.8.8.8", "100.63.255.255", "100.128.0.0", "198.17.255.255", "198.20.0.0"};
  for (size_t i = 0; i < sizeof(public_v4) / sizeof(public_v4[0]); ++i) {
    ASCIICHAT_REQUIRE(is_internet_ipv4(public_v4[i]), "%s", public_v4[i]);
  }
  const char *non_public_v6[] = {
      "::1",       "[::1]",       "fe80::1",           "fc00::1", "fdff::1", "ff02::1", "::", "::ffff:8.8.8.8",
      "::8.8.8.8", "2001:db8::1", "2002:0808:0808::1", "[bad",    "invalid", NULL};
  for (size_t i = 0; i < sizeof(non_public_v6) / sizeof(non_public_v6[0]); ++i) {
    ASCIICHAT_REQUIRE_NOT(is_internet_ipv6(non_public_v6[i]));
  }
  ASCIICHAT_REQUIRE(is_internet_ipv6("2001:4860:4860::8888"));
  ASCIICHAT_REQUIRE(is_internet_ipv6("[2606:4700:4700::1111]"));
}

TEST_CASE("ip_utils/endpoint_identity_normalizes_ipv6_but_preserves_port_and_family", "[ip_utils]") {
  ASCIICHAT_REQUIRE(compare_ip_port_strings("[2001:4860::1]:27224", "[2001:4860:0:0:0:0:0:1]:27224"));
  ASCIICHAT_REQUIRE(compare_ip_port_strings("8.8.8.8:27224", "8.8.8.8:27224"));
  ASCIICHAT_REQUIRE_NOT(compare_ip_port_strings("[2001:4860::1]:27224", "[2001:4860::1]:27225"));
  ASCIICHAT_REQUIRE_NOT(compare_ip_port_strings("[2001:4860::1]:27224", "[2001:4860::2]:27224"));
  ASCIICHAT_REQUIRE_NOT(compare_ip_port_strings("8.8.8.8:27224", "[::ffff:8.8.8.8]:27224"));
  ASCIICHAT_REQUIRE_NOT(compare_ip_port_strings("bad", "8.8.8.8:27224"));
  ASCIICHAT_REQUIRE_NOT(compare_ip_port_strings("8.8.8.8:27224", "bad"));
  ASCIICHAT_REQUIRE_NOT(compare_ip_port_strings(NULL, "8.8.8.8:27224"));
  const struct {
    const char *ip;
    const char *label;
  } labels[] = {{"0.0.0.0", "All Interfaces"}, {"::", "All Interfaces"},
                {"127.0.0.2", "Localhost"},    {"::1", "Localhost"},
                {"10.1.2.3", "LAN"},           {"fd00::1", "LAN"},
                {"8.8.8.8", "Internet"},       {"2606:4700:4700::1111", "Internet"},
                {"invalid", "Unknown"},        {NULL, ""}};
  for (size_t i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
    ASCIICHAT_REQUIRE_STR_EQ(get_ip_type_string(labels[i].ip), labels[i].label);
  }
}

// =============================================================================
// IPv4 Validation Tests - Parameterized
// =============================================================================

typedef struct {
  char ip[256];
  int expected_result;
  char description[64];
} ipv4_test_case_t;

static ipv4_test_case_t ipv4_cases[] = {
    // Valid IPv4 addresses
    {"0.0.0.0", 1, "all zeros"},
    {"127.0.0.1", 1, "localhost"},
    {"192.168.1.1", 1, "private network"},
    {"255.255.255.255", 1, "broadcast"},
    {"10.0.0.1", 1, "class A private"},
    {"172.16.0.1", 1, "class B private"},
    {"8.8.8.8", 1, "Google DNS"},
    {"1.2.3.4", 1, "simple valid"},

    // Invalid IPv4 addresses
    {"", 0, "empty string"},
    {"192.168.1", 0, "too few octets"},
    {"192.168.1.1.1", 0, "too many octets"},
    {"256.1.1.1", 0, "octet > 255"},
    {"192.168.1.256", 0, "last octet > 255"},
    {"192.168.-1.1", 0, "negative octet"},
    {"192.168.1.1a", 0, "trailing letters"},
    {"a.b.c.d", 0, "all letters"},
    {"192.168.1.1 ", 0, "trailing space"},
    {" 192.168.1.1", 0, "leading space"},
    {"192.168.1.01", 0, "leading zero"},
    {"01.2.3.4", 0, "leading zero in first octet"},
    {"1.02.3.4", 0, "leading zero in middle octet"},
    {"1.2.003.4", 0, "multiple leading zeros"},
    {"192.168.1.", 0, "trailing dot"},
    {".192.168.1.1", 0, "leading dot"},
    {"192..168.1.1", 0, "double dot"},
    {"::1", 0, "IPv6 address"},
    {"2001:db8::1", 0, "IPv6 address with colons"},
};



TEST_CASE("ip_utils/ipv4_validation", "[ip_utils]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(ipv4_cases)); auto *tc = &generated_case;
  int result = is_valid_ipv4(tc->ip);
  ASCIICHAT_REQUIRE_EQ(result, tc->expected_result, "is_valid_ipv4(\"%s\") = %d, expected %d (%s)", tc->ip, result,
               tc->expected_result, tc->description);
}

// =============================================================================
// IPv6 Validation Tests - Parameterized
// =============================================================================

typedef struct {
  char ip[256];
  int expected_result;
  char description[64];
} ipv6_test_case_t;

static ipv6_test_case_t ipv6_cases[] = {
    // Valid IPv6 addresses
    {"::", 1, "all zeros compressed"},
    {"::1", 1, "loopback compressed"},
    {"::ffff:192.0.2.1", 1, "IPv4-mapped"},
    {"2001:db8::1", 1, "documentation prefix compressed"},
    {"2001:db8:0:0:0:0:0:1", 1, "documentation prefix full"},
    {"fe80::1", 1, "link-local compressed"},
    {"ff02::1", 1, "multicast compressed"},
    {"2001:0db8:0000:0000:0000:ff00:0042:8329", 1, "full form with leading zeros"},
    {"2001:db8:0:0:1:0:0:1", 1, "partial compression"},
    {"2001:db8::8a2e:370:7334", 1, "compressed middle"},
    {"2001:db8:85a3::8a2e:370:7334", 1, "documentation example"},
    {"::ffff:127.0.0.1", 1, "IPv4-mapped localhost"},
    {"::1234:5678", 1, "compressed start"},
    {"1234:5678::", 1, "compressed end"},

    // Invalid IPv6 addresses
    {"", 0, "empty string"},
    {"192.168.1.1", 0, "IPv4 address"},
    {"gggg::1", 0, "invalid hex characters"},
    {"12345::1", 0, "segment too long"},
    {"::1::2", 0, "multiple double colons"},
    {"2001:db8:", 0, "trailing single colon"},
    {":2001:db8::1", 0, "leading single colon"},
    {"2001:db8::1 ", 0, "trailing space"},
    {" 2001:db8::1", 0, "leading space"},
    {"2001:db8::g", 0, "invalid hex digit"},
    {"2001:db8:0:0:0:0:0:0:1", 0, "too many segments"},
    {"hello world", 0, "random text"},
};



TEST_CASE("ip_utils/ipv6_validation", "[ip_utils]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(ipv6_cases)); auto *tc = &generated_case;
  int result = is_valid_ipv6(tc->ip);
  ASCIICHAT_REQUIRE_EQ(result, tc->expected_result, "is_valid_ipv6(\"%s\") = %d, expected %d (%s)", tc->ip, result,
               tc->expected_result, tc->description);
}

// =============================================================================
// IPv6 Address Parsing Tests - Parameterized
// =============================================================================

typedef struct {
  char input[256];
  char expected_output[256];
  int expected_result;
  char description[64];
} ipv6_parse_test_case_t;

static ipv6_parse_test_case_t ipv6_parse_cases[] = {
    // Valid parsing cases - remove brackets
    {"[::1]", "::1", 0, "loopback with brackets"},
    {"[2001:db8::1]", "2001:db8::1", 0, "regular IPv6 with brackets"},
    {"[::ffff:192.0.2.1]", "::ffff:192.0.2.1", 0, "IPv4-mapped with brackets"},

    // Valid parsing cases - no brackets (pass through)
    {"::1", "::1", 0, "loopback without brackets"},
    {"2001:db8::1", "2001:db8::1", 0, "regular IPv6 without brackets"},
    {"::", "::", 0, "all zeros without brackets"},

    // Error cases
    {"", "", -1, "empty string"},
    {"[::1", "", -1, "missing closing bracket"},
    {"::1]", "", -1, "missing opening bracket"},
    {"[[::1]]", "", -1, "double brackets"},
};



TEST_CASE("ip_utils/ipv6_parsing", "[ip_utils]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(ipv6_parse_cases)); auto *tc = &generated_case;
  char output[256];
  memset(output, 0xAA, sizeof(output)); // Fill with pattern to detect no-write

  int result = parse_ipv6_address(tc->input, output, sizeof(output));

  ASCIICHAT_REQUIRE_EQ(result, tc->expected_result, "parse_ipv6_address(\"%s\") returned %d, expected %d (%s)", tc->input,
               result, tc->expected_result, tc->description);

  if (tc->expected_result == 0) {
    ASCIICHAT_REQUIRE_STR_EQ(output, tc->expected_output, "parse_ipv6_address(\"%s\") output mismatch (%s)", tc->input,
                     tc->description);
  }
}

TEST_CASE("ip_utils/ipv6_parsing_buffer_too_small", "[ip_utils]") {
  char small_output[3]; // Too small for "::1" (3 chars) + null terminator
  int result = parse_ipv6_address("[::1]", small_output, sizeof(small_output));
  ASCIICHAT_REQUIRE_EQ(result, -1, "Should fail with buffer too small");
}

TEST_CASE("ip_utils/ipv6_parsing_null_output", "[ip_utils]") {
  int result = parse_ipv6_address("[::1]", NULL, 256);
  ASCIICHAT_REQUIRE_EQ(result, -1, "Should fail with NULL output buffer");
}

// =============================================================================
// IP:Port Formatting Tests - Parameterized
// =============================================================================

typedef struct {
  char ip[256];
  uint16_t port;
  char expected_output[512];
  int expected_result;
  char description[64];
} format_ip_port_test_case_t;

static format_ip_port_test_case_t format_cases[] = {
    // IPv4 formatting
    {"127.0.0.1", 8080, "127.0.0.1:8080", 0, "IPv4 localhost"},
    {"192.168.1.1", 27224, "192.168.1.1:27224", 0, "IPv4 private network"},
    {"0.0.0.0", 80, "0.0.0.0:80", 0, "IPv4 all zeros"},
    {"255.255.255.255", 65535, "255.255.255.255:65535", 0, "IPv4 max values"},

    // IPv6 formatting (with brackets)
    {"::1", 8080, "[::1]:8080", 0, "IPv6 loopback"},
    {"::", 27224, "[::]:27224", 0, "IPv6 all zeros"},
    {"2001:db8::1", 443, "[2001:db8::1]:443", 0, "IPv6 documentation prefix"},
    {"fe80::1", 8080, "[fe80::1]:8080", 0, "IPv6 link-local"},
    {"::ffff:192.0.2.1", 8080, "[::ffff:192.0.2.1]:8080", 0, "IPv6 IPv4-mapped"},
    {"2001:db8:85a3::8a2e:370:7334", 22, "[2001:db8:85a3::8a2e:370:7334]:22", 0, "IPv6 long address"},
};



TEST_CASE("ip_utils/format_ip_with_port", "[ip_utils]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(format_cases)); auto *tc = &generated_case;
  char output[512];
  memset(output, 0xAA, sizeof(output));

  int result = format_ip_with_port(tc->ip, tc->port, output, sizeof(output));

  ASCIICHAT_REQUIRE_EQ(result, tc->expected_result, "format_ip_with_port(\"%s\", %u) returned %d, expected %d (%s)", tc->ip,
               tc->port, result, tc->expected_result, tc->description);

  if (tc->expected_result == 0) {
    ASCIICHAT_REQUIRE_STR_EQ(output, tc->expected_output, "format_ip_with_port(\"%s\", %u) output mismatch (%s)", tc->ip,
                     tc->port, tc->description);
  }
}

TEST_CASE("ip_utils/format_ip_with_port_buffer_too_small", "[ip_utils]") {
  char small_output[10];
  asciichat_error_t result = format_ip_with_port("192.168.1.1", 8080, small_output, sizeof(small_output));
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with buffer too small");
}

TEST_CASE("ip_utils/format_ip_with_port_null_output", "[ip_utils]") {
  asciichat_error_t result = format_ip_with_port("192.168.1.1", 8080, NULL, 512);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL output buffer");
}

TEST_CASE("ip_utils/format_ip_with_port_zero_size", "[ip_utils]") {
  char output[512];
  asciichat_error_t result = format_ip_with_port("192.168.1.1", 8080, output, 0);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with zero size buffer");
}

// =============================================================================
// IP:Port Parsing Tests - Parameterized
// =============================================================================

typedef struct {
  char input[512];
  char expected_ip[256];
  uint16_t expected_port;
  int expected_result;
  char description[64];
} parse_ip_port_test_case_t;

static parse_ip_port_test_case_t parse_cases[] = {
    // IPv4 parsing
    {"192.168.1.1:8080", "192.168.1.1", 8080, 0, "IPv4 with port"},
    {"127.0.0.1:27224", "127.0.0.1", 27224, 0, "IPv4 localhost"},
    {"0.0.0.0:80", "0.0.0.0", 80, 0, "IPv4 all zeros"},
    {"255.255.255.255:65535", "255.255.255.255", 65535, 0, "IPv4 max port"},
    {"10.0.0.1:1", "10.0.0.1", 1, 0, "IPv4 min port"},

    // IPv6 parsing (with brackets)
    {"[::1]:8080", "::1", 8080, 0, "IPv6 loopback"},
    {"[::]:27224", "::", 27224, 0, "IPv6 all zeros"},
    {"[2001:db8::1]:443", "2001:db8::1", 443, 0, "IPv6 documentation"},
    {"[fe80::1]:8080", "fe80::1", 8080, 0, "IPv6 link-local"},
    {"[::ffff:192.0.2.1]:8080", "::ffff:192.0.2.1", 8080, 0, "IPv6 IPv4-mapped"},
    {"[2001:db8:85a3::8a2e:370:7334]:22", "2001:db8:85a3::8a2e:370:7334", 22, 0, "IPv6 long"},

    // Error cases
    {"", "", 0, -1, "empty string"},
    {"192.168.1.1", "", 0, -1, "IPv4 no port"},
    {"192.168.1.1:", "", 0, -1, "IPv4 empty port"},
    {"192.168.1.1:abc", "", 0, -1, "IPv4 non-numeric port"},
    {"192.168.1.1:99999", "", 0, -1, "IPv4 port out of range"},
    {"[::1]", "", 0, -1, "IPv6 no port"},
    {"[::1]:", "", 0, -1, "IPv6 empty port"},
    {"::1:8080", "", 0, -1, "IPv6 without brackets"},
    {"[::1:8080", "", 0, -1, "IPv6 missing closing bracket"},
    {"::1]:8080", "", 0, -1, "IPv6 missing opening bracket"},
};



TEST_CASE("ip_utils/parse_ip_with_port", "[ip_utils]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(parse_cases)); auto *tc = &generated_case;
  char ip_output[256];
  uint16_t port_output = 0;
  memset(ip_output, 0xAA, sizeof(ip_output));

  int result = parse_ip_with_port(tc->input, ip_output, sizeof(ip_output), &port_output);

  ASCIICHAT_REQUIRE_EQ(result, tc->expected_result, "parse_ip_with_port(\"%s\") returned %d, expected %d (%s)", tc->input,
               result, tc->expected_result, tc->description);

  if (tc->expected_result == 0) {
    ASCIICHAT_REQUIRE_STR_EQ(ip_output, tc->expected_ip, "parse_ip_with_port(\"%s\") IP mismatch (%s)", tc->input,
                     tc->description);
    ASCIICHAT_REQUIRE_EQ(port_output, tc->expected_port, "parse_ip_with_port(\"%s\") port mismatch: got %u, expected %u (%s)",
                 tc->input, port_output, tc->expected_port, tc->description);
  }
}

TEST_CASE("ip_utils/parse_ip_with_port_buffer_too_small", "[ip_utils]") {
  char small_output[5];
  uint16_t port = 0;
  int result = parse_ip_with_port("192.168.1.1:8080", small_output, sizeof(small_output), &port);
  ASCIICHAT_REQUIRE_EQ(result, -1, "Should fail with buffer too small");
}

TEST_CASE("ip_utils/parse_ip_with_port_null_output", "[ip_utils]") {
  uint16_t port = 0;
  int result = parse_ip_with_port("192.168.1.1:8080", NULL, 256, &port);
  ASCIICHAT_REQUIRE_EQ(result, -1, "Should fail with NULL IP output buffer");
}

TEST_CASE("ip_utils/parse_ip_with_port_null_port", "[ip_utils]") {
  char ip_output[256];
  int result = parse_ip_with_port("192.168.1.1:8080", ip_output, sizeof(ip_output), NULL);
  ASCIICHAT_REQUIRE_EQ(result, -1, "Should fail with NULL port output");
}

// =============================================================================
// Roundtrip Tests - Format then Parse
// =============================================================================

typedef struct {
  char ip[256];
  uint16_t port;
  char description[64];
} roundtrip_test_case_t;

static roundtrip_test_case_t roundtrip_cases[] = {
    {"127.0.0.1", 8080, "IPv4 localhost"},
    {"192.168.1.1", 27224, "IPv4 private"},
    {"::1", 8080, "IPv6 loopback"},
    {"::", 27224, "IPv6 all zeros"},
    {"2001:db8::1", 443, "IPv6 documentation"},
    {"fe80::1", 8080, "IPv6 link-local"},
    {"::ffff:192.0.2.1", 8080, "IPv6 IPv4-mapped"},
};



TEST_CASE("ip_utils/format_parse_roundtrip", "[ip_utils]") { auto&& generated_case = GENERATE(Catch::Generators::from_range(roundtrip_cases)); auto *tc = &generated_case;
  char formatted[512];
  char parsed_ip[256];
  uint16_t parsed_port = 0;

  // Format
  int format_result = format_ip_with_port(tc->ip, tc->port, formatted, sizeof(formatted));
  ASCIICHAT_REQUIRE_EQ(format_result, 0, "format_ip_with_port(\"%s\", %u) should succeed (%s)", tc->ip, tc->port,
               tc->description);

  // Parse
  int parse_result = parse_ip_with_port(formatted, parsed_ip, sizeof(parsed_ip), &parsed_port);
  ASCIICHAT_REQUIRE_EQ(parse_result, 0, "parse_ip_with_port(\"%s\") should succeed (%s)", formatted, tc->description);

  // Verify roundtrip
  ASCIICHAT_REQUIRE_STR_EQ(parsed_ip, tc->ip, "Roundtrip IP mismatch for %s:%u (%s)", tc->ip, tc->port, tc->description);
  ASCIICHAT_REQUIRE_EQ(parsed_port, tc->port, "Roundtrip port mismatch for %s:%u (%s)", tc->ip, tc->port, tc->description);
}

// =============================================================================
// Edge Case Tests
// =============================================================================

TEST_CASE("ip_utils/format_ipv6_very_long_address", "[ip_utils]") {
  // Test longest possible IPv6 address
  const char *long_ipv6 = "2001:0db8:0000:0000:0000:ff00:0042:8329";
  char output[512];
  int result = format_ip_with_port(long_ipv6, 8080, output, sizeof(output));
  ASCIICHAT_REQUIRE_EQ(result, 0, "Should format long IPv6 address");
  ASCIICHAT_REQUIRE_STR_EQ(output, "[2001:0db8:0000:0000:0000:ff00:0042:8329]:8080", "Long IPv6 format mismatch");
}

TEST_CASE("ip_utils/parse_ipv6_very_long_formatted", "[ip_utils]") {
  const char *long_formatted = "[2001:0db8:0000:0000:0000:ff00:0042:8329]:8080";
  char ip_output[256];
  uint16_t port = 0;
  int result = parse_ip_with_port(long_formatted, ip_output, sizeof(ip_output), &port);
  ASCIICHAT_REQUIRE_EQ(result, 0, "Should parse long IPv6 address");
  ASCIICHAT_REQUIRE_STR_EQ(ip_output, "2001:0db8:0000:0000:0000:ff00:0042:8329", "Long IPv6 parse IP mismatch");
  ASCIICHAT_REQUIRE_EQ(port, 8080, "Long IPv6 parse port mismatch");
}

TEST_CASE("ip_utils/ipv4_validation_boundary_cases", "[ip_utils]") {
  // Test boundary values
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv4("0.0.0.0"), 1, "Should accept 0.0.0.0");
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv4("255.255.255.255"), 1, "Should accept 255.255.255.255");
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv4("256.0.0.0"), 0, "Should reject 256.x.x.x");
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv4("0.256.0.0"), 0, "Should reject x.256.x.x");
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv4("0.0.256.0"), 0, "Should reject x.x.256.x");
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv4("0.0.0.256"), 0, "Should reject x.x.x.256");
}

TEST_CASE("ip_utils/ipv6_special_addresses", "[ip_utils]") {
  // Test special IPv6 addresses
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv6("::"), 1, "Should accept :: (all zeros)");
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv6("::1"), 1, "Should accept ::1 (loopback)");
  ASCIICHAT_REQUIRE_EQ(is_valid_ipv6("::ffff:0:0"), 1, "Should accept ::ffff:0:0 (IPv4-mapped prefix)");
}
