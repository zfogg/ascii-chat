#include <criterion/criterion.h>
#include "http_renderer.h"

Test(http_renderer, request_initializes_safe_defaults) {
  http_render_request_t request;
  http_render_request_init(&request);
  cr_assert_eq(request.source.type, HTTP_RENDER_SOURCE_UPLOAD);
  cr_assert_eq(request.output_type, HTTP_RENDER_OUTPUT_TEXT);
  cr_assert_eq(request.source.body_length, 0);
  cr_assert_not(request.restricted);
}

Test(http_renderer, result_destroy_is_idempotent) {
  http_render_result_t result = {0};
  result.ascii = SAFE_MALLOC(4, char *);
  SAFE_STRNCPY(result.ascii, "abc", 4);
  result.ascii_length = 3;
  http_render_result_destroy(&result);
  http_render_result_destroy(&result);
  cr_assert_null(result.ascii);
  cr_assert_eq(result.ascii_length, 0);
}

Test(http_renderer, service_reaps_failed_normalized_jobs) {
  http_renderer_t *renderer = NULL;
  http_renderer_config_t config = {.max_jobs = 1, .max_input_bytes = 1024, .max_output_bytes = 1024};
  cr_assert_eq(http_renderer_init(&renderer, &config), ASCIICHAT_OK);
  http_render_request_t request;
  http_render_request_init(&request);
  request.source.type = HTTP_RENDER_SOURCE_FILE;
  http_renderer_job_t *job = NULL;
  cr_assert_eq(http_renderer_submit(renderer, &request, &job), ASCIICHAT_OK);
  http_renderer_job_wait(job);
  cr_assert_neq(job->result.error, ASCIICHAT_OK);
  http_renderer_reap(renderer, false);
  http_renderer_destroy(&renderer);
  cr_assert_null(renderer);
}
