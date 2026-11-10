#include "main.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/network/http/server/server.h>
#include <ascii-chat/network/quic/quic.h>
#include <ascii-chat/util/ip.h>
#include "../common/http_renderer.h"
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/string.h>
#include <ascii-chat/platform/systemd.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/version.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <uv.h>

static void api_finish(http_response_t *response, unsigned status) {
  (void)http_response_set_status(response, status);
  (void)http_response_finish(response);
}

static const unsigned char *api_find_bytes(const unsigned char *data, size_t length, const char *needle) {
  size_t needle_length = strlen(needle);
  if (!needle_length || needle_length > length)
    return NULL;
  for (size_t i = 0; i + needle_length <= length; ++i)
    if (!memcmp(data + i, needle, needle_length))
      return data + i;
  return NULL;
}

typedef struct {
  http_renderer_t *renderer;
} api_server_t;

static void api_request(http_request_t *request, http_response_t *response, void *user_data) {
  api_server_t *server = user_data;
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
  if (strcmp(info->method, "POST")) {
    api_finish(response, 405);
    return;
  }
  const char *content_type = NULL;
  for (size_t i = 0; i < info->header_count; ++i)
    if (!platform_strcasecmp(info->headers[i].name, "content-type"))
      content_type = info->headers[i].value;
  bool multipart = content_type && !strncmp(content_type, "multipart/form-data;", 20);
  if (!content_type || (!multipart && strncmp(content_type, "image/", 6) && strncmp(content_type, "application/octet-stream", 24))) {
    api_finish(response, 415);
    return;
  }
  size_t length = 0;
  const unsigned char *body = http_request_body(request, &length);
  if (!body || !length) {
    api_finish(response, 400);
    return;
  }
  if (multipart) {
    const char *boundary = strstr(content_type, "boundary=");
    if (!boundary || !boundary[9]) { api_finish(response, 400); return; }
    boundary += 9;
    char marker[256];
    int marker_len = safe_snprintf(marker, sizeof(marker), "--%s", boundary);
    if (marker_len <= 0 || (size_t)marker_len >= sizeof(marker)) { api_finish(response, 400); return; }
    const unsigned char *start = api_find_bytes(body, length, "\r\n\r\n");
    if (!start || (size_t)(start - body) >= length) { api_finish(response, 400); return; }
    if (!api_find_bytes(body, (size_t)(start - body), "name=\"image\"")) {
      api_finish(response, 400);
      return;
    }
    start += 4;
    const unsigned char *end = api_find_bytes(start, length - (size_t)(start - body), marker);
    if (!end || end <= start || (size_t)(end - body) > length) {
      api_finish(response, 400); return;
    }
    body = start;
    length = (size_t)(end - start);
    while (length && (body[length - 1] == '\r' || body[length - 1] == '\n')) --length;
    if (!length) { api_finish(response, 400); return; }
  }
  http_render_request_t render_request;
  http_render_request_init(&render_request);
  render_request.max_output_bytes = 8u * 1024u * 1024u;
  const char *query = info->query ? info->query : "";
  char format[16] = "text";
  for (const char *p = query; *p;) {
    const char *eq = strchr(p, '=');
    const char *amp = strchr(p, '&');
    if (!eq || (amp && eq > amp)) { api_finish(response, 400); return; }
    size_t key_len = (size_t)(eq - p), value_len = (size_t)((amp ? amp : p + strlen(p)) - eq - 1);
    char key[32], value[256];
    if (!key_len || key_len >= sizeof(key) || value_len >= sizeof(value)) { api_finish(response, 400); return; }
    memcpy(key, p, key_len); key[key_len] = 0; memcpy(value, eq + 1, value_len); value[value_len] = 0;
    char *endptr = NULL;
    if (!strcmp(key, "width") || !strcmp(key, "height")) {
      long n = strtol(value, &endptr, 10);
      if (!endptr || *endptr || n < 1 || n > 3840) { api_finish(response, 422); return; }
      if (!strcmp(key, "width")) render_request.options.width = (int)n; else render_request.options.height = (int)n;
    } else if (!strcmp(key, "palette")) {
      SAFE_STRNCPY(render_request.options.palette, value, sizeof(render_request.options.palette));
    } else if (!strcmp(key, "format")) {
      SAFE_STRNCPY(format, value, sizeof(format));
      if (strcmp(format, "text") && strcmp(format, "json") && strcmp(format, "html")) { api_finish(response, 422); return; }
    } else if (!strcmp(key, "palette-chars")) {
      SAFE_STRNCPY(render_request.options.palette_chars, value, sizeof(render_request.options.palette_chars));
    } else if (!strcmp(key, "color")) {
      if (!strcmp(value, "none")) render_request.options.color_mode = TERM_COLOR_NONE;
      else if (!strcmp(value, "16")) render_request.options.color_mode = TERM_COLOR_16;
      else if (!strcmp(value, "256")) render_request.options.color_mode = TERM_COLOR_256;
      else if (!strcmp(value, "truecolor") || !strcmp(value, "24bit")) render_request.options.color_mode = TERM_COLOR_TRUECOLOR;
      else if (!strcmp(value, "auto")) render_request.options.color_mode = TERM_COLOR_AUTO;
      else { api_finish(response, 422); return; }
    } else if (!strcmp(key, "renderer")) {
      if (strcmp(value, "auto") && strcmp(value, "block") && strcmp(value, "braille") && strcmp(value, "half-block")) {
        api_finish(response, 422); return;
      }
      SAFE_STRNCPY(render_request.options.render_theme, value, sizeof(render_request.options.render_theme));
    } else {
      api_finish(response, 400); return;
    }
    p = amp ? amp + 1 : p + strlen(p);
  }
  char path[PLATFORM_MAX_PATH_LENGTH];
  int fd = -1;
  if (platform_create_temp_file(path, sizeof(path), "ascii-api", &fd) != 0 ||
      platform_write(fd, body, length) != (ssize_t)length) {
    if (fd >= 0) platform_close(fd);
    api_finish(response, 500);
    return;
  }
  platform_close(fd);
  render_request.source.type = HTTP_RENDER_SOURCE_FILE;
  SAFE_STRNCPY(render_request.source.path, path, sizeof(render_request.source.path));
  render_request.source.body_length = length;
  render_request.max_output_bytes = 8u * 1024u * 1024u;
  SAFE_STRNCPY(render_request.source.mime_type, content_type, sizeof(render_request.source.mime_type));
  http_renderer_job_t *render_job = NULL;
  asciichat_error_t err = http_renderer_submit(server->renderer, &render_request, &render_job);
  if (err == ASCIICHAT_OK) {
    http_renderer_job_wait(render_job);
    err = render_job->result.error;
  }
  platform_delete_temp_file(path);
  if (err != ASCIICHAT_OK) {
    log_warn("API render failed: %s", asciichat_error_string(err));
    if (render_job)
      http_renderer_reap(server->renderer, false);
    unsigned status = err == ERROR_BUFFER_FULL ? 503 : err == ERROR_BUFFER_OVERFLOW ? 413 : 422;
    api_finish(response, status);
    return;
  }
  http_render_result_t *result = &render_job->result;
  if (!strcmp(format, "json")) {
    size_t cap = result->ascii_length * 2 + 32;
    char *json = SAFE_MALLOC(cap, char *);
    size_t out = 0; out += (size_t)safe_snprintf(json + out, cap - out, "{\"ascii\":\"");
    for (size_t i = 0; i < result->ascii_length && out + 8 < cap; ++i) {
      unsigned char c = (unsigned char)result->ascii[i];
      if (c == '"' || c == '\\') json[out++] = '\\';
      if (c == '\n') { json[out++] = '\\'; json[out++] = 'n'; } else if (c != '\r') json[out++] = (char)c;
    }
    out += (size_t)safe_snprintf(json + out, cap - out, "\"}");
    (void)http_response_set_header(response, "Content-Type", "application/json");
    (void)http_response_write(response, json, out); SAFE_FREE(json);
  } else if (!strcmp(format, "html")) {
    (void)http_response_set_header(response, "Content-Type", "text/html; charset=utf-8");
    (void)http_response_write(response, "<pre>", 5);
    for (size_t i = 0; i < result->ascii_length; ++i) {
      const char *s = result->ascii[i] == '&' ? "&amp;" : result->ascii[i] == '<' ? "&lt;" : result->ascii[i] == '>' ? "&gt;" : NULL;
      (void)http_response_write(response, s ? s : &result->ascii[i], s ? strlen(s) : 1);
    }
    (void)http_response_write(response, "</pre>", 6);
  } else {
    (void)http_response_set_header(response, "Content-Type", "text/plain; charset=utf-8");
    (void)http_response_write(response, result->ascii, result->ascii_length);
  }
  http_renderer_reap(server->renderer, false);
  api_finish(response, 200);
}

