#include <catch2/catch_test_macros.hpp>
#include <common/http_renderer.h>

TEST_CASE("http_renderer/request_initializes_safe_defaults", "[http_renderer] [unit]") {
  http_render_request_t request;
  http_render_request_init(&request);
  REQUIRE((request.source.type) == (HTTP_RENDER_SOURCE_UPLOAD));
  REQUIRE((request.output_type) == (HTTP_RENDER_OUTPUT_TEXT));
  REQUIRE((request.source.body_length) == (0));
  REQUIRE(!(request.restricted));
}

TEST_CASE("http_renderer/result_destroy_is_idempotent", "[http_renderer] [unit]") {
  http_render_result_t result = {0};
  result.ascii = SAFE_MALLOC(4, char *);
  SAFE_STRNCPY(result.ascii, "abc", 4);
  result.ascii_length = 3;
  http_render_result_destroy(&result);
  http_render_result_destroy(&result);
  REQUIRE((result.ascii) == nullptr);
  REQUIRE((result.ascii_length) == (0));
}

TEST_CASE("http_renderer/result_collects_bounded_text_frames", "[http_renderer] [unit]") {
  http_render_result_t result = {0};
  REQUIRE((http_render_result_add_text_frame(&result, "first", 5, 0)) == (ASCIICHAT_OK));
  REQUIRE((http_render_result_add_text_frame(&result, "second", 6, 40)) == (ASCIICHAT_OK));
  REQUIRE((result.frame_count) == (2));
  REQUIRE(std::strcmp((result.frames[1].text), ("second")) == 0);
  REQUIRE((result.frames[1].timestamp_ms) == (40));
  http_render_result_destroy(&result);
  REQUIRE((result.frames) == nullptr);
}

TEST_CASE("http_renderer/result_rejects_excess_frames", "[http_renderer] [unit]") {
  http_render_result_t result = {0};
  for (size_t i = 0; i < HTTP_RENDERER_MAX_FRAMES; ++i)
    REQUIRE((http_render_result_add_file_frame(&result, "frame.png", i)) == (ASCIICHAT_OK));
  REQUIRE((http_render_result_add_file_frame(&result, "overflow.png", 0)) == (ERROR_BUFFER_OVERFLOW));
  http_render_result_destroy(&result);
}

TEST_CASE("http_renderer/service_reaps_failed_normalized_jobs", "[http_renderer] [unit]") {
  http_renderer_t *renderer = NULL;
  http_renderer_config_t config = {.max_jobs = 1, .max_input_bytes = 1024, .max_output_bytes = 1024};
  REQUIRE((http_renderer_init(&renderer, &config)) == (ASCIICHAT_OK));
  http_render_request_t request;
  http_render_request_init(&request);
  request.source.type = HTTP_RENDER_SOURCE_FILE;
  http_renderer_job_t *job = NULL;
  REQUIRE((http_renderer_submit(renderer, &request, NULL, &job)) == (ASCIICHAT_OK));
  http_renderer_job_wait(job);
  REQUIRE((job->result.error) != (ASCIICHAT_OK));
  http_renderer_reap(renderer, false);
  http_renderer_destroy(&renderer);
  REQUIRE((renderer) == nullptr);
}
