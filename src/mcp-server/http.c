
/* Storage capacities in elements; text buffers include the terminating NUL. */
#define MCP_SERVER_HTTP_ORIGIN_BUFFER_SIZE 2048
#define MCP_SERVER_HTTP_TYPE_BUFFER_SIZE 128
#define MCP_SERVER_HTTP_ACCEPT_BUFFER_SIZE 512
#define MCP_SERVER_HTTP_VERSION_BUFFER_SIZE 64

#include "mcp.h"
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/tui/status.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/network/nat/upnp.h>
#include <sodium.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <uv.h>
#include <ascii-chat/platform/stat.h>

static const char *shared_header(const http_request_info_t *info, const char *name) {
  for (size_t i = 0; i < info->header_count; ++i)
    if (!platform_strcasecmp(info->headers[i].name, name))
      return info->headers[i].value;
  return NULL;
}

void mcp_http_wake(mcp_server_t *s) {
  if (s && s->shared_http)
    (void)http_server_wakeup(s->shared_http);
}

static void shared_http_finish(http_response_t *response, unsigned status) {
  (void)http_response_set_status(response, status);
  ASCII_OTEL_SCOPE(MCP_HTTP_RESPONSE);
  ASCII_OTEL_ATTR_INT("http.response.status_code", status);
  ASCII_OTEL_ATTR_INT("http.response.body.size", (int64_t)http_response_body_length(response));
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_HTTP_RESPONSES, 1);
  (void)http_response_finish(response);
}

static void shared_http_wakeup(void *user_data) {
  mcp_server_t *s = user_data;
  for (unsigned i = 0; i < s->policy.max_jobs; ++i) {
    mcp_job_t *j = &s->jobs[i];
    if (!j->started || !j->connection || !atomic_load_bool(&j->done))
      continue;
    http_response_t *response = j->connection;
    if (http_response_is_cancelled(response)) {
      j->connection = NULL;
      http_response_release(&response);
      continue;
    }
    if (s->policy.restricted && mcp_download_finalize(&s->policy, j->response, (int64_t)time(NULL)) != ASCIICHAT_OK) {
      yyjson_mut_val *root = yyjson_mut_doc_get_root(j->response);
      yyjson_mut_obj_remove_str(root, "result");
      yyjson_mut_val *error = yyjson_mut_obj(j->response);
      yyjson_mut_obj_add_int(j->response, error, "code", -32603);
      yyjson_mut_obj_add_str(j->response, error, "message", "Cannot publish download");
      yyjson_mut_obj_add_val(j->response, root, "error", error);
    }
    size_t length = 0;
    char *body = yyjson_mut_write(j->response, 0, &length);
    if (body) {
      (void)http_response_set_status(response, 200);
      (void)http_response_write(response, body, length);
      shared_http_finish(response, 200);
      free(body);
    } else {
      /* Always terminate the retained transport before releasing its slot. */
      shared_http_finish(response, 500);
    }
    j->connection = NULL;
    http_response_release(&response);
  }
  mcp_jobs_reap(s, false);
}

