#include <criterion/criterion.h>
#include <ascii-chat/network/http/server.h>

static void request(http_request_t *request, http_response_t *response, void *data) {
  (void)request;
  (void)response;
  (void)data;
}

Test(http_server, rejects_missing_handler) {
  http_server_t *server = NULL;
  http_server_config_t config = {.enable_http1 = true, .max_connections = 1, .max_request_body_bytes = 1024};
  cr_assert_neq(http_server_init(&server, &config), ASCIICHAT_OK);
  cr_assert_null(server);
}

Test(http_server, accepts_http1_contract) {
  http_server_t *server = NULL;
  http_server_config_t config = {.bind_address = "127.0.0.1",
                                 .port = 0,
                                 .enable_http1 = true,
                                 .max_connections = 4,
                                 .max_request_body_bytes = 1024,
                                 .on_request = request};
  cr_assert_eq(http_server_init(&server, &config), ASCIICHAT_OK);
  cr_assert_not_null(server);
  http_server_destroy(&server);
  cr_assert_null(server);
}

Test(http_server, starts_and_drains_private_loop) {
  http_server_t *server = NULL;
  http_server_config_t config = {.bind_address = "127.0.0.1",
                                 .port = 0,
                                 .enable_http1 = true,
                                 .max_connections = 4,
                                 .max_request_body_bytes = 1024,
                                 .on_request = request};
  cr_assert_eq(http_server_init(&server, &config), ASCIICHAT_OK);
  cr_assert_eq(http_server_start(server), ASCIICHAT_OK);
  http_server_stop(server);
  http_server_stop(server);
  http_server_destroy(&server);
  cr_assert_null(server);
}
