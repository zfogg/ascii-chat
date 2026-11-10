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
#include <ascii-chat/otel.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <uv.h>

typedef struct api_render_job api_render_job_t;
typedef struct {
  http_renderer_t *renderer;
  http_server_t *http;
  api_render_job_t *jobs;
} api_server_t;

struct api_render_job {
  api_server_t *server;
  http_renderer_job_t *renderer_job;
  http_response_t *response;
  char format[16];
  char path[PLATFORM_MAX_PATH_LENGTH];
  atomic_t completed;
  api_render_job_t *next;
};

static void api_finish(http_response_t *response, unsigned status) {
  (void)http_response_set_status(response, status);
  (void)http_response_finish(response);
}

static unsigned api_render_status(asciichat_error_t error) {
  if (error == ERROR_BUFFER_FULL)
    return 503;
  if (error == ERROR_BUFFER_OVERFLOW)
    return 413;
  if (error == ERROR_INVALID_PARAM || error == ERROR_MEDIA_INIT || error == ERROR_MEDIA_DECODE)
    return 422;
  return 500;
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

static bool api_decode_query_component(const char *src, size_t length, char *dst, size_t capacity) {
  size_t out = 0;
  for (size_t i = 0; i < length; ++i) {
    unsigned char c = (unsigned char)src[i];
    if (c == '%') {
      if (i + 2 >= length) return false;
      int hi = (src[i + 1] >= '0' && src[i + 1] <= '9') ? src[i + 1] - '0' :
               (src[i + 1] >= 'a' && src[i + 1] <= 'f') ? src[i + 1] - 'a' + 10 :
               (src[i + 1] >= 'A' && src[i + 1] <= 'F') ? src[i + 1] - 'A' + 10 : -1;
      int lo = (src[i + 2] >= '0' && src[i + 2] <= '9') ? src[i + 2] - '0' :
               (src[i + 2] >= 'a' && src[i + 2] <= 'f') ? src[i + 2] - 'a' + 10 :
               (src[i + 2] >= 'A' && src[i + 2] <= 'F') ? src[i + 2] - 'A' + 10 : -1;
      if (hi < 0 || lo < 0) return false;
      c = (unsigned char)((hi << 4) | lo);
      i += 2;
    } else if (c == '+') c = ' ';
    if (!c || out + 1 >= capacity) return false;
    dst[out++] = (char)c;
  }
  dst[out] = 0;
  return true;
}

static void api_write_result(http_response_t *response, const char *format, const http_render_result_t *result) {
  if (!strcmp(format, "files")) {
    static const char boundary[] = "ascii-chat-frame";
    (void)http_response_set_header(response, "Content-Type", "multipart/mixed; boundary=ascii-chat-frame");
    for (size_t i = 0; i < result->frame_count; ++i) {
      const http_render_frame_t *frame = &result->frames[i];
      if (!frame->text) continue;
      char header[256];
      int length = safe_snprintf(header, sizeof(header), "--%s\r\nContent-Type: text/plain; charset=utf-8\r\n"
                                                   "Content-Disposition: attachment; filename=frame-%zu.txt\r\n\r\n",
                                 boundary, i);
      (void)http_response_write(response, header, (size_t)length);
      (void)http_response_write(response, frame->text, frame->text_length);
      (void)http_response_write(response, "\r\n", 2);
    }
    (void)http_response_write(response, "--ascii-chat-frame--\r\n", 22);
  } else if (!strcmp(format, "json")) {
    size_t cap = result->ascii_length * 6 + result->frame_count * 64 + 64;
    char *json = SAFE_MALLOC(cap, char *);
    size_t out = (size_t)safe_snprintf(json, cap, "{\"frames\":[");
    for (size_t frame_index = 0; frame_index < result->frame_count; ++frame_index) {
      const http_render_frame_t *frame = &result->frames[frame_index];
      if (frame_index) out += (size_t)safe_snprintf(json + out, cap - out, ",");
      out += (size_t)safe_snprintf(json + out, cap - out, "{\"timestamp_ms\":%llu,\"text\":\"",
                                   (unsigned long long)frame->timestamp_ms);
      for (size_t i = 0; i < frame->text_length && out + 8 < cap; ++i) {
        unsigned char c = (unsigned char)frame->text[i];
        if (c == '"' || c == '\\') { json[out++] = '\\'; json[out++] = (char)c; }
        else if (c == '\n') { json[out++] = '\\'; json[out++] = 'n'; }
        else if (c < 0x20) out += (size_t)safe_snprintf(json + out, cap - out, "\\u%04x", c);
        else json[out++] = (char)c;
      }
      out += (size_t)safe_snprintf(json + out, cap - out, "\"}");
    }
    out += (size_t)safe_snprintf(json + out, cap - out, "]}");
    (void)http_response_set_header(response, "Content-Type", "application/json");
    (void)http_response_write(response, json, out);
    SAFE_FREE(json);
  } else if (!strcmp(format, "html")) {
    (void)http_response_set_header(response, "Content-Type", "text/html; charset=utf-8");
    (void)http_response_write(response, "<pre>", 5);
    for (size_t i = 0; i < result->ascii_length; ++i) {
      const char *escaped = result->ascii[i] == '&' ? "&amp;" : result->ascii[i] == '<' ? "&lt;" :
                            result->ascii[i] == '>' ? "&gt;" : NULL;
      (void)http_response_write(response, escaped ? escaped : &result->ascii[i], escaped ? strlen(escaped) : 1);
    }
    (void)http_response_write(response, "</pre>", 6);
  } else {
    (void)http_response_set_header(response, "Content-Type", "text/plain; charset=utf-8");
    (void)http_response_write(response, result->ascii, result->ascii_length);
  }
}

static void api_render_complete(http_renderer_job_t *job, const http_render_result_t *result, void *user_data) {
  (void)result;
  api_server_t *server = user_data;
  api_render_job_t *pending = job->user_data;
  if (pending) {
    atomic_store_bool(&pending->completed, true);
  }
  if (server) (void)http_server_wakeup(server->http);
}

static void api_render_error(http_renderer_job_t *job, asciichat_error_t error, void *user_data) {
  (void)error;
  api_render_complete(job, &job->result, user_data);
}

static void api_wakeup(void *user_data) {
  api_server_t *server = user_data;
  api_render_job_t **link = &server->jobs;
  while (*link) {
    api_render_job_t *pending = *link;
    if (pending->renderer_job && pending->response && http_response_is_cancelled(pending->response)) {
      (void)http_renderer_cancel(pending->renderer_job);
      http_response_release(&pending->response);
    }
    if (!pending->renderer_job || !atomic_load_bool(&pending->completed) || !http_renderer_job_is_done(pending->renderer_job)) {
      link = &pending->next;
      continue;
    }
    *link = pending->next;
    if (pending->response) {
      if (pending->renderer_job->result.error == ASCIICHAT_OK) {
        api_write_result(pending->response, pending->format, &pending->renderer_job->result);
        api_finish(pending->response, 200);
      } else {
        log_warn("API render failed: %s", asciichat_error_string(pending->renderer_job->result.error));
        api_finish(pending->response, api_render_status(pending->renderer_job->result.error));
      }
    }
    http_response_release(&pending->response);
    platform_delete_temp_file(pending->path);
    SAFE_FREE(pending);
  }
  /* Every renderer job is owned by a pending entry.  Reap only after the
   * completed entries have been unlinked and no callback can use them. */
  http_renderer_reap(server->renderer, false);
}

static void api_discard_jobs(api_server_t *server) {
  while (server->jobs) {
    api_render_job_t *pending = server->jobs;
    server->jobs = pending->next;
    http_response_release(&pending->response);
    platform_delete_temp_file(pending->path);
    SAFE_FREE(pending);
  }
}

static void api_request(http_request_t *request, http_response_t *response, void *user_data) {
  api_server_t *server = user_data;
  const http_request_info_t *info = http_request_info(request);
  ASCII_OTEL_SCOPE(HTTP);
  ASCII_OTEL_ATTR_STRING("service.name", "ascii-chat-api");
  ASCII_OTEL_ATTR_STRING("http.route", info->path);
  ASCII_OTEL_ATTR_STRING("http.request.method", info->method);
  ASCII_OTEL_ATTR_INT("http.protocol", info->protocol);
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
  if (!strcmp(info->path, "/health")) { api_finish(response, 405); return; }
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
    char boundary_value[256];
    size_t boundary_len = strcspn(boundary, ";\r\n");
    if (boundary_len >= sizeof(boundary_value)) { api_finish(response, 400); return; }
    memcpy(boundary_value, boundary, boundary_len); boundary_value[boundary_len] = 0;
    if (boundary_value[0] == '"') {
      size_t n = strlen(boundary_value);
      if (n < 2 || boundary_value[n - 1] != '"') { api_finish(response, 400); return; }
      memmove(boundary_value, boundary_value + 1, n - 2); boundary_value[n - 2] = 0;
    }
    boundary = boundary_value;
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
  char output[16] = "files";
  for (const char *p = query; *p;) {
    const char *eq = strchr(p, '=');
    const char *amp = strchr(p, '&');
    if (!eq || (amp && eq > amp)) { api_finish(response, 400); return; }
    size_t key_len = (size_t)(eq - p), value_len = (size_t)((amp ? amp : p + strlen(p)) - eq - 1);
    char key[32], value[256];
    if (!key_len || key_len >= sizeof(key) || value_len >= sizeof(value)) { api_finish(response, 400); return; }
    if (!api_decode_query_component(p, key_len, key, sizeof(key)) ||
        !api_decode_query_component(eq + 1, value_len, value, sizeof(value))) { api_finish(response, 400); return; }
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
    } else if (!strcmp(key, "output")) {
      SAFE_STRNCPY(output, value, sizeof(output));
      if (strcmp(output, "text") && strcmp(output, "files")) { api_finish(response, 422); return; }
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
  ASCII_OTEL_ATTR_STRING("http.render.format", format);
  ASCII_OTEL_ATTR_INT("http.render.width", render_request.options.width);
  ASCII_OTEL_ATTR_INT("http.render.height", render_request.options.height);
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
  render_request.output_type = !strcmp(output, "files") ? HTTP_RENDER_OUTPUT_FILES : HTTP_RENDER_OUTPUT_TEXT;
  render_request.max_output_bytes = 8u * 1024u * 1024u;
  SAFE_STRNCPY(render_request.source.mime_type, content_type, sizeof(render_request.source.mime_type));
  api_render_job_t *pending = SAFE_CALLOC(1, sizeof(*pending), api_render_job_t *);
  if (!pending) { platform_delete_temp_file(path); api_finish(response, 500); return; }
  pending->server = server;
  SAFE_STRNCPY(pending->format, !strcmp(output, "files") ? "files" : format, sizeof(pending->format));
  SAFE_STRNCPY(pending->path, path, sizeof(pending->path));
  atomic_store_bool(&pending->completed, false);
  http_response_t *retained = NULL;
  if (http_response_retain(response, &retained) != ASCIICHAT_OK) {
    SAFE_FREE(pending);
    platform_delete_temp_file(path);
    api_finish(response, 500);
    return;
  }
  pending->response = retained;
  render_request.user_data = pending;
  http_renderer_job_t *render_job = NULL;
  asciichat_error_t err = http_renderer_submit(server->renderer, &render_request, &render_job);
  if (err != ASCIICHAT_OK) {
    http_response_release(&pending->response); SAFE_FREE(pending); platform_delete_temp_file(path);
    api_finish(response, api_render_status(err)); return;
  }
  pending->renderer_job = render_job;
  pending->next = server->jobs;
  server->jobs = pending;
  if (atomic_load_bool(&pending->completed)) (void)http_server_wakeup(server->http);
}

int api_server_main(void) {
  http_server_t *server = NULL;
  api_server_t api = {0};
  quic_endpoint_t *http3_endpoint = NULL;
  quic_endpoint_t *http3_endpoint6 = NULL;
  const char *tls_cert = GET_OPTION(tls_cert);
  const char *tls_key = GET_OPTION(tls_key);
  const char *tls_password = GET_OPTION(tls_key_password);
  if (!tls_cert || !*tls_cert) tls_cert = NULL;
  if (!tls_key || !*tls_key) tls_key = NULL;
  if (!tls_password || !*tls_password) tls_password = NULL;
  http_server_config_t config = {.bind_address = GET_OPTION(address), .bind_address6 = GET_OPTION(address6),
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
                                 .on_request = api_request,
                                 .on_wakeup = api_wakeup};
  if (!config.max_connections)
    config.max_connections = 1;
  asciichat_error_t err = ASCIICHAT_OK;
  http_renderer_config_t renderer_config = {.max_jobs = (unsigned)config.max_connections,
                                            .max_input_bytes = config.max_request_body_bytes,
                                            .max_output_bytes = config.max_response_body_bytes,
                                            .timeout_ms = 10000,
                                            .restricted = true,
                                            .on_complete = api_render_complete,
                                            .on_error = api_render_error,
                                            .user_data = &api};
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
  api.http = server;
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
      if (err == ASCIICHAT_OK && GET_OPTION(address6) && *GET_OPTION(address6)) {
        struct sockaddr_storage bind_address6;
        socklen_t bind_length6 = 0;
        err = parse_numeric_socket_address(GET_OPTION(address6), (uint16_t)GET_OPTION(port), &bind_address6,
                                           &bind_length6);
        if (err == ASCIICHAT_OK)
          err = quic_endpoint_open((const struct sockaddr *)&bind_address6, bind_length6, true, &quic_config,
                                   &http3_endpoint6);
      }
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
    if (http3_endpoint6) {
      (void)quic_endpoint_stop(http3_endpoint6);
      (void)quic_endpoint_destroy(&http3_endpoint6);
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
  if (http3_endpoint6) {
    (void)quic_endpoint_stop(http3_endpoint6);
    (void)quic_endpoint_destroy(&http3_endpoint6);
  }
  http_renderer_destroy(&api.renderer);
  api_discard_jobs(&api);
  http_server_destroy(&server);
  return ASCIICHAT_OK;
}
