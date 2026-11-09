
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
#include <libwebsockets.h>
#include <sodium.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <uv.h>
#include <ascii-chat/platform/stat.h>

typedef struct {
  char *body, *response;
  size_t length, response_size, sent;
  bool pending, file_open;
  uv_file file;
  const char *mime;
  bool issue_session;
  char session_id[MCP_SESSION_ID_LENGTH + 1];
  unsigned status;
  /**
   */
} http_session_t;
static void clear_session(http_session_t *session) {
  if (session->file_open) {
    uv_fs_t close_request;
    uv_fs_close(NULL, &close_request, session->file, NULL);
    uv_fs_req_cleanup(&close_request);
  }
  SAFE_FREE(session->body);
  free(session->response);
  memset(session, 0, sizeof(*session));
}
static int respond(struct lws *wsi, http_session_t *h, unsigned status, yyjson_mut_doc *doc) {
  h->status = status;
  if (doc) {
    h->response = yyjson_mut_write(doc, 0, &h->response_size);
    yyjson_mut_doc_free(doc);
  }
  lws_callback_on_writable(wsi);
  return 0;
}
static bool origin_allowed(struct lws *wsi, const mcp_policy_t *p) {
  char origin[MCP_SERVER_HTTP_ORIGIN_BUFFER_SIZE];
  int n = lws_hdr_copy(wsi, origin, sizeof(origin), WSI_TOKEN_ORIGIN);
  if (n <= 0)
    return true;
  return p->origin && !strcmp(origin, p->origin);
}
static int callback(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
  mcp_server_t *s = lws_context_user(lws_get_context(wsi));
  http_session_t *h = user;
  switch (reason) {
  case LWS_CALLBACK_HTTP: {
    if (!h)
      return -1;
    clear_session(h);
    if (s->policy.restricted && !strncmp((const char *)in, "/downloads/", sizeof("/downloads/") - 1)) {
      if (lws_hdr_total_length(wsi, WSI_TOKEN_GET_URI) <= 0)
        return respond(wsi, h, 405, NULL);
      const char *name = (const char *)in + 11;
      int64_t expires;
      if (!mcp_download_name(name, &expires))
        return respond(wsi, h, 404, NULL);
      char path[PLATFORM_MAX_PATH_LENGTH];
      int n = safe_snprintf(path, sizeof(path), "%s/%s", s->policy.downloads_dir, name);
      if (n < 0 || (size_t)n >= sizeof(path))
        return respond(wsi, h, 404, NULL);
      uv_fs_t fs = {0};
      if (expires <= (int64_t)time(NULL)) {
        uv_fs_unlink(NULL, &fs, path, NULL);
        uv_fs_req_cleanup(&fs);
        return respond(wsi, h, 404, NULL);
      }
      int rc = uv_fs_lstat(NULL, &fs, path, NULL);
      uv_stat_t before = fs.statbuf;
      bool regular = rc == 0 && S_ISREG(before.st_mode);
      uv_fs_req_cleanup(&fs);
      if (!regular)
        return respond(wsi, h, 404, NULL);
      rc = uv_fs_open(NULL, &fs, path, UV_FS_O_RDONLY | UV_FS_O_NOFOLLOW, 0, NULL);
      uv_fs_req_cleanup(&fs);
      if (rc < 0)
        return respond(wsi, h, 404, NULL);
      h->file = rc;
      h->file_open = true;
      rc = uv_fs_fstat(NULL, &fs, h->file, NULL);
      regular = rc == 0 && S_ISREG(fs.statbuf.st_mode) && before.st_dev == fs.statbuf.st_dev &&
                before.st_ino == fs.statbuf.st_ino;
      if (regular)
        h->response_size = (size_t)fs.statbuf.st_size;
      uv_fs_req_cleanup(&fs);
      if (!regular) {
        clear_session(h);
        return respond(wsi, h, 404, NULL);
      }
      const char *ext = strrchr(name, '.');
      h->mime = mcp_output_mime(ext + 1);
      h->status = 200;
      lws_callback_on_writable(wsi);
      return 0;
    }
    if (!origin_allowed(wsi, &s->policy))
      return respond(wsi, h, 403, NULL);
    if (strcmp((const char *)in, "/mcp"))
      return respond(wsi, h, 404, NULL);
    if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) <= 0)
      return respond(wsi, h, 405, NULL);
    char type[MCP_SERVER_HTTP_TYPE_BUFFER_SIZE];
    lws_hdr_copy(wsi, type, sizeof(type), WSI_TOKEN_HTTP_CONTENT_TYPE);
    if (strncmp(type, "application/json", sizeof("application/json") - 1))
      return respond(wsi, h, 415, NULL);
    char accept[MCP_SERVER_HTTP_ACCEPT_BUFFER_SIZE];
    lws_hdr_copy(wsi, accept, sizeof(accept), WSI_TOKEN_HTTP_ACCEPT);
    if (!strstr(accept, "application/json") || !strstr(accept, "text/event-stream"))
      return respond(wsi, h, 406, NULL);
    char version[MCP_SERVER_HTTP_VERSION_BUFFER_SIZE] = {0};
    int vn = lws_hdr_custom_copy(wsi, version, sizeof(version), "mcp-protocol-version:", 21);
    if (vn > 0 && strcmp(version, "2025-11-25") && strcmp(version, "2025-06-18") && strcmp(version, "2025-03-26"))
      return respond(wsi, h, 400, NULL);
    int session_length = lws_hdr_custom_copy(wsi, h->session_id, sizeof(h->session_id), "mcp-session-id:", 15);
    if (session_length > 0) {
      if (session_length != MCP_SESSION_ID_LENGTH)
        return respond(wsi, h, 400, NULL);
      for (int i = 0; i < session_length; ++i)
        if (!((h->session_id[i] >= '0' && h->session_id[i] <= '9') ||
              (h->session_id[i] >= 'a' && h->session_id[i] <= 'f')))
          return respond(wsi, h, 400, NULL);
    }
    h->body = SAFE_MALLOC(MCP_MAX_INPUT + 1, char *);
    return 0;
  }
  case LWS_CALLBACK_HTTP_BODY:
    if (h->status)
      return 0;
    if (!h->body || len > MCP_MAX_INPUT - h->length)
      return respond(wsi, h, 413, NULL);
    memcpy(h->body + h->length, in, len);
    h->length += len;
    return 0;
  case LWS_CALLBACK_HTTP_BODY_COMPLETION: {
    if (!h->body || h->status)
      return 0;
    yyjson_doc *doc = yyjson_read(h->body, h->length, 0);
    SAFE_FREE(h->body);
    if (!doc)
      return respond(wsi, h, 400, mcp_error(NULL, -32700, "Parse error"));
    yyjson_val *root = yyjson_doc_get_root(doc);
    yyjson_val *method = mcp_get(root, "method");
    if (yyjson_is_str(method) && yyjson_equals_str(method, "initialize") && mcp_get(root, "id")) {
      unsigned char random[16];
      randombytes_buf(random, sizeof(random));
      sodium_bin2hex(h->session_id, sizeof(h->session_id), random, sizeof(random));
      h->issue_session = true;
    }
    bool pending = false;
    yyjson_mut_doc *response = mcp_dispatch(s, doc, wsi, h->session_id, &pending);
    yyjson_doc_free(doc);
    h->pending = pending;
    if (pending) {
      lws_set_timeout(wsi, PENDING_TIMEOUT_HTTP_CONTENT, 70);
      return 0;
    }
    return respond(wsi, h, response ? 200 : 202, response);
  }
  case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
    for (unsigned i = 0; i < s->policy.max_jobs; ++i)
      if (s->jobs[i].started && s->jobs[i].connection && atomic_load_bool(&s->jobs[i].done))
        lws_callback_on_writable(s->jobs[i].connection);
    return 0;
  case LWS_CALLBACK_HTTP_WRITEABLE: {
    if (h->pending) {
      for (unsigned i = 0; i < s->policy.max_jobs; ++i) {
        mcp_job_t *j = &s->jobs[i];
        if (j->started && j->connection == wsi && atomic_load_bool(&j->done)) {
          h->pending = false;
          h->status = 200;
          if (s->policy.restricted &&
              mcp_download_finalize(&s->policy, j->response, (int64_t)time(NULL)) != ASCIICHAT_OK) {
            yyjson_mut_val *root = yyjson_mut_doc_get_root(j->response);
            yyjson_mut_obj_remove_str(root, "result");
            yyjson_mut_val *error = yyjson_mut_obj(j->response);
            yyjson_mut_obj_add_int(j->response, error, "code", -32603);
            yyjson_mut_obj_add_str(j->response, error, "message", "Cannot publish download");
            yyjson_mut_obj_add_val(j->response, root, "error", error);
          }
          h->response = yyjson_mut_write(j->response, 0, &h->response_size);
          j->connection = NULL;
          mcp_jobs_reap(s, false);
          break;
        }
      }
      if (h->pending)
        return 0;
    }
    if (!h->status)
      return 0;
    if (!h->sent) {
      ASCII_OTEL_SCOPE(MCP_HTTP_RESPONSE);
      ASCII_OTEL_ATTR_INT("http.response.status_code", h->status);
      ASCII_OTEL_ATTR_INT("http.response.body.size", (int64_t)h->response_size);
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_HTTP_RESPONSES, 1);
      unsigned char buffer[LWS_PRE + 1024], *p = buffer + LWS_PRE, *end = buffer + sizeof(buffer);
      if (lws_add_http_common_headers(wsi, h->status, h->mime ? h->mime : "application/json",
                                      (lws_filepos_t)h->response_size, &p, end) ||
          (h->file_open && lws_add_http_header_by_name(wsi, (const unsigned char *)"cache-control:",
                                                       (const unsigned char *)"no-store", 8, &p, end)) ||
          (h->issue_session && lws_add_http_header_by_name(
                                   wsi, (const unsigned char *)"mcp-session-id:", (const unsigned char *)h->session_id,
                                   MCP_SESSION_ID_LENGTH, &p, end)) ||
          lws_finalize_write_http_header(wsi, buffer + LWS_PRE, &p, end))
        return -1;
      if (!h->response_size) {
        clear_session(h);
        return lws_http_transaction_completed(wsi) ? -1 : 0;
      }
    }
    size_t n = h->response_size - h->sent;
    if (n > 16384)
      n = 16384;
    unsigned char buffer[LWS_PRE + 16384];
    if (h->file_open) {
      uv_buf_t chunk = uv_buf_init((char *)buffer + LWS_PRE, (unsigned int)n);
      uv_fs_t read_request;
      int read_size = uv_fs_read(NULL, &read_request, h->file, &chunk, 1, (int64_t)h->sent, NULL);
      uv_fs_req_cleanup(&read_request);
      if (read_size <= 0)
        return -1;
      n = (size_t)read_size;
    } else {
      memcpy(buffer + LWS_PRE, h->response + h->sent, n);
    }
    bool final = h->sent + n == h->response_size;
    int wrote = lws_write(wsi, buffer + LWS_PRE, n, final ? LWS_WRITE_HTTP_FINAL : LWS_WRITE_HTTP);
    if (wrote < 0 || (size_t)wrote != n)
      return -1;
    h->sent += n;
    if (final) {
      clear_session(h);
      return lws_http_transaction_completed(wsi) ? -1 : 0;
    }
    lws_callback_on_writable(wsi);
    return 0;
  }
  case LWS_CALLBACK_HTTP_FILE_COMPLETION:
    return lws_http_transaction_completed(wsi) ? -1 : 0;
  case LWS_CALLBACK_CLOSED_HTTP:
    for (unsigned i = 0; i < s->policy.max_jobs; ++i)
      if (s->jobs[i].started && s->jobs[i].connection == wsi) {
        s->jobs[i].connection = NULL;
        atomic_store_bool(&s->jobs[i].cancelled, true);
      }
    if (h)
      clear_session(h);
    return 0;
  default:
    return 0;
  }
}
void mcp_http_wake(mcp_server_t *s) {
  if (s->shared_http) {
    (void)http_server_wakeup(s->shared_http);
    return;
  }
  if (s->http_context)
    lws_cancel_service(s->http_context);
}

