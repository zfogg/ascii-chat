#include "mcp.h"
#include <ascii-chat/platform/filesystem.h>
#include <ascii-chat/util/time.h>
#include <uv.h>
#include <signal.h>
#include <string.h>

// libuv supplies portable process and pipe I/O; each renderer owns its event loop.
typedef struct {
  uv_loop_t loop;
  uv_process_t process;
  uv_pipe_t output, errors;
  uv_timer_t timer;
  atomic_t *cancelled;
  mcp_render_result_t *result;
  uint64_t started, exit_time;
  unsigned timeout;
  bool exited, failed;
} render_worker_t;
static void stop_worker(render_worker_t *w, const char *reason) {
  if (w->failed)
    return;
  w->failed = true;
  SAFE_STRNCPY(w->result->error, reason, sizeof(w->result->error));
  if (!w->exited)
    uv_process_kill(&w->process, SIGKILL);
}
static void allocate(uv_handle_t *handle, size_t suggested, uv_buf_t *buf) {
  (void)handle;
  (void)suggested;
  buf->len = 16384;
  buf->base = SAFE_MALLOC(buf->len, char *);
}
static void read_output(uv_stream_t *stream, ssize_t count, const uv_buf_t *buf) {
  render_worker_t *w = stream->data;
  if (count > 0) {
    if (stream == (uv_stream_t *)&w->output) {
      size_t n = (size_t)count;
      if (w->result->length + n > MCP_MAX_OUTPUT)
        stop_worker(w, "Rendered text exceeds 8 MiB");
      else {
        memcpy(w->result->text + w->result->length, buf->base, n);
        w->result->length += n;
        w->result->text[w->result->length] = '\0';
      }
    } else if (!w->failed) {
      // Keep the tail, which normally contains the most useful decoder error.
      size_t old = strlen(w->result->error), n = (size_t)count, cap = sizeof(w->result->error) - 1;
      if (n >= cap) {
        memcpy(w->result->error, buf->base + n - cap, cap);
        w->result->error[cap] = '\0';
      } else {
        if (old + n > cap) {
          size_t drop = old + n - cap;
          memmove(w->result->error, w->result->error + drop, old - drop);
          old -= drop;
        }
        memcpy(w->result->error + old, buf->base, n);
        w->result->error[old + n] = '\0';
      }
    }
  }
  char *allocation = buf->base;
  SAFE_FREE(allocation);
  if (count < 0 && !uv_is_closing((uv_handle_t *)stream))
    uv_close((uv_handle_t *)stream, NULL);
}
static void exited(uv_process_t *process, int64_t status, int signal) {
  render_worker_t *w = process->data;
  w->exited = true;
  w->exit_time = time_get_ns();
  if (!w->failed && (status || signal)) {
    w->failed = true;
    if (!w->result->error[0])
      SAFE_SNPRINTF(w->result->error, sizeof(w->result->error), "Renderer failed (exit %lld, signal %d)",
                    (long long)status, signal);
  }
  uv_close((uv_handle_t *)process, NULL);
}
static void tick(uv_timer_t *timer) {
  render_worker_t *w = timer->data;
  if (atomic_load_bool(w->cancelled))
    stop_worker(w, "Rendering cancelled");
  if (time_get_ns() - w->started > (uint64_t)w->timeout * 1000000)
    stop_worker(w, "Rendering exceeded timeout_ms");
  if (w->exited) {
    bool drained = uv_is_closing((uv_handle_t *)&w->output) && uv_is_closing((uv_handle_t *)&w->errors);
    // Allow pending pipe reads to drain after process exit, but bound inherited-descriptor stalls.
    if (drained || time_get_ns() - w->exit_time > 1000000000) {
      if (!drained)
        stop_worker(w, "Renderer output pipes did not close");
      uv_close((uv_handle_t *)&w->timer, NULL);
      if (!uv_is_closing((uv_handle_t *)&w->output))
        uv_close((uv_handle_t *)&w->output, NULL);
      if (!uv_is_closing((uv_handle_t *)&w->errors))
        uv_close((uv_handle_t *)&w->errors, NULL);
    }
  }
}
void mcp_render_execute(mcp_render_request_t *r, atomic_t *cancelled, mcp_render_result_t *result) {
  memset(result, 0, sizeof(*result));
  render_worker_t w = {.cancelled = cancelled, .result = result, .started = time_get_ns(), .timeout = r->timeout_ms};
  result->text = SAFE_CALLOC(MCP_MAX_OUTPUT + 1, 1, char *);
  if (uv_loop_init(&w.loop)) {
    SAFE_STRNCPY(result->error, "Cannot initialize renderer event loop", sizeof(result->error));
    return;
  }
  uv_pipe_init(&w.loop, &w.output, 0);
  uv_pipe_init(&w.loop, &w.errors, 0);
  uv_timer_init(&w.loop, &w.timer);
  w.output.data = w.errors.data = w.timer.data = w.process.data = &w;
  uv_stdio_container_t streams[3] = {0};
  streams[0].flags = UV_IGNORE;
  streams[1].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  streams[1].data.stream = (uv_stream_t *)&w.output;
  streams[2].flags = UV_CREATE_PIPE | UV_WRITABLE_PIPE;
  streams[2].data.stream = (uv_stream_t *)&w.errors;
  // Only runtime essentials are inherited; user config, ASCII_CHAT_* and credentials are excluded.
  const char *keys[] = {
      "PATH", "SystemRoot", "WINDIR", "TEMP", "TMP", "TMPDIR", "LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "TESTING", NULL};
  char *env[16] = {0};
  size_t ne = 0;
  for (size_t i = 0; keys[i]; ++i) {
    const char *v = platform_getenv(keys[i]);
    if (v) {
      size_t n = strlen(keys[i]) + strlen(v) + 2;
      env[ne] = SAFE_MALLOC(n, char *);
      SAFE_SNPRINTF(env[ne], n, "%s=%s", keys[i], v);
      ++ne;
    }
  }
  env[ne++] = platform_strdup("ASCII_CHAT_MCP_WORKER=1");
  env[ne++] = platform_strdup("ASCII_CHAT_OTEL_ENABLED_TELEMETRY=none");
  if (r->capture_webcam)
    env[ne++] = platform_strdup("ASCII_CHAT_MCP_WEBCAM=1");
  uv_process_options_t opts = {
      .exit_cb = exited, .file = r->argv[0], .args = r->argv, .env = env, .stdio_count = 3, .stdio = streams};
  int rc = uv_spawn(&w.loop, &w.process, &opts);
  for (size_t i = 0; i < ne; ++i)
    SAFE_FREE(env[i]);
  if (rc) {
    SAFE_SNPRINTF(result->error, sizeof(result->error), "Cannot start renderer: %s", uv_strerror(rc));
    uv_close((uv_handle_t *)&w.output, NULL);
    uv_close((uv_handle_t *)&w.errors, NULL);
    uv_close((uv_handle_t *)&w.timer, NULL);
  } else {
    uv_read_start((uv_stream_t *)&w.output, allocate, read_output);
    uv_read_start((uv_stream_t *)&w.errors, allocate, read_output);
    uv_timer_start(&w.timer, tick, 20, 20);
  }
  uv_run(&w.loop, UV_RUN_DEFAULT);
  uv_loop_close(&w.loop);
  result->elapsed_ms = (time_get_ns() - w.started) / 1000000;
  if (rc || w.failed)
    return;
  result->error[0] = '\0';
  if (!result->length && !r->stage_file[0]) {
    SAFE_STRNCPY(result->error, "Renderer returned no ASCII art", sizeof(result->error));
    return;
  }
  if (r->stage_file[0]) {
    uv_fs_t fs;
    int e = uv_fs_stat(NULL, &fs, r->stage_file, NULL);
    bool valid = e == 0 && fs.statbuf.st_size > 0;
    uv_fs_req_cleanup(&fs);
    if (!valid) {
      SAFE_STRNCPY(result->error, "Renderer did not create an output file", sizeof(result->error));
      return;
    }
    // Hard-link publication is atomic and fails if any file/symlink already owns the destination.
    // Staging beside the destination keeps this on the same filesystem.
    e = uv_fs_link(NULL, &fs, r->stage_file, r->output_path, NULL);
    uv_fs_req_cleanup(&fs);
    if (e) {
      SAFE_SNPRINTF(result->error, sizeof(result->error), "Cannot save without replacing an existing file: %s",
                    uv_strerror(e));
      return;
    }
    SAFE_STRNCPY(result->output_path, r->output_path, sizeof(result->output_path));
  }
}
