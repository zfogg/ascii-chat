#include <catch2/catch_test_macros.hpp>
/**
 * @file rcu_validation_test.c
 * @brief Unit tests for RCU option value validation (issue #403)
 *
 * Tests that options_set_int/double/bool/string validate values against
 * registry metadata before publishing to RCU.
 */

#include <catch2/catch_all.hpp>
#include <math.h>

#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/options.h>
#include <ascii-chat/tui/session.h>
#include <ascii-chat/asciichat_errno.h>

/**
 * Setup and teardown for each test
 */
static void setup(void) {
  options_state_init();
}

static void teardown(void) {
  options_state_destroy();
}
TEST_CASE("rcu_validation/webrtc_bind_address_accepts_numeric_interfaces", "[rcu_validation]") {
  REQUIRE(GET_OPTION(webrtc_bind_address)[0] == '\0');
  const char *addresses[] = {"127.0.0.1", "::1", "192.0.2.1", "2001:db8::1", "0.0.0.0", "::", ""};
  for (size_t i = 0; i < sizeof(addresses) / sizeof(*addresses); ++i) {
    REQUIRE((options_set_string("webrtc_bind_address", addresses[i])) == (ASCIICHAT_OK));
    REQUIRE(std::strcmp((GET_OPTION(webrtc_bind_address)), (addresses[i])) == 0);
  }
}

TEST_CASE("rcu_validation/webrtc_bind_address_rejects_invalid_values_without_publishing", "[rcu_validation]") {
  REQUIRE((options_set_string("webrtc_bind_address", "127.0.0.1")) == (ASCIICHAT_OK));
  const char *invalid[] = {"localhost", "127.0.0.1:5000", "[::1]", "::not-an-ip", "256.0.0.1"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    REQUIRE((options_set_string("webrtc_bind_address", invalid[i])) != (ASCIICHAT_OK));
    REQUIRE(std::strcmp((GET_OPTION(webrtc_bind_address)), ("127.0.0.1")) == 0);
  }
}

TEST_CASE("rcu_validation/face_follow_and_zalgo_keyboard_round_trip", "[rcu_validation]") {
  double volume = 1.0;
  REQUIRE((options_set_bool("face_follow", false)) == (ASCIICHAT_OK));
  REQUIRE((options_set_int("zalgo", 0)) == (ASCIICHAT_OK));
  REQUIRE((ui_session_key((keyboard_key_t)'4', &volume)) == (UI_ACTION_NONE));
  REQUIRE(!(GET_OPTION(face_follow)));
  REQUIRE((ui_session_key((keyboard_key_t)'w', &volume)) == (UI_ACTION_SETTINGS));
  REQUIRE(GET_OPTION(face_follow));
  REQUIRE((ui_session_key((keyboard_key_t)'z', &volume)) == (UI_ACTION_SETTINGS));
  REQUIRE((GET_OPTION(zalgo)) == (8));
  ui_session_key((keyboard_key_t)'W', &volume);
  ui_session_key((keyboard_key_t)'Z', &volume);
  REQUIRE(!(GET_OPTION(face_follow)));
  REQUIRE((GET_OPTION(zalgo)) == (0));
}

TEST_CASE("rcu_validation/zalgo_rejects_invalid_runtime_intensity", "[rcu_validation]") {
  REQUIRE((options_set_int("zalgo", 8)) == (ASCIICHAT_OK));
  REQUIRE((options_set_int("zalgo", -1)) != (ASCIICHAT_OK));
  REQUIRE((GET_OPTION(zalgo)) == (8));
  REQUIRE((options_set_int("zalgo", 11)) != (ASCIICHAT_OK));
  REQUIRE((GET_OPTION(zalgo)) == (8));
}

// ============================================================================
// Integer Field Validation Tests
// ============================================================================

/**
 * Test: FPS validation - reject value below minimum (range 0-144)
 */
