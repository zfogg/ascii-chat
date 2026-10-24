#pragma once

#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/platform/abstraction.h>
#include <yyjson.h>
#include <string.h>

// Avoid yyjson's intentional unsigned wraparound in object lookup under integer sanitizers.
static inline yyjson_val *mcp_get(yyjson_val *obj, const char *name) {
  if (!yyjson_is_obj(obj) || !yyjson_obj_size(obj))
    return NULL;
  size_t i, count;
  yyjson_val *key, *value;
  yyjson_obj_foreach(obj, i, count, key, value) if (yyjson_equals_str(key, name)) return value;
  return NULL;
}

#define MCP_MAX_INPUT (24u * 1024u * 1024u)
#define MCP_MAX_OUTPUT (8u * 1024u * 1024u)
#define MCP_MAX_JOBS 4

typedef enum { MCP_FILE, MCP_URL, MCP_WEBCAM, MCP_VISUALIZATION } mcp_tool_t;
typedef struct {
  bool hosted;
  const char *address;
  const char *address6;
  const char *tls_cert, *tls_key, *tls_password;
  unsigned max_jobs;
  const char *origin;
  int port;
} mcp_policy_t;

typedef struct {
  char **argv;
  size_t argc;
  char upload_dir[4096];
  char stage_dir[4096];
  char stage_file[4096];
  char output_path[4096];
  unsigned timeout_ms;
  bool capture_webcam;
} mcp_render_request_t;

typedef struct {
  char *text;
  size_t length;
  char error[2048];
  uint64_t elapsed_ms;
  bool timed_out;
  char output_path[4096];
} mcp_render_result_t;

typedef struct mcp_job {
  struct mcp_server *server;
  yyjson_doc *message;
  yyjson_mut_doc *response;
  mcp_render_request_t request;
  mcp_tool_t tool;
#ifdef ASCIICHAT_WITH_OTEL
  gotel_context_t telemetry_parent;
#endif
  asciichat_thread_t thread;
  atomic_t cancelled;
  atomic_t done;
  bool started;
  void *connection;
} mcp_job_t;

typedef struct mcp_server {
  mcp_policy_t policy;
  mcp_job_t jobs[MCP_MAX_JOBS];
  mutex_t output_mutex;
  void *http_context;
  atomic_t stopping;
  bool initialized;
} mcp_server_t;

int mcp_server_main(void);
asciichat_error_t mcp_stdio_reserve(void);
void mcp_stdio_release(void);
yyjson_mut_val *mcp_tools_schema(yyjson_mut_doc *doc, const mcp_policy_t *policy);
asciichat_error_t mcp_request_parse(const mcp_policy_t *policy, mcp_tool_t tool, yyjson_val *arguments,
                                    mcp_render_request_t *out);
void mcp_request_destroy(mcp_render_request_t *request);
void mcp_render_execute(mcp_render_request_t *request, atomic_t *cancelled, mcp_render_result_t *result);
yyjson_mut_doc *mcp_dispatch(mcp_server_t *server, yyjson_doc *message, void *connection, bool *pending);
yyjson_mut_doc *mcp_error(yyjson_val *id, int code, const char *message);
void mcp_jobs_reap(mcp_server_t *server, bool all);
void mcp_stdio_send(mcp_server_t *server, yyjson_mut_doc *response);
asciichat_error_t mcp_stdio_run(mcp_server_t *server);
asciichat_error_t mcp_http_run(mcp_server_t *server);
void mcp_http_wake(mcp_server_t *server);
