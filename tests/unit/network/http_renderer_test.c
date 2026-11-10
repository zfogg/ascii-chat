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
