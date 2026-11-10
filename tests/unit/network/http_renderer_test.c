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

Test(http_renderer, result_collects_bounded_text_frames) {
  http_render_result_t result = {0};
  cr_assert_eq(http_render_result_add_text_frame(&result, "first", 5, 0), ASCIICHAT_OK);
  cr_assert_eq(http_render_result_add_text_frame(&result, "second", 6, 40), ASCIICHAT_OK);
  cr_assert_eq(result.frame_count, 2);
  cr_assert_str_eq(result.frames[1].text, "second");
  cr_assert_eq(result.frames[1].timestamp_ms, 40);
  http_render_result_destroy(&result);
  cr_assert_null(result.frames);
}

Test(http_renderer, result_rejects_excess_frames) {
  http_render_result_t result = {0};
  for (size_t i = 0; i < HTTP_RENDERER_MAX_FRAMES; ++i)
    cr_assert_eq(http_render_result_add_file_frame(&result, "frame.png", i), ASCIICHAT_OK);
  cr_assert_eq(http_render_result_add_file_frame(&result, "overflow.png", 0), ERROR_BUFFER_OVERFLOW);
  http_render_result_destroy(&result);
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
