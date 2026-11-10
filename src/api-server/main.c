#include "main.h"
#include <ascii-chat/network/http/server/server.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/platform/memory.h>
#include <ascii-chat/platform/string.h>
#include <ascii-chat/log/log.h>
#include <ascii-chat/stats/stats.h>
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/platform/filesystem.h>
#include <string.h>
#include <stdio.h>
#include "../common/http_basic_auth.h"
#include "../mcp-server/mcp.h"

typedef struct { bool enabled; char username[OPTIONS_BUFF_SIZE]; char password[OPTIONS_BUFF_SIZE]; } api_auth_t;
static api_auth_t g_auth;

static const char *api_header(const http_request_info_t *info, const char *name) {
  for (size_t i = 0; i < info->header_count; ++i)
    if (!platform_strcasecmp(info->headers[i].name, name)) return info->headers[i].value;
  return NULL;
}

static asciichat_error_t api_render_upload(http_request_t *request, http_response_t *response) {
  size_t body_length = 0;
  const void *body = http_request_body(request, &body_length);
  if (!body || !body_length || body_length > 16u * 1024u * 1024u)
    return SET_ERRNO(ERROR_INVALID_PARAM, "Request body must contain an image no larger than 16 MiB");

  mcp_render_request_t render = {0};
  asciichat_error_t err = platform_mkdtemp(render.upload_dir, sizeof(render.upload_dir), "ascii-api-input");
  if (err != ASCIICHAT_OK)
    return err;
  SAFE_SNPRINTF(render.stage_file, sizeof(render.stage_file), "%s/image", render.upload_dir);
  FILE *file = platform_fopen("api-upload", render.stage_file, "wb");
  bool staged = file && fwrite(body, 1, body_length, file) == body_length;
  if (file && fclose(file) != 0)
    staged = false;
  if (!staged) {
    mcp_request_destroy(&render);
    return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot stage uploaded image");
  }

  char executable[PLATFORM_MAX_PATH_LENGTH];
  if (!platform_get_executable_path(executable, sizeof(executable))) {
    mcp_request_destroy(&render);
    return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot locate renderer executable");
  }
  const char *args[] = {executable, "--log-level=error", "--no-check-update", "mirror", "--file", render.stage_file,
                        "--width", "100", "--height", "40", "--color-mode", "none", "--strip-ansi", "true",
                        "--snapshot", "--snapshot-delay", "0", "--splash-screen", "false", "--audio", "false",
                        "--no-audio-playback", NULL};
  size_t argc = 0;
  while (args[argc]) argc++;
  render.argv = SAFE_CALLOC(argc + 1, sizeof(*render.argv), char **);
  for (size_t i = 0; i < argc; ++i) render.argv[i] = platform_strdup(args[i]);
  render.argc = argc;
  render.timeout_ms = 60000;
  mcp_render_result_t result = {0};
  mcp_render_execute(&render, &(atomic_t){0}, &result);
  if (result.error[0]) {
    SAFE_FREE(result.text);
    mcp_request_destroy(&render);
    return SET_ERRNO(ERROR_RUNTIME, "%s", result.error);
  }
  if (http_response_set_status(response, 200) != ASCIICHAT_OK ||
      http_response_set_header(response, "Content-Type", "text/plain; charset=utf-8") != ASCIICHAT_OK ||
      http_response_write(response, result.text, result.length) != ASCIICHAT_OK ||
      http_response_finish(response) != ASCIICHAT_OK) {
    SAFE_FREE(result.text);
    mcp_request_destroy(&render);
    return SET_ERRNO(ERROR_NETWORK, "Cannot send API response");
  }
  SAFE_FREE(result.text);
  mcp_request_destroy(&render);
  return ASCIICHAT_OK;
}

