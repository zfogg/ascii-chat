/**
 * @file rcu_validation_test.c
 * @brief Unit tests for RCU option value validation (issue #403)
 *
 * Tests that options_set_int/double/bool/string validate values against
 * registry metadata before publishing to RCU.
 */

#include <criterion/criterion.h>
#include <math.h>

#include <ascii-chat/options/rcu.h>
#include <ascii-chat/options/options.h>
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

TestSuite(rcu_validation, .init = setup, .fini = teardown);

// ============================================================================
// Integer Field Validation Tests
// ============================================================================

/**
 * Test: FPS validation - reject value below minimum (range 1-144)
 */
Test(rcu_validation, fps_below_minimum) {
  asciichat_error_t err = options_set_int("fps", 0);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected validation error for fps=0");
}

/**
 * Test: FPS validation - accept valid value (range 1-144)
 */
Test(rcu_validation, fps_valid) {
  asciichat_error_t err = options_set_int("fps", 30);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for fps=30");

  // Verify value was set
  const options_t *opts = options_get();
  cr_assert_eq(opts->fps, 30, "Expected fps to be 30");
}

/**
 * Test: FPS validation - reject value above maximum
 */
Test(rcu_validation, fps_above_maximum) {
  asciichat_error_t err = options_set_int("fps", 200);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected validation error for fps=200");
}

/**
 * Test: Port validation - reject negative port
 */
Test(rcu_validation, port_negative) {
  asciichat_error_t err = options_set_int("port", -1);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected validation error for port=-1");
}

/**
 * Test: Port validation - accept valid port
 */
Test(rcu_validation, port_valid) {
  asciichat_error_t err = options_set_int("port", 27224);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for port=27224");

  // Verify value was set
  const options_t *opts = options_get();
  cr_assert_eq(opts->port, 27224, "Expected port to be 27224");
}

/**
 * Test: Max clients validation - reject zero
 */
Test(rcu_validation, max_clients_zero) {
  asciichat_error_t err = options_set_int("max_clients", 0);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected validation error for max_clients=0");
}

/**
 * Test: Max clients validation - accept valid value
 */
Test(rcu_validation, max_clients_valid) {
  asciichat_error_t err = options_set_int("max_clients", 4);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for max_clients=4");

  // Verify value was set
  const options_t *opts = options_get();
  cr_assert_eq(opts->max_clients, 4, "Expected max_clients to be 4");
}

/**
 * Test: Enum validation - color_mode below valid range
 */
Test(rcu_validation, color_mode_invalid_negative) {
  asciichat_error_t err = options_set_int("color_mode", -1);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected validation error for invalid color_mode");
}

/**
 * Test: Enum validation - color_mode above valid range
 */
Test(rcu_validation, color_mode_invalid_high) {
  asciichat_error_t err = options_set_int("color_mode", 9999);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected validation error for color_mode=9999");
}

/**
 * Test: Enum validation - color_mode with valid value (0=auto)
 */
Test(rcu_validation, color_mode_valid) {
  asciichat_error_t err = options_set_int("color_mode", 0);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for valid color_mode");

  const options_t *opts = options_get();
  cr_assert_eq((int)opts->color_mode, 0, "Expected color_mode to be 0");
}

/**
 * Test: Log level enum validation - valid value (2=info)
 */
Test(rcu_validation, log_level_valid) {
  asciichat_error_t err = options_set_int("log_level", 2);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for valid log_level");

  const options_t *opts = options_get();
  cr_assert_eq((int)opts->log_level, 2, "Expected log_level to be 2");
}

// ============================================================================
// Double Field Validation Tests
// ============================================================================

/**
 * Test: Snapshot delay - valid value
 */
Test(rcu_validation, snapshot_delay_valid) {
  asciichat_error_t err = options_set_double("snapshot_delay", 1.5);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for snapshot_delay=1.5");

  const options_t *opts = options_get();
  cr_assert(fabs(opts->snapshot_delay - 1.5) < 0.001, "Expected snapshot_delay to be 1.5");
}

/**
 * Test: Microphone sensitivity - valid value
 */
Test(rcu_validation, microphone_sensitivity_valid) {
  asciichat_error_t err = options_set_double("microphone_sensitivity", 0.8);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for microphone_sensitivity=0.8");

  const options_t *opts = options_get();
  cr_assert(fabs(opts->microphone_sensitivity - 0.8) < 0.001,
            "Expected microphone_sensitivity to be 0.8");
}

/**
 * Test: Speakers volume - valid value
 */
Test(rcu_validation, speakers_volume_valid) {
  asciichat_error_t err = options_set_double("speakers_volume", 1.0);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for speakers_volume=1.0");

  const options_t *opts = options_get();
  cr_assert(fabs(opts->speakers_volume - 1.0) < 0.001, "Expected speakers_volume to be 1.0");
}

// ============================================================================
// Boolean Field Validation Tests
// ============================================================================

/**
 * Test: Boolean field - valid true
 */
Test(rcu_validation, bool_field_true) {
  asciichat_error_t err = options_set_bool("no_compress", true);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for no_compress=true");

  const options_t *opts = options_get();
  cr_assert_eq(opts->no_compress, true, "Expected no_compress to be true");
}

/**
 * Test: Boolean field - valid false
 */
Test(rcu_validation, bool_field_false) {
  asciichat_error_t err = options_set_bool("no_compress", false);
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for no_compress=false");

  const options_t *opts = options_get();
  cr_assert_eq(opts->no_compress, false, "Expected no_compress to be false");
}

// ============================================================================
// String Field Validation Tests
// ============================================================================

/**
 * Test: String field - valid address
 */
Test(rcu_validation, string_field_address) {
  asciichat_error_t err = options_set_string("address", "127.0.0.1");
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for valid address");

  const options_t *opts = options_get();
  cr_assert_str_eq(opts->address, "127.0.0.1", "Expected address to be 127.0.0.1");
}

/**
 * Test: String field - valid password
 */
Test(rcu_validation, string_field_password) {
  asciichat_error_t err = options_set_string("password", "secret123");
  cr_assert_eq(err, ASCIICHAT_OK, "Expected success for valid password");

  const options_t *opts = options_get();
  cr_assert_str_eq(opts->password, "secret123", "Expected password to be set");
}

// ============================================================================
// Error Handling Tests
// ============================================================================

/**
 * Test: options_set_int with NULL field name
 */
Test(rcu_validation, set_int_null_field) {
  asciichat_error_t err = options_set_int(NULL, 42);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected error for NULL field_name");
}

/**
 * Test: options_set_int with unknown field
 */
Test(rcu_validation, set_int_unknown_field) {
  asciichat_error_t err = options_set_int("nonexistent_field", 42);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected error for unknown field");
}

/**
 * Test: options_set_bool with NULL field name
 */
Test(rcu_validation, set_bool_null_field) {
  asciichat_error_t err = options_set_bool(NULL, true);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected error for NULL field_name");
}

/**
 * Test: options_set_string with NULL field name
 */
Test(rcu_validation, set_string_null_field) {
  asciichat_error_t err = options_set_string(NULL, "test");
  cr_assert_neq(err, ASCIICHAT_OK, "Expected error for NULL field_name");
}

/**
 * Test: options_set_string with NULL value
 */
Test(rcu_validation, set_string_null_value) {
  asciichat_error_t err = options_set_string("address", NULL);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected error for NULL value");
}

/**
 * Test: options_set_double with NULL field name
 */
Test(rcu_validation, set_double_null_field) {
  asciichat_error_t err = options_set_double(NULL, 1.5);
  cr_assert_neq(err, ASCIICHAT_OK, "Expected error for NULL field_name");
}
