#include "mcp.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/app_callbacks.h>
#include <sodium.h>
#include <ascii-chat/stats/runtime.h>

static void mcp_stats_view(stats_view_t *view, void *data) {
  const mcp_server_t *server = data;
  stats_view_add_header(view, "MCP transport");
  stats_view_add(view, "Transport: %s   Render concurrency limit: %u", server->policy.hosted ? "HTTP" : "stdio",
                 server->policy.max_jobs);
  if (server->policy.hosted)
    stats_view_add(view, "Listener: %s:%d/mcp", server->policy.address, server->policy.port);
  stats_view_add(view, "Render duration includes capture, encoding, and output publication.");
}

int mcp_server_main(void) {
  mcp_server_t *s = SAFE_CALLOC(1, sizeof(*s), mcp_server_t *);
  s->policy = (mcp_policy_t){.hosted = GET_OPTION(http_server),
                             .address = GET_OPTION(address),
                             .address6 = GET_OPTION(address6),
                             .port = GET_OPTION(port),
                             .origin = GET_OPTION(http_origin),
                             .tls_cert = GET_OPTION(http_tls_cert),
                             .tls_key = GET_OPTION(http_tls_key),
                             .tls_password = GET_OPTION(http_tls_key_password),
                             .max_jobs = (unsigned)GET_OPTION(max_clients)};
  log_set_terminal_output(s->policy.hosted);
  if (!s->policy.max_jobs || s->policy.max_jobs > MCP_MAX_JOBS)
    s->policy.max_jobs = MCP_MAX_JOBS;
  if (s->policy.hosted && (!!s->policy.tls_cert[0] != !!s->policy.tls_key[0])) {
    asciichat_error_t err = SET_ERRNO(ERROR_CONFIG, "MCP HTTPS requires both --http-tls-cert and --http-tls-key");
    log_error("MCP HTTPS requires both --http-tls-cert and --http-tls-key");
    SAFE_FREE(s);
    return err;
  }
  // Initialize immutable registry metadata before worker threads are started.
  registry_init_size();
  if (sodium_init() < 0 || mutex_init(&s->output_mutex, "mcp.output")) {
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot initialize MCP server");
  }
  log_info("MCP server started (%s, four rendering tools)", s->policy.hosted ? "restricted HTTP" : "local stdio");
  stats_runtime_set_provider(mcp_stats_view, s);
  asciichat_error_t err = s->policy.hosted ? mcp_http_run(s) : mcp_stdio_run(s);
  stats_runtime_set_provider(NULL, NULL);
  for (int i = 0; i < MCP_MAX_JOBS; ++i)
    if (s->jobs[i].started)
      atomic_store_bool(&s->jobs[i].cancelled, true);
  mcp_jobs_reap(s, true);
  mcp_stdio_release();
  mutex_destroy(&s->output_mutex);
  SAFE_FREE(s);
  return err;
}
