#include "mcp.h"
#include <ascii-chat/stats/runtime.h>
#include <ascii-chat/util/time.h>
#include <ascii-chat/version.h>
#include <libwebsockets.h>
#include <string.h>

static yyjson_mut_doc *envelope(yyjson_val *id) {
  yyjson_mut_doc *d = yyjson_mut_doc_new(NULL);
  yyjson_mut_val *root = yyjson_mut_obj(d);
  yyjson_mut_doc_set_root(d, root);
  yyjson_mut_obj_add_str(d, root, "jsonrpc", "2.0");
  yyjson_mut_obj_add_val(d, root, "id", id ? yyjson_val_mut_copy(d, id) : yyjson_mut_null(d));
  return d;
}
yyjson_mut_doc *mcp_error(yyjson_val *id, int code, const char *message) {
  ASCII_OTEL_SCOPE(MCP_PROTOCOL_ERROR);
  ASCII_OTEL_ATTR_INT("rpc.jsonrpc.error_code", code);
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_REJECTED, 1);
#ifdef ASCIICHAT_WITH_OTEL
  ascii_otel_error("MCP protocol request rejected");
#endif
  yyjson_mut_doc *d = envelope(id);
  yyjson_mut_val *e = yyjson_mut_obj(d);
  yyjson_mut_obj_add_int(d, e, "code", code);
  yyjson_mut_obj_add_strcpy(d, e, "message", message);
  yyjson_mut_obj_add_val(d, yyjson_mut_doc_get_root(d), "error", e);
  return d;
}
static yyjson_mut_doc *tool_result(yyjson_val *id, const mcp_render_result_t *r) {
  yyjson_mut_doc *d = envelope(id);
  yyjson_mut_val *root = yyjson_mut_doc_get_root(d), *result = yyjson_mut_obj(d), *content = yyjson_mut_arr(d),
                 *item = yyjson_mut_obj(d);
  yyjson_mut_obj_add_str(d, item, "type", "text");
  const char *text = r->error[0] ? r->error : r->text && r->text[0] ? r->text : r->output_path;
  yyjson_mut_obj_add_strcpy(d, item, "text", text ? text : "");
  yyjson_mut_arr_add_val(content, item);
  yyjson_mut_obj_add_val(d, result, "content", content);
  yyjson_mut_obj_add_bool(d, result, "isError", r->error[0] != 0);
  if (!r->error[0]) {
    yyjson_mut_val *data = yyjson_mut_obj(d), *files = yyjson_mut_arr(d);
    yyjson_mut_obj_add_strcpy(d, data, "ascii", r->text ? r->text : "");
    yyjson_mut_obj_add_uint(d, data, "elapsed_ms", r->elapsed_ms);
    if (r->output_path[0])
      yyjson_mut_arr_add_strcpy(d, files, r->output_path);
    yyjson_mut_obj_add_val(d, data, "saved_files", files);
    yyjson_mut_obj_add_val(d, result, "structuredContent", data);
  }
  yyjson_mut_obj_add_val(d, root, "result", result);
  return d;
}
static void *run_job(void *arg) {
  mcp_job_t *job = arg;
  ASCII_OTEL_SCOPE_PARENT(MCP_RENDER, &job->telemetry_parent);
  static const char *const tools[] = {"render_file", "render_url", "render_webcam", "render_visualization"};
  ASCII_OTEL_ATTR_STRING("mcp.tool", tools[job->tool]);
  ASCII_OTEL_ATTR_STRING("mcp.transport", job->server->policy.hosted ? "http" : "stdio");
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_RENDERS, 1);
  mcp_render_result_t result;
  uint64_t render_started = time_get_ns();
  log_info("MCP render started");
  mcp_render_execute(&job->request, &job->cancelled, &result);
  stats_duration_record(stats_runtime_scope(), STATS_DURATION_MCP_RENDER, time_get_ns() - render_started);
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_OUTPUT_BYTES, result.length);
  ASCII_OTEL_ATTR_INT("mcp.output.bytes", (int64_t)result.length);
  if (result.error[0]) {
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_RENDER_FAILURES, 1);
    const char *failure = result.timed_out ? "timeout" : atomic_load_bool(&job->cancelled) ? "cancelled" : "render";
    ASCII_OTEL_ATTR_STRING("error.type", failure);
    if (result.timed_out)
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_TIMEOUTS, 1);
    else if (atomic_load_bool(&job->cancelled))
      stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_CANCELLED, 1);
    SET_ERRNO(ERROR_FILE_OPERATION, "MCP render failed: %s", result.error);
    log_error("MCP render failed: %s", result.error);
  } else
    log_info("MCP render completed in %llu ms", (unsigned long long)result.elapsed_ms);
  if (!result.error[0] && result.output_path[0])
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_SAVED_FILES, 1);
  job->response = tool_result(mcp_get(yyjson_doc_get_root(job->message), "id"), &result);
  SAFE_FREE(result.text);
  mcp_request_destroy(&job->request);
  if (!job->server->policy.hosted) {
    mcp_stdio_send(job->server, job->response);
    yyjson_mut_doc_free(job->response);
    job->response = NULL;
  }
  atomic_store_bool(&job->done, true);
  if (job->server->policy.hosted)
    mcp_http_wake(job->server);
  return NULL;
}
void mcp_jobs_reap(mcp_server_t *s, bool all) {
  for (int i = 0; i < MCP_MAX_JOBS; ++i) {
    mcp_job_t *j = &s->jobs[i];
    if (!j->started || (!all && (!atomic_load_bool(&j->done) || (s->policy.hosted && j->connection))))
      continue;
    thread_join(&j->thread, NULL);
    yyjson_doc_free(j->message);
    if (j->response)
      yyjson_mut_doc_free(j->response);
    memset(j, 0, sizeof(*j));
  }
}
static bool same_id(yyjson_val *a, yyjson_val *b) {
  return a && b &&
         ((yyjson_is_str(a) && yyjson_is_str(b) && yyjson_get_len(a) == yyjson_get_len(b) &&
           !strcmp(yyjson_get_str(a), yyjson_get_str(b))) ||
          (yyjson_is_int(a) && yyjson_is_int(b) && yyjson_get_sint(a) == yyjson_get_sint(b)));
}
yyjson_mut_doc *mcp_dispatch(mcp_server_t *s, yyjson_doc *message, void *connection, bool *pending) {
  ASCII_OTEL_SCOPE(MCP_REQUEST);
  ASCII_OTEL_ATTR_STRING("rpc.system", "jsonrpc");
  ASCII_OTEL_ATTR_STRING("mcp.transport", s->policy.hosted ? "http" : "stdio");
  stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_REQUESTS, 1);
  *pending = false;
  yyjson_val *root = yyjson_doc_get_root(message), *id = mcp_get(root, "id"), *method = mcp_get(root, "method"),
             *version = mcp_get(root, "jsonrpc");
  if (!yyjson_is_obj(root) || !yyjson_is_str(version) || strcmp(yyjson_get_str(version), "2.0") ||
      !yyjson_is_str(method) || strlen(yyjson_get_str(method)) != yyjson_get_len(method) ||
      (id && !yyjson_is_str(id) && !yyjson_is_int(id)))
    return mcp_error(id, -32600, "Invalid JSON-RPC request");
  const char *name = yyjson_get_str(method);
  static const char *const methods[] = {
      "initialize", "ping", "tools/list", "tools/call", "notifications/initialized", "notifications/cancelled"};
  const char *method_name = "unknown";
  for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++)
    if (!strcmp(name, methods[i]))
      method_name = methods[i];
  ASCII_OTEL_ATTR_STRING("rpc.method", method_name);
  yyjson_val *params = mcp_get(root, "params");
  if (params && !yyjson_is_obj(params))
    return id ? mcp_error(id, -32602, "params must be an object") : NULL;
  if (!id) {
    if (!strcmp(name, "notifications/initialized"))
      s->initialized = true;
    if (!strcmp(name, "notifications/cancelled")) {
      yyjson_val *cancel = mcp_get(params, "requestId");
      for (int i = 0; i < MCP_MAX_JOBS; ++i)
        if (s->jobs[i].started && (!s->policy.hosted || s->jobs[i].connection == connection) &&
            same_id(cancel, mcp_get(yyjson_doc_get_root(s->jobs[i].message), "id")))
          atomic_store_bool(&s->jobs[i].cancelled, true);
    }
    return NULL;
  }
  yyjson_mut_doc *d = envelope(id);
  yyjson_mut_val *result = yyjson_mut_obj(d);
  yyjson_mut_obj_add_val(d, yyjson_mut_doc_get_root(d), "result", result);
  if (!strcmp(name, "initialize")) {
    yyjson_val *requested = mcp_get(params, "protocolVersion");
    const char *v = yyjson_get_str(requested);
    if (!v || (strcmp(v, "2025-11-25") && strcmp(v, "2025-06-18") && strcmp(v, "2025-03-26")))
      v = "2025-11-25";
    yyjson_mut_obj_add_strcpy(d, result, "protocolVersion", v);
    yyjson_mut_val *caps = yyjson_mut_obj(d), *info = yyjson_mut_obj(d);
    yyjson_mut_obj_add_val(d, caps, "tools", yyjson_mut_obj(d));
    yyjson_mut_obj_add_val(d, result, "capabilities", caps);
    yyjson_mut_obj_add_str(d, info, "name", "ascii-chat");
    yyjson_mut_obj_add_str(d, info, "version", ASCII_CHAT_VERSION_FULL);
    yyjson_mut_obj_add_val(d, result, "serverInfo", info);
    yyjson_mut_obj_add_str(
        d, result, "instructions",
        "Render media as ASCII art. Uploaded bytes must be base64 encoded by the client. snapshot-delay measures "
        "capture duration after the first rendered frame; use seek for media timestamps. output_path saves ASCII art "
        "as an image/video and never replaces an existing file.");
    return d;
  }
  if (!strcmp(name, "ping"))
    return d;
  if (!strcmp(name, "tools/list")) {
    yyjson_mut_obj_add_val(d, result, "tools", mcp_tools_schema(d, &s->policy));
    return d;
  }
  yyjson_mut_doc_free(d);
  if (strcmp(name, "tools/call"))
    return mcp_error(id, -32601, "Method not found");
  const char *tool = yyjson_get_str(mcp_get(params, "name"));
  const char *names[] = {"render_file", "render_url", "render_webcam", "render_visualization"};
  int kind = -1;
  for (int i = 0; i < 4; ++i)
    if (tool && strlen(tool) == yyjson_get_len(mcp_get(params, "name")) && !strcmp(tool, names[i]))
      kind = i;
  if (kind < 0 || (s->policy.hosted && kind == MCP_WEBCAM))
    return mcp_error(id, -32602, "Unknown tool");
  mcp_jobs_reap(s, false);
  mcp_job_t *j = NULL;
  for (unsigned i = 0; i < s->policy.max_jobs; ++i)
    if (!s->jobs[i].started) {
      j = &s->jobs[i];
      break;
    }
  if (!j)
    return mcp_error(id, -32000, "Server busy; at most four renders may run concurrently");
  yyjson_val *arguments = mcp_get(params, "arguments");
  yyjson_doc *empty = NULL;
  if (!arguments) {
    empty = yyjson_read("{}", 2, 0);
    arguments = yyjson_doc_get_root(empty);
  }
  asciichat_error_t err = mcp_request_parse(&s->policy, (mcp_tool_t)kind, arguments, &j->request);
  if (empty)
    yyjson_doc_free(empty);
  if (err != ASCIICHAT_OK) {
    stats_counter_add(stats_runtime_scope(), STATS_COUNTER_MCP_REJECTED, 1);
    ASCII_OTEL_ATTR_STRING("error.type", "invalid_arguments");
    asciichat_error_context_t ctx;
    mcp_render_result_t fail = {0};
    SAFE_STRNCPY(fail.error, HAS_ERRNO(&ctx) ? ctx.context_message : "Invalid render request", sizeof(fail.error));
    mcp_request_destroy(&j->request);
    return tool_result(id, &fail);
  }
  // Copy before returning to the transport, which owns and frees the input document.
  size_t len;
  char *copy = yyjson_write(message, 0, &len);
  j->message = yyjson_read(copy, len, 0);
  free(copy);
  j->tool = (mcp_tool_t)kind;
#ifdef ASCIICHAT_WITH_OTEL
  j->telemetry_parent = ascii_otel_context();
#endif
  j->server = s;
  j->connection = connection;
  j->started = true;
  if (acthread_create(&j->thread, "mcp-render", run_job, j)) {
    mcp_request_destroy(&j->request);
    yyjson_doc_free(j->message);
    memset(j, 0, sizeof(*j));
    return mcp_error(id, -32603, "Cannot create render worker");
  }
  *pending = true;
  return NULL;
}
