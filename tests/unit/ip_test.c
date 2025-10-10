#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <criterion/parameterized.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "ip.h"
#include "tests/common.h"
#include "tests/logging.h"

// Use quiet logging for tests
TEST_SUITE_WITH_QUIET_LOGGING_AND_LOG_LEVELS(ip_utils, LOG_FATAL, LOG_DEBUG, false, false);

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
    {"192.168.1.", 0, "trailing dot"},
    {".192.168.1.1", 0, "leading dot"},
    {"192..168.1.1", 0, "double dot"},
    {"::1", 0, "IPv6 address"},
    {"2001:db8::1", 0, "IPv6 address with colons"},
};

ParameterizedTestParameters(ip_utils, ipv4_validation) {
  return cr_make_param_array(ipv4_test_case_t, ipv4_cases, sizeof(ipv4_cases) / sizeof(ipv4_cases[0]));
}

ParameterizedTest(ipv4_test_case_t *tc, ip_utils, ipv4_validation) {
  int result = is_valid_ipv4(tc->ip);
  cr_assert_eq(result, tc->expected_result, "is_valid_ipv4(\"%s\") = %d, expected %d (%s)", tc->ip, result,
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

ParameterizedTestParameters(ip_utils, ipv6_validation) {
  return cr_make_param_array(ipv6_test_case_t, ipv6_cases, sizeof(ipv6_cases) / sizeof(ipv6_cases[0]));
}

ParameterizedTest(ipv6_test_case_t *tc, ip_utils, ipv6_validation) {
  int result = is_valid_ipv6(tc->ip);
  cr_assert_eq(result, tc->expected_result, "is_valid_ipv6(\"%s\") = %d, expected %d (%s)", tc->ip ? tc->ip : "NULL",
               result, tc->expected_result, tc->description);
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

ParameterizedTestParameters(ip_utils, ipv6_parsing) {
  return cr_make_param_array(ipv6_parse_test_case_t, ipv6_parse_cases,
                             sizeof(ipv6_parse_cases) / sizeof(ipv6_parse_cases[0]));
}

ParameterizedTest(ipv6_parse_test_case_t *tc, ip_utils, ipv6_parsing) {
  char output[256];
  memset(output, 0xAA, sizeof(output)); // Fill with pattern to detect no-write

  int result = parse_ipv6_address(tc->input, output, sizeof(output));

  cr_assert_eq(result, tc->expected_result, "parse_ipv6_address(\"%s\") returned %d, expected %d (%s)",
               tc->input ? tc->input : "NULL", result, tc->expected_result, tc->description);

  if (tc->expected_result == 0) {
    cr_assert_str_eq(output, tc->expected_output, "parse_ipv6_address(\"%s\") output mismatch (%s)",
                     tc->input ? tc->input : "NULL", tc->description);
  }
}

Test(ip_utils, ipv6_parsing_buffer_too_small) {
  char small_output[3]; // Too small for "::1" (3 chars) + null terminator
  int result = parse_ipv6_address("[::1]", small_output, sizeof(small_output));
  cr_assert_eq(result, -1, "Should fail with buffer too small");
}

Test(ip_utils, ipv6_parsing_null_output) {
  int result = parse_ipv6_address("[::1]", NULL, 256);
  cr_assert_eq(result, -1, "Should fail with NULL output buffer");
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

ParameterizedTestParameters(ip_utils, format_ip_with_port) {
  return cr_make_param_array(format_ip_port_test_case_t, format_cases, sizeof(format_cases) / sizeof(format_cases[0]));
}

ParameterizedTest(format_ip_port_test_case_t *tc, ip_utils, format_ip_with_port) {
  char output[512];
  memset(output, 0xAA, sizeof(output));

  int result = format_ip_with_port(tc->ip, tc->port, output, sizeof(output));

  cr_assert_eq(result, tc->expected_result, "format_ip_with_port(\"%s\", %u) returned %d, expected %d (%s)",
               tc->ip ? tc->ip : "NULL", tc->port, result, tc->expected_result, tc->description);

  if (tc->expected_result == 0) {
    cr_assert_str_eq(output, tc->expected_output, "format_ip_with_port(\"%s\", %u) output mismatch (%s)",
                     tc->ip ? tc->ip : "NULL", tc->port, tc->description);
  }
}

Test(ip_utils, format_ip_with_port_buffer_too_small) {
  char small_output[10];
  int result = format_ip_with_port("192.168.1.1", 8080, small_output, sizeof(small_output));
  cr_assert_eq(result, -1, "Should fail with buffer too small");
}

Test(ip_utils, format_ip_with_port_null_output) {
  int result = format_ip_with_port("192.168.1.1", 8080, NULL, 512);
  cr_assert_eq(result, -1, "Should fail with NULL output buffer");
}

Test(ip_utils, format_ip_with_port_zero_size) {
  char output[512];
  int result = format_ip_with_port("192.168.1.1", 8080, output, 0);
  cr_assert_eq(result, -1, "Should fail with zero size buffer");
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

ParameterizedTestParameters(ip_utils, parse_ip_with_port) {
  return cr_make_param_array(parse_ip_port_test_case_t, parse_cases, sizeof(parse_cases) / sizeof(parse_cases[0]));
}

ParameterizedTest(parse_ip_port_test_case_t *tc, ip_utils, parse_ip_with_port) {
  char ip_output[256];
  uint16_t port_output = 0;
  memset(ip_output, 0xAA, sizeof(ip_output));

  int result = parse_ip_with_port(tc->input, ip_output, sizeof(ip_output), &port_output);

  cr_assert_eq(result, tc->expected_result, "parse_ip_with_port(\"%s\") returned %d, expected %d (%s)",
               tc->input ? tc->input : "NULL", result, tc->expected_result, tc->description);

  if (tc->expected_result == 0) {
    cr_assert_str_eq(ip_output, tc->expected_ip, "parse_ip_with_port(\"%s\") IP mismatch (%s)",
                     tc->input ? tc->input : "NULL", tc->description);
    cr_assert_eq(port_output, tc->expected_port, "parse_ip_with_port(\"%s\") port mismatch: got %u, expected %u (%s)",
                 tc->input ? tc->input : "NULL", port_output, tc->expected_port, tc->description);
  }
}

Test(ip_utils, parse_ip_with_port_buffer_too_small) {
  char small_output[5];
  uint16_t port = 0;
  int result = parse_ip_with_port("192.168.1.1:8080", small_output, sizeof(small_output), &port);
  cr_assert_eq(result, -1, "Should fail with buffer too small");
}

Test(ip_utils, parse_ip_with_port_null_output) {
  uint16_t port = 0;
  int result = parse_ip_with_port("192.168.1.1:8080", NULL, 256, &port);
  cr_assert_eq(result, -1, "Should fail with NULL IP output buffer");
}

Test(ip_utils, parse_ip_with_port_null_port) {
  char ip_output[256];
  int result = parse_ip_with_port("192.168.1.1:8080", ip_output, sizeof(ip_output), NULL);
  cr_assert_eq(result, -1, "Should fail with NULL port output");
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

ParameterizedTestParameters(ip_utils, format_parse_roundtrip) {
  return cr_make_param_array(roundtrip_test_case_t, roundtrip_cases,
                             sizeof(roundtrip_cases) / sizeof(roundtrip_cases[0]));
}

ParameterizedTest(roundtrip_test_case_t *tc, ip_utils, format_parse_roundtrip) {
  char formatted[512];
  char parsed_ip[256];
  uint16_t parsed_port = 0;

  // Format
  int format_result = format_ip_with_port(tc->ip, tc->port, formatted, sizeof(formatted));
  cr_assert_eq(format_result, 0, "format_ip_with_port(\"%s\", %u) should succeed (%s)", tc->ip, tc->port,
               tc->description);

  // Parse
  int parse_result = parse_ip_with_port(formatted, parsed_ip, sizeof(parsed_ip), &parsed_port);
  cr_assert_eq(parse_result, 0, "parse_ip_with_port(\"%s\") should succeed (%s)", formatted, tc->description);

  // Verify roundtrip
  cr_assert_str_eq(parsed_ip, tc->ip, "Roundtrip IP mismatch for %s:%u (%s)", tc->ip, tc->port, tc->description);
  cr_assert_eq(parsed_port, tc->port, "Roundtrip port mismatch for %s:%u (%s)", tc->ip, tc->port, tc->description);
}

// =============================================================================
// Edge Case Tests
// =============================================================================

Test(ip_utils, format_ipv6_very_long_address) {
  // Test longest possible IPv6 address
  const char *long_ipv6 = "2001:0db8:0000:0000:0000:ff00:0042:8329";
  char output[512];
  int result = format_ip_with_port(long_ipv6, 8080, output, sizeof(output));
  cr_assert_eq(result, 0, "Should format long IPv6 address");
  cr_assert_str_eq(output, "[2001:0db8:0000:0000:0000:ff00:0042:8329]:8080", "Long IPv6 format mismatch");
}

Test(ip_utils, parse_ipv6_very_long_formatted) {
  const char *long_formatted = "[2001:0db8:0000:0000:0000:ff00:0042:8329]:8080";
  char ip_output[256];
  uint16_t port = 0;
  int result = parse_ip_with_port(long_formatted, ip_output, sizeof(ip_output), &port);
  cr_assert_eq(result, 0, "Should parse long IPv6 address");
  cr_assert_str_eq(ip_output, "2001:0db8:0000:0000:0000:ff00:0042:8329", "Long IPv6 parse IP mismatch");
  cr_assert_eq(port, 8080, "Long IPv6 parse port mismatch");
}

Test(ip_utils, ipv4_validation_boundary_cases) {
  // Test boundary values
  cr_assert_eq(is_valid_ipv4("0.0.0.0"), 1, "Should accept 0.0.0.0");
  cr_assert_eq(is_valid_ipv4("255.255.255.255"), 1, "Should accept 255.255.255.255");
  cr_assert_eq(is_valid_ipv4("256.0.0.0"), 0, "Should reject 256.x.x.x");
  cr_assert_eq(is_valid_ipv4("0.256.0.0"), 0, "Should reject x.256.x.x");
  cr_assert_eq(is_valid_ipv4("0.0.256.0"), 0, "Should reject x.x.256.x");
  cr_assert_eq(is_valid_ipv4("0.0.0.256"), 0, "Should reject x.x.x.256");
}

Test(ip_utils, ipv6_special_addresses) {
  // Test special IPv6 addresses
  cr_assert_eq(is_valid_ipv6("::"), 1, "Should accept :: (all zeros)");
  cr_assert_eq(is_valid_ipv6("::1"), 1, "Should accept ::1 (loopback)");
  cr_assert_eq(is_valid_ipv6("::ffff:0:0"), 1, "Should accept ::ffff:0:0 (IPv4-mapped prefix)");
}