TEST_CASE("rcu_validation/fps_below_minimum", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("fps", -1);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: FPS validation - accept valid value (range 0-144)
 */
TEST_CASE("rcu_validation/fps_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("fps", 30);
  REQUIRE((err) == (ASCIICHAT_OK));

  // Verify value was set
  const options_t *opts = options_get();
  REQUIRE((opts->fps) == (30));
}

/**
 * Test: FPS validation - reject value above maximum
 */
TEST_CASE("rcu_validation/fps_above_maximum", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("fps", 200);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: Port validation - reject negative port
 */
TEST_CASE("rcu_validation/port_negative", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("port", -1);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: Port validation - accept valid port
 */
TEST_CASE("rcu_validation/port_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("port", 27224);
  REQUIRE((err) == (ASCIICHAT_OK));

  // Verify value was set
  const options_t *opts = options_get();
  REQUIRE((opts->port) == (27224));
}

/**
 * Test: Max clients validation - reject zero
 */
TEST_CASE("rcu_validation/max_clients_zero", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("max_clients", 0);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: Max clients validation - accept valid value
 */
TEST_CASE("rcu_validation/max_clients_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("max_clients", 4);
  REQUIRE((err) == (ASCIICHAT_OK));

  // Verify value was set
  const options_t *opts = options_get();
  REQUIRE((opts->max_clients) == (4));
}

/**
 * Test: Enum validation - color_mode below valid range
 */
TEST_CASE("rcu_validation/color_mode_invalid_negative", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("color_mode", -2);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: Enum validation - color_mode above valid range
 */
TEST_CASE("rcu_validation/color_mode_invalid_high", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("color_mode", 9999);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: Enum validation - color_mode with valid value (0=auto)
 */
TEST_CASE("rcu_validation/color_mode_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("color_mode", 0);
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE(((int)opts->color_mode) == (0));
}

/**
 * Test: Log level enum validation - valid value (2=info)
 */
TEST_CASE("rcu_validation/log_level_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("log_level", 2);
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE(((int)opts->log_level) == (2));
}

// ============================================================================
// Double Field Validation Tests
// ============================================================================

/**
 * Test: Snapshot delay - valid value
 */
TEST_CASE("rcu_validation/snapshot_delay_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_double("snapshot_delay", 1.5);
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE(fabs(opts->snapshot_delay - 1.5) < 0.001);
}

/**
 * Test: Microphone sensitivity - valid value
 */
TEST_CASE("rcu_validation/microphone_sensitivity_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_double("microphone_sensitivity", 0.8);
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE(fabs(opts->microphone_sensitivity - 0.8) < 0.001);
}

/**
 * Test: Speakers volume - valid value
 */
TEST_CASE("rcu_validation/speakers_volume_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_double("speakers_volume", 1.0);
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE(fabs(opts->speakers_volume - 1.0) < 0.001);
}

// ============================================================================
// Boolean Field Validation Tests
// ============================================================================

/**
 * Test: Boolean field - valid true
 */
TEST_CASE("rcu_validation/bool_field_true", "[rcu_validation]") {
  asciichat_error_t err = options_set_bool("no_compress", true);
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE((opts->no_compress) == (true));
}

/**
 * Test: Boolean field - valid false
 */
TEST_CASE("rcu_validation/bool_field_false", "[rcu_validation]") {
  asciichat_error_t err = options_set_bool("no_compress", false);
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE((opts->no_compress) == (false));
}

// ============================================================================
// String Field Validation Tests
// ============================================================================

/**
 * Test: String field - valid address
 */
TEST_CASE("rcu_validation/string_field_address", "[rcu_validation]") {
  asciichat_error_t err = options_set_string("address", "127.0.0.1");
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE(std::strcmp((opts->address), ("127.0.0.1")) == 0);
}

/**
 * Test: String field - valid password
 */
TEST_CASE("rcu_validation/string_field_password", "[rcu_validation]") {
  asciichat_error_t err = options_set_string("password", "secret123");
  REQUIRE((err) == (ASCIICHAT_OK));

  const options_t *opts = options_get();
  REQUIRE(std::strcmp((opts->password), ("secret123")) == 0);
}

// ============================================================================
// Error Handling Tests
// ============================================================================

/**
 * Test: options_set_int with NULL field name
 */
TEST_CASE("rcu_validation/set_int_null_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_int(NULL, 42);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: options_set_int with unknown field
 */
TEST_CASE("rcu_validation/set_int_unknown_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("nonexistent_field", 42);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: options_set_bool with NULL field name
 */
TEST_CASE("rcu_validation/set_bool_null_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_bool(NULL, true);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: options_set_string with NULL field name
 */
TEST_CASE("rcu_validation/set_string_null_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_string(NULL, "test");
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: options_set_string with NULL value
 */
TEST_CASE("rcu_validation/set_string_null_value", "[rcu_validation]") {
  asciichat_error_t err = options_set_string("address", NULL);
  REQUIRE((err) != (ASCIICHAT_OK));
}

/**
 * Test: options_set_double with NULL field name
 */
TEST_CASE("rcu_validation/set_double_null_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_double(NULL, 1.5);
  REQUIRE((err) != (ASCIICHAT_OK));
}
