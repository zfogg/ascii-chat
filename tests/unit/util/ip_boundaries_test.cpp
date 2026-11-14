#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>
TEST_CASE("ip_boundaries/cidr_network_edges_and_address_families", "[ip_boundaries]") {
  const struct {
    const char *ip, *network;
    int contains;
  } cases[] = {{"10.0.0.0", "10.0.0.0/8", 1},
               {"10.255.255.255", "10.0.0.0/8", 1},
               {"11.0.0.0", "10.0.0.0/8", 0},
               {"192.0.2.7", "0.0.0.0/0", 1},
               {"192.0.2.7", "192.0.2.7/32", 1},
               {"192.0.2.8", "192.0.2.7/32", 0},
               {"2001:db8::ffff", "2001:db8::/32", 1},
               {"2001:db9::", "2001:db8::/32", 0},
               {"::1", "::/0", 1},
               {"::1", "::1/128", 1},
               {"::2", "::1/128", 0},
               {"::1", "127.0.0.0/8", 0},
               {"127.0.0.1", "::/0", 0},
               {"invalid", "::/0", 0}};
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i)
    ASCIICHAT_REQUIRE_EQ(ip_in_cidr(cases[i].ip, cases[i].network), cases[i].contains, "%s in %s", cases[i].ip,
                 cases[i].network);
}

TEST_CASE("ip_boundaries/rejects_malformed_cidr_and_short_buffers", "[ip_boundaries]") {
  const char *invalid[] = {"",
                           "/24",
                           "10.0.0.1",
                           "bad/24",
                           "10.0.0.0/",
                           "10.0.0.0/-1",
                           "10.0.0.0/33",
                           "::/129",
                           "::/x",
                           "::/64junk",
                           "::/9999999999999999999999999999999"};
  char output[128];
  int prefix;
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
    ASCIICHAT_REQUIRE_EQ(parse_cidr(invalid[i], output, sizeof(output), &prefix), -1, "%s", invalid[i]);
  ASCIICHAT_REQUIRE_EQ(parse_cidr(NULL, output, sizeof(output), &prefix), -1);
  ASCIICHAT_REQUIRE_EQ(parse_cidr("::/64", NULL, sizeof(output), &prefix), -1);
  ASCIICHAT_REQUIRE_EQ(parse_cidr("::/64", output, 1, &prefix), -1);
  ASCIICHAT_REQUIRE_EQ(parse_cidr("::/64", output, sizeof(output), NULL), -1);
  ASCIICHAT_REQUIRE_EQ(parse_cidr("[2001:db8::]/48", output, sizeof(output), &prefix), 0);
  ASCIICHAT_REQUIRE_STR_EQ(output, "2001:db8::");
  ASCIICHAT_REQUIRE_EQ(prefix, 48);
  ASCIICHAT_REQUIRE_EQ(ip_in_cidr_parsed("::1", "::", -1), 0);
  ASCIICHAT_REQUIRE_EQ(ip_in_cidr_parsed("::1", "::", 129), 0);
}

