#include <criterion/criterion.h>
#include <ascii-chat/util/env.h>
#include <ascii-chat/platform/abstraction.h>
#include <ascii-chat/log/log.h>
#include <string.h>

static void setup(void) {
  log_init(NULL, LOG_ERROR, false, false);
  platform_unsetenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
}
static void cleanup(void) {
  platform_unsetenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE");
  log_destroy();
}
TestSuite(prompt_stack, .init = setup, .fini = cleanup);

Test(prompt_stack, escaped_delimiters_and_backslashes_survive_fifo_consumption) {
  cr_assert_eq(platform_setenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", "pass\\;word;path\\\\file;yes;"), 0);
  cr_assert(env_validate_prompt_response_format());
  char response[256];
  const char *expected[] = {"pass;word", "path\\file", "yes"};
  for (size_t i = 0; i < 3; ++i) {
    cr_assert(env_has_prompt_response());
    cr_assert(env_pop_prompt_response(response, sizeof(response)));
    cr_assert_str_eq(response, expected[i]);
  }
  cr_assert_not(env_has_prompt_response());
  cr_assert_not(env_pop_prompt_response(response, sizeof(response)));
  cr_assert(env_validate_prompt_response_format());
}

Test(prompt_stack, short_buffers_leave_the_response_available_for_retry) {
  char response[256] = "unchanged";
  const char *values[] = {"long-response", "long-response;next"};
  for (size_t i = 0; i < 2; ++i) {
    cr_assert_eq(platform_setenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", values[i]), 0);
    cr_assert_not(env_pop_prompt_response(NULL, sizeof(response)));
    cr_assert_not(env_pop_prompt_response(response, 1));
    cr_assert_not(env_pop_prompt_response(response, 4));
    cr_assert_str_eq(response, "unchanged");
    cr_assert_str_eq(SAFE_GETENV("ASCII_CHAT_QUESTION_PROMPT_RESPONSE"), values[i]);
    cr_assert(env_pop_prompt_response(response, sizeof(response)));
    cr_assert_str_eq(response, "long-response");
    strcpy(response, "unchanged");
  }
}

Test(prompt_stack, format_validation_rejects_empty_segments_and_accepts_literal_escapes) {
  const char *invalid[] = {";", ";yes", "yes;;no", ";;"};
  for (size_t i = 0; i < 4; ++i) {
    cr_assert_eq(platform_setenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", invalid[i]), 0);
    cr_assert_not(env_validate_prompt_response_format(), "%s", invalid[i]);
  }
  char response[32];
  cr_assert_not(env_has_prompt_response());
  cr_assert_not(env_pop_prompt_response(response, sizeof(response)));
  const char *valid[] = {"", "yes", "yes;no", "yes;", "\\;;no", "path\\\\file", "trailing\\"};
  for (size_t i = 0; i < 7; ++i) {
    cr_assert_eq(platform_setenv("ASCII_CHAT_QUESTION_PROMPT_RESPONSE", valid[i]), 0);
    cr_assert(env_validate_prompt_response_format(), "%s", valid[i]);
  }
  cr_assert(env_pop_prompt_response(response, sizeof(response)));
  cr_assert_str_eq(response, "trailing\\");
}
