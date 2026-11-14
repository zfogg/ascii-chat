/**
 * @file options_help_api_test.c
 * @brief Unit tests for options_get_help_text() API
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/options/options.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

// Test suite setup
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("options_help_api/returns_help_text_for_valid_option_client_mode", "[options_help_api]") {
  // Get help text for a known option in client mode
  const char *help = options_get_help_text(MODE_CLIENT, "color-mode");

  // Should return non-NULL help text
  ASCIICHAT_REQUIRE_NOT_NULL(help, "Help text should not be NULL for valid option in applicable mode");
  ASCIICHAT_REQUIRE(strlen(help) > 0, "Help text should not be empty");
}

TEST_CASE("options_help_api/returns_help_text_for_fps_option", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_CLIENT, "fps");

  ASCIICHAT_REQUIRE_NOT_NULL(help, "fps should have help text in client mode");
  ASCIICHAT_REQUIRE(strlen(help) > 0, "Help text should not be empty");
}

TEST_CASE("options_help_api/returns_help_text_for_mirror_mode", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_MIRROR, "width");

  ASCIICHAT_REQUIRE_NOT_NULL(help, "width should have help text in mirror mode");
  ASCIICHAT_REQUIRE(strlen(help) > 0, "Help text should not be empty");
}

TEST_CASE("options_help_api/returns_null_for_nonexistent_option", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_CLIENT, "nonexistent-option-xyz");

  ASCIICHAT_REQUIRE_NULL(help, "Should return NULL for nonexistent option");
}

TEST_CASE("options_help_api/returns_null_for_empty_option_name", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_CLIENT, "");

  ASCIICHAT_REQUIRE_NULL(help, "Should return NULL for empty option name");
}

TEST_CASE("options_help_api/returns_null_for_null_option_name", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_CLIENT, NULL);

  ASCIICHAT_REQUIRE_NULL(help, "Should return NULL for NULL option name");
}

TEST_CASE("options_help_api/works_across_multiple_modes", "[options_help_api]") {
  // Test the same option in different modes
  const char *help_mirror = options_get_help_text(MODE_MIRROR, "fps");
  const char *help_client = options_get_help_text(MODE_CLIENT, "fps");

  // Both should return help text (fps applies to both)
  ASCIICHAT_REQUIRE_NOT_NULL(help_mirror, "fps should have help in mirror mode");
  ASCIICHAT_REQUIRE_NOT_NULL(help_client, "fps should have help in client mode");

  // Should be the same text (same option, same help text across modes)
  ASCIICHAT_REQUIRE_STR_EQ(help_mirror, help_client, "Same option should have same help text");
}

TEST_CASE("options_help_api/server_specific_option", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_SERVER, "max-clients");

  ASCIICHAT_REQUIRE_NOT_NULL(help, "max-clients should have help in server mode");
  ASCIICHAT_REQUIRE(strlen(help) > 0, "Help text should not be empty");
}

TEST_CASE("options_help_api/server_option_not_in_client_mode", "[options_help_api]") {
  // max-clients is server-only, should not be available in client mode
  const char *help = options_get_help_text(MODE_CLIENT, "max-clients");

  ASCIICHAT_REQUIRE_NULL(help, "max-clients should not be available in client mode");
}

TEST_CASE("options_help_api/help_text_is_consistent", "[options_help_api]") {
  // Same option in same mode should always return same text
  const char *help1 = options_get_help_text(MODE_CLIENT, "color-mode");
  const char *help2 = options_get_help_text(MODE_CLIENT, "color-mode");

  ASCIICHAT_REQUIRE_NOT_NULL(help1, "First call should return help text");
  ASCIICHAT_REQUIRE_NOT_NULL(help2, "Second call should return help text");
  ASCIICHAT_REQUIRE_STR_EQ(help1, help2, "Repeated calls should return same text");
}

TEST_CASE("options_help_api/supports_all_modes", "[options_help_api]") {
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

  ASCIICHAT_REQUIRE(1, "API supports all modes without crashing");
}

TEST_CASE("options_help_api/returns_consistent_pointer", "[options_help_api]") {
  // Help text should point to static data (same address on repeated calls)
  const char *help1 = options_get_help_text(MODE_CLIENT, "fps");
  const char *help2 = options_get_help_text(MODE_CLIENT, "fps");

  if (help1 && help2) {
    ASCIICHAT_REQUIRE_EQ(help1, help2, "Help text pointers should be identical (static data)");
  }
}
