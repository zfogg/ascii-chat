#include "mcp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctoon.h>

static int protocol_fd = -1;
asciichat_error_t mcp_stdio_reserve(void) {
  protocol_fd = platform_dup(STDOUT_FILENO);
  if (protocol_fd < 0)
    return SET_ERRNO(ERROR_FILE_OPERATION, "Cannot reserve MCP stdout");
  return platform_dup2(STDERR_FILENO, STDOUT_FILENO);
}
void mcp_stdio_release(void) {
  if (protocol_fd >= 0) {
    platform_close(protocol_fd);
    protocol_fd = -1;
  }
}
char *mcp_response_encode_toon(yyjson_mut_doc *response, size_t *length) {
  size_t json_length = 0;
  char *json = yyjson_mut_write(response, 0, &json_length);
  if (!json)
    return NULL;
  ctoon_doc *doc = ctoon_read_json_const(json, json_length, CTOON_READ_NOFLAG);
  free(json);
  if (!doc)
    return NULL;
  char *toon = ctoon_write(doc, length);
  ctoon_doc_free(doc);
  return toon;
}

void mcp_stdio_send(mcp_server_t *s, yyjson_mut_doc *response) {
  size_t size = 0;
  char *toon = mcp_response_encode_toon(response, &size);
  if (!toon)
    return;
  mutex_lock(&s->output_mutex);
  bool ok = platform_write_all(protocol_fd, toon, size) == size && platform_write_all(protocol_fd, "\n", 1) == 1;
  mutex_unlock(&s->output_mutex);
  free(toon);
  if (!ok)
    atomic_store_bool(&s->stopping, true);
}
asciichat_error_t mcp_stdio_run(mcp_server_t *s) {
  char *line = SAFE_MALLOC(MCP_MAX_INPUT + 1, char *);
  size_t n = 0;
  bool overflow = false;
  while (!atomic_load_bool(&s->stopping)) {
    int c = fgetc(stdin);
    if (c == EOF)
      break;
    if (c != '\n') {
      if (n < MCP_MAX_INPUT)
        line[n++] = (char)c;
      else
        overflow = true;
      continue;
    }
    if (n && line[n - 1] == '\r')
      --n;
    if (!n && !overflow)
      continue;
    yyjson_doc *doc = overflow ? NULL : yyjson_read(line, n, YYJSON_READ_NOFLAG);
    bool pending = false;
    yyjson_mut_doc *response = doc ? mcp_dispatch(s, doc, NULL, NULL, &pending) : mcp_error(NULL, overflow ? -32600 : -32700, overflow ? "Request exceeds 24 MiB" : "Parse error");
    if (response) {
      mcp_stdio_send(s, response);
      yyjson_mut_doc_free(response);
    }
    if (doc)
      yyjson_doc_free(doc);
    n = 0;
    overflow = false;
    mcp_jobs_reap(s, false);
  }
  SAFE_FREE(line);
  for (unsigned i = 0; i < s->policy.max_jobs; ++i)
    if (s->jobs[i].renderer_job.started)
      http_renderer_job_cancel(&s->jobs[i].renderer_job);
  mcp_jobs_reap(s, true);
  return ASCIICHAT_OK;
}
