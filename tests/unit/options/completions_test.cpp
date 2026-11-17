#include <catch2/catch_test_macros.hpp>
/**
 * @file options/completions_test.c
 * @brief Tests for shell completion generation
 */

#include <catch2/catch_all.hpp>

#include <ascii-chat/options/completions/completions.h>
#include <stdio.h>
#include <string.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/registry.h>
TEST_CASE("completions/parse_shell_name_bash", "[completions]") {
  completion_format_t format = completions_parse_shell_name("bash");
  REQUIRE((format) == (COMPLETION_FORMAT_BASH));
}

TEST_CASE("completions/parse_shell_name_fish", "[completions]") {
  completion_format_t format = completions_parse_shell_name("fish");
  REQUIRE((format) == (COMPLETION_FORMAT_FISH));
}

TEST_CASE("completions/parse_shell_name_zsh", "[completions]") {
  completion_format_t format = completions_parse_shell_name("zsh");
  REQUIRE((format) == (COMPLETION_FORMAT_ZSH));
}

TEST_CASE("completions/parse_shell_name_powershell", "[completions]") {
  completion_format_t format = completions_parse_shell_name("powershell");
  REQUIRE((format) == (COMPLETION_FORMAT_POWERSHELL));
}

TEST_CASE("completions/parse_shell_name_case_insensitive", "[completions]") {
  completion_format_t format = completions_parse_shell_name("BASH");
  REQUIRE((format) == (COMPLETION_FORMAT_BASH));
}

TEST_CASE("completions/parse_shell_name_ps_alias", "[completions]") {
  completion_format_t format = completions_parse_shell_name("ps");
  REQUIRE((format) == (COMPLETION_FORMAT_POWERSHELL));
}

TEST_CASE("completions/parse_shell_name_unknown", "[completions]") {
  completion_format_t format = completions_parse_shell_name("invalid_shell");
  REQUIRE((format) == (COMPLETION_FORMAT_UNKNOWN));
}

TEST_CASE("completions/parse_shell_name_null", "[completions]") {
  completion_format_t format = completions_parse_shell_name(NULL);
  REQUIRE((format) == (COMPLETION_FORMAT_UNKNOWN));
}

TEST_CASE("completions/get_shell_name_bash", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_BASH);
  REQUIRE(std::strcmp((name), ("bash")) == 0);
}

TEST_CASE("completions/get_shell_name_fish", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_FISH);
  REQUIRE(std::strcmp((name), ("fish")) == 0);
}

TEST_CASE("completions/get_shell_name_zsh", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_ZSH);
  REQUIRE(std::strcmp((name), ("zsh")) == 0);
}

TEST_CASE("completions/get_shell_name_powershell", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_POWERSHELL);
  REQUIRE(std::strcmp((name), ("powershell")) == 0);
}

TEST_CASE("completions/get_shell_name_unknown", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_UNKNOWN);
  REQUIRE(std::strcmp((name), ("unknown")) == 0);
}

TEST_CASE("completions/roundtrip_bash", "[completions]") {
  completion_format_t format1 = COMPLETION_FORMAT_BASH;
  const char *name = completions_get_shell_name(format1);
  completion_format_t format2 = completions_parse_shell_name(name);
  REQUIRE((format1) == (format2));
}

TEST_CASE("completions/roundtrip_fish", "[completions]") {
  completion_format_t format1 = COMPLETION_FORMAT_FISH;
  const char *name = completions_get_shell_name(format1);
  completion_format_t format2 = completions_parse_shell_name(name);
  REQUIRE((format1) == (format2));
}

TEST_CASE("completions/generate_bash_to_stdout", "[completions]") {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_BASH, stdout);
  REQUIRE((result) == (ASCIICHAT_OK));
}

TEST_CASE("completions/generate_fish_to_stdout", "[completions]") {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_FISH, stdout);
  REQUIRE((result) == (ASCIICHAT_OK));
}

TEST_CASE("completions/generate_null_output", "[completions]") {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_BASH, NULL);
  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("completions/generate_invalid_format", "[completions]") {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_UNKNOWN, stdout);
  REQUIRE((result) != (ASCIICHAT_OK));
}

TEST_CASE("completions/mcp_mode_registry_includes_transport_and_bind_options", "[completions]") {
  size_t count = 0;
  const option_descriptor_t *options = options_registry_get_for_display(MODE_MCP_SERVER, false, &count);
  REQUIRE((options) != nullptr);
  const char *required[] = {"transport", "server", "http-origin", "tls-cert", "tls-key", "tls-key-password", "port", "address", "address6"};
  for (size_t r = 0; r < sizeof(required) / sizeof(required[0]); r++) {
    bool found = false;
    for (size_t i = 0; i < count; i++) {
      found |= strcmp(options[i].long_name, required[r]) == 0;
    }
    REQUIRE(found);
  }
  SAFE_FREE(options);
  options = options_registry_get_for_mode(MODE_MCP_SERVER, &count);
  REQUIRE((options) != nullptr);
  REQUIRE((count) > (0));
  SAFE_FREE(options);
}
