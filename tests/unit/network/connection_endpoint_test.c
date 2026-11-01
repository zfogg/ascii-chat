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

Test(connection_endpoint, url_storage_boundary_does_not_truncate_or_mutate_on_error) {
  char input[URL_BUFFER_SIZE + 1];
  const char *prefix = "wss://example.com/";
  size_t prefix_len = strlen(prefix);
  memcpy(input, prefix, prefix_len);
  memset(input + prefix_len, 'a', MAX_URL_LENGTH - prefix_len);
  input[MAX_URL_LENGTH] = '\0';
  connection_endpoint_t endpoint = {0};
  cr_assert_eq(connection_endpoint_resolve(input, 443, &endpoint), ASCIICHAT_OK);
  cr_assert_str_eq(endpoint.input, input);
  connection_endpoint_t before = endpoint;
  input[MAX_URL_LENGTH] = 'a';
  input[MAX_URL_LENGTH + 1] = '\0';
  cr_assert_eq(connection_endpoint_resolve(input, 443, &endpoint), ERROR_INVALID_PARAM);
  cr_assert_arr_eq(&endpoint, &before, sizeof(endpoint));
}

Test(connection_endpoint, host_storage_boundary_preserves_full_host) {
  char host[HOSTNAME_BUFFER_SIZE + 1];
  memset(host, 'a', sizeof(host));
  host[63] = host[127] = host[191] = '.';
  host[HOSTNAME_BUFFER_SIZE - 1] = '\0';
  connection_endpoint_t endpoint = {0};
  cr_assert_eq(connection_endpoint_resolve(host, 27224, &endpoint), ASCIICHAT_OK);
  cr_assert_str_eq(endpoint.host, host);
  connection_endpoint_t before = endpoint;
  host[HOSTNAME_BUFFER_SIZE - 1] = 'a';
  host[HOSTNAME_BUFFER_SIZE] = '\0';
  cr_assert_eq(connection_endpoint_resolve(host, 27224, &endpoint), ERROR_INVALID_PARAM);
  cr_assert_arr_eq(&endpoint, &before, sizeof(endpoint));
}
