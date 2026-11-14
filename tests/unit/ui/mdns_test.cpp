#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tui/mdns.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/parsers.h>
#include <string.h>

TEST_CASE("mdns_selection/instance_label_is_never_used_as_a_destination", "[mdns_selection]") {
  ui_mdns_server_t server = {.name = "friendly-lan-server", .address = "host.local", .ipv6 = "fe80::1234"};
  ASCIICHAT_REQUIRE_STR_EQ(ui_mdns_get_best_address(&server), "fe80::1234");
  strcpy(server.ipv4, "192.0.2.10");
  ASCIICHAT_REQUIRE_STR_EQ(ui_mdns_get_best_address(&server), "192.0.2.10");
  server.ipv4[0] = server.ipv6[0] = '\0';
  ASCIICHAT_REQUIRE_STR_EQ(ui_mdns_get_best_address(&server), "host.local");
  ASCIICHAT_REQUIRE_STR_EMPTY(ui_mdns_get_best_address(NULL));
}

TEST_CASE("mdns_selection/empty_and_noninteractive_lists_never_select_a_server", "[mdns_selection]") {
  ui_mdns_server_t server = {.address = "192.0.2.10", .port = 27224};
  ASCIICHAT_REQUIRE_EQ(ui_mdns_select(NULL, 0), -1);
  ASCIICHAT_REQUIRE_EQ(ui_mdns_select(&server, -1), -1);
  ASCIICHAT_REQUIRE_EQ(ui_mdns_select(&server, 1), -1);
}

TEST_CASE("mdns_selection/explicit_localhost_and_websocket_addresses_disable_automatic_discovery", "[mdns_selection]") {
  options_t options{};
  char *error = NULL;
  ASCIICHAT_REQUIRE_NOT(options.address_explicit);
  ASCIICHAT_REQUIRE_EQ(parse_client_address("localhost", &options, NULL, 0, &error), 1);
  ASCIICHAT_REQUIRE(options.address_explicit);
  ASCIICHAT_REQUIRE_STR_EQ(options.address, "localhost");
  options.address_explicit = false;
  ASCIICHAT_REQUIRE_EQ(parse_client_address("ws://localhost:27226", &options, NULL, 0, &error), 1);
  ASCIICHAT_REQUIRE(options.address_explicit);
  ASCIICHAT_REQUIRE_STR_EQ(options.address, "ws://localhost:27226");
}