static void api_request(http_request_t *request, http_response_t *response, void *user_data) {
  (void)user_data;
  const http_request_info_t *info = http_request_info(request);
  if (!strcmp(info->path, "/convert") && !strcmp(info->method, "POST")) {
    bool authorized = !g_auth.enabled;
    http_basic_credentials_t credentials;
    if (!authorized && api_header(info, "authorization") &&
        http_basic_auth_parse(api_header(info, "authorization"), &credentials) == ASCIICHAT_OK) {
      authorized = http_basic_auth_matches(&credentials, g_auth.username, g_auth.password);
      http_basic_credentials_clear(&credentials);
    }
    if (!authorized) {
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_REJECTED, 1);
      if (http_response_set_status(response, 401) == ASCIICHAT_OK &&
          http_response_set_header(response, "WWW-Authenticate", "Basic realm=\"ascii-chat-api\"") == ASCIICHAT_OK)
        (void)http_response_finish(response);
      return;
    }
    if (api_render_upload(request, response) != ASCIICHAT_OK && !http_response_is_cancelled(response)) {
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_HTTP_ERRORS, 1);
      if (http_response_set_status(response, 500) == ASCIICHAT_OK)
        (void)http_response_finish(response);
    }
    return;
  }
  if (!strcmp(info->path, "/health") && !strcmp(info->method, "GET")) {
    if (http_response_set_status(response, 200) != ASCIICHAT_OK ||
        http_response_set_header(response, "Content-Type", "application/json; charset=utf-8") != ASCIICHAT_OK)
      return;
    const char body[] = "{\"status\":\"ok\",\"service\":\"ascii-chat-api\"}";
    if (http_response_write(response, body, sizeof(body) - 1) == ASCIICHAT_OK)
      (void)http_response_finish(response);
    return;
  }
  if (http_response_set_status(response, 404) == ASCIICHAT_OK)
    (void)http_response_finish(response);
}

int api_server_main(void) {
  memset(&g_auth, 0, sizeof(g_auth));
  const char *username = GET_OPTION(api_username);
  const char *password = GET_OPTION(api_password);
  const char *password_file = GET_OPTION(api_password_file);
  if ((username[0] != '\0') != (password[0] != '\0' || password_file[0] != '\0') ||
      (password[0] != '\0' && password_file[0] != '\0'))
    return SET_ERRNO(ERROR_CONFIG, "api-server credentials require exactly one password source and a username");
  if (username[0]) {
    SAFE_STRNCPY(g_auth.username, username, sizeof(g_auth.username));
    if (password_file[0]) {
      FILE *file = fopen(password_file, "rb");
      if (!file || !fgets(g_auth.password, sizeof(g_auth.password), file)) {
        if (file) fclose(file);
        return SET_ERRNO(ERROR_CONFIG, "Cannot read --api-password-file");
      }
      fclose(file);
      g_auth.password[strcspn(g_auth.password, "\r\n")] = '\0';
    } else SAFE_STRNCPY(g_auth.password, password, sizeof(g_auth.password));
    if (!g_auth.password[0]) return SET_ERRNO(ERROR_CONFIG, "API password must not be empty");
    g_auth.enabled = true;
  }
  if (g_auth.enabled && !GET_OPTION(tls_cert)[0] && !GET_OPTION(tls_key)[0] &&
      !GET_OPTION(api_allow_insecure_basic_auth))
    return SET_ERRNO(ERROR_CONFIG, "Basic authentication without TLS requires --api-allow-insecure-basic-auth");
  if (!!GET_OPTION(tls_cert)[0] != !!GET_OPTION(tls_key)[0])
    return SET_ERRNO(ERROR_CONFIG, "api-server TLS requires both --tls-cert and --tls-key");
  http_server_t *server = NULL;
  http_server_config_t config = {.bind_address = GET_OPTION(address), .port = (uint16_t)GET_OPTION(port),
                                 .enable_http1 = true, .enable_http2 = true, .enable_http3 = true,
                                 .tls_cert_path = GET_OPTION(tls_cert), .tls_key_path = GET_OPTION(tls_key),
                                 .tls_key_password = GET_OPTION(tls_key_password),
                                 .max_connections = (size_t)GET_OPTION(max_clients), .on_request = api_request};
  asciichat_error_t err = http_server_init(&server, &config);
  if (err != ASCIICHAT_OK)
    return err;
  err = http_server_run(server);
  http_server_destroy(&server);
  return err;
}
