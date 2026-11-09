#pragma once

/* Storage capacities in elements; text buffers include the terminating NUL. */
#define MCP_SERVER_MCP_ERROR_BUFFER_SIZE 2048

#include <ascii-chat/common.h>
#include <ascii-chat/atomic.h>
#include <ascii-chat/otel.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/platform/string.h>
#include <ascii-chat/network/http/server.h>
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
#define MCP_MAX_DOWNLOAD (64u * 1024u * 1024u)
#define MCP_SESSION_ID_LENGTH 32

typedef enum { MCP_FILE, MCP_URL, MCP_WEBCAM, MCP_VISUALIZATION } mcp_tool_t;
typedef struct {
  bool hosted;
  bool restricted;
  const char *address;
  const char *address6;
  const char *tls_cert, *tls_key, *tls_password;
  unsigned max_jobs;
  const char *origin;
  int port;
  char downloads_dir[PLATFORM_MAX_PATH_LENGTH];
  const char *downloads_url_prefix;
  /**
   */
} mcp_policy_t;

/**
 * @brief When code needs to work with mcp policy, it keeps `hosted`, `restricted`, `address`, `address6` here instead
 * of scattering it across globals. Initialization establishes the handles and sizes; teardown must release any
 * allocation, lock, thread, or callback context recorded in those fields.
 */
typedef struct {
  char **argv;
  size_t argc;
  char upload_dir[PLATFORM_MAX_PATH_LENGTH];
  char stage_dir[PLATFORM_MAX_PATH_LENGTH];
  char stage_file[PLATFORM_MAX_PATH_LENGTH];
  char output_path[PLATFORM_MAX_PATH_LENGTH];
  unsigned timeout_ms;
  bool capture_webcam;
  /**
   */
} mcp_render_request_t;

typedef struct {
  char *text;
  size_t length;
  char error[MCP_SERVER_MCP_ERROR_BUFFER_SIZE];
  uint64_t elapsed_ms;
  bool timed_out;
  char output_path[PLATFORM_MAX_PATH_LENGTH];
  /**
   * @brief mcp render request is intentionally a small, concrete record. Fields such as `argv`, `argc`, `timeout_ms`,
   * `capture_webcam` give callers the identity and current value they need. It is normally borrowed from, or owned by,
   * the surrounding subsystem—not independently managed.
   */
} mcp_render_result_t;

/**
 * @brief For mcp render result, this is the live record that ties `text`, `length`, `elapsed_ms`, `timed_out` together.
 * The owning code creates it, updates it while work is active, and releases its handles and pointed-to storage before
 * freeing the record.
 */
typedef struct mcp_job {
  struct mcp_server *server;
  yyjson_doc *message;
  yyjson_mut_doc *response;
  mcp_render_request_t request;
  mcp_tool_t tool;
  gotel_context_t telemetry_parent;
  asciichat_thread_t thread;
  atomic_t cancelled;
  atomic_t done;
  bool started;
  void *connection;
  char session_id[MCP_SESSION_ID_LENGTH + 1];
} mcp_job_t;

/**
 * @brief The useful part of mcp job is in `server`, `message`, `response`, `request`: together they describe what is
 * pending, who owns it, and how much data is valid. Keep the object with its parent operation and use the parent
 * cleanup path.
 * @brief mcp server keeps the values that move through the subsystem: `policy`, `jobs`, `output_mutex`, `http_context`.
 * Some members are pointers into caller- or subsystem-owned memory, so lifetime follows the API that populated them
 * rather than the struct copy itself.
 */
typedef struct mcp_server {
  mcp_policy_t policy;
  mcp_job_t *jobs;
  mutex_t output_mutex;
  void *http_context;
  http_server_t *shared_http;
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
yyjson_mut_doc *mcp_dispatch(mcp_server_t *server, yyjson_doc *message, void *connection, const char *session_id,
                             bool *pending);
yyjson_mut_doc *mcp_error(yyjson_val *id, int code, const char *message);
void mcp_jobs_reap(mcp_server_t *server, bool all);
void mcp_stdio_send(mcp_server_t *server, yyjson_mut_doc *response);
asciichat_error_t mcp_stdio_run(mcp_server_t *server);
asciichat_error_t mcp_http_run(mcp_server_t *server);
void mcp_http_wake(mcp_server_t *server);
extern const char *const mcp_output_formats[];
const char *mcp_output_mime(const char *extension);
bool mcp_download_name(const char *name, int64_t *expires);
void mcp_download_prune(const char *directory, int64_t now);
asciichat_error_t mcp_download_finalize(const mcp_policy_t *policy, yyjson_mut_doc *doc, int64_t now);
