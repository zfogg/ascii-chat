#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/util/url.h>
#include <ascii-chat/util/ip.h>

#include <ascii-chat/network/connection_endpoint.h>
TEST_CASE("connection_endpoint/resolve_ws_with_port", "[connection_endpoint]") {
  connection_endpoint_t endpoint{};
  asciichat_error_t result = connection_endpoint_resolve("ws://example.com:443", 27225, &endpoint);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((endpoint.protocol) == (CONNECTION_ENDPOINT_WEBSOCKET));
  REQUIRE(std::strcmp((endpoint.host), ("example.com")) == 0);
  REQUIRE((endpoint.port) == (443));
}

TEST_CASE("connection_endpoint/resolve_wss_without_port", "[connection_endpoint]") {
  connection_endpoint_t endpoint{};
  asciichat_error_t result = connection_endpoint_resolve("wss://example.com", 27225, &endpoint);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((endpoint.protocol) == (CONNECTION_ENDPOINT_WEBSOCKET));
  REQUIRE(std::strcmp((endpoint.host), ("example.com")) == 0);
  REQUIRE((endpoint.port) == (27225));
}

TEST_CASE("connection_endpoint/resolve_tcp_with_port", "[connection_endpoint]") {
  connection_endpoint_t endpoint{};
  asciichat_error_t result = connection_endpoint_resolve("tcp://example.com:5000", 27225, &endpoint);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((endpoint.protocol) == (CONNECTION_ENDPOINT_TCP));
  REQUIRE(std::strcmp((endpoint.host), ("example.com")) == 0);
  REQUIRE((endpoint.port) == (5000));
}

