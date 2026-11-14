/**
 * @file rcu_validation_test.c
 * @brief Unit tests for RCU option value validation (issue #403)
 *
 * Tests that options_set_int/double/bool/string validate values against
 * registry metadata before publishing to RCU.
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
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
  ASCIICHAT_REQUIRE_STR_EMPTY(GET_OPTION(webrtc_bind_address));
  const char *addresses[] = {"127.0.0.1", "::1", "192.0.2.1", "2001:db8::1", "0.0.0.0", "::", ""};
  for (size_t i = 0; i < sizeof(addresses) / sizeof(*addresses); ++i) {
    ASCIICHAT_REQUIRE_EQ(options_set_string("webrtc_bind_address", addresses[i]), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(GET_OPTION(webrtc_bind_address), addresses[i]);
  }
}

TEST_CASE("rcu_validation/webrtc_bind_address_rejects_invalid_values_without_publishing", "[rcu_validation]") {
  ASCIICHAT_REQUIRE_EQ(options_set_string("webrtc_bind_address", "127.0.0.1"), ASCIICHAT_OK);
  const char *invalid[] = {"localhost", "127.0.0.1:5000", "[::1]", "::not-an-ip", "256.0.0.1"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
    ASCIICHAT_REQUIRE_NE(options_set_string("webrtc_bind_address", invalid[i]), ASCIICHAT_OK);
    ASCIICHAT_REQUIRE_STR_EQ(GET_OPTION(webrtc_bind_address), "127.0.0.1");
  }
}

TEST_CASE("rcu_validation/face_follow_and_zalgo_keyboard_round_trip", "[rcu_validation]") {
  double volume = 1.0;
  ASCIICHAT_REQUIRE_EQ(options_set_bool("face_follow", false), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(options_set_int("zalgo", 0), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(ui_session_key((keyboard_key_t)'4', &volume), UI_ACTION_NONE);
  ASCIICHAT_REQUIRE_NOT(GET_OPTION(face_follow));
  ASCIICHAT_REQUIRE_EQ(ui_session_key((keyboard_key_t)'w', &volume), UI_ACTION_SETTINGS);
  ASCIICHAT_REQUIRE(GET_OPTION(face_follow));
  ASCIICHAT_REQUIRE_EQ(ui_session_key((keyboard_key_t)'z', &volume), UI_ACTION_SETTINGS);
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(zalgo), 8);
  ui_session_key((keyboard_key_t)'W', &volume);
  ui_session_key((keyboard_key_t)'Z', &volume);
  ASCIICHAT_REQUIRE_NOT(GET_OPTION(face_follow));
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(zalgo), 0);
}

TEST_CASE("rcu_validation/zalgo_rejects_invalid_runtime_intensity", "[rcu_validation]") {
  ASCIICHAT_REQUIRE_EQ(options_set_int("zalgo", 8), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_NE(options_set_int("zalgo", -1), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(zalgo), 8);
  ASCIICHAT_REQUIRE_NE(options_set_int("zalgo", 11), ASCIICHAT_OK);
  ASCIICHAT_REQUIRE_EQ(GET_OPTION(zalgo), 8);
}

// ============================================================================
// Integer Field Validation Tests
// ============================================================================

/**
 * Test: FPS validation - reject value below minimum (range 0-144)
 */
TEST_CASE("rcu_validation/fps_below_minimum", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("fps", -1);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected validation error for fps=-1");
}

/**
 * Test: FPS validation - accept valid value (range 0-144)
 */
TEST_CASE("rcu_validation/fps_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("fps", 30);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for fps=30");

  // Verify value was set
  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ(opts->fps, 30, "Expected fps to be 30");
}

/**
 * Test: FPS validation - reject value above maximum
 */
TEST_CASE("rcu_validation/fps_above_maximum", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("fps", 200);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected validation error for fps=200");
}

/**
 * Test: Port validation - reject negative port
 */
TEST_CASE("rcu_validation/port_negative", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("port", -1);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected validation error for port=-1");
}

/**
 * Test: Port validation - accept valid port
 */
TEST_CASE("rcu_validation/port_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("port", 27224);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for port=27224");

  // Verify value was set
  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ(opts->port, 27224, "Expected port to be 27224");
}

/**
 * Test: Max clients validation - reject zero
 */
TEST_CASE("rcu_validation/max_clients_zero", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("max_clients", 0);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected validation error for max_clients=0");
}

/**
 * Test: Max clients validation - accept valid value
 */
TEST_CASE("rcu_validation/max_clients_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("max_clients", 4);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for max_clients=4");

  // Verify value was set
  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ(opts->max_clients, 4, "Expected max_clients to be 4");
}

/**
 * Test: Enum validation - color_mode below valid range
 */
