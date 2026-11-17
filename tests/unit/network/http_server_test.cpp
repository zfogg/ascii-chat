#include <catch2/catch_test_macros.hpp>
#include <ascii-chat/network/http/server/server.h>
#include "../../../lib/network/http/server/server_internal.h"
#include <string.h>

static bool saw_request;
static bool saw_cancelled;
static char submitted_body[64];
static unsigned submitted_status;

static asciichat_error_t submit_response(void *context, unsigned status, const http_header_t *headers, size_t header_count, const void *body, size_t body_length) {
  (void)context;
  (void)headers;
  (void)header_count;
  submitted_status = status;
  REQUIRE((body_length) < (sizeof(submitted_body)));
  if (body_length)
    memcpy(submitted_body, body, body_length);
  submitted_body[body_length] = 0;
  return ASCIICHAT_OK;
}

static void request(http_request_t *request, http_response_t *response, void *data) {
  (void)request;
  (void)response;
  (void)data;
}

static void dispatch_request(http_request_t *request, http_response_t *response, void *data) {
  (void)data;
  const http_request_info_t *info = http_request_info(request);
  size_t body_length = 0;
  const char *body = (const char *)http_request_body(request, &body_length);
  saw_request = info && !strcmp(info->path, "/route") && info->query && !strcmp(info->query, "x=1") && info->protocol == HTTP_PROTOCOL_3 && body_length == 4 && !memcmp(body, "body", 4);
  REQUIRE((http_request_cancel(request)) == (ASCIICHAT_OK));
  saw_cancelled = http_request_is_cancelled(request);
  REQUIRE((http_response_write(response, "one", 3)) == (ASCIICHAT_OK));
  REQUIRE((http_response_write(response, "two", 3)) == (ASCIICHAT_OK));
  REQUIRE((http_response_finish(response)) == (ASCIICHAT_OK));
}

TEST_CASE("http_server/rejects_missing_handler", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.enable_http1 = true, .max_connections = 1, .max_request_body_bytes = 1024};
  REQUIRE((http_server_init(&server, &config)) != (ASCIICHAT_OK));
  REQUIRE((server) == nullptr);
}

TEST_CASE("http_server/accepts_http1_contract", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.bind_address = "127.0.0.1", .port = 0, .enable_http1 = true, .max_connections = 4, .max_request_body_bytes = 1024, .on_request = request};
  REQUIRE((http_server_init(&server, &config)) == (ASCIICHAT_OK));
  REQUIRE((server) != nullptr);
  http_server_destroy(&server);
  REQUIRE((server) == nullptr);
}

TEST_CASE("http_server/accepts_http2_contract", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.bind_address = "127.0.0.1", .port = 0, .enable_http2 = true, .max_connections = 4, .max_request_body_bytes = 1024, .on_request = request};
  REQUIRE((http_server_init(&server, &config)) == (ASCIICHAT_OK));
  http_server_destroy(&server);
  REQUIRE((server) == nullptr);
}

TEST_CASE("http_server/exposes_http1_protocol_versions", "[http_server] [unit]") {
  REQUIRE((HTTP_PROTOCOL_10) == (0));
  REQUIRE((HTTP_PROTOCOL_11) == (1));
  REQUIRE((HTTP_PROTOCOL_2) == (2));
  REQUIRE((HTTP_PROTOCOL_3) == (3));
}

TEST_CASE("http_server/exposes_wakeup_callback_in_configuration", "[http_server] [unit]") {
  http_server_config_t config = {0};
  config.on_wakeup = NULL;
  REQUIRE((config.on_wakeup) == nullptr);
}

TEST_CASE("http_server/rejects_unavailable_http3_and_tls", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.enable_http1 = true, .enable_http3 = true, .tls_cert_path = "certificate.pem", .max_connections = 4, .max_request_body_bytes = 1024, .on_request = request};
  REQUIRE((http_server_init(&server, &config)) != (ASCIICHAT_OK));
  REQUIRE((server) == nullptr);
}

TEST_CASE("http_server/accepts_http3_with_preopened_endpoint", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.enable_http3 = true, .http3_endpoint = (void *)1, .max_connections = 1, .max_request_body_bytes = 1024, .on_request = request};
  REQUIRE((http_server_init(&server, &config)) == (ASCIICHAT_OK));
  http_server_destroy(&server);
  REQUIRE((server) == nullptr);
}

TEST_CASE("http_server/accepts_custom_request_rate_limit", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.bind_address = "127.0.0.1",
                                 .port = 0,
                                 .enable_http1 = true,
                                 .max_connections = 4,
                                 .max_request_body_bytes = 1024,
                                 .max_requests_per_peer = 3,
                                 .request_rate_window_seconds = 10,
                                 .on_request = request};
  REQUIRE((http_server_init(&server, &config)) == (ASCIICHAT_OK));
  http_server_destroy(&server);
  REQUIRE((server) == nullptr);
}

TEST_CASE("http_server/dispatches_http3_request_contract", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.enable_http3 = true, .http3_endpoint = (void *)1, .max_connections = 1, .max_request_body_bytes = 1024, .on_request = dispatch_request};
  REQUIRE((http_server_init(&server, &config)) == (ASCIICHAT_OK));
  saw_request = false;
  saw_cancelled = false;
  submitted_body[0] = 0;
  REQUIRE((http_server_dispatch_http3(server, "POST", "/route?x=1", NULL, 0, "body", 4, "peer", submit_response, NULL)) == (ASCIICHAT_OK));
  REQUIRE(saw_request);
  REQUIRE(saw_cancelled);
  REQUIRE((submitted_status) == (200));
  REQUIRE(std::strcmp((submitted_body), ("onetwo")) == 0);
  http_server_destroy(&server);
}

TEST_CASE("http_server/rejects_http3_body_over_configured_limit", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.enable_http3 = true, .http3_endpoint = (void *)1, .max_connections = 1, .max_request_body_bytes = 3, .on_request = request};
  REQUIRE((http_server_init(&server, &config)) == (ASCIICHAT_OK));
  REQUIRE((http_server_dispatch_http3(server, "POST", "/route", NULL, 0, "body", 4, "peer", submit_response, NULL)) != (ASCIICHAT_OK));
  http_server_destroy(&server);
}

TEST_CASE("http_server/starts_and_drains_private_loop", "[http_server] [unit]") {
  http_server_t *server = NULL;
  http_server_config_t config = {.bind_address = "127.0.0.1", .port = 0, .enable_http1 = true, .max_connections = 4, .max_request_body_bytes = 1024, .on_request = request};
  REQUIRE((http_server_init(&server, &config)) == (ASCIICHAT_OK));
  REQUIRE((http_server_start(server)) == (ASCIICHAT_OK));
  REQUIRE((http_server_wakeup(server)) == (ASCIICHAT_OK));
  http_server_stop(server);
  http_server_stop(server);
  http_server_destroy(&server);
  REQUIRE((server) == nullptr);
}
