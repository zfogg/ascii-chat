/**
 * @file options/completions_test.c
 * @brief Tests for shell completion generation
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include <ascii-chat/options/completions/completions.h>
#include <stdio.h>

TestSuite(completions);

Test(completions, parse_shell_name_bash) {
  completion_format_t format = completions_parse_shell_name("bash");
  cr_assert_eq(format, COMPLETION_FORMAT_BASH, "Should parse 'bash' correctly");
}

Test(completions, parse_shell_name_fish) {
  completion_format_t format = completions_parse_shell_name("fish");
  cr_assert_eq(format, COMPLETION_FORMAT_FISH, "Should parse 'fish' correctly");
}

Test(completions, parse_shell_name_zsh) {
  completion_format_t format = completions_parse_shell_name("zsh");
  cr_assert_eq(format, COMPLETION_FORMAT_ZSH, "Should parse 'zsh' correctly");
}

Test(completions, parse_shell_name_powershell) {
  completion_format_t format = completions_parse_shell_name("powershell");
  cr_assert_eq(format, COMPLETION_FORMAT_POWERSHELL, "Should parse 'powershell' correctly");
}

Test(completions, parse_shell_name_case_insensitive) {
  completion_format_t format = completions_parse_shell_name("BASH");
  cr_assert_eq(format, COMPLETION_FORMAT_BASH, "Should be case-insensitive");
}

Test(completions, parse_shell_name_ps_alias) {
  completion_format_t format = completions_parse_shell_name("ps");
  cr_assert_eq(format, COMPLETION_FORMAT_POWERSHELL, "Should recognize 'ps' as PowerShell alias");
}

Test(completions, parse_shell_name_unknown) {
  completion_format_t format = completions_parse_shell_name("invalid_shell");
  cr_assert_eq(format, COMPLETION_FORMAT_UNKNOWN, "Should return UNKNOWN for invalid shell");
}

Test(completions, parse_shell_name_null) {
  completion_format_t format = completions_parse_shell_name(NULL);
  cr_assert_eq(format, COMPLETION_FORMAT_UNKNOWN, "Should return UNKNOWN for NULL input");
}

Test(completions, get_shell_name_bash) {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_BASH);
  cr_assert_str_eq(name, "bash", "Should return 'bash' for COMPLETION_FORMAT_BASH");
}

Test(completions, get_shell_name_fish) {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_FISH);
  cr_assert_str_eq(name, "fish", "Should return 'fish' for COMPLETION_FORMAT_FISH");
}

Test(completions, get_shell_name_zsh) {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_ZSH);
  cr_assert_str_eq(name, "zsh", "Should return 'zsh' for COMPLETION_FORMAT_ZSH");
}

Test(completions, get_shell_name_powershell) {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_POWERSHELL);
  cr_assert_str_eq(name, "powershell", "Should return 'powershell' for COMPLETION_FORMAT_POWERSHELL");
}

Test(completions, get_shell_name_unknown) {
  const char *name = completions_get_shell_name(COMPLETION_FORMAT_UNKNOWN);
  cr_assert_str_eq(name, "unknown", "Should return 'unknown' for COMPLETION_FORMAT_UNKNOWN");
}

Test(completions, roundtrip_bash) {
  completion_format_t format1 = COMPLETION_FORMAT_BASH;
  const char *name = completions_get_shell_name(format1);
  completion_format_t format2 = completions_parse_shell_name(name);
  cr_assert_eq(format1, format2, "Roundtrip conversion should preserve format");
}

Test(completions, roundtrip_fish) {
  completion_format_t format1 = COMPLETION_FORMAT_FISH;
  const char *name = completions_get_shell_name(format1);
  completion_format_t format2 = completions_parse_shell_name(name);
  cr_assert_eq(format1, format2, "Roundtrip conversion should preserve format");
}

Test(completions, generate_bash_to_stdout) {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_BASH, stdout);
  cr_assert_eq(result, ASCIICHAT_OK, "Should generate bash completions without error");
}

Test(completions, generate_fish_to_stdout) {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_FISH, stdout);
  cr_assert_eq(result, ASCIICHAT_OK, "Should generate fish completions without error");
}

Test(completions, generate_null_output) {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_BASH, NULL);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with NULL output stream");
}

Test(completions, generate_invalid_format) {
  asciichat_error_t result = completions_generate_for_shell(COMPLETION_FORMAT_UNKNOWN, stdout);
  cr_assert_neq(result, ASCIICHAT_OK, "Should fail with invalid completion format");
}
