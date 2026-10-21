#include <criterion/criterion.h>
#include <ascii-chat/util/ip.h>
#include <ascii-chat/tests/common.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

TestSuite(ip_boundaries);

Test(ip_boundaries, cidr_network_edges_and_address_families) {
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
    cr_assert_eq(ip_in_cidr(cases[i].ip, cases[i].network), cases[i].contains, "%s in %s", cases[i].ip,
                 cases[i].network);
}

Test(ip_boundaries, rejects_malformed_cidr_and_short_buffers) {
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
    cr_assert_eq(parse_cidr(invalid[i], output, sizeof(output), &prefix), -1, "%s", invalid[i]);
  cr_assert_eq(parse_cidr(NULL, output, sizeof(output), &prefix), -1);
  cr_assert_eq(parse_cidr("::/64", NULL, sizeof(output), &prefix), -1);
  cr_assert_eq(parse_cidr("::/64", output, 1, &prefix), -1);
  cr_assert_eq(parse_cidr("::/64", output, sizeof(output), NULL), -1);
  cr_assert_eq(parse_cidr("[2001:db8::]/48", output, sizeof(output), &prefix), 0);
  cr_assert_str_eq(output, "2001:db8::");
  cr_assert_eq(prefix, 48);
  cr_assert_eq(ip_in_cidr_parsed("::1", "::", -1), 0);
  cr_assert_eq(ip_in_cidr_parsed("::1", "::", 129), 0);
}

Test(ip_boundaries, canonical_addresses_and_mapped_ipv4_round_trip) {
  char mapped[128], v4[32], expanded[128], compact[128];
  const char *addresses[] = {"0.0.0.0", "192.0.2.42", "255.255.255.255"};
  for (size_t i = 0; i < sizeof(addresses) / sizeof(*addresses); ++i) {
    cr_assert_eq(ipv4_to_ipv6_mapped(addresses[i], mapped, sizeof(mapped)), ASCIICHAT_OK);
    cr_assert(is_ipv4_mapped_ipv6(mapped));
    cr_assert_eq(extract_ipv4_from_mapped_ipv6(mapped, v4, sizeof(v4)), ASCIICHAT_OK);
    cr_assert_str_eq(v4, addresses[i]);
  }
  cr_assert_eq(expand_ipv6("2001:db8::1", expanded, sizeof(expanded)), ASCIICHAT_OK);
  cr_assert_str_eq(expanded, "2001:0db8:0000:0000:0000:0000:0000:0001");
  cr_assert_eq(compact_ipv6(expanded, compact, sizeof(compact)), ASCIICHAT_OK);
  cr_assert_str_eq(compact, "2001:db8::1");
  cr_assert_eq(canonicalize_ipv6("[2001:DB8::1]", compact, sizeof(compact)), ASCIICHAT_OK);
  cr_assert(ip_equals(compact, expanded));
  cr_assert_neq(ipv4_to_ipv6_mapped("bad", mapped, sizeof(mapped)), ASCIICHAT_OK);
  cr_assert_neq(ipv4_to_ipv6_mapped("1.2.3.4", mapped, 2), ASCIICHAT_OK);
  cr_assert_neq(extract_ipv4_from_mapped_ipv6("::1", v4, sizeof(v4)), ASCIICHAT_OK);
  cr_assert_neq(expand_ipv6("bad", expanded, sizeof(expanded)), ASCIICHAT_OK);
  cr_assert_neq(canonicalize_ipv6("::1", compact, 1), ASCIICHAT_OK);
  cr_assert_not(is_ipv4_mapped_ipv6(NULL));
}

Test(ip_boundaries, numeric_sorting_normalization_and_invalid_addresses) {
  cr_assert(ip_equals("[::1]", "0:0:0:0:0:0:0:1"));
  cr_assert_not(ip_equals("127.0.0.1", "::1"));
  cr_assert_not(ip_equals(NULL, "::1"));
  cr_assert_lt(ip_compare("10.0.0.2", "10.0.0.10"), 0);
  cr_assert_gt(ip_compare("10.0.0.10", "10.0.0.2"), 0);
  cr_assert_eq(ip_compare("::1", "[::1]"), 0);
  cr_assert_lt(ip_compare("::1", "::2"), 0);
  cr_assert_lt(ip_compare("1.2.3.4", "::1"), 0);
  cr_assert_gt(ip_compare("bad", "::1"), 0);
  cr_assert_lt(ip_compare("::1", "bad"), 0);
  cr_assert_eq(ip_compare("bad", "invalid"), 0);
  cr_assert_eq(ip_compare(NULL, "::1"), -2);
  cr_assert_eq(compare_ip_port_strings("127.0.0.1:20", "127.0.0.1:30"), 0);
  cr_assert_eq(compare_ip_port_strings("[::1]:42", "[0:0:0:0:0:0:0:1]:42"), 1);
}

Test(ip_boundaries, classify_private_loopback_link_local_and_public_addresses) {
  const struct {
    const char *address;
    int lan, loopback, link, multicast, internet;
  } ipv4[] = {{"10.255.255.255", 1, 0, 0, 0, 0},  {"172.16.0.0", 1, 0, 0, 0, 0}, {"172.31.255.255", 1, 0, 0, 0, 0},
              {"192.168.1.1", 1, 0, 0, 0, 0},     {"127.0.0.2", 0, 1, 0, 0, 0},  {"169.254.1.1", 0, 0, 1, 0, 0},
              {"255.255.255.255", 0, 0, 0, 1, 0}, {"8.8.8.8", 0, 0, 0, 0, 1},    {"bad", 0, 0, 0, 0, 0}};
  for (size_t i = 0; i < sizeof(ipv4) / sizeof(*ipv4); ++i) {
    const char *a = ipv4[i].address;
    cr_assert_eq(is_lan_ipv4(a), ipv4[i].lan, "%s", a);
    cr_assert_eq(is_localhost_ipv4(a), ipv4[i].loopback, "%s", a);
    cr_assert_eq(is_link_local_ipv4(a), ipv4[i].link, "%s", a);
    cr_assert_eq(is_broadcast_ipv4(a), ipv4[i].multicast, "%s", a);
    cr_assert_eq(is_internet_ipv4(a), ipv4[i].internet, "%s", a);
  }
  cr_assert(is_lan_ipv6("fd00::1"));
  cr_assert(is_lan_ipv6("fc00::1"));
  cr_assert_not(is_lan_ipv6("2001:4860::1"));
  cr_assert(is_localhost_ipv6("[::1]"));
  cr_assert_not(is_localhost_ipv6("::2"));
  cr_assert(is_link_local_ipv6("fe80::1"));
  cr_assert_not(is_link_local_ipv6("fec0::1"));
  cr_assert(is_broadcast_ipv6("ff02::1"));
  cr_assert_not(is_broadcast_ipv6("fd00::1"));
  cr_assert(is_internet_ipv6("2001:4860:4860::8888"));
  cr_assert_not(is_internet_ipv6("::1"));
  cr_assert_not(is_internet_ipv6("fd00::1"));
  cr_assert_not(is_internet_ipv6(NULL));
  cr_assert_not(is_link_local_ipv6("invalid"));
  cr_assert_not(is_broadcast_ipv6("invalid"));
  cr_assert_not(is_lan_ipv6(NULL));
}
