/**
 * @file options/completions_test.c
 * @brief Tests for shell completion generation
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>

#include <ascii-chat/options/completions/completions.h>
#include <stdio.h>
#include <string.h>
#include <ascii-chat/common.h>
#include <ascii-chat/options/registry.h>
TEST_CASE("completions/parse_shell_name_bash", "[completions]") {
  completion_format_t format = completions_parse_shell_name("bash");
  ASCIICHAT_REQUIRE_EQ(format, COMPLETION_FORMAT_BASH, "Should parse 'bash' correctly");
}

TEST_CASE("completions/parse_shell_name_fish", "[completions]") {
  completion_format_t format = completions_parse_shell_name("fish");
  ASCIICHAT_REQUIRE_EQ(format, COMPLETION_FORMAT_FISH, "Should parse 'fish' correctly");
}

TEST_CASE("completions/parse_shell_name_zsh", "[completions]") {
  completion_format_t format = completions_parse_shell_name("zsh");
  ASCIICHAT_REQUIRE_EQ(format, COMPLETION_FORMAT_ZSH, "Should parse 'zsh' correctly");
}

TEST_CASE("completions/parse_shell_name_powershell", "[completions]") {
  completion_format_t format = completions_parse_shell_name("powershell");
  ASCIICHAT_REQUIRE_EQ(format, COMPLETION_FORMAT_POWERSHELL, "Should parse 'powershell' correctly");
}

TEST_CASE("completions/parse_shell_name_case_insensitive", "[completions]") {
  completion_format_t format = completions_parse_shell_name("BASH");
  ASCIICHAT_REQUIRE_EQ(format, COMPLETION_FORMAT_BASH, "Should be case-insensitive");
}

TEST_CASE("completions/parse_shell_name_ps_alias", "[completions]") {
  completion_format_t format = completions_parse_shell_name("ps");
  ASCIICHAT_REQUIRE_EQ(format, COMPLETION_FORMAT_POWERSHELL, "Should recognize 'ps' as PowerShell alias");
}

TEST_CASE("completions/parse_shell_name_unknown", "[completions]") {
  completion_format_t format = completions_parse_shell_name("invalid_shell");
  ASCIICHAT_REQUIRE_EQ(format, COMPLETION_FORMAT_UNKNOWN, "Should return UNKNOWN for invalid shell");
}

TEST_CASE("completions/parse_shell_name_null", "[completions]") {
  completion_format_t format = completions_parse_shell_name(NULL);
  ASCIICHAT_REQUIRE_EQ(format, COMPLETION_FORMAT_UNKNOWN, "Should return UNKNOWN for NULL input");
}

TEST_CASE("completions/get_shell_name_bash", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_BASH);
  ASCIICHAT_REQUIRE_STR_EQ(name, "bash", "Should return 'bash' for COMPLETION_FORMAT_BASH");
}

TEST_CASE("completions/get_shell_name_fish", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_FISH);
  ASCIICHAT_REQUIRE_STR_EQ(name, "fish", "Should return 'fish' for COMPLETION_FORMAT_FISH");
}

TEST_CASE("completions/get_shell_name_zsh", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_ZSH);
  ASCIICHAT_REQUIRE_STR_EQ(name, "zsh", "Should return 'zsh' for COMPLETION_FORMAT_ZSH");
}

TEST_CASE("completions/get_shell_name_powershell", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_POWERSHELL);
  ASCIICHAT_REQUIRE_STR_EQ(name, "powershell", "Should return 'powershell' for COMPLETION_FORMAT_POWERSHELL");
}

TEST_CASE("completions/get_shell_name_unknown", "[completions]") {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_UNKNOWN);
  ASCIICHAT_REQUIRE_STR_EQ(name, "unknown", "Should return 'unknown' for COMPLETION_FORMAT_UNKNOWN");
}

TEST_CASE("completions/roundtrip_bash", "[completions]") {
  completion_format_t format1 = COMPLETION_FORMAT_BASH;
  const char *name = completions_get_shell_name(format1);
  completion_format_t format2 = completions_parse_shell_name(name);
  ASCIICHAT_REQUIRE_EQ(format1, format2, "Roundtrip conversion should preserve format");
}

TEST_CASE("completions/roundtrip_fish", "[completions]") {
  completion_format_t format1 = COMPLETION_FORMAT_FISH;
  const char *name = completions_get_shell_name(format1);
  completion_format_t format2 = completions_parse_shell_name(name);
  ASCIICHAT_REQUIRE_EQ(format1, format2, "Roundtrip conversion should preserve format");
}

TEST_CASE("completions/generate_bash_to_stdout", "[completions]") {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_BASH, stdout);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should generate bash completions without error");
}

TEST_CASE("completions/generate_fish_to_stdout", "[completions]") {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_FISH, stdout);
  ASCIICHAT_REQUIRE_EQ(result, ASCIICHAT_OK, "Should generate fish completions without error");
}

TEST_CASE("completions/generate_null_output", "[completions]") {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_BASH, NULL);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with NULL output stream");
}

TEST_CASE("completions/generate_invalid_format", "[completions]") {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_UNKNOWN, stdout);
  ASCIICHAT_REQUIRE_NE(result, ASCIICHAT_OK, "Should fail with invalid completion format");
}

TEST_CASE("completions/mcp_mode_registry_includes_transport_and_bind_options", "[completions]") {
  size_t count = 0;
  const option_descriptor_t *options = options_registry_get_for_display(MODE_MCP_SERVER, false, &count);
  ASCIICHAT_REQUIRE_NOT_NULL(options);
  const char *required[] = {"transport", "server", "http-origin", "tls-cert", "tls-key", "tls-key-password",
                            "port",          "address",      "address6"};
  for (size_t r = 0; r < sizeof(required) / sizeof(required[0]); r++) {
    bool found = false;
    for (size_t i = 0; i < count; i++) {
      found |= strcmp(options[i].long_name, required[r]) == 0;
    }
    ASCIICHAT_REQUIRE(found, "Missing MCP completion option: %s", required[r]);
  }
  SAFE_FREE(options);
  options = options_registry_get_for_mode(MODE_MCP_SERVER, &count);
  ASCIICHAT_REQUIRE_NOT_NULL(options);
  ASCIICHAT_REQUIRE_GT(count, 0);
  SAFE_FREE(options);
}
