#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/network/webrtc/ice.h>
#include <ascii-chat/common.h>
#include <string.h>

TEST_CASE("ice_candidate/parses_and_formats_all_candidate_types_and_tcp_modes", "[ice_candidate] [unit]") {
  const struct {
    const char *line;
    ice_candidate_type_t type;
    ice_protocol_t protocol;
    ice_tcp_type_t tcp_type;
  } cases[] = {
      {"local 1 udp 2130706431 192.168.1.20 5000 typ host", ICE_CANDIDATE_HOST, ICE_PROTOCOL_UDP, ICE_TCP_TYPE_ACTIVE},
      {"nat 2 udp 1694498814 8.8.8.8 54321 typ srflx raddr 192.168.1.20 rport 5000", ICE_CANDIDATE_SRFLX, ICE_PROTOCOL_UDP, ICE_TCP_TYPE_ACTIVE},
      {"peer 1 udp 1862270975 2001:4860:4860::8888 6000 typ prflx raddr fd00::20 rport 5000", ICE_CANDIDATE_PRFLX, ICE_PROTOCOL_UDP, ICE_TCP_TYPE_ACTIVE},
      {"turn 1 udp 16777215 1.1.1.1 3478 typ relay raddr 10.0.0.2 rport 6000", ICE_CANDIDATE_RELAY, ICE_PROTOCOL_UDP, ICE_TCP_TYPE_ACTIVE},
      {"active 1 tcp 1 ::1 9 typ host tcptype active", ICE_CANDIDATE_HOST, ICE_PROTOCOL_TCP, ICE_TCP_TYPE_ACTIVE},
      {"passive 1 tcp 2 127.0.0.1 443 typ host tcptype passive", ICE_CANDIDATE_HOST, ICE_PROTOCOL_TCP, ICE_TCP_TYPE_PASSIVE},
      {"simultaneous 1 tcp 3 10.0.0.1 443 typ host tcptype so", ICE_CANDIDATE_HOST, ICE_PROTOCOL_TCP, ICE_TCP_TYPE_SO},
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    ice_candidate_t candidate;
    REQUIRE((ice_parse_candidate(cases[i].line, &candidate)) == (ASCIICHAT_OK));
    REQUIRE((candidate.type) == (cases[i].type));
    REQUIRE((candidate.protocol) == (cases[i].protocol));
    REQUIRE((candidate.component_id) >= (1));
    REQUIRE((candidate.component_id) <= (2));
    REQUIRE((candidate.port) != (0));
    REQUIRE((candidate.ip_address[0]) != ('\0'));
    if (candidate.protocol == ICE_PROTOCOL_TCP)
      REQUIRE((candidate.tcp_type) == (cases[i].tcp_type));
    char line[512];
    REQUIRE((ice_format_candidate(&candidate, line, sizeof(line))) == (ASCIICHAT_OK));
    REQUIRE(std::strcmp((line), (cases[i].line)) == 0);
    char exact[512];
    REQUIRE((ice_format_candidate(&candidate, exact, strlen(line) + 1)) == (ASCIICHAT_OK));
    REQUIRE((ice_format_candidate(&candidate, exact, strlen(line))) == (ERROR_BUFFER_FULL));
  }
}

