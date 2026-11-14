/**
 * @file query_test.c
 * @brief Unit tests for the query tool runtime API
 *
 * Tests cover:
 * - QUERY_INIT / QUERY_SHUTDOWN lifecycle
 * - Port allocation and validation
 * - Graceful handling of missing controller binary
 * - State transitions and error handling
 *
 * Note: Tests that require actual LLDB attachment are in integration tests.
 * These unit tests focus on the C API and macro behavior.
 */

#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <ascii-chat/tests/catch2_test_helpers.hpp>
#include <stdlib.h>

#include <ascii-chat/tooling/query/query.h>
#include <ascii-chat/tests/logging.h>
#include <libplatform/system.h>

#define unsetenv platform_unsetenv

// ============================================================================
// Basic API Tests
// ============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("query_api/macros_compile_in_debug", "[query_api]") {
  // Verify macros are defined and callable
  // In debug builds, these should call real functions
  // In release builds, they should be no-ops

  // These just verify compilation - actual behavior tested below
  (void)QUERY_ACTIVE();
  (void)QUERY_PORT();
}

TEST_CASE("query_api/shutdown_safe_when_not_initialized", "[query_api]") {
  // Shutdown should be safe to call even if init was never called
  QUERY_SHUTDOWN();
  ASCIICHAT_REQUIRE(true, "Shutdown did not crash when called without init");
}

TEST_CASE("query_api/shutdown_multiple_calls_safe", "[query_api]") {
  // Multiple shutdown calls should be safe
  QUERY_SHUTDOWN();
  QUERY_SHUTDOWN();
  QUERY_SHUTDOWN();
  ASCIICHAT_REQUIRE(true, "Multiple shutdown calls did not crash");
}

TEST_CASE("query_api/active_returns_false_when_not_initialized", "[query_api]") {
  // Before init, active should return false
  ASCIICHAT_REQUIRE_EQ(QUERY_ACTIVE(), false, "Expected QUERY_ACTIVE() to return false before init");
}

TEST_CASE("query_api/port_returns_negative_when_not_initialized", "[query_api]") {
  // Before init, port should return -1
  ASCIICHAT_REQUIRE_EQ(QUERY_PORT(), -1, "Expected QUERY_PORT() to return -1 before init");
}

// ============================================================================
// Init failure cases (no valid controller binary)
// ============================================================================

TEST_CASE("query_api/init_returns_negative_without_controller", "[query_api]") {
  // When ascii-query-server binary is not found, init should fail gracefully
  // Clear the environment variable to ensure it doesn't find a controller
  unsetenv("ASCIICHAT_QUERY_SERVER");

  // Use an unlikely port to avoid conflicts
  int result = QUERY_INIT(59999);

  // Should fail because controller binary not in standard paths
  // (unless actually present in .deps-cache, which is fine - it would pass)
  if (result < 0) {
    ASCIICHAT_REQUIRE_EQ(result, -1, "Expected -1 on init failure");
    ASCIICHAT_REQUIRE_EQ(QUERY_ACTIVE(), false, "Should not be active after failed init");
    ASCIICHAT_REQUIRE_EQ(QUERY_PORT(), -1, "Port should be -1 after failed init");
  } else {
    // Controller was found and initialized - clean up
    INFO("Controller binary was found and initialized");
    QUERY_SHUTDOWN();
  }
}

TEST_CASE("query_api/init_with_invalid_port_zero", "[query_api]") {
  // Port 0 is technically valid (OS assigns port), but controller may reject it
  // This tests the boundary condition
  unsetenv("ASCIICHAT_QUERY_SERVER");

  int result = QUERY_INIT(0);

  // Regardless of outcome, ensure clean state
  QUERY_SHUTDOWN();

  // Just verify it didn't crash - port 0 behavior depends on controller
  ASCIICHAT_REQUIRE(true, "Init with port 0 did not crash");
}

// ============================================================================
// State consistency tests
// ============================================================================

TEST_CASE("query_api/state_consistent_after_shutdown", "[query_api]") {
  // Ensure state is reset properly after shutdown
  QUERY_SHUTDOWN();

  ASCIICHAT_REQUIRE_EQ(QUERY_ACTIVE(), false, "Should not be active after shutdown");
  ASCIICHAT_REQUIRE_EQ(QUERY_PORT(), -1, "Port should be -1 after shutdown");
}

TEST_CASE("query_api/active_reflects_port_state", "[query_api]") {
  // QUERY_ACTIVE and QUERY_PORT should be consistent
  bool active = QUERY_ACTIVE();
  int port = QUERY_PORT();

  if (active) {
    ASCIICHAT_REQUIRE(port > 0, "If active, port should be positive");
  } else {
    ASCIICHAT_REQUIRE_EQ(port, -1, "If not active, port should be -1");
  }
}

// ============================================================================
// Release build macro tests (compile-time verification)
// ============================================================================

#ifdef NDEBUG
TEST_CASE("query_api_release/macros_are_noops_in_release", "[query_api_release]") {
  // In release builds, macros should be no-ops that don't call functions
  // QUERY_INIT should return -1
  int result = QUERY_INIT(9999);
  ASCIICHAT_REQUIRE_EQ(result, -1, "QUERY_INIT should return -1 in release builds");

  // QUERY_ACTIVE should return false
  ASCIICHAT_REQUIRE_EQ(QUERY_ACTIVE(), false, "QUERY_ACTIVE should return false in release builds");

  // QUERY_PORT should return -1
  ASCIICHAT_REQUIRE_EQ(QUERY_PORT(), -1, "QUERY_PORT should return -1 in release builds");

  // QUERY_SHUTDOWN should be safe
  QUERY_SHUTDOWN();
  ASCIICHAT_REQUIRE(true, "QUERY_SHUTDOWN is safe no-op in release builds");
}
#endif