TEST_CASE("connection_endpoint/resolve_quic_endpoints", "[connection_endpoint]") {
  connection_endpoint_t endpoint{};
  REQUIRE((connection_endpoint_resolve("quic://example.com:443", 27224, &endpoint)) == (ASCIICHAT_OK));
  REQUIRE((endpoint.protocol) == (CONNECTION_ENDPOINT_QUIC));
  REQUIRE(std::strcmp((endpoint.host), ("example.com")) == 0);
  REQUIRE((endpoint.port) == (443));
  REQUIRE((connection_endpoint_resolve("quic://[::1]", 27224, &endpoint)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((endpoint.host), ("::1")) == 0);
  REQUIRE((endpoint.port) == (27224));
  REQUIRE((connection_endpoint_resolve("quic://example.com/path", 27224, &endpoint)) == (ERROR_INVALID_PARAM));
  REQUIRE((connection_endpoint_resolve("quic://user@example.com", 27224, &endpoint)) == (ERROR_INVALID_PARAM));
}

TEST_CASE("connection_endpoint/resolve_bare_hostname", "[connection_endpoint]") {
  connection_endpoint_t endpoint{};
  asciichat_error_t result = connection_endpoint_resolve("example.com", 27224, &endpoint);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((endpoint.protocol) == (CONNECTION_ENDPOINT_TCP));
  REQUIRE(std::strcmp((endpoint.host), ("example.com")) == 0);
  REQUIRE((endpoint.port) == (27224));
}

TEST_CASE("connection_endpoint/resolve_hostname_with_port", "[connection_endpoint]") {
  connection_endpoint_t endpoint{};
  asciichat_error_t result = connection_endpoint_resolve("example.com:6000", 27224, &endpoint);

  REQUIRE((result) == (ASCIICHAT_OK));
  REQUIRE((endpoint.protocol) == (CONNECTION_ENDPOINT_TCP));
  REQUIRE(std::strcmp((endpoint.host), ("example.com")) == 0);
  REQUIRE((endpoint.port) == (6000));
}

TEST_CASE("connection_endpoint/resolve_invalid_scheme", "[connection_endpoint]") {
  connection_endpoint_t endpoint{};
  asciichat_error_t result = connection_endpoint_resolve("http://example.com", 27224, &endpoint);

  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("connection_endpoint/url_storage_boundary_does_not_truncate_or_mutate_on_error", "[connection_endpoint]") {
  char input[URL_BUFFER_SIZE + 1];
  const char *prefix = "wss://example.com/";
  size_t prefix_len = strlen(prefix);
  memcpy(input, prefix, prefix_len);
  memset(input + prefix_len, 'a', MAX_URL_LENGTH - prefix_len);
  input[MAX_URL_LENGTH] = '\0';
  connection_endpoint_t endpoint{};
  REQUIRE((connection_endpoint_resolve(input, 443, &endpoint)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((endpoint.input), (input)) == 0);
  connection_endpoint_t before = endpoint;
  input[MAX_URL_LENGTH] = 'a';
  input[MAX_URL_LENGTH + 1] = '\0';
  REQUIRE((connection_endpoint_resolve(input, 443, &endpoint)) == (ERROR_INVALID_PARAM));
  REQUIRE(std::memcmp((&endpoint), (&before), (sizeof(endpoint))) == 0);
}

TEST_CASE("connection_endpoint/host_storage_boundary_preserves_full_host", "[connection_endpoint]") {
  char host[HOSTNAME_BUFFER_SIZE + 1];
  memset(host, 'a', sizeof(host));
  host[63] = host[127] = host[191] = '.';
  host[MAX_HOSTNAME_LENGTH] = '\0';
  connection_endpoint_t endpoint{};
  REQUIRE((connection_endpoint_resolve(host, 27224, &endpoint)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((endpoint.host), (host)) == 0);
  connection_endpoint_t before = endpoint;
  host[MAX_HOSTNAME_LENGTH] = 'a';
  host[MAX_HOSTNAME_LENGTH + 1] = '\0';
  REQUIRE((connection_endpoint_resolve(host, 27224, &endpoint)) == (ERROR_INVALID_PARAM));
  REQUIRE(std::memcmp((&endpoint), (&before), (sizeof(endpoint))) == 0);
}

TEST_CASE("connection_endpoint/unicode_and_ascii_names_share_connection_identity", "[connection_endpoint]") {
  connection_endpoint_t unicode{}, ascii{};
  REQUIRE((connection_endpoint_resolve("wss://bücher.de:443/日本語", 443, &unicode)) == (ASCIICHAT_OK));
  REQUIRE((connection_endpoint_resolve("wss://xn--bcher-kva.de:443/日本語", 443, &ascii)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((unicode.host), (ascii.host)) == 0);
  REQUIRE(std::strcmp((unicode.input), ("wss://bücher.de:443/日本語")) == 0);
  REQUIRE((unicode.port) == (ascii.port));
}

TEST_CASE("connection_endpoint/unicode_input_byte_length_does_not_bound_ascii_host_storage", "[connection_endpoint]") {
  char host[512], ascii[HOSTNAME_BUFFER_SIZE];
  size_t used = 0;
  for (size_t label = 0; label < 4; ++label) {
    if (label)
      host[used++] = '.';
    for (size_t i = 0; i < 50; ++i) {
      host[used++] = (char)0xC3;
      host[used++] = (char)0xBC;
    }
  }
  host[used] = '\0';
  REQUIRE((used) > (HOSTNAME_BUFFER_SIZE));
  REQUIRE((hostname_to_ascii(host, ascii, sizeof(ascii))) == (ASCIICHAT_OK));
  connection_endpoint_t endpoint{};
  REQUIRE((connection_endpoint_resolve(host, 27224, &endpoint)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((endpoint.host), (ascii)) == 0);
  uint16_t port;
  char parsed[HOSTNAME_BUFFER_SIZE];
  REQUIRE((parse_address_with_optional_port(host, parsed, sizeof(parsed), &port, 27224)) == (0));
  REQUIRE(std::strcmp((parsed), (ascii)) == 0);
}
