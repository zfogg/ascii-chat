/**
 * Example demonstrating defer() macro usage
 *
 * This file shows how to use defer() for automatic cleanup.
 *
 * IMPORTANT: This file will NOT compile without ASCII_BUILD_WITH_DEFER=ON
 * The defer() macro causes a compile error to prevent shipping untransformed code.
 *
 * To build this example:
 *   cmake -B build -DASCII_BUILD_WITH_DEFER=ON
 *   cmake --build build
 *
 * The ascii-defer-tool will transform defer() calls before compilation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "defer.h"

// Disable Windows deprecated warnings for this example
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

// Simple malloc wrapper for examples (standalone, no common.h dependency)
#define SAFE_MALLOC(size, type) ((type)malloc(size))

/**
 * Example 1: Simple file cleanup
 */
void example_file_cleanup(const char *path) {
    FILE *f = fopen(path, "r");
    defer(fclose(f));  // File closes automatically at any return

    if (!f) {
        printf("Failed to open file\n");
        return;  // fclose(f) runs here
    }

    char buffer[256];
    if (fgets(buffer, sizeof(buffer), f)) {
        printf("Read: %s\n", buffer);
    }

    // fclose(f) runs here too at function exit
}

/**
 * Example 2: Multiple resources (LIFO order)
 */
void example_multiple_resources(void) {
    FILE *input = fopen("input.txt", "r");
    defer(fclose(input));  // Closes second (LIFO)

    FILE *output = fopen("output.txt", "w");
    defer(fclose(output));  // Closes first (LIFO)

    if (!input || !output) {
        printf("Failed to open files\n");
        return;  // Both files close here in LIFO order
    }

    // ... copy data from input to output ...

    // Both files close here in LIFO order: output then input
}

/**
 * Example 3: Memory cleanup
 */
void example_memory_cleanup(size_t size) {
    uint8_t *buffer = SAFE_MALLOC(size, uint8_t*);
    defer(free(buffer));  // Memory freed automatically

    if (!buffer) {
        printf("Allocation failed\n");
        return;  // free(buffer) runs here (though buffer is NULL, safe)
    }

    // ... use buffer ...

    // free(buffer) runs here
}

/**
 * Example 4: Lock/unlock pattern
 */
typedef struct {
    int locked;
} mutex_t;

void mutex_lock(mutex_t *m) { m->locked = 1; }
void mutex_unlock(mutex_t *m) { m->locked = 0; }

void example_critical_section(mutex_t *mtx) {
    mutex_lock(mtx);
    defer(mutex_unlock(mtx));  // Unlock happens automatically

    // ... critical section code ...

    // mutex_unlock runs here at function exit
}

/**
 * Example 5: Complex error handling
 */
int example_error_handling(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        return -1;  // No defer registered yet
    }
    defer(fclose(f));  // Close on any error path

    uint8_t *buffer = SAFE_MALLOC(4096, uint8_t*);
    if (!buffer) {
        return -1;  // fclose(f) runs here
    }
    defer(free(buffer));  // Free on any error path

    size_t bytes_read = fread(buffer, 1, 4096, f);
    if (bytes_read == 0) {
        return -1;  // Both free(buffer) and fclose(f) run here (LIFO)
    }

    // ... process buffer ...

    return 0;  // Both free(buffer) and fclose(f) run here (LIFO)
}

/**
 * Example 6: Nested scopes
 */
void example_nested_scopes(void) {
    FILE *outer = fopen("outer.txt", "r");
    defer(fclose(outer));  // Closes at function exit

    {
        FILE *inner = fopen("inner.txt", "r");
        defer(fclose(inner));  // Closes at block exit

        if (inner) {
            // ... use inner file ...
        }
        // fclose(inner) runs here (block exit)
    }

    // ... continue with outer file ...

    // fclose(outer) runs here (function exit)
}

/**
 * Example 7: Custom cleanup functions
 */
typedef struct {
    int *data;
    size_t size;
} resource_t;

void resource_cleanup(resource_t *res) {
    if (res->data) {
        free(res->data);
        res->data = NULL;
    }
}

void example_custom_cleanup(void) {
    resource_t res = {
        .data = SAFE_MALLOC(100 * sizeof(int), int*),
        .size = 100
    };
    defer(resource_cleanup(&res));  // Custom cleanup function

    if (!res.data) {
        return;  // resource_cleanup runs here
    }

    // ... use resource ...

    // resource_cleanup runs here
}

/**
 * Main function demonstrating all examples
 */
int main(void) {
    printf("Defer Usage Examples\n");
    printf("====================\n\n");

    printf("Note: In normal builds, defer() is a no-op.\n");
    printf("With ASCII_BUILD_WITH_DEFER, defer() is transformed to runtime calls.\n\n");

    // These examples won't actually work in normal builds
    // because defer() expands to a no-op
    // But they compile cleanly and show the syntax!

    printf("Example 1: File cleanup\n");
    example_file_cleanup("test.txt");

    printf("Example 2: Multiple resources\n");
    example_multiple_resources();

    printf("Example 3: Memory cleanup\n");
    example_memory_cleanup(1024);

    printf("Example 4: Lock/unlock\n");
    mutex_t mtx = {0};
    example_critical_section(&mtx);

    printf("Example 5: Error handling\n");
    int result = example_error_handling("data.bin");
    printf("Result: %d\n", result);

    printf("Example 6: Nested scopes\n");
    example_nested_scopes();

    printf("Example 7: Custom cleanup\n");
    example_custom_cleanup();

    printf("\nAll examples completed!\n");
    return 0;
}
