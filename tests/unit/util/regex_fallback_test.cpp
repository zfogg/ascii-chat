#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_all.hpp>
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
  REQUIRE((code) != nullptr);
  size_t jit_size = 1;
  REQUIRE((pcre2_pattern_info(code, PCRE2_INFO_JITSIZE, &jit_size)) == (0));
  REQUIRE((jit_size) == (0));
  pcre2_code_free(code);
}
TEST_CASE("regex_fallback/valid_and_invalid_ports_without_jit", "[regex_fallback]") {
  uint16_t port = 0;
  REQUIRE((parse_port("47097", &port)) == (ASCIICHAT_OK));
  REQUIRE((port) == (47097));
  REQUIRE((parse_port("65535", &port)) == (ASCIICHAT_OK));
  REQUIRE((port) == (65535));
  REQUIRE((parse_port("0", &port)) == (ASCIICHAT_OK));
  REQUIRE((port) == (0));
  REQUIRE((parse_port("65536", &port)) != (ASCIICHAT_OK));
  REQUIRE((parse_port("invalid", &port)) != (ASCIICHAT_OK));
  errno_clear();
}

TEST_CASE("regex_fallback/urls_validate_and_parse_without_jit", "[regex_fallback]") {
  REQUIRE(url_is_valid("https://example.com:443/video.mp4"));
  REQUIRE(!(url_is_valid("invalid URL")));
  url_parts_t parts = {0};
  REQUIRE((url_parse("https://example.com:443/video.mp4", &parts)) == (ASCIICHAT_OK));
  REQUIRE(std::strcmp((parts.host), ("example.com")) == 0);
  REQUIRE((parts.port) == (443));
  REQUIRE(std::strcmp((parts.path), ("/video.mp4")) == 0);
  url_parts_destroy(&parts);
}

TEST_CASE("regex_fallback/paths_normalize_without_jit", "[regex_fallback]") {
  char normalized[256];
  REQUIRE(path_normalize_copy("/media//./video.mp4", normalized, sizeof(normalized)));
  REQUIRE(std::strcmp((normalized), ("/media/video.mp4")) == 0);
}

TEST_CASE("regex_fallback/settings_parse_without_jit", "[regex_fallback]") {
  int setting = 0;
  REQUIRE(parse_color_setting("false", &setting, NULL));
  REQUIRE((setting) == (COLOR_SETTING_FALSE));
  REQUIRE(parse_color_setting("true", &setting, NULL));
  REQUIRE((setting) == (COLOR_SETTING_TRUE));
  REQUIRE(!(parse_color_setting("invalid", &setting, NULL)));
}

TEST_CASE("regex_fallback/session_strings_validate_without_jit", "[regex_fallback]") {
  REQUIRE(is_session_string("happy-cat-dog"));
  REQUIRE(!(is_session_string("happy--dog")));
  REQUIRE(!(is_session_string("happy-cat-dog-extra")));
}
