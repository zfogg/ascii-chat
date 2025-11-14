# Defer Implementation Status

**Last Updated:** 2025-11-14
**Status:** Phase 1 Complete (Runtime + Macro Safety)

## ✅ Completed

### 1. Defer Runtime Library
- **Location:** `lib/tooling/defer/`
- **Files:** `defer.h`, `defer.c`
- **Features:**
  - LIFO execution (last defer = first execute)
  - Context capture with memory copy
  - Double-execution protection
  - Max 32 actions per scope (configurable)
  - Memory-safe using SAFE_MALLOC

### 2. Compile-Time Safety ✨ NEW
The `defer()` macro now **forces a compile error** if used without transformation:

```c
#include "defer.h"

void example() {
    FILE *f = fopen("test.txt", "r");
    defer(fclose(f));  // ❌ COMPILE ERROR without ASCII_BUILD_WITH_DEFER
}
```

**Error Message:**
```
error: invalid application of 'sizeof' to an incomplete type
'struct __ascii_defer_error__requires_ASCII_BUILD_WITH_DEFER_transformation'
```

**Why This Matters:**
- ✅ Prevents accidentally shipping non-functional defer code
- ✅ Forces you to run the transformation tool
- ✅ Clear error message explains what's needed
- ✅ Text editors still see valid syntax (error only at compile time)

### 3. Build System Integration
- **CMake Module:** `ascii-chat-defer` library
- **Source Files:** Listed in `cmake/targets/SourceFiles.cmake`
- **Language Support:** C++ enabled when either instrumentation type requested
- **Variables:**
  - `ASCII_BUILD_WITH_DEFER` - Enable defer transformation (future)
  - `ASCII_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION` - Enable source print (working)

### 4. Unit Tests
- **Location:** `tests/unit/tooling/defer/test_defer.c`
- **Coverage:**
  - Scope initialization
  - Single/multiple defer actions
  - LIFO order verification
  - NULL context handling
  - Max actions limit
  - Double-execution protection
  - File handle cleanup (realistic use case)

### 5. Example Code
- **Location:** `tests/unit/tooling/defer/example_defer_usage.c`
- **Examples:**
  1. Simple file cleanup
  2. Multiple resources (LIFO order)
  3. Memory cleanup
  4. Lock/unlock pattern
  5. Complex error handling
  6. Nested scopes
  7. Custom cleanup functions
- **Note:** Will not compile without transformation tool

## ⏸️ Pending (Phase 2)

### 1. Clang Transformation Tool
- **Location:** `src/tooling/defer/tool.cpp` (not created yet)
- **Responsibilities:**
  - Detect `defer(expression)` macro invocations
  - Transform to `ascii_defer_push()` runtime calls
  - Inject `ascii_defer_scope_t` at function entry
  - Inject `ascii_defer_execute_all()` at all exit points
  - Handle nested scopes correctly

### 2. CMake Build Integration
- **Location:** `cmake/tooling/Defer.cmake` (not created yet)
- **Responsibilities:**
  - Implement `ascii_defer_prepare()` function
  - Implement `ascii_defer_finalize()` function
  - Similar to `Instrumentation.cmake` for source_print
  - Generate transformed sources in `build/defer/`

### 3. End-to-End Testing
- Transform example code
- Verify deferred cleanup actually runs
- Test with multiple exit paths
- Verify LIFO execution order

## How It Works

### Normal Build (Without Transformation)
```c
#include "defer.h"

void example() {
    FILE *f = fopen("test.txt", "r");
    defer(fclose(f));  // ❌ COMPILE ERROR

    // error: invalid application of 'sizeof' to an incomplete type
    // 'struct __ascii_defer_error__requires_ASCII_BUILD_WITH_DEFER_transformation'
}
```

### With Transformation (ASCII_BUILD_WITH_DEFER=ON)
**Before transformation:**
```c
void example() {
    FILE *f = fopen("test.txt", "r");
    defer(fclose(f));

    if (!f) return;
    // ... use file ...
    return;
}
```

**After transformation (by ascii-defer-tool):**
```c
void example() {
    ascii_defer_scope_t __defer_scope_0;
    ascii_defer_scope_init(&__defer_scope_0);

    FILE *f = fopen("test.txt", "r");
    // defer() transformed to:
    {
        FILE __defer_ctx_f = f;
        ascii_defer_push(&__defer_scope_0, (ascii_defer_fn_t)fclose,
                         &__defer_ctx_f, sizeof(__defer_ctx_f));
    }

    if (!f) {
        ascii_defer_execute_all(&__defer_scope_0);
        return;
    }

    // ... use file ...

    ascii_defer_execute_all(&__defer_scope_0);
    return;
}
```

## Macro Implementation Details

### Without ASCII_BUILD_WITH_DEFER (Normal Builds)
```c
#define defer(expr) \
    do { \
        if (0) { (void)(expr); }  /* Type-check expr for editor */ \
        struct __ascii_defer_error__requires_ASCII_BUILD_WITH_DEFER_transformation; \
        (void)sizeof(struct __ascii_defer_error__requires_ASCII_BUILD_WITH_DEFER_transformation); \
    } while(0)
```

**How it works:**
1. `if (0) { (void)(expr); }` - Type-checks the expression but never executes (optimized away)
   - This makes text editors happy (sees valid syntax)
   - Validates that `expr` is a valid expression
2. Forward-declares an incomplete struct
3. Takes `sizeof()` of the incomplete struct → **COMPILE ERROR**

### With ASCII_BUILD_WITH_DEFER (Instrumented Builds)
The tool **transforms the source code before compilation**, so the macro definition is never actually used. If it is used (transformation didn't run), you get:
```c
error: invalid application of 'sizeof' to an incomplete type
'struct __ascii_defer_error__transformation_tool_did_not_run'
```

## Next Steps

1. **Implement `src/tooling/defer/tool.cpp`**
   - Use `src/tooling/source_print/tool.cpp` as reference
   - Start with simple function call transformation
   - Add multi-argument support later

2. **Create `cmake/tooling/Defer.cmake`**
   - Mirror `Instrumentation.cmake` structure
   - Add `ASCII_BUILD_WITH_DEFER` option
   - Generate transformed sources before compilation

3. **Test Transformation**
   - Build example_defer_usage.c with transformation
   - Verify cleanup actually runs
   - Test all 7 example patterns

4. **Integration**
   - Update build scripts
   - Add to CI/CD
   - Document usage in CLAUDE.md

## References

- Runtime API: `lib/tooling/defer/defer.h`
- Examples: `tests/unit/tooling/defer/example_defer_usage.c`
- Unit Tests: `tests/unit/tooling/defer/test_defer.c`
- Design Doc: `docs/tooling/defer.md`
- Source Print (reference): `cmake/tooling/Instrumentation.cmake`