TEST_CASE("ip_boundaries/canonical_addresses_and_mapped_ipv4_round_trip", "[ip_boundaries]") {
  char mapped[128], v4[32], expanded[128], compact[128];
  const char *addresses[] = {"0.0.0.0", "192.0.2.42", "255.255.255.255"};
  for (size_t i = 0; i < sizeof(addresses) / sizeof(*addresses); ++i) {
    ASCIICHAT_REQUIRE_EQ(ipv4_to_ipv6_mapped(addresses[i], mapped, sizeof(mapped)), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE(is_ipv4_mapped_ipv6(mapped));
    ASCIICHAT_REQUIRE_EQ(extract_ipv4_from_mapped_ipv6(mapped, v4, sizeof(v4)), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(v4, addresses[i]);
  }
  ASCIICHAT_REQUIRE_EQ(expand_ipv6("2001:db8::1", expanded, sizeof(expanded)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(expanded, "2001:0db8:0000:0000:0000:0000:0000:0001");
  ASCIICHAT_REQUIRE_EQ(compact_ipv6(expanded, compact, sizeof(compact)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(compact, "2001:db8::1");
  ASCIICHAT_REQUIRE_EQ(canonicalize_ipv6("[2001:DB8::1]", compact, sizeof(compact)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE(ip_equals(compact, expanded));
  ASCIICHAT_REQUIRE_NE(ipv4_to_ipv6_mapped("bad", mapped, sizeof(mapped)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(ipv4_to_ipv6_mapped("1.2.3.4", mapped, 2), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(extract_ipv4_from_mapped_ipv6("::1", v4, sizeof(v4)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(expand_ipv6("bad", expanded, sizeof(expanded)), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(canonicalize_ipv6("::1", compact, 1), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NOT(is_ipv4_mapped_ipv6(NULL));
}

TEST_CASE("ip_boundaries/numeric_sorting_normalization_and_invalid_addresses", "[ip_boundaries]") {
  ASCIICHAT_REQUIRE(ip_equals("[::1]", "0:0:0:0:0:0:0:1"));
  ASCIICHAT_REQUIRE_NOT(ip_equals("127.0.0.1", "::1"));
  ASCIICHAT_REQUIRE_NOT(ip_equals(NULL, "::1"));
  ASCIICHAT_REQUIRE_LT(ip_compare("10.0.0.2", "10.0.0.10"), 0);
  ASCIICHAT_REQUIRE_GT(ip_compare("10.0.0.10", "10.0.0.2"), 0);
  ASCIICHAT_REQUIRE_EQ(ip_compare("::1", "[::1]"), 0);
  ASCIICHAT_REQUIRE_LT(ip_compare("::1", "::2"), 0);
  ASCIICHAT_REQUIRE_LT(ip_compare("1.2.3.4", "::1"), 0);
  ASCIICHAT_REQUIRE_GT(ip_compare("bad", "::1"), 0);
  ASCIICHAT_REQUIRE_LT(ip_compare("::1", "bad"), 0);
  ASCIICHAT_REQUIRE_EQ(ip_compare("bad", "invalid"), 0);
  ASCIICHAT_REQUIRE_EQ(ip_compare(NULL, "::1"), -2);
  ASCIICHAT_REQUIRE_EQ(compare_ip_port_strings("127.0.0.1:20", "127.0.0.1:30"), 0);
  ASCIICHAT_REQUIRE_EQ(compare_ip_port_strings("[::1]:42", "[0:0:0:0:0:0:0:1]:42"), 1);
}

TEST_CASE("ip_boundaries/classify_private_loopback_link_local_and_public_addresses", "[ip_boundaries]") {
  const struct {
    const char *address;
    int lan, loopback, link, multicast, internet;
  } ipv4[] = {{"10.255.255.255", 1, 0, 0, 0, 0},  {"172.16.0.0", 1, 0, 0, 0, 0}, {"172.31.255.255", 1, 0, 0, 0, 0},
              {"192.168.1.1", 1, 0, 0, 0, 0},     {"127.0.0.2", 0, 1, 0, 0, 0},  {"169.254.1.1", 0, 0, 1, 0, 0},
              {"255.255.255.255", 0, 0, 0, 1, 0}, {"8.8.8.8", 0, 0, 0, 0, 1},    {"bad", 0, 0, 0, 0, 0}};
  for (size_t i = 0; i < sizeof(ipv4) / sizeof(*ipv4); ++i) {
    const char *a = ipv4[i].address;
    ASCIICHAT_REQUIRE_EQ(is_lan_ipv4(a), ipv4[i].lan, "%s", a);
    ASCIICHAT_REQUIRE_EQ(is_localhost_ipv4(a), ipv4[i].loopback, "%s", a);
    ASCIICHAT_REQUIRE_EQ(is_link_local_ipv4(a), ipv4[i].link, "%s", a);
    ASCIICHAT_REQUIRE_EQ(is_broadcast_ipv4(a), ipv4[i].multicast, "%s", a);
    ASCIICHAT_REQUIRE_EQ(is_internet_ipv4(a), ipv4[i].internet, "%s", a);
  }
  ASCIICHAT_REQUIRE(is_lan_ipv6("fd00::1"));
  ASCIICHAT_REQUIRE(is_lan_ipv6("fc00::1"));
  ASCIICHAT_REQUIRE_NOT(is_lan_ipv6("2001:4860::1"));
  ASCIICHAT_REQUIRE(is_localhost_ipv6("[::1]"));
  ASCIICHAT_REQUIRE_NOT(is_localhost_ipv6("::2"));
  ASCIICHAT_REQUIRE(is_link_local_ipv6("fe80::1"));
  ASCIICHAT_REQUIRE_NOT(is_link_local_ipv6("fec0::1"));
  ASCIICHAT_REQUIRE(is_broadcast_ipv6("ff02::1"));
  ASCIICHAT_REQUIRE_NOT(is_broadcast_ipv6("fd00::1"));
  ASCIICHAT_REQUIRE(is_internet_ipv6("2001:4860:4860::8888"));
  ASCIICHAT_REQUIRE_NOT(is_internet_ipv6("::1"));
  ASCIICHAT_REQUIRE_NOT(is_internet_ipv6("fd00::1"));
  ASCIICHAT_REQUIRE_NOT(is_internet_ipv6(NULL));
  ASCIICHAT_REQUIRE_NOT(is_link_local_ipv6("invalid"));
  ASCIICHAT_REQUIRE_NOT(is_broadcast_ipv6("invalid"));
  ASCIICHAT_REQUIRE_NOT(is_lan_ipv6(NULL));
}

TEST_CASE("ip_boundaries/optional_ports_parse_hostnames_ipv4_and_unambiguous_ipv6", "[ip_boundaries]") {
  const struct {
    const char *input, *address;
    uint16_t port;
  } cases[] = {{"localhost", "localhost", 27224},
               {"Example-host.test:443", "example-host.test", 443},
               {"192.0.2.1", "192.0.2.1", 27224},
               {"192.0.2.1:1", "192.0.2.1", 1},
               {"192.0.2.1:65535", "192.0.2.1", 65535},
               {"::1", "::1", 27224},
               {"[::1]", "::1", 27224},
               {"[::1]:0", "::1", 0},
               {"[2001:db8::1]:443", "2001:db8::1", 443},
               {"2001:db8::1234", "2001:db8::1234", 27224}};
  for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
    char output[128];
    uint16_t port = 0;
    ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(cases[i].input, output, sizeof(output), &port, 27224), 0, "%s",
                 cases[i].input);
    ASCIICHAT_REQUIRE_STR_EQ(output, cases[i].address);
    ASCIICHAT_REQUIRE_EQ(port, cases[i].port);
    ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(cases[i].input, output, strlen(cases[i].address), &port, 27224), -1);
    ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(cases[i].input, output, strlen(cases[i].address) + 1, &port, 27224),
                 0);
  }
}
TEST_CASE("ip_boundaries/optional_port_parser_rejects_malformed_addresses_and_port_overflows", "[ip_boundaries]") {
  const char *invalid[] = {"",
                           "[]",
                           "[::1",
                           "[bad]:443",
                           "[::1]junk",
                           "[::1]:",

                           "[::1]:65536",
                           "[::1]:-1",
                           "host:",
                           ":443",
                           "host:65536",
                           "host:12junk",
                           "host:9999999999999999999999999",
                           "2001:db8::wrong:443",
                           "two..labels",
                           "bad_name",
                           "-host",
                           "host-",
                           ".host",
                           "host.",
                           "bad/name:12"};
  char output[300];
  uint16_t port;
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
    ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(invalid[i], output, sizeof(output), &port, 27224), -1, "%s",
                 invalid[i]);
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(NULL, output, sizeof(output), &port, 1), -1);
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port("host", NULL, sizeof(output), &port, 1), -1);
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port("host", output, 0, &port, 1), -1);
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port("host", output, sizeof(output), NULL, 1), -1);
  char oversized[258];
  memset(oversized, 'a', sizeof(oversized) - 1);
  oversized[sizeof(oversized) - 1] = 0;
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(oversized, output, sizeof(output), &port, 1), -1);
}
TEST_CASE("ip_boundaries/hostname_label_and_total_length_limits_are_enforced_without_dns", "[ip_boundaries]") {
  char hostname[260], output[260];
  uint16_t port;
  memset(hostname, 'a', 63);
  strcpy(hostname + 63, ".test");
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(hostname, output, sizeof(output), &port, 42), 0);
  ASCIICHAT_REQUIRE_STR_EQ(hostname, output);
  hostname[63] = 'a';
  strcpy(hostname + 64, ".test");
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(hostname, output, sizeof(output), &port, 42), -1);
  memset(hostname, 'a', 253);
  hostname[63] = hostname[127] = hostname[191] = '.';
  hostname[253] = 0;
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(hostname, output, sizeof(output), &port, 42), 0);
  hostname[253] = 'a';
  hostname[254] = 0;
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(hostname, output, sizeof(output), &port, 42), -1);
}
TEST_CASE("ip_boundaries/formatted_addresses_round_trip_and_reject_short_destinations", "[ip_boundaries]") {
  const char *addresses[] = {"192.0.2.1", "2001:db8::1"};
  for (size_t i = 0; i < 2; ++i) {
    char formatted[128], recovered[128];
    uint16_t port;
    ASCIICHAT_REQUIRE_EQ(format_ip_with_port(addresses[i], 65535, formatted, sizeof(formatted)), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(formatted, recovered, sizeof(recovered), &port, 1), 0);
    ASCIICHAT_REQUIRE_STR_EQ(recovered, addresses[i]);
    ASCIICHAT_REQUIRE_EQ(port, 65535);
    ASCIICHAT_REQUIRE_EQ(format_ip_with_port(addresses[i], 65535, recovered, strlen(formatted)), ERROR_INVALID_PARAM);
    ASCIICHAT_REQUIRE_EQ(format_ip_with_port(addresses[i], 65535, recovered, strlen(formatted) + 1), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(formatted, recovered);
  }
  char out[32];
  ASCIICHAT_REQUIRE_EQ(format_ip_with_port("", 1, out, sizeof(out)), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(format_ip_with_port(NULL, 1, out, sizeof(out)), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(format_ip_with_port("::1", 1, NULL, 32), ERROR_INVALID_PARAM);
  ASCIICHAT_REQUIRE_EQ(format_ip_with_port("::1", 1, out, 0), ERROR_INVALID_PARAM);
}

TEST_CASE("ip_boundaries/hostname_and_label_byte_limits", "[ip_boundaries]") {
  char host[MAX_HOSTNAME_LENGTH + 2], parsed[HOSTNAME_BUFFER_SIZE];
  uint16_t port;
  memset(host, 'a', sizeof(host));
  host[63] = host[127] = host[191] = '.';
  host[MAX_HOSTNAME_LENGTH] = '\0';
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(host, parsed, sizeof(parsed), &port, 27224), 0);
  ASCIICHAT_REQUIRE_STR_EQ(parsed, host);
  host[MAX_HOSTNAME_LENGTH] = 'a';
  host[MAX_HOSTNAME_LENGTH + 1] = '\0';
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(host, parsed, sizeof(parsed), &port, 27224), -1);
  memset(host, 'a', sizeof(host));
  host[MAX_HOSTNAME_LABEL_LENGTH] = '\0';
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(host, parsed, sizeof(parsed), &port, 27224), 0);
  ASCIICHAT_REQUIRE_STR_EQ(parsed, host);
  host[MAX_HOSTNAME_LABEL_LENGTH] = 'a';
  host[MAX_HOSTNAME_LABEL_LENGTH + 1] = '\0';
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port(host, parsed, sizeof(parsed), &port, 27224), -1);
}

TEST_CASE("ip_boundaries/unicode_hostname_with_port_is_normalized", "[ip_boundaries]") {
  char output[HOSTNAME_BUFFER_SIZE];
  uint16_t port = 0;
  ASCIICHAT_REQUIRE_EQ(parse_address_with_optional_port("bücher.de:443", output, sizeof(output), &port, 27224), 0);
  ASCIICHAT_REQUIRE_STR_EQ(output, "xn--bcher-kva.de");
  ASCIICHAT_REQUIRE_EQ(port, 443);
}