TEST_CASE("ice_candidate/rejects_malformed_input_and_missing_outputs", "[ice_candidate] [unit]") {
  const char *invalid[] = {"", "host 1 udp", "f 1 sctp 1 127.0.0.1 9 typ host", "f 1 udp 1 127.0.0.1 9 typ other", "f one udp 1 127.0.0.1 9 typ host", "f 1 udp 1 not-an-ip 9 typ host"};
  ice_candidate_t candidate = {0};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    REQUIRE((ice_parse_candidate(invalid[i], &candidate)) == (ERROR_INVALID_PARAM));
  REQUIRE((ice_parse_candidate(NULL, &candidate)) == (ERROR_INVALID_PARAM));
  REQUIRE((ice_parse_candidate("f 1 udp 1 ::1 9 typ host", NULL)) == (ERROR_INVALID_PARAM));
  char line[512];
  REQUIRE((ice_format_candidate(NULL, line, sizeof(line))) == (ERROR_INVALID_PARAM));
  REQUIRE((ice_format_candidate(&candidate, NULL, sizeof(line))) == (ERROR_INVALID_PARAM));
  REQUIRE((ice_format_candidate(&candidate, line, 0)) == (ERROR_INVALID_PARAM));
  REQUIRE((ice_add_remote_candidate(NULL, &candidate, "video")) == (ERROR_INVALID_PARAM));
  REQUIRE(!(ice_is_connected(NULL)));
  REQUIRE((ice_calculate_priority_for_candidate(NULL)) == (0));
  REQUIRE(std::strcmp((ice_candidate_type_name((ice_candidate_type_t)99)), ("unknown")) == 0);
  REQUIRE(std::strcmp((ice_protocol_name((ice_protocol_t)99)), ("unknown")) == 0);
}

TEST_CASE("ice_candidate/formats_extensions_without_overwriting_short_buffers", "[ice_candidate] [unit]") {
  ice_candidate_t candidate = {.component_id = 1, .protocol = ICE_PROTOCOL_TCP, .type = ICE_CANDIDATE_RELAY, .priority = 1, .port = 443, .rport = 5000, .tcp_type = ICE_TCP_TYPE_SO};
  SAFE_STRNCPY(candidate.foundation, "relay", sizeof(candidate.foundation));
  SAFE_STRNCPY(candidate.ip_address, "1.1.1.1", sizeof(candidate.ip_address));
  SAFE_STRNCPY(candidate.raddr, "10.0.0.1", sizeof(candidate.raddr));
  SAFE_STRNCPY(candidate.extensions, "generation 0", sizeof(candidate.extensions));
  const char expected[] = "relay 1 tcp 1 1.1.1.1 443 typ relay raddr 10.0.0.1 rport 5000 tcptype so generation 0";
  char output[sizeof(expected) + 1];
  for (size_t size = 1; size < sizeof(expected); ++size) {
    memset(output, '!', sizeof(output));
    REQUIRE((ice_format_candidate(&candidate, output, size)) == (ERROR_BUFFER_FULL));
    REQUIRE((output[size]) == ('!'));
  }
  REQUIRE((ice_format_candidate(&candidate, output, sizeof(expected))) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((output), (expected)) == 0);
}

TEST_CASE("ice_candidate/priority_follows_rfc_and_prefers_lan_then_loopback_then_internet", "[ice_candidate] [unit]") {
  REQUIRE((ice_calculate_priority(ICE_CANDIDATE_HOST, 65535, 1)) == (UINT32_C(2130706431)));
  REQUIRE((ice_calculate_priority(ICE_CANDIDATE_SRFLX, 65535, 2)) == (UINT32_C(1694498814)));
  REQUIRE((ice_calculate_priority(ICE_CANDIDATE_PRFLX, 0, 1)) == (UINT32_C(1845494015)));
  REQUIRE((ice_calculate_priority(ICE_CANDIDATE_RELAY, 0, 1)) == (255));
  REQUIRE((ice_calculate_priority((ice_candidate_type_t)99, 0, 1)) == (255));
  const struct {
    const char *ip;
    uint32_t expected;
  } cases[] = {{"192.168.1.5", 2130706431},          {"10.0.0.1", 2130706431}, {"fd00::1", 2130706431}, {"127.0.0.1", 2130569471}, {"::1", 2130569471},    {"8.8.8.8", 2122318079},
               {"2001:4860:4860::8888", 2122318079}, {"0.0.0.0", 2113929471},  {"::", 2113929471},      {"", 2113929471},          {"invalid", 2113929471}};
  ice_candidate_t candidate = {.type = ICE_CANDIDATE_HOST, .component_id = 1};
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    SAFE_STRNCPY(candidate.ip_address, cases[i].ip, sizeof(candidate.ip_address));
    REQUIRE((ice_calculate_priority_for_candidate(&candidate)) == (cases[i].expected));
  }
}