TEST_CASE("rcu_validation/color_mode_invalid_negative", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("color_mode", -2);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected validation error for invalid color_mode");
}

/**
 * Test: Enum validation - color_mode above valid range
 */
TEST_CASE("rcu_validation/color_mode_invalid_high", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("color_mode", 9999);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected validation error for color_mode=9999");
}

/**
 * Test: Enum validation - color_mode with valid value (0=auto)
 */
TEST_CASE("rcu_validation/color_mode_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("color_mode", 0);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for valid color_mode");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ((int)opts->color_mode, 0, "Expected color_mode to be 0");
}

/**
 * Test: Log level enum validation - valid value (2=info)
 */
TEST_CASE("rcu_validation/log_level_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("log_level", 2);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for valid log_level");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ((int)opts->log_level, 2, "Expected log_level to be 2");
}

// ============================================================================
// Double Field Validation Tests
// ============================================================================

/**
 * Test: Snapshot delay - valid value
 */
TEST_CASE("rcu_validation/snapshot_delay_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_double("snapshot_delay", 1.5);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for snapshot_delay=1.5");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE(fabs(opts->snapshot_delay - 1.5) < 0.001, "Expected snapshot_delay to be 1.5");
}

/**
 * Test: Microphone sensitivity - valid value
 */
TEST_CASE("rcu_validation/microphone_sensitivity_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_double("microphone_sensitivity", 0.8);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for microphone_sensitivity=0.8");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE(fabs(opts->microphone_sensitivity - 0.8) < 0.001, "Expected microphone_sensitivity to be 0.8");
}

/**
 * Test: Speakers volume - valid value
 */
TEST_CASE("rcu_validation/speakers_volume_valid", "[rcu_validation]") {
  asciichat_error_t err = options_set_double("speakers_volume", 1.0);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for speakers_volume=1.0");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE(fabs(opts->speakers_volume - 1.0) < 0.001, "Expected speakers_volume to be 1.0");
}

// ============================================================================
// Boolean Field Validation Tests
// ============================================================================

/**
 * Test: Boolean field - valid true
 */
TEST_CASE("rcu_validation/bool_field_true", "[rcu_validation]") {
  asciichat_error_t err = options_set_bool("no_compress", true);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for no_compress=true");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ(opts->no_compress, true, "Expected no_compress to be true");
}

/**
 * Test: Boolean field - valid false
 */
TEST_CASE("rcu_validation/bool_field_false", "[rcu_validation]") {
  asciichat_error_t err = options_set_bool("no_compress", false);
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for no_compress=false");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_EQ(opts->no_compress, false, "Expected no_compress to be false");
}

// ============================================================================
// String Field Validation Tests
// ============================================================================

/**
 * Test: String field - valid address
 */
TEST_CASE("rcu_validation/string_field_address", "[rcu_validation]") {
  asciichat_error_t err = options_set_string("address", "127.0.0.1");
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for valid address");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_STR_EQ(opts->address, "127.0.0.1", "Expected address to be 127.0.0.1");
}

/**
 * Test: String field - valid password
 */
TEST_CASE("rcu_validation/string_field_password", "[rcu_validation]") {
  asciichat_error_t err = options_set_string("password", "secret123");
  ASCIICHAT_REQUIRE_EQ(err, ASCIICHAT_OK, "Expected success for valid password");

  const options_t *opts = options_get();
  ASCIICHAT_REQUIRE_STR_EQ(opts->password, "secret123", "Expected password to be set");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

/**
 * Test: options_set_int with NULL field name
 */
TEST_CASE("rcu_validation/set_int_null_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_int(NULL, 42);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected error for NULL field_name");
}

/**
 * Test: options_set_int with unknown field
 */
TEST_CASE("rcu_validation/set_int_unknown_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_int("nonexistent_field", 42);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected error for unknown field");
}

/**
 * Test: options_set_bool with NULL field name
 */
TEST_CASE("rcu_validation/set_bool_null_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_bool(NULL, true);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected error for NULL field_name");
}

/**
 * Test: options_set_string with NULL field name
 */
TEST_CASE("rcu_validation/set_string_null_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_string(NULL, "test");
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected error for NULL field_name");
}

/**
 * Test: options_set_string with NULL value
 */
TEST_CASE("rcu_validation/set_string_null_value", "[rcu_validation]") {
  asciichat_error_t err = options_set_string("address", NULL);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected error for NULL value");
}

/**
 * Test: options_set_double with NULL field name
 */
TEST_CASE("rcu_validation/set_double_null_field", "[rcu_validation]") {
  asciichat_error_t err = options_set_double(NULL, 1.5);
  ASCIICHAT_REQUIRE_NE(err, ASCIICHAT_OK, "Expected error for NULL field_name");
}
