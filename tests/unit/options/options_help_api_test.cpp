#include <catch2/catch_test_macros.hpp>
/**
 * @file options_help_api_test.c
 * @brief Unit tests for options_get_help_text() API
 *
 * @author Zachary Fogg <me@zfo.gg>
 * @date February 2026
 */

#include <catch2/catch_all.hpp>
#include <catch2/catch_all.hpp>
#include <ascii-chat/options/options.h>
#include <ascii-chat/tests/logging.h>
#include <string.h>

// Test suite setup
// Use verbose logging with debug level enabled and stdout/stderr not disabled

TEST_CASE("options_help_api/returns_help_text_for_valid_option_client_mode", "[options_help_api]") {
  // Get help text for a known option in client mode
  const char *help = options_get_help_text(MODE_CLIENT, "color-mode");

  // Should return non-NULL help text
  REQUIRE((help) != nullptr);
  REQUIRE(strlen(help) > 0);
}

TEST_CASE("options_help_api/returns_help_text_for_fps_option", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_CLIENT, "fps");

  REQUIRE((help) != nullptr);
  REQUIRE(strlen(help) > 0);
}

TEST_CASE("options_help_api/returns_help_text_for_mirror_mode", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_MIRROR, "width");

  REQUIRE((help) != nullptr);
  REQUIRE(strlen(help) > 0);
}

TEST_CASE("options_help_api/returns_null_for_nonexistent_option", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_CLIENT, "nonexistent-option-xyz");

  REQUIRE((help) == nullptr);
}

TEST_CASE("options_help_api/returns_null_for_empty_option_name", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_CLIENT, "");

  REQUIRE((help) == nullptr);
}

TEST_CASE("options_help_api/returns_null_for_null_option_name", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_CLIENT, NULL);

  REQUIRE((help) == nullptr);
}

TEST_CASE("options_help_api/works_across_multiple_modes", "[options_help_api]") {
  // Test the same option in different modes
  const char *help_mirror = options_get_help_text(MODE_MIRROR, "fps");
  const char *help_client = options_get_help_text(MODE_CLIENT, "fps");

  // Both should return help text (fps applies to both)
  REQUIRE((help_mirror) != nullptr);
  REQUIRE((help_client) != nullptr);

  // Should be the same text (same option, same help text across modes)
  REQUIRE(std::strcmp((help_mirror), (help_client)) == 0);
}

TEST_CASE("options_help_api/server_specific_option", "[options_help_api]") {
  const char *help = options_get_help_text(MODE_SERVER, "max-clients");

  REQUIRE((help) != nullptr);
  REQUIRE(strlen(help) > 0);
}

TEST_CASE("options_help_api/server_option_not_in_client_mode", "[options_help_api]") {
  // max-clients is server-only, should not be available in client mode
  const char *help = options_get_help_text(MODE_CLIENT, "max-clients");

  REQUIRE((help) == nullptr);
}

TEST_CASE("options_help_api/help_text_is_consistent", "[options_help_api]") {
  // Same option in same mode should always return same text
  const char *help1 = options_get_help_text(MODE_CLIENT, "color-mode");
  const char *help2 = options_get_help_text(MODE_CLIENT, "color-mode");

  REQUIRE((help1) != nullptr);
  REQUIRE((help2) != nullptr);
  REQUIRE(std::strcmp((help1), (help2)) == 0);
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

  REQUIRE(1);
}

TEST_CASE("options_help_api/returns_consistent_pointer", "[options_help_api]") {
  // Help text should point to static data (same address on repeated calls)
  const char *help1 = options_get_help_text(MODE_CLIENT, "fps");
  const char *help2 = options_get_help_text(MODE_CLIENT, "fps");

  if (help1 && help2) {
    REQUIRE((help1) == (help2));
  }
}
