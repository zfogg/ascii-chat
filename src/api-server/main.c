#include "main.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/network/http/server/server.h>
#include <ascii-chat/platform/systemd.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/version.h>
#include <string.h>
#include <uv.h>

static void api_finish(http_response_t *response, unsigned status) {
  (void)http_response_set_status(response, status);
  (void)http_response_finish(response);
}

static void api_request(http_request_t *request, http_response_t *response, void *user_data) {
  (void)user_data;
  const http_request_info_t *info = http_request_info(request);
  if (!strcmp(info->method, "GET") && !strcmp(info->path, "/health")) {
    static const char body[] = "{\"status\":\"ok\",\"service\":\"ascii-chat-api\",\"version\":\"" ASCII_CHAT_VERSION_FULL "\"}";
    (void)http_response_set_header(response, "Content-Type", "application/json");
    (void)http_response_write(response, body, sizeof(body) - 1);
    api_finish(response, 200);
    return;
  }
  if (strcmp(info->path, "/health") && strcmp(info->path, "/convert")) {
    api_finish(response, 404);
    return;
  }
  api_finish(response, !strcmp(info->path, "/health") ? 405 : 501);
}

int api_server_main(void) {
  http_server_t *server = NULL;
  http_server_config_t config = {.bind_address = GET_OPTION(address),
                                 .port = (uint16_t)GET_OPTION(port),
                                 .enable_http1 = true,
                                 .enable_http2 = true,
                                 .enable_http3 = true,
                                 .tls_cert_path = GET_OPTION(tls_cert),
                                 .tls_key_path = GET_OPTION(tls_key),
                                 .tls_key_password = GET_OPTION(tls_key_password),
                                 .max_connections = (size_t)GET_OPTION(max_clients),
                                 .max_request_body_bytes = 10u * 1024u * 1024u,
                                 .max_response_body_bytes = 8u * 1024u * 1024u,
                                 .on_request = api_request};
  if (!config.max_connections)
    config.max_connections = 1;
  asciichat_error_t err = http_server_init(&server, &config);
  if (err != ASCIICHAT_OK)
    return err;
  err = http_server_start(server);
  if (err != ASCIICHAT_OK) {
    http_server_destroy(&server);
    return err;
  }
  platform_systemd_notify_status("API HTTP listener started");
  platform_systemd_notify_ready();
  while (!APP_CALLBACK_BOOL(should_exit))
    uv_sleep(16);
  http_server_stop(server);
  http_server_destroy(&server);
  return ASCIICHAT_OK;
}
