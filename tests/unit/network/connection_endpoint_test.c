#include <criterion/criterion.h>
#include <criterion/parameterized.h>

#include <ascii-chat/network/connection_endpoint.h>

#include <string.h>

typedef struct {
  const char *input;
  uint16_t default_port;
  connection_endpoint_protocol_t protocol;
  const char *host;
  uint16_t port;
  bool valid;
} endpoint_case_t;

static void assert_endpoint_case(const endpoint_case_t *tc) {
  connection_endpoint_t endpoint = {0};
  asciichat_error_t result = connection_endpoint_resolve(tc->input, tc->default_port, &endpoint);

  if (!tc->valid) {
    cr_assert_neq(result, ASCIICHAT_OK, "Expected endpoint resolution to fail for %s", tc->input);
    return;
  }

  cr_assert_eq(result, ASCIICHAT_OK, "Expected endpoint resolution to succeed for %s", tc->input);
  cr_assert_eq(endpoint.protocol, tc->protocol, "Unexpected protocol for %s", tc->input);
  cr_assert_str_eq(endpoint.host, tc->host, "Unexpected host for %s", tc->input);
  cr_assert_eq(endpoint.port, tc->port, "Unexpected port for %s", tc->input);
}

static endpoint_case_t endpoint_cases[] = {
    {.input = "ws://example.com:443", .default_port = 27225, .protocol = CONNECTION_ENDPOINT_WEBSOCKET,
     .host = "example.com", .port = 443, .valid = true},
    {.input = "wss://example.com", .default_port = 27225, .protocol = CONNECTION_ENDPOINT_WEBSOCKET,
     .host = "example.com", .port = 27225, .valid = true},
    {.input = "tcp://example.com:5000", .default_port = 27225, .protocol = CONNECTION_ENDPOINT_TCP,
     .host = "example.com", .port = 5000, .valid = true},
    {.input = "example.com", .default_port = 27224, .protocol = CONNECTION_ENDPOINT_TCP, .host = "example.com",
     .port = 27224, .valid = true},
    {.input = "example.com:6000", .default_port = 27224, .protocol = CONNECTION_ENDPOINT_TCP, .host = "example.com",
     .port = 6000, .valid = true},
    {.input = "http://example.com", .default_port = 27224, .valid = false},
};

ParameterizedTestParameters(connection_endpoint, cases) {
  return cr_make_param_array(endpoint_case_t, endpoint_cases, sizeof(endpoint_cases) / sizeof(endpoint_case_t));
}

ParameterizedTest(endpoint_case_t *tc, connection_endpoint, cases) {
  assert_endpoint_case(tc);
}
