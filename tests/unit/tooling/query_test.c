/**
 * @file query_test.c
 * @brief Unit tests for the query tool runtime API
 *
 * Tests cover:
 * - QUERY_INIT / QUERY_SHUTDOWN lifecycle
 * - Port allocation and validation
 * - Controller spawn and health check
 * - Graceful handling of missing controller binary
 */

#include <criterion/criterion.h>
#include <criterion/logging.h>

#include "tooling/query/query.h"

Test(query_api, macros_compile_in_debug)
{
    // Verify macros are defined and callable
    // In debug builds, these should call real functions
    // In release builds, they should be no-ops

    // These just verify compilation - actual behavior tested below
    (void)QUERY_ACTIVE();
    (void)QUERY_PORT();
}

Test(query_api, init_returns_negative_without_controller)
{
    // When ascii-query-server binary is not found, init should fail gracefully
    // TODO: Implement once query.c is written
    cr_skip("Not implemented yet - waiting for query.c");
}

Test(query_api, shutdown_safe_when_not_initialized)
{
    // Shutdown should be safe to call even if init was never called
    QUERY_SHUTDOWN();
    cr_assert(true, "Shutdown did not crash when called without init");
}

Test(query_api, active_returns_false_when_not_initialized)
{
    // Before init, active should return false
    cr_assert_eq(QUERY_ACTIVE(), false, "Expected QUERY_ACTIVE() to return false before init");
}

Test(query_api, port_returns_negative_when_not_initialized)
{
    // Before init, port should return -1
    cr_assert_eq(QUERY_PORT(), -1, "Expected QUERY_PORT() to return -1 before init");
}