static void shared_http_request(http_request_t *request, http_response_t *response, void *user_data) {
  mcp_server_t *s = user_data;
  const http_request_info_t *info = http_request_info(request);
  const char *origin = shared_header(info, "origin");
  if (origin && (!s->policy.origin || strcmp(origin, s->policy.origin))) {
    shared_http_finish(response, 403);
    return;
  }
  if (s->policy.restricted && !strncmp(info->path, "/downloads/", sizeof("/downloads/") - 1)) {
    if (strcmp(info->method, "GET")) {
      shared_http_finish(response, 405);
      return;
    }
    const char *name = info->path + sizeof("/downloads/") - 1;
    int64_t expires = 0;
    char path[PLATFORM_MAX_PATH_LENGTH];
    int n = safe_snprintf(path, sizeof(path), "%s/%s", s->policy.downloads_dir, name);
    if (n < 0 || (size_t)n >= sizeof(path) || !mcp_download_name(name, &expires) || expires <= (int64_t)time(NULL)) {
      shared_http_finish(response, 404);
      return;
    }
    uv_fs_t fs = {0};
    int rc = uv_fs_open(NULL, &fs, path, UV_FS_O_RDONLY | UV_FS_O_NOFOLLOW, 0, NULL);
    uv_fs_req_cleanup(&fs);
    if (rc < 0) {
      shared_http_finish(response, 404);
      return;
    }
    uv_file file = rc;
    rc = uv_fs_fstat(NULL, &fs, file, NULL);
    size_t size = rc == 0 && fs.statbuf.st_size >= 0 ? (size_t)fs.statbuf.st_size : 0;
    uv_fs_req_cleanup(&fs);
    if (!size || size > MCP_MAX_DOWNLOAD) {
      uv_fs_close(NULL, &fs, file, NULL);
      uv_fs_req_cleanup(&fs);
      shared_http_finish(response, size > MCP_MAX_DOWNLOAD ? 413 : 404);
      return;
    }
    char *contents = SAFE_MALLOC(size, char *);
    uv_buf_t buffer = uv_buf_init(contents, (unsigned)size);
    rc = contents ? uv_fs_read(NULL, &fs, file, &buffer, 1, 0, NULL) : -1;
    uv_fs_req_cleanup(&fs);
    uv_fs_close(NULL, &fs, file, NULL);
    uv_fs_req_cleanup(&fs);
    if (rc != (int)size || http_response_write(response, contents, size) != ASCIICHAT_OK) {
      SAFE_FREE(contents);
      shared_http_finish(response, 500);
      return;
    }
    SAFE_FREE(contents);
    const char *extension = strrchr(name, '.');
    if (http_response_set_header(response, "Content-Type", mcp_output_mime(extension ? extension + 1 : "")) !=
            ASCIICHAT_OK ||
        http_response_set_header(response, "Cache-Control", "no-store") != ASCIICHAT_OK) {
      shared_http_finish(response, 500);
      return;
    }
    shared_http_finish(response, 200);
    return;
  }
  if (strcmp(info->path, "/mcp")) {
    shared_http_finish(response, 404);
    return;
  }
  if (strcmp(info->method, "POST")) {
    shared_http_finish(response, 405);
    return;
  }
  const char *type = shared_header(info, "content-type");
  if (!type || strncmp(type, "application/json", sizeof("application/json") - 1)) {
    shared_http_finish(response, 415);
    return;
  }
  const char *accept = shared_header(info, "accept");
  if (!accept || !strstr(accept, "application/json") || !strstr(accept, "text/event-stream")) {
    shared_http_finish(response, 406);
    return;
  }
  const char *version = shared_header(info, "mcp-protocol-version");
  if (version && strcmp(version, "2025-11-25") && strcmp(version, "2025-06-18") && strcmp(version, "2025-03-26")) {
    shared_http_finish(response, 400);
    return;
  }
  size_t length = 0;
  const char *body = http_request_body(request, &length);
  yyjson_doc *doc = body ? yyjson_read(body, length, 0) : NULL;
  if (!doc) {
    yyjson_mut_doc *error = mcp_error(NULL, -32700, "Parse error");
    char *encoded = yyjson_mut_write(error, 0, &length);
    yyjson_mut_doc_free(error);
    if (encoded) {
      (void)http_response_write(response, encoded, length);
      free(encoded);
    }
    shared_http_finish(response, 400);
    return;
  }
  char session_id[MCP_SESSION_ID_LENGTH + 1] = {0};
  const char *session = shared_header(info, "mcp-session-id");
  if (session) {
    if (strlen(session) != MCP_SESSION_ID_LENGTH) {
      yyjson_doc_free(doc);
      shared_http_finish(response, 400);
      return;
    }
    for (size_t i = 0; i < MCP_SESSION_ID_LENGTH; ++i) {
      if (!((session[i] >= '0' && session[i] <= '9') || (session[i] >= 'a' && session[i] <= 'f'))) {
        yyjson_doc_free(doc);
        shared_http_finish(response, 400);
        return;
      }
    }
    SAFE_STRNCPY(session_id, session, sizeof(session_id));
  }
  bool issue_session = false;
  yyjson_val *root = yyjson_doc_get_root(doc), *method = mcp_get(root, "method");
  if (yyjson_is_str(method) && yyjson_equals_str(method, "initialize") && mcp_get(root, "id")) {
    unsigned char random[16];
    randombytes_buf(random, sizeof(random));
    sodium_bin2hex(session_id, sizeof(session_id), random, sizeof(random));
    issue_session = true;
  }
  bool pending = false;
  yyjson_mut_doc *reply = mcp_dispatch(s, doc, response, session_id, &pending);
  yyjson_doc_free(doc);
  if (pending) {
    http_response_t *retained = NULL;
    if (http_response_retain(response, &retained) != ASCIICHAT_OK) {
      shared_http_finish(response, 503);
      return;
    }
    for (unsigned i = 0; i < s->policy.max_jobs; ++i)
      if (s->jobs[i].started && s->jobs[i].connection == response)
        s->jobs[i].connection = retained;
    return;
  }
  size_t encoded_length = 0;
  bool has_reply = reply != NULL;
  char *encoded = reply ? yyjson_mut_write(reply, 0, &encoded_length) : NULL;
  if (reply)
    yyjson_mut_doc_free(reply);
  if (encoded) {
    (void)http_response_write(response, encoded, encoded_length);
    free(encoded);
  }
  if (issue_session && http_response_set_header(response, "MCP-Session-Id", session_id) != ASCIICHAT_OK) {
    shared_http_finish(response, 500);
    return;
  }
  shared_http_finish(response, has_reply ? (encoded ? 200 : 500) : 202);
}

