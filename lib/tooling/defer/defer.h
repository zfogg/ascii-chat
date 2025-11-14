#ifndef ASCII_TOOLING_DEFER_H
#define ASCII_TOOLING_DEFER_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * ASCII-Chat Defer Implementation
 *
 * Provides Go/Zig-style defer statements for C using clang tooling transformation.
 *
 * Usage (before transformation):
 *   #include "defer.h"
 *
 *   void func() {
 *       FILE *f = fopen("test.txt", "r");
 *       defer(fclose(f));  // Cleanup happens automatically at scope exit
 *       // ... rest of function ...
 *   }
 *
 * The defer() macro expands to valid C code that does nothing in normal builds,
 * but is detected and transformed by the ascii-defer-tool during instrumented builds.
 * This ensures no syntax errors in text editors or IDEs.
 */

// Maximum number of deferred actions per scope
#ifndef ASCII_DEFER_MAX_ACTIONS
#define ASCII_DEFER_MAX_ACTIONS 32
#endif

// Defer action function signature
typedef void (*ascii_defer_fn_t)(void *context);

// Storage for a single deferred action
typedef struct {
    ascii_defer_fn_t fn;           // Function to call
    void *context;                  // Context data (heap allocated)
    size_t context_size;            // Size of context data
} ascii_defer_action_t;

// Defer scope - tracks all deferred actions for a scope
typedef struct {
    ascii_defer_action_t actions[ASCII_DEFER_MAX_ACTIONS];
    size_t count;                   // Number of registered actions
    bool executed;                  // Whether cleanup has been executed
} ascii_defer_scope_t;

/**
 * Initialize a defer scope
 *
 * This is automatically injected at the beginning of functions with defer statements.
 */
void ascii_defer_scope_init(ascii_defer_scope_t *scope);

/**
 * Register a deferred action
 *
 * @param scope The defer scope
 * @param fn Function to call at scope exit
 * @param context Pointer to context data to capture
 * @param context_size Size of context data
 * @return true if registered successfully, false if scope is full
 *
 * The context data is copied and stored internally. The function will be called
 * with a pointer to this copy when the scope exits.
 */
bool ascii_defer_push(ascii_defer_scope_t *scope, ascii_defer_fn_t fn,
                       const void *context, size_t context_size);

/**
 * Execute all deferred actions in LIFO order
 *
 * This is automatically injected at all scope exit points (return, end of block, etc.)
 *
 * @param scope The defer scope
 */
void ascii_defer_execute_all(ascii_defer_scope_t *scope);

/**
 * Defer Macro - User-facing API
 *
 * Usage: defer(expression);
 *
 * IMPORTANT: defer() requires ASCII_BUILD_WITH_DEFER=ON and transformation by ascii-defer-tool.
 * Normal builds will fail with a compiler error to prevent shipping untransformed defer code.
 *
 * In normal builds (without ASCII_BUILD_WITH_DEFER):
 *   - Causes a compiler error: "defer() requires ASCII_BUILD_WITH_DEFER transformation"
 *   - Prevents accidentally shipping non-functional defer code
 *   - Text editors still see valid syntax (error only at compile time)
 *
 * In instrumented builds (with ASCII_BUILD_WITH_DEFER):
 *   - The ascii-defer-tool detects defer() macro invocations
 *   - Transforms them into ascii_defer_push() calls with proper scope handling
 *   - Injects ascii_defer_execute_all() at all function exit points
 *   - Transformation happens BEFORE compilation, so macro is never actually used
 *
 * Example:
 *   void process_file(const char *path) {
 *       FILE *f = fopen(path, "r");
 *       defer(fclose(f));  // Cleanup happens at any return or scope exit
 *
 *       if (!f) return;    // fclose(f) runs here
 *       // ... process file ...
 *       return;            // fclose(f) runs here too
 *   }
 */
#ifndef ASCII_BUILD_WITH_DEFER
    // Normal build: Cause a COMPILE-TIME error to prevent shipping untransformed defer code
    // Uses an incomplete type to force a compile error (not just link error)
    #define defer(expr) \
        do { \
            /* Type-check the expression but don't execute (editor validation) */ \
            if (0) { (void)(expr); } \
            /* COMPILE ERROR: Incomplete type cannot be used */ \
            struct __ascii_defer_error__requires_ASCII_BUILD_WITH_DEFER_transformation; \
            (void)sizeof(struct __ascii_defer_error__requires_ASCII_BUILD_WITH_DEFER_transformation); \
        } while(0)
#else
    // During transformation: The tool replaces defer() with runtime API calls
    // This definition should never actually be compiled (tool runs before compilation)
    #define defer(expr) \
        do { \
            /* If you see this error, the ascii-defer-tool didn't run properly */ \
            struct __ascii_defer_error__transformation_tool_did_not_run; \
            (void)sizeof(struct __ascii_defer_error__transformation_tool_did_not_run); \
        } while(0)
#endif

/**
 * Manual defer helper for runtime use (without transformation)
 *
 * This is for testing or when you want to use defer manually without the tool.
 */
#define ASCII_DEFER_MANUAL(scope, cleanup_fn, context_ptr, context_size) \
    ascii_defer_push((scope), (cleanup_fn), (context_ptr), (context_size))

#ifdef __cplusplus
}
#endif

#endif // ASCII_TOOLING_DEFER_H
