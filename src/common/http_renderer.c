#include "http_renderer.h"

#include <string.h>

void http_render_request_init(http_render_request_t *request) {
  if (!request)
    return;
  memset(request, 0, sizeof(*request));
  request->source.type = HTTP_RENDER_SOURCE_UPLOAD;
  request->output_type = HTTP_RENDER_OUTPUT_TEXT;
}

void http_render_result_destroy(http_render_result_t *result) {
  if (!result)
    return;
  SAFE_FREE(result->ascii);
  result->ascii_length = 0;
  result->output_path[0] = '\0';
  result->mime_type[0] = '\0';
}