asciichat_error_t mcp_http_run(mcp_server_t *s) {
  http_server_config_t config = {.bind_address = s->policy.address,
                                 .port = (uint16_t)s->policy.port,
                                 .enable_http1 = true,
                                 .enable_http2 = true,
                                 .tls_certificate = s->policy.tls_cert,
                                 .tls_private_key = s->policy.tls_key,
                                 .tls_private_key_password = s->policy.tls_password,
                                 .max_connections = s->policy.max_jobs * 4U,
                                 .max_request_body_bytes = MCP_MAX_INPUT,
                                 .max_response_body_bytes = MCP_MAX_DOWNLOAD,
                                 .on_request = shared_http_request,
                                 .on_wakeup = shared_http_wakeup,
                                 .user_data = s};
  if (!config.max_connections)
    config.max_connections = 1;
  asciichat_error_t result = http_server_init(&s->shared_http, &config);
  if (result != ASCIICHAT_OK)
    return result;
  result = http_server_start(s->shared_http);
  if (result != ASCIICHAT_OK) {
    http_server_destroy(&s->shared_http);
    return result;
  }
  nat_upnp_context_t *mapping = NULL;
  if (GET_OPTION(enable_upnp)) {
    asciichat_error_t err = nat_upnp_open((uint16_t)s->policy.port, "ascii-chat MCP", &mapping);
    if (err != ASCIICHAT_OK)
      LOG_ERRNO_IF_SET("MCP port forwarding unavailable");
  }
  while (!atomic_load_bool(&s->stopping) && !APP_CALLBACK_BOOL(should_exit)) {
    uv_sleep(16);
    mcp_jobs_reap(s, false);
    if (mapping && time_get_ns() >= mapping->refresh_at_ns) {
      asciichat_error_t err = nat_upnp_refresh(mapping);
      if (err != ASCIICHAT_OK)
        LOG_ERRNO_IF_SET("MCP mapping renewal failed");
    }
  }
  for (unsigned i = 0; i < s->policy.max_jobs; ++i)
    if (s->jobs[i].started)
      atomic_store_bool(&s->jobs[i].cancelled, true);
  http_server_stop(s->shared_http);
  http_server_destroy(&s->shared_http);
  nat_upnp_close(&mapping);
  mcp_jobs_reap(s, true);
  return ASCIICHAT_OK;
}
