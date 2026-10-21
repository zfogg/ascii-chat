#include <criterion/criterion.h>
#include <ascii-chat/ui/mdns.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/parsers.h>
#include <string.h>

Test(mdns_selection, instance_label_is_never_used_as_a_destination) {
  ui_mdns_server_t server = {.name = "friendly-lan-server", .address = "host.local", .ipv6 = "fe80::1234"};
  cr_assert_str_eq(ui_mdns_get_best_address(&server), "fe80::1234");
  strcpy(server.ipv4, "192.0.2.10");
  cr_assert_str_eq(ui_mdns_get_best_address(&server), "192.0.2.10");
  server.ipv4[0] = server.ipv6[0] = '\0';
  cr_assert_str_eq(ui_mdns_get_best_address(&server), "host.local");
  cr_assert_str_empty(ui_mdns_get_best_address(NULL));
}

Test(mdns_selection, empty_and_noninteractive_lists_never_select_a_server) {
  ui_mdns_server_t server = {.address = "192.0.2.10", .port = 27224};
  cr_assert_eq(ui_mdns_select(NULL, 0), -1);
  cr_assert_eq(ui_mdns_select(&server, -1), -1);
  cr_assert_eq(ui_mdns_select(&server, 1), -1);
}

Test(mdns_selection, explicit_localhost_and_websocket_addresses_disable_automatic_discovery) {
  options_t options = {0};
  char *error = NULL;
  cr_assert_not(options.address_explicit);
  cr_assert_eq(parse_client_address("localhost", &options, NULL, 0, &error), 1);
  cr_assert(options.address_explicit);
  cr_assert_str_eq(options.address, "localhost");
  options.address_explicit = false;
  cr_assert_eq(parse_client_address("ws://localhost:27226", &options, NULL, 0, &error), 1);
  cr_assert(options.address_explicit);
  cr_assert_str_eq(options.address, "ws://localhost:27226");
}
