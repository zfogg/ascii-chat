#include <catch2/catch_test_macros.hpp>
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

#include <stdlib.h>

#include <ascii-chat/tooling/query/query.h>
#include <ascii-chat/tests/logging.h>
#include <libplatform/system.h>

#define unsetenv platform_unsetenv

// ============================================================================
// Basic API Tests
// ============================================================================
// Use verbose logging with debug level enabled and stdout/stderr not disabled
TEST_CASE("query_api/macros_compile_in_debug", "[query_api] [unit]") {
  // Verify macros are defined and callable
  // In debug builds, these should call real functions
  // In release builds, they should be no-ops

  // These just verify compilation - actual behavior tested below
  (void)QUERY_ACTIVE();
  (void)QUERY_PORT();
}

TEST_CASE("query_api/shutdown_safe_when_not_initialized", "[query_api] [unit]") {
  // Shutdown should be safe to call even if init was never called
  QUERY_SHUTDOWN();
  REQUIRE(true);
}

TEST_CASE("query_api/shutdown_multiple_calls_safe", "[query_api] [unit]") {
  // Multiple shutdown calls should be safe
  QUERY_SHUTDOWN();
  QUERY_SHUTDOWN();
  QUERY_SHUTDOWN();
  REQUIRE(true);
}

TEST_CASE("query_api/active_returns_false_when_not_initialized", "[query_api] [unit]") {
  // Before init, active should return false
  REQUIRE((QUERY_ACTIVE()) == (false));
}

TEST_CASE("query_api/port_returns_negative_when_not_initialized", "[query_api] [unit]") {
  // Before init, port should return -1
  REQUIRE((QUERY_PORT()) == (-1));
}

// ============================================================================
// Init failure cases (no valid controller binary)
// ============================================================================

TEST_CASE("query_api/init_returns_negative_without_controller", "[query_api] [unit]") {
  // When ascii-query-server binary is not found, init should fail gracefully
  // Clear the environment variable to ensure it doesn't find a controller
  unsetenv("ASCIICHAT_QUERY_SERVER");

  // Use an unlikely port to avoid conflicts
  int result = QUERY_INIT(59999);

  // Should fail because controller binary not in standard paths
  // (unless actually present in .deps-cache, which is fine - it would pass)
  if (result < 0) {
    REQUIRE((result) == (-1));
    REQUIRE((QUERY_ACTIVE()) == (false));
    REQUIRE((QUERY_PORT()) == (-1));
  } else {
    // Controller was found and initialized - clean up
    INFO("Controller binary was found and initialized");
    QUERY_SHUTDOWN();
  }
}

TEST_CASE("query_api/init_with_invalid_port_zero", "[query_api] [unit]") {
  // Port 0 is technically valid (OS assigns port), but controller may reject it
  // This tests the boundary condition
  unsetenv("ASCIICHAT_QUERY_SERVER");

  int result = QUERY_INIT(0);

  // Regardless of outcome, ensure clean state
  QUERY_SHUTDOWN();

  // Just verify it didn't crash - port 0 behavior depends on controller
  REQUIRE(true);
}

// ============================================================================
// State consistency tests
// ============================================================================

TEST_CASE("query_api/state_consistent_after_shutdown", "[query_api] [unit]") {
  // Ensure state is reset properly after shutdown
  QUERY_SHUTDOWN();

  REQUIRE((QUERY_ACTIVE()) == (false));
  REQUIRE((QUERY_PORT()) == (-1));
}

TEST_CASE("query_api/active_reflects_port_state", "[query_api] [unit]") {
  // QUERY_ACTIVE and QUERY_PORT should be consistent
  bool active = QUERY_ACTIVE();
  int port = QUERY_PORT();

  if (active) {
    REQUIRE(port > 0);
  } else {
    REQUIRE((port) == (-1));
  }
}

// ============================================================================
// Release build macro tests (compile-time verification)
// ============================================================================

#ifdef NDEBUG
TEST_CASE("query_api_release/macros_are_noops_in_release", "[query_api_release] [unit]") {
  // In release builds, macros should be no-ops that don't call functions
  // QUERY_INIT should return -1
  int result = QUERY_INIT(9999);
  REQUIRE((result) == (-1));

  // QUERY_ACTIVE should return false
  REQUIRE((QUERY_ACTIVE()) == (false));

  // QUERY_PORT should return -1
  REQUIRE((QUERY_PORT()) == (-1));

  // QUERY_SHUTDOWN should be safe
  QUERY_SHUTDOWN();
  REQUIRE(true);
}
#endif
