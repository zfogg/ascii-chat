/**
 * @file options_help_api_test.c
 * @brief Unit tests for options_get_help_text() API
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <ascii-chat/options/options.h>
#include <string.h>

// Test suite setup
TestSuite(options_help_api);

Test(options_help_api, returns_help_text_for_valid_option_client_mode) {
  // Get help text for a known option in client mode
  const char *help = options_get_help_text(MODE_CLIENT, "color-mode");

  // Should return non-NULL help text
  cr_assert_not_null(help, "Help text should not be NULL for valid option in applicable mode");
  cr_assert(strlen(help) > 0, "Help text should not be empty");
}

Test(options_help_api, returns_help_text_for_fps_option) {
  const char *help = options_get_help_text(MODE_CLIENT, "fps");

  cr_assert_not_null(help, "fps should have help text in client mode");
  cr_assert(strlen(help) > 0, "Help text should not be empty");
}

Test(options_help_api, returns_help_text_for_mirror_mode) {
  const char *help = options_get_help_text(MODE_MIRROR, "width");

  cr_assert_not_null(help, "width should have help text in mirror mode");
  cr_assert(strlen(help) > 0, "Help text should not be empty");
}

Test(options_help_api, returns_null_for_nonexistent_option) {
  const char *help = options_get_help_text(MODE_CLIENT, "nonexistent-option-xyz");

  cr_assert_null(help, "Should return NULL for nonexistent option");
}

Test(options_help_api, returns_null_for_empty_option_name) {
  const char *help = options_get_help_text(MODE_CLIENT, "");

  cr_assert_null(help, "Should return NULL for empty option name");
}

Test(options_help_api, returns_null_for_null_option_name) {
  const char *help = options_get_help_text(MODE_CLIENT, NULL);

  cr_assert_null(help, "Should return NULL for NULL option name");
}

Test(options_help_api, works_across_multiple_modes) {
  // Test the same option in different modes
  const char *help_mirror = options_get_help_text(MODE_MIRROR, "fps");
  const char *help_client = options_get_help_text(MODE_CLIENT, "fps");

  // Both should return help text (fps applies to both)
  cr_assert_not_null(help_mirror, "fps should have help in mirror mode");
  cr_assert_not_null(help_client, "fps should have help in client mode");

  // Should be the same text (same option, same help text across modes)
  cr_assert_str_eq(help_mirror, help_client, "Same option should have same help text");
}

Test(options_help_api, server_specific_option) {
  const char *help = options_get_help_text(MODE_SERVER, "max-clients");

  cr_assert_not_null(help, "max-clients should have help in server mode");
  cr_assert(strlen(help) > 0, "Help text should not be empty");
}

Test(options_help_api, server_option_not_in_client_mode) {
  // max-clients is server-only, should not be available in client mode
  const char *help = options_get_help_text(MODE_CLIENT, "max-clients");

  cr_assert_null(help, "max-clients should not be available in client mode");
}

Test(options_help_api, help_text_is_consistent) {
  // Same option in same mode should always return same text
  const char *help1 = options_get_help_text(MODE_CLIENT, "color-mode");
  const char *help2 = options_get_help_text(MODE_CLIENT, "color-mode");

  cr_assert_not_null(help1, "First call should return help text");
  cr_assert_not_null(help2, "Second call should return help text");
  cr_assert_str_eq(help1, help2, "Repeated calls should return same text");
}

Test(options_help_api, supports_all_modes) {
  // Test that the API works with all valid modes
  asciichat_mode_t modes[] = {MODE_SERVER, MODE_CLIENT, MODE_MIRROR, MODE_DISCOVERY_SERVICE};

  for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
    // Try to get help for a universal option that applies to all modes
    // (width is available in most modes)
    const char *help = options_get_help_text(modes[i], "width");

    // Help might be NULL if width doesn't apply to this mode, which is OK
    // The important thing is that the API doesn't crash
    (void)help; // Use the variable to avoid compiler warnings
  }

  cr_assert(1, "API supports all modes without crashing");
}

Test(options_help_api, returns_consistent_pointer) {
  // Help text should point to static data (same address on repeated calls)
  const char *help1 = options_get_help_text(MODE_CLIENT, "fps");
  const char *help2 = options_get_help_text(MODE_CLIENT, "fps");

  if (help1 && help2) {
    cr_assert_eq(help1, help2, "Help text pointers should be identical (static data)");
  }
}
