/**
 * @file defer_minimal_test.c
 * @brief Unit test for defer() runtime library
 *
 * Tests the defer runtime directly without transformation tool.
 * Verifies LIFO cleanup ordering and memory management.
 */
#include "tooling/defer/defer.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

static int cleanup_order[10];
static int cleanup_count = 0;

static void cleanup_1(void *ctx) {
    (void)ctx;
    cleanup_order[cleanup_count++] = 1;
}

static void cleanup_2(void *ctx) {
    (void)ctx;
    cleanup_order[cleanup_count++] = 2;
}

static void cleanup_3(void *ctx) {
    (void)ctx;
    cleanup_order[cleanup_count++] = 3;
}

static void free_ptr(void *ctx) {
    char **ptr = (char **)ctx;
    if (ptr && *ptr) {
        free(*ptr);
        *ptr = NULL;
        cleanup_order[cleanup_count++] = 100;
    }
}

// Test 1: LIFO ordering
void test_lifo_order(void) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    cleanup_count = 0;

    void *fn1 = (void*)(cleanup_1);
    void *ctx1 = NULL;
    ascii_defer_push(&scope, (ascii_defer_fn_t)fn1, &ctx1, sizeof(ctx1));

    void *fn2 = (void*)(cleanup_2);
    void *ctx2 = NULL;
    ascii_defer_push(&scope, (ascii_defer_fn_t)fn2, &ctx2, sizeof(ctx2));

    void *fn3 = (void*)(cleanup_3);
    void *ctx3 = NULL;
    ascii_defer_push(&scope, (ascii_defer_fn_t)fn3, &ctx3, sizeof(ctx3));

    assert(cleanup_count == 0);
    ascii_defer_execute_all(&scope);

    assert(cleanup_count == 3);
    assert(cleanup_order[0] == 3);  // Last pushed, first executed
    assert(cleanup_order[1] == 2);
    assert(cleanup_order[2] == 1);  // First pushed, last executed
}

// Test 2: Memory cleanup with pointer context
void test_memory_cleanup(void) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    cleanup_count = 0;

    char *str = malloc(100);
    assert(str != NULL);

    void *fn = (void*)(free_ptr);
    ascii_defer_push(&scope, (ascii_defer_fn_t)fn, &str, sizeof(str));

    assert(str != NULL);
    ascii_defer_execute_all(&scope);

    assert(cleanup_count == 1);
    assert(cleanup_order[0] == 100);
    // Note: str itself is not nullified because defer copies the context
    // The memory was freed, but the local pointer still holds the old address
    // This is expected - the defer runtime copies context for safety
}

// Test 3: Double execution protection
void test_double_execution(void) {
    ascii_defer_scope_t scope;
    ascii_defer_scope_init(&scope);

    cleanup_count = 0;

    void *fn = (void*)(cleanup_1);
    void *ctx = NULL;
    ascii_defer_push(&scope, (ascii_defer_fn_t)fn, &ctx, sizeof(ctx));

    ascii_defer_execute_all(&scope);
    assert(cleanup_count == 1);

    // Second call should be no-op
    ascii_defer_execute_all(&scope);
    assert(cleanup_count == 1);  // Still 1, not 2
}

int main(void) {
    printf("Test 1: LIFO ordering... ");
    test_lifo_order();
    printf("PASS\n");

    printf("Test 2: Memory cleanup... ");
    test_memory_cleanup();
    printf("PASS\n");

    printf("Test 3: Double execution protection... ");
    test_double_execution();
    printf("PASS\n");

    printf("\nAll defer runtime tests PASSED!\n");
    return 0;
}
