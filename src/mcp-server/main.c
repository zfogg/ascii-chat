#include "mcp.h"
#include <ascii-chat/options/options.h>
#include <ascii-chat/options/registry/core.h>
#include <ascii-chat/app_callbacks.h>
#include <sodium.h>
#include <uv.h>
#include <ascii-chat/stats/runtime.h>
#include <uv.h>

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
  if (strcmp(GET_OPTION(mcp_transport), "http") && strcmp(GET_OPTION(mcp_transport), "stdio"))
    return SET_ERRNO(ERROR_CONFIG, "--transport must be http or stdio");
  mcp_server_t *s = SAFE_CALLOC(1, sizeof(*s), mcp_server_t *);
  s->policy = (mcp_policy_t){.hosted = !strcmp(GET_OPTION(mcp_transport), "http"),
                             .restricted = GET_OPTION(mcp_server_restricted),
                             .address = GET_OPTION(address),
                             .address6 = GET_OPTION(address6),
                             .port = GET_OPTION(port),
                             .origin = GET_OPTION(http_origin),
                             .tls_cert = GET_OPTION(tls_cert),
                             .tls_key = GET_OPTION(tls_key),
                             .tls_password = GET_OPTION(tls_key_password),
                             .max_jobs = (unsigned)GET_OPTION(max_clients),
                             .downloads_url_prefix = GET_OPTION(mcp_downloads_url_prefix)};
  if (s->policy.restricted && s->policy.hosted && s->policy.downloads_url_prefix && *s->policy.downloads_url_prefix) {
    const char *prefix = s->policy.downloads_url_prefix;
    const char *authority = !strncmp(prefix, "https://", sizeof("https://") - 1) ? prefix + 8
                            : !strncmp(prefix, "http://", sizeof("http://") - 1) ? prefix + 7
                                                                                 : NULL;
    if (!authority || !*authority || *authority == '/' || strpbrk(prefix, " \t\r\n?#")) {
      SAFE_FREE(s);
      return SET_ERRNO(ERROR_CONFIG, "--downloads-url-prefix must be an HTTP(S) URL without query or fragment");
    }
  }
  if (s->policy.restricted && s->policy.hosted) {
    const char *override = GET_OPTION(mcp_downloads_dir);
    char *data_dir = NULL;
    if (override && *override) {
      SAFE_STRNCPY(s->policy.downloads_dir, override, sizeof(s->policy.downloads_dir));
    } else {
      data_dir = platform_get_data_dir();
      if (!data_dir) {
        SAFE_FREE(s);
        return SET_ERRNO(ERROR_CONFIG, "Cannot determine MCP data directory");
      }
      SAFE_SNPRINTF(s->policy.downloads_dir, sizeof(s->policy.downloads_dir), "%s/downloads", data_dir);
      SAFE_FREE(data_dir);
    }
    asciichat_error_t directory_error = platform_mkdir_recursive(s->policy.downloads_dir, 0700);
    if (directory_error != ASCIICHAT_OK) {
      SAFE_FREE(s);
      return directory_error;
    }
    uv_fs_t resolved;
    int rc = uv_fs_realpath(NULL, &resolved, s->policy.downloads_dir, NULL);
    if (rc < 0 || strlen((const char *)resolved.ptr) >= sizeof(s->policy.downloads_dir)) {
      uv_fs_req_cleanup(&resolved);
      SAFE_FREE(s);
      return SET_ERRNO(ERROR_CONFIG, "Cannot resolve MCP downloads directory");
    }
    SAFE_STRNCPY(s->policy.downloads_dir, (const char *)resolved.ptr, sizeof(s->policy.downloads_dir));
    uv_fs_req_cleanup(&resolved);
  }
  log_set_terminal_output(s->policy.hosted);
  unsigned available_cpus = uv_available_parallelism();
  if (!available_cpus)
    available_cpus = 1;
  if (!s->policy.max_jobs || s->policy.max_jobs > available_cpus)
    s->policy.max_jobs = available_cpus;
  if (s->policy.hosted && (!!s->policy.tls_cert[0] != !!s->policy.tls_key[0])) {
    asciichat_error_t err = SET_ERRNO(ERROR_CONFIG, "MCP HTTPS requires both --tls-cert and --tls-key");
    log_error("MCP HTTPS requires both --tls-cert and --tls-key");
    SAFE_FREE(s);
    return err;
  }
  // Initialize immutable registry metadata before worker threads are started.
  registry_init_size();
  if (sodium_init() < 0 || mutex_init(&s->output_mutex, "mcp.output")) {
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_INVALID_STATE, "Cannot initialize MCP server");
  }
  s->jobs = SAFE_CALLOC(s->policy.max_jobs, sizeof(*s->jobs), mcp_job_t *);
  if (!s->jobs) {
    mutex_destroy(&s->output_mutex);
    SAFE_FREE(s);
    return SET_ERRNO(ERROR_MEMORY, "Cannot allocate MCP render jobs");
  }
  log_info("MCP server started (%s, %s capabilities, %u render jobs)", s->policy.hosted ? "HTTP" : "stdio",
           s->policy.restricted ? "restricted" : "local", s->policy.max_jobs);
  stats_runtime_set_provider(mcp_stats_view, s);
  asciichat_error_t err = s->policy.hosted ? mcp_http_run(s) : mcp_stdio_run(s);
  stats_runtime_set_provider(NULL, NULL);
  for (unsigned i = 0; i < s->policy.max_jobs; ++i)
    if (s->jobs[i].started)
      atomic_store_bool(&s->jobs[i].cancelled, true);
  mcp_jobs_reap(s, true);
  mcp_stdio_release();
  mutex_destroy(&s->output_mutex);
  SAFE_FREE(s->jobs);
  SAFE_FREE(s);
  return err;
}
