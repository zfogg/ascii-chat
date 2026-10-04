#include <criterion/criterion.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/options/options.h>

/* Simulate systems where executable JIT memory is unavailable. */
int pcre2_jit_compile(pcre2_code *code, uint32_t options) {
  (void)code;
  (void)options;
  return PCRE2_ERROR_JIT_BADOPTION;
}

static void setup(void) {
#ifndef __APPLE__
  pcre2_singleton_t *singleton = asciichat_pcre2_singleton_compile("^fallback-probe$", 0);
  cr_assert_not_null(singleton);
  pcre2_code *code = asciichat_pcre2_singleton_get_code(singleton);
  cr_assert_not_null(code);
  size_t jit_size = 1;
  cr_assert_eq(pcre2_pattern_info(code, PCRE2_INFO_JITSIZE, &jit_size), 0);
  cr_assert_eq(jit_size, 0, "The fixture must exercise interpreted regex matching");
#endif
}

TestSuite(regex_fallback, .init = setup);

Test(regex_fallback, valid_and_invalid_ports_without_jit) {
  uint16_t port = 0;
  cr_assert_eq(parse_port("47097", &port), ASCIICHAT_OK);
  cr_assert_eq(port, 47097);
  cr_assert_eq(parse_port("65535", &port), ASCIICHAT_OK);
  cr_assert_eq(port, 65535);
  cr_assert_eq(parse_port("0", &port), ASCIICHAT_OK);
  cr_assert_eq(port, 0);
  cr_assert_neq(parse_port("65536", &port), ASCIICHAT_OK);
  cr_assert_neq(parse_port("invalid", &port), ASCIICHAT_OK);
  asciichat_clear_errno();
}

Test(regex_fallback, urls_validate_and_parse_without_jit) {
  cr_assert(url_is_valid("https://example.com:443/video.mp4"));
  cr_assert_not(url_is_valid("invalid URL"));
  url_parts_t parts = {0};
  cr_assert_eq(url_parse("https://example.com:443/video.mp4", &parts), ASCIICHAT_OK);
  cr_assert_str_eq(parts.host, "example.com");
  cr_assert_eq(parts.port, 443);
  cr_assert_str_eq(parts.path, "/video.mp4");
  url_parts_destroy(&parts);
}

Test(regex_fallback, paths_normalize_without_jit) {
  char normalized[256];
  cr_assert(path_normalize_copy("/media//./video.mp4", normalized, sizeof(normalized)));
  cr_assert_str_eq(normalized, "/media/video.mp4");
}

Test(regex_fallback, settings_parse_without_jit) {
  int setting = 0;
  cr_assert(parse_color_setting("false", &setting, NULL));
  cr_assert_eq(setting, COLOR_SETTING_FALSE);
  cr_assert(parse_color_setting("true", &setting, NULL));
  cr_assert_eq(setting, COLOR_SETTING_TRUE);
  cr_assert_not(parse_color_setting("invalid", &setting, NULL));
}
