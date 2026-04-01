#include <criterion/criterion.h>

#include <ascii-chat/network/connection_endpoint.h>

TestSuite(connection_endpoint);

Test(connection_endpoint, resolve_ws_with_port) {
  connection_endpoint_t endpoint = {0};
  asciichat_error_t result = connection_endpoint_resolve("ws://example.com:443", 27225, &endpoint);

  cr_assert_eq(result, ASCIICHAT_OK, "Expected resolution to succeed");
  cr_assert_eq(endpoint.protocol, CONNECTION_ENDPOINT_WEBSOCKET, "Unexpected protocol");
  cr_assert_str_eq(endpoint.host, "example.com", "Unexpected host");
  cr_assert_eq(endpoint.port, 443, "Unexpected port");
}

Test(connection_endpoint, resolve_wss_without_port) {
  connection_endpoint_t endpoint = {0};
  asciichat_error_t result = connection_endpoint_resolve("wss://example.com", 27225, &endpoint);

  cr_assert_eq(result, ASCIICHAT_OK, "Expected resolution to succeed");
  cr_assert_eq(endpoint.protocol, CONNECTION_ENDPOINT_WEBSOCKET, "Unexpected protocol");
  cr_assert_str_eq(endpoint.host, "example.com", "Unexpected host");
  cr_assert_eq(endpoint.port, 27225, "Should use default port");
}

Test(connection_endpoint, resolve_tcp_with_port) {
  connection_endpoint_t endpoint = {0};
  asciichat_error_t result = connection_endpoint_resolve("tcp://example.com:5000", 27225, &endpoint);

  cr_assert_eq(result, ASCIICHAT_OK, "Expected resolution to succeed");
  cr_assert_eq(endpoint.protocol, CONNECTION_ENDPOINT_TCP, "Unexpected protocol");
  cr_assert_str_eq(endpoint.host, "example.com", "Unexpected host");
  cr_assert_eq(endpoint.port, 5000, "Unexpected port");
}

Test(connection_endpoint, resolve_bare_hostname) {
  connection_endpoint_t endpoint = {0};
  asciichat_error_t result = connection_endpoint_resolve("example.com", 27224, &endpoint);

  cr_assert_eq(result, ASCIICHAT_OK, "Expected resolution to succeed");
  cr_assert_eq(endpoint.protocol, CONNECTION_ENDPOINT_TCP, "Unexpected protocol");
  cr_assert_str_eq(endpoint.host, "example.com", "Unexpected host");
  cr_assert_eq(endpoint.port, 27224, "Should use default port");
}

Test(connection_endpoint, resolve_hostname_with_port) {
  connection_endpoint_t endpoint = {0};
  asciichat_error_t result = connection_endpoint_resolve("example.com:6000", 27224, &endpoint);

  cr_assert_eq(result, ASCIICHAT_OK, "Expected resolution to succeed");
  cr_assert_eq(endpoint.protocol, CONNECTION_ENDPOINT_TCP, "Unexpected protocol");
  cr_assert_str_eq(endpoint.host, "example.com", "Unexpected host");
  cr_assert_eq(endpoint.port, 6000, "Unexpected port");
}

Test(connection_endpoint, resolve_invalid_scheme) {
  connection_endpoint_t endpoint = {0};
  asciichat_error_t result = connection_endpoint_resolve("http://example.com", 27224, &endpoint);

  cr_assert_neq(result, ASCIICHAT_OK, "Expected resolution to fail for invalid scheme");
}