int api_server_main(void) {
  http_server_t *server = NULL;
  api_server_t api = {0};
  quic_endpoint_t *http3_endpoint = NULL;
  const char *tls_cert = GET_OPTION(tls_cert);
  const char *tls_key = GET_OPTION(tls_key);
  const char *tls_password = GET_OPTION(tls_key_password);
  if (!tls_cert || !*tls_cert) tls_cert = NULL;
  if (!tls_key || !*tls_key) tls_key = NULL;
  if (!tls_password || !*tls_password) tls_password = NULL;
  http_server_config_t config = {.bind_address = GET_OPTION(address),
                                 .port = (uint16_t)GET_OPTION(port),
                                 .enable_http1 = true,
                                 .enable_http2 = true,
                                 .enable_http3 = false,
                                 .tls_cert_path = tls_cert,
                                 .tls_key_path = tls_key,
                                 .tls_key_password = tls_password,
                                 .max_connections = (size_t)GET_OPTION(max_clients),
                                 .max_request_body_bytes = 10u * 1024u * 1024u,
                                 .max_response_body_bytes = 8u * 1024u * 1024u,
                                 .on_request = api_request};
  if (!config.max_connections)
    config.max_connections = 1;
  asciichat_error_t err = ASCIICHAT_OK;
  http_renderer_config_t renderer_config = {.max_jobs = (unsigned)config.max_connections,
                                            .max_input_bytes = config.max_request_body_bytes,
                                            .max_output_bytes = config.max_response_body_bytes,
                                            .timeout_ms = 10000,
                                            .restricted = true};
  err = http_renderer_init(&api.renderer, &renderer_config);
  if (err != ASCIICHAT_OK)
    return err;
  config.on_request = api_request;
  config.user_data = &api;
  err = http_server_init(&server, &config);
  if (err != ASCIICHAT_OK) {
    LOG_ERRNO_IF_SET("API HTTP server initialization failed");
    http_renderer_destroy(&api.renderer);
    return err;
  }
  if (tls_cert && tls_key) {
    struct sockaddr_storage bind_address;
    socklen_t bind_length = 0;
    err = parse_numeric_socket_address(GET_OPTION(address), (uint16_t)GET_OPTION(port), &bind_address, &bind_length);
    if (err == ASCIICHAT_OK) {
      quic_config_t quic_config = {.alpn = "h3",
                                   .certificate_file = tls_cert,
                                   .private_key_file = tls_key,
                                   .enable_http3 = true,
                                   .http_server = server};
      err = quic_endpoint_open((const struct sockaddr *)&bind_address, bind_length, true, &quic_config,
                               &http3_endpoint);
    }
    if (err != ASCIICHAT_OK) {
      LOG_ERRNO_IF_SET("API HTTP/3 listener initialization failed");
      http_server_destroy(&server);
      http_renderer_destroy(&api.renderer);
      return err;
    }
  }
  err = http_server_start(server);
  if (err != ASCIICHAT_OK) {
    LOG_ERRNO_IF_SET("API HTTP server start failed");
    if (http3_endpoint) {
      (void)quic_endpoint_stop(http3_endpoint);
      (void)quic_endpoint_destroy(&http3_endpoint);
    }
    http_server_destroy(&server);
    http_renderer_destroy(&api.renderer);
    return err;
  }
  platform_systemd_notify_status("API HTTP listener started");
  platform_systemd_notify_ready();
  while (!APP_CALLBACK_BOOL(should_exit))
    uv_sleep(16);
  http_server_stop(server);
  if (http3_endpoint) {
    (void)quic_endpoint_stop(http3_endpoint);
    (void)quic_endpoint_destroy(&http3_endpoint);
  }
  http_server_destroy(&server);
  http_renderer_destroy(&api.renderer);
  return ASCIICHAT_OK;
}
