#include <criterion/criterion.h>
#include "defer.h"
#include <stdbool.h>
#include <string.h>

// Test helper: Simple cleanup function
static int cleanup_call_count = 0;
static int cleanup_values[ASCII_DEFER_MAX_ACTIONS];

static void test_cleanup_fn(void *ctx) {
    if (ctx) {
        int *value = (int*)ctx;
        cleanup_values[cleanup_call_count] = *value;
    } else {
        cleanup_values[cleanup_call_count] = -1;
    }
    cleanup_call_count++;
}

// Reset test state before each test
void defer_test_setup(void) {
    cleanup_call_count = 0;
    memset(cleanup_values, 0, sizeof(cleanup_values));
}

TestSuite(defer_runtime, .init = defer_test_setup);

Test(defer_runtime, scope_initialization) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    cr_assert_eq(scope.count, 0, "Scope count should be 0");
    cr_assert_eq(scope.executed, false, "Scope should not be executed");
}

Test(defer_runtime, single_defer_action) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    int value = 42;
    bool result = ascii_defer_push(&scope, test_cleanup_fn, &value, sizeof(value));

    cr_assert_eq(result, true, "Should register action successfully");
    cr_assert_eq(scope.count, 1, "Should have 1 deferred action");

    ascii_defer_execute_all(&scope);

    cr_assert_eq(cleanup_call_count, 1, "Cleanup should be called once");
    cr_assert_eq(cleanup_values[0], 42, "Cleanup should receive correct value");
    cr_assert_eq(scope.executed, true, "Scope should be marked as executed");
}

Test(defer_runtime, multiple_defer_actions_lifo_order) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    // Register 3 actions
    int value1 = 10;
    int value2 = 20;
    int value3 = 30;

    ascii_defer_push(&scope, test_cleanup_fn, &value1, sizeof(value1));
    ascii_defer_push(&scope, test_cleanup_fn, &value2, sizeof(value2));
    ascii_defer_push(&scope, test_cleanup_fn, &value3, sizeof(value3));

    cr_assert_eq(scope.count, 3, "Should have 3 deferred actions");

    ascii_defer_execute_all(&scope);

    cr_assert_eq(cleanup_call_count, 3, "All 3 cleanups should be called");

    // LIFO order: last registered (30) should be called first
    cr_assert_eq(cleanup_values[0], 30, "First cleanup should be value3 (LIFO)");
    cr_assert_eq(cleanup_values[1], 20, "Second cleanup should be value2 (LIFO)");
    cr_assert_eq(cleanup_values[2], 10, "Third cleanup should be value1 (LIFO)");
}

Test(defer_runtime, null_context) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    bool result = ascii_defer_push(&scope, test_cleanup_fn, NULL, 0);

    cr_assert_eq(result, true, "Should register action with NULL context");
    cr_assert_eq(scope.count, 1, "Should have 1 deferred action");

    ascii_defer_execute_all(&scope);

    cr_assert_eq(cleanup_call_count, 1, "Cleanup should be called");
    cr_assert_eq(cleanup_values[0], -1, "Cleanup should receive NULL context");
}

Test(defer_runtime, max_actions_limit) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    int value = 1;

    // Fill scope to maximum
    for (int i = 0; i < ASCII_DEFER_MAX_ACTIONS; i++) {
        bool result = ascii_defer_push(&scope, test_cleanup_fn, &value, sizeof(value));
        cr_assert_eq(result, true, "Should register action %d", i);
    }

    cr_assert_eq(scope.count, ASCII_DEFER_MAX_ACTIONS, "Should have max actions");

    // Try to add one more - should fail
    bool result = ascii_defer_push(&scope, test_cleanup_fn, &value, sizeof(value));
    cr_assert_eq(result, false, "Should fail to register when full");
    cr_assert_eq(scope.count, ASCII_DEFER_MAX_ACTIONS, "Count should not change");

    ascii_defer_execute_all(&scope);

    cr_assert_eq(cleanup_call_count, ASCII_DEFER_MAX_ACTIONS,
                 "All %d actions should be executed", ASCII_DEFER_MAX_ACTIONS);
}

Test(defer_runtime, double_execution_protection) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    int value = 99;
    ascii_defer_push(&scope, test_cleanup_fn, &value, sizeof(value));

    ascii_defer_execute_all(&scope);
    cr_assert_eq(cleanup_call_count, 1, "First execution should call cleanup");

    // Try to execute again - should not call cleanup again
    ascii_defer_execute_all(&scope);
    cr_assert_eq(cleanup_call_count, 1, "Second execution should not call cleanup again");
}

Test(defer_runtime, push_after_execution_fails) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    int value = 5;
    ascii_defer_push(&scope, test_cleanup_fn, &value, sizeof(value));
    ascii_defer_execute_all(&scope);

    // Try to push after execution - should fail
    bool result = ascii_defer_push(&scope, test_cleanup_fn, &value, sizeof(value));
    cr_assert_eq(result, false, "Should fail to push after execution");
}

// Test with file handle cleanup (realistic use case)
Test(defer_runtime, file_handle_cleanup) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    // Create a temp file
    FILE *f = tmpfile();
    cr_assert_not_null(f, "Should create temp file");

    // Register defer to close it
    ascii_defer_push(&scope, (ascii_defer_fn_t)fclose, &f, sizeof(f));

    // File is still open here
    fprintf(f, "test data");
    fflush(f);

    // Execute deferred cleanup
    ascii_defer_execute_all(&scope);

    // File should be closed now (can't verify directly, but no crash is good)
    cr_assert_eq(scope.executed, true, "Cleanup should be executed");
}
