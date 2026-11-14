#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <stdlib.h>
#include <ascii-chat/discovery/strings.h>
#include <ascii-chat/util/pcre2.h>
#include <ascii-chat/util/parsing.h>
#include <ascii-chat/util/path.h>
#include <ascii-chat/util/url.h>
#include <ascii-chat/options/parsers.h>
#include <ascii-chat/options/options.h>
#include <libplatform/system.h>

#define setenv(name, value, overwrite) platform_setenv((name), (value))

static void setup(void) {
  setenv("ASCII_CHAT_PCRE2_DISABLE_JIT", "1", 1);
  pcre2_code *code = asciichat_pcre2_compile("^fallback-probe$", 0);
  ASCIICHAT_REQUIRE_NOT_NULL(code);
  size_t jit_size = 1;
  ASCIICHAT_REQUIRE_EQ(pcre2_pattern_info(code, PCRE2_INFO_JITSIZE, &jit_size), 0);
  ASCIICHAT_REQUIRE_EQ(jit_size, 0, "The fixture must exercise interpreted regex matching");
  pcre2_code_free(code);
}
TEST_CASE("regex_fallback/valid_and_invalid_ports_without_jit", "[regex_fallback]") {
  uint16_t port = 0;
  ASCIICHAT_REQUIRE_EQ(parse_port("47097", &port), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(port, 47097);
  ASCIICHAT_REQUIRE_EQ(parse_port("65535", &port), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(port, 65535);
  ASCIICHAT_REQUIRE_EQ(parse_port("0", &port), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(port, 0);
  ASCIICHAT_REQUIRE_NE(parse_port("65536", &port), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(parse_port("invalid", &port), ASCIICHAT_OK);
  errno_clear();
}

TEST_CASE("regex_fallback/urls_validate_and_parse_without_jit", "[regex_fallback]") {
  ASCIICHAT_REQUIRE(url_is_valid("https://example.com:443/video.mp4"));
  ASCIICHAT_REQUIRE_NOT(url_is_valid("invalid URL"));
  url_parts_t parts = {0};
  ASCIICHAT_REQUIRE_EQ(url_parse("https://example.com:443/video.mp4", &parts), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_STR_EQ(parts.host, "example.com");
  ASCIICHAT_REQUIRE_EQ(parts.port, 443);
  ASCIICHAT_REQUIRE_STR_EQ(parts.path, "/video.mp4");
  url_parts_destroy(&parts);
}

TEST_CASE("regex_fallback/paths_normalize_without_jit", "[regex_fallback]") {
  char normalized[256];
  ASCIICHAT_REQUIRE(path_normalize_copy("/media//./video.mp4", normalized, sizeof(normalized)));
  ASCIICHAT_REQUIRE_STR_EQ(normalized, "/media/video.mp4");
}

TEST_CASE("regex_fallback/settings_parse_without_jit", "[regex_fallback]") {
  int setting = 0;
  ASCIICHAT_REQUIRE(parse_color_setting("false", &setting, NULL));
  ASCIICHAT_REQUIRE_EQ(setting, COLOR_SETTING_FALSE);
  ASCIICHAT_REQUIRE(parse_color_setting("true", &setting, NULL));
  ASCIICHAT_REQUIRE_EQ(setting, COLOR_SETTING_TRUE);
  ASCIICHAT_REQUIRE_NOT(parse_color_setting("invalid", &setting, NULL));
}

TEST_CASE("regex_fallback/session_strings_validate_without_jit", "[regex_fallback]") {
  ASCIICHAT_REQUIRE(is_session_string("happy-cat-dog"));
  ASCIICHAT_REQUIRE_NOT(is_session_string("happy--dog"));
  ASCIICHAT_REQUIRE_NOT(is_session_string("happy-cat-dog-extra"));
}
