#include "mcp.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/app_callbacks.h>
#include <sodium.h>

int mcp_server_main(void) {
  mcp_server_t *s = SAFE_CALLOC(1, sizeof(*s), mcp_server_t *);
  s->policy = (mcp_policy_t){.hosted = GET_OPTION(mcp_server),
                             .address = GET_OPTION(address),
                             .address6 = GET_OPTION(address6),
                             .port = GET_OPTION(port),
                             .origin = GET_OPTION(mcp_origin),
                             .tls_cert = GET_OPTION(websocket_tls_cert),
                             .tls_key = GET_OPTION(websocket_tls_key),
                             .tls_password = GET_OPTION(websocket_tls_key_password),
                             .max_jobs = (unsigned)GET_OPTION(max_clients)};
  if (!s->policy.max_jobs || s->policy.max_jobs > MCP_MAX_JOBS)
    s->policy.max_jobs = MCP_MAX_JOBS;
  if (s->policy.hosted && (!!s->policy.tls_cert[0] != !!s->policy.tls_key[0])) {
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_CONFIG, "MCP HTTPS requires both --websocket-tls-cert and --websocket-tls-key");
  }
  // Initialize immutable registry metadata before worker threads are started.
  registry_init_size();
  if (sodium_init() < 0 || mutex_init(&s->output_mutex, "mcp.output")) {
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot initialize MCP server");
  }
  if (!s->policy.hosted)
    log_set_terminal_output(false);
  log_info("MCP server started (%s, four rendering tools)", s->policy.hosted ? "restricted HTTP" : "local stdio");
  asciichat_error_t err = s->policy.hosted ? mcp_http_run(s) : mcp_stdio_run(s);
  for (int i = 0; i < MCP_MAX_JOBS; ++i)
    if (s->jobs[i].started)
      atomic_store_bool(&s->jobs[i].cancelled, true);
  mcp_jobs_reap(s, true);
  mcp_stdio_release();
  mutex_destroy(&s->output_mutex);
  SAFE_FREE(s);
  return err;
}
