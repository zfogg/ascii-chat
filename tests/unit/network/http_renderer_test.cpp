#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <common/http_renderer.h>

TEST_CASE("http_renderer/request_initializes_safe_defaults", "[http_renderer]") {
  http_render_request_t request;
  http_render_request_init(&request);
  ASCIICHAT_REQUIRE_EQ(request.source.type, HTTP_RENDER_SOURCE_UPLOAD);
  ASCIICHAT_REQUIRE_EQ(request.output_type, HTTP_RENDER_OUTPUT_TEXT);
  ASCIICHAT_REQUIRE_EQ(request.source.body_length, 0);
  ASCIICHAT_REQUIRE_NOT(request.restricted);
}

TEST_CASE("http_renderer/result_destroy_is_idempotent", "[http_renderer]") {
  http_render_result_t result = {0};
  result.ascii = SAFE_MALLOC(4, char *);
  SAFE_STRNCPY(result.ascii, "abc", 4);
  result.ascii_length = 3;
  http_render_result_destroy(&result);
  http_render_result_destroy(&result);
  ASCIICHAT_REQUIRE_NULL(result.ascii);
  ASCIICHAT_REQUIRE_EQ(result.ascii_length, 0);
}

TEST_CASE("http_renderer/result_collects_bounded_text_frames", "[http_renderer]") {
  http_render_result_t result = {0};
  ASCIICHAT_REQUIRE_EQ(http_render_result_add_text_frame(&result, "first", 5, 0), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(http_render_result_add_text_frame(&result, "second", 6, 40), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(result.frame_count, 2);
  ASCIICHAT_REQUIRE_STR_EQ(result.frames[1].text, "second");
  ASCIICHAT_REQUIRE_EQ(result.frames[1].timestamp_ms, 40);
  http_render_result_destroy(&result);
  ASCIICHAT_REQUIRE_NULL(result.frames);
}

TEST_CASE("http_renderer/result_rejects_excess_frames", "[http_renderer]") {
  http_render_result_t result = {0};
  for (size_t i = 0; i < HTTP_RENDERER_MAX_FRAMES; ++i)
    ASCIICHAT_REQUIRE_EQ(http_render_result_add_file_frame(&result, "frame.png", i), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(http_render_result_add_file_frame(&result, "overflow.png", 0), ERROR_BUFFER_OVERFLOW);
  http_render_result_destroy(&result);
}

TEST_CASE("http_renderer/service_reaps_failed_normalized_jobs", "[http_renderer]") {
  http_renderer_t *renderer = NULL;
  http_renderer_config_t config = {.max_jobs = 1, .max_input_bytes = 1024, .max_output_bytes = 1024};
  ASCIICHAT_REQUIRE_EQ(http_renderer_init(&renderer, &config), ASCIICHAT_OK);
  http_render_request_t request;
  http_render_request_init(&request);
  request.source.type = HTTP_RENDER_SOURCE_FILE;
  http_renderer_job_t *job = NULL;
  ASCIICHAT_REQUIRE_EQ(http_renderer_submit(renderer, &request, NULL, &job), ASCIICHAT_OK);
  http_renderer_job_wait(job);
  ASCIICHAT_REQUIRE_NE(job->result.error, ASCIICHAT_OK);
  http_renderer_reap(renderer, false);
  http_renderer_destroy(&renderer);
  ASCIICHAT_REQUIRE_NULL(renderer);
}
