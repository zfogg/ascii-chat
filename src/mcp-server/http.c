#include "mcp.h"
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/tui/status.h>
#include <ascii-chat/tui/controller.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/app_callbacks.h>
#include <ascii-chat/network/nat/upnp.h>
#include <libwebsockets.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

typedef struct {
  char *body, *response;
  size_t length, response_size, sent;
  bool pending;
  unsigned status;
} http_session_t;
static void clear_session(http_session_t *session) {
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
  char origin[2048];
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
    if (!origin_allowed(wsi, &s->policy))
      return respond(wsi, h, 403, NULL);
    if (strcmp((const char *)in, "/mcp"))
      return respond(wsi, h, 404, NULL);
    if (lws_hdr_total_length(wsi, WSI_TOKEN_POST_URI) <= 0)
      return respond(wsi, h, 405, NULL);
    char type[128];
    lws_hdr_copy(wsi, type, sizeof(type), WSI_TOKEN_HTTP_CONTENT_TYPE);
    if (strncmp(type, "application/json", 16))
      return respond(wsi, h, 415, NULL);
    char accept[512];
    lws_hdr_copy(wsi, accept, sizeof(accept), WSI_TOKEN_HTTP_ACCEPT);
    if (!strstr(accept, "application/json") || !strstr(accept, "text/event-stream"))
      return respond(wsi, h, 406, NULL);
    char version[64] = {0};
    int vn = lws_hdr_custom_copy(wsi, version, sizeof(version), "mcp-protocol-version:", 21);
    if (vn > 0 && strcmp(version, "2025-11-25") && strcmp(version, "2025-06-18") && strcmp(version, "2025-03-26"))
      return respond(wsi, h, 400, NULL);
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
    bool pending = false;
    yyjson_mut_doc *response = mcp_dispatch(s, doc, wsi, &pending);
    yyjson_doc_free(doc);
    h->pending = pending;
    if (pending) {
      lws_set_timeout(wsi, PENDING_TIMEOUT_HTTP_CONTENT, 70);
      return 0;
    }
    return respond(wsi, h, response ? 200 : 202, response);
  }
  case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
    for (int i = 0; i < MCP_MAX_JOBS; ++i)
      if (s->jobs[i].started && s->jobs[i].connection && atomic_load_bool(&s->jobs[i].done))
        lws_callback_on_writable(s->jobs[i].connection);
    return 0;
  case LWS_CALLBACK_HTTP_WRITEABLE: {
    if (h->pending) {
      for (int i = 0; i < MCP_MAX_JOBS; ++i) {
        mcp_job_t *j = &s->jobs[i];
        if (j->started && j->connection == wsi && atomic_load_bool(&j->done)) {
          h->pending = false;
          h->status = 200;
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
      if (lws_add_http_common_headers(wsi, h->status, "application/json", (lws_filepos_t)h->response_size, &p, end) ||
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
    memcpy(buffer + LWS_PRE, h->response + h->sent, n);
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
  case LWS_CALLBACK_CLOSED_HTTP:
    for (int i = 0; i < MCP_MAX_JOBS; ++i)
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
  if (s->http_context)
    lws_cancel_service(s->http_context);
}
typedef struct {
  lws_sorted_usec_list_t timer;
  struct lws_context *context;
} http_pulse_t;
static void http_pulse(lws_sorted_usec_list_t *timer) {
  http_pulse_t *pulse = (http_pulse_t *)timer;
  // libwebsockets ignores the legacy service timeout; schedule terminal and shutdown polling explicitly.
  lws_cancel_service(pulse->context);
  lws_sul_schedule(pulse->context, 0, &pulse->timer, http_pulse, 16000);
}
asciichat_error_t mcp_http_run(mcp_server_t *s) {
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
  while (!atomic_load_bool(&s->stopping) && !APP_CALLBACK_BOOL(should_exit)) {
    lws_service(context, 16);
    mcp_jobs_reap(s, false);
    if (mapping && time_get_ns() >= mapping->refresh_at_ns) {
      asciichat_error_t e = nat_upnp_refresh(mapping);
      if (e)
        LOG_ERRNO_IF_SET("MCP mapping renewal failed");
    }
    if (ui) {
      status.connected_count = 0;
      for (int i = 0; i < MCP_MAX_JOBS; ++i)
        if (s->jobs[i].started && !atomic_load_bool(&s->jobs[i].done))
          ++status.connected_count;
      if (!ui_status_display_interactive(&status))
        atomic_store_bool(&s->stopping, true);
    }
  }
  lws_sul_cancel(&pulse.timer);
  for (int i = 0; i < MCP_MAX_JOBS; ++i)
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