static const char *shared_header(const http_request_info_t *info, const char *name) {
  for (size_t i = 0; i < info->header_count; ++i)
    if (!platform_strcasecmp(info->headers[i].name, name))
      return info->headers[i].value;
  return NULL;
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
    (void)http_response_set_header(response, "Content-Type", mcp_output_mime(extension ? extension + 1 : ""));
    (void)http_response_set_header(response, "Cache-Control", "no-store");
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
  if (issue_session)
    (void)http_response_set_header(response, "MCP-Session-Id", session_id);
  shared_http_finish(response, has_reply ? 200 : 202);
}

static asciichat_error_t mcp_http_run_shared(mcp_server_t *s) {
  http_server_config_t config = {.bind_address = s->policy.address,
                                 .port = (uint16_t)s->policy.port,
                                 .enable_http1 = true,
                                 .enable_http2 = true,
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
/**
 * @brief http session keeps the values that move through the subsystem: `body`, `response`, `length`, `response_size`.
 * Some members are pointers into caller- or subsystem-owned memory, so lifetime follows the API that populated them
 * rather than the struct copy itself.
 */
typedef struct {
  lws_sorted_usec_list_t timer;
  struct lws_context *context;
  /**
   * @brief For http pulse, this is the live record that ties `timer`, `context` together. The owning code creates it,
   * updates it while work is active, and releases its handles and pointed-to storage before freeing the record.
   */
} http_pulse_t;
static void http_pulse(lws_sorted_usec_list_t *timer) {
  http_pulse_t *pulse = (http_pulse_t *)timer;
  // libwebsockets ignores the legacy service timeout; schedule terminal and shutdown polling explicitly.
  lws_cancel_service(pulse->context);
  lws_sul_schedule(pulse->context, 0, &pulse->timer, http_pulse, 16000);
}
asciichat_error_t mcp_http_run(mcp_server_t *s) {
  if ((!s->policy.tls_cert || !*s->policy.tls_cert) && (!s->policy.tls_key || !*s->policy.tls_key) &&
      (!s->policy.address6 || !*s->policy.address6))
    return mcp_http_run_shared(s);
  static const struct lws_protocols protocols[] = {{"http", callback, sizeof(http_session_t), 16384, 0, NULL, 0},
                                                   {NULL, NULL, 0, 0, 0, NULL, 0}};
  struct lws_context_creation_info info = {0};
  info.port = CONTEXT_PORT_NO_LISTEN;
  info.protocols = protocols;
  info.user = s;
  info.fd_limit_per_thread = 32;
  info.timeout_secs = 70;
  info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT | LWS_SERVER_OPTION_EXPLICIT_VHOSTS;
  struct lws_context *context = lws_create_context(&info);
  if (!context)
    return SET_ERRNO(ERROR_NETWORK, "Cannot create MCP HTTP context");
  s->http_context = context;
  const char *addresses[] = {s->policy.address, s->policy.address6};
  int bound = 0;
  for (int i = 0; i < 2; ++i)
    if (addresses[i] && addresses[i][0]) {
      info.port = s->policy.port;
      info.iface = addresses[i];
      info.vhost_name = i ? "mcp-ipv6" : "mcp-ipv4";
      info.ssl_cert_filepath = s->policy.tls_cert && *s->policy.tls_cert ? s->policy.tls_cert : NULL;
      info.ssl_private_key_filepath = s->policy.tls_key && *s->policy.tls_key ? s->policy.tls_key : NULL;
      info.ssl_private_key_password = s->policy.tls_password;
      if (i == 1)
        info.options |= LWS_SERVER_OPTION_IPV6_V6ONLY_MODIFY | LWS_SERVER_OPTION_IPV6_V6ONLY_VALUE;
      if (!lws_create_vhost(context, &info)) {
        lws_context_destroy(context);
        s->http_context = NULL;
        return SET_ERRNO(ERROR_NETWORK, "Cannot bind MCP HTTP listener at %s:%d", addresses[i], info.port);
      }
      ++bound;
      log_info("MCP HTTP listening at %s:%d/mcp", addresses[i], info.port);
    }
  if (!bound) {
    lws_context_destroy(context);
    s->http_context = NULL;
    return SET_ERRNO(ERROR_NETWORK, "No MCP bind addresses configured");
  }
  nat_upnp_context_t *mapping = NULL;
  if (GET_OPTION(enable_upnp)) {
    asciichat_error_t err = nat_upnp_open((uint16_t)s->policy.port, "ascii-chat MCP", &mapping);
    if (err != ASCIICHAT_OK)
      LOG_ERRNO_IF_SET("MCP port forwarding unavailable");
  }
  bool ui = GET_OPTION(status_screen) && terminal_is_interactive();
  if (ui)
    ui_status_log_init();
  ui_status_t status = {.port = (uint16_t)s->policy.port,
                        .start_time = time(NULL),
                        .mode_name = "MCP Server",
                        .detail = "/mcp · media rendering"};
  status.ipv4_bound = addresses[0] && *addresses[0];
  status.ipv6_bound = addresses[1] && *addresses[1];
  SAFE_SNPRINTF(status.ipv4_address, sizeof(status.ipv4_address), "%s:%d", addresses[0], s->policy.port);
  SAFE_SNPRINTF(status.ipv6_address, sizeof(status.ipv6_address), "[%s]:%d", addresses[1], s->policy.port);
  http_pulse_t pulse = {.context = context};
  lws_sul_schedule(context, 0, &pulse.timer, http_pulse, 16000);
  int64_t last_prune = 0;
  while (!atomic_load_bool(&s->stopping) && !APP_CALLBACK_BOOL(should_exit)) {
    lws_service(context, 16);
    int64_t now = (int64_t)time(NULL);
    if (s->policy.restricted && (last_prune == 0 || now - last_prune >= 1)) {
      mcp_download_prune(s->policy.downloads_dir, now);
      last_prune = now;
    }
    mcp_jobs_reap(s, false);
    if (mapping && time_get_ns() >= mapping->refresh_at_ns) {
      asciichat_error_t e = nat_upnp_refresh(mapping);
      if (e)
        LOG_ERRNO_IF_SET("MCP mapping renewal failed");
    }
    if (ui) {
      status.connected_count = 0;
      for (unsigned i = 0; i < s->policy.max_jobs; ++i)
        if (s->jobs[i].started && !atomic_load_bool(&s->jobs[i].done))
          ++status.connected_count;
      if (!ui_status_display_interactive(&status))
        atomic_store_bool(&s->stopping, true);
    }
  }
  lws_sul_cancel(&pulse.timer);
  for (unsigned i = 0; i < s->policy.max_jobs; ++i)
    if (s->jobs[i].started)
      atomic_store_bool(&s->jobs[i].cancelled, true);
  mcp_jobs_reap(s, true);
  if (ui) {
    ui_controller_remove(UI_SCREEN_STATUS);
    ui_status_log_destroy();
  }
  nat_upnp_close(&mapping);
  lws_context_destroy(context);
  s->http_context = NULL;
  return ASCIICHAT_OK;
}
