# Defer Implementation Status

**Last Updated:** 2025-11-14
**Status:** Phase 2 Complete (Runtime + Transformation Tool + Build Integration)

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
    defer(fclose(f));  // ❌ COMPILE ERROR without ASCIICHAT_BUILD_WITH_DEFER
}
```

**Error Message:**
```
error: invalid application of 'sizeof' to an incomplete type
'struct __ascii_defer_error__requires_ASCIICHAT_BUILD_WITH_DEFER_transformation'
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
  - `ASCIICHAT_BUILD_WITH_DEFER` - Enable defer transformation (future)
  - `ASCIICHAT_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION` - Enable source print (working)

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

### 6. Clang Transformation Tool ✨ NEW
- **Location:** `src/tooling/defer/tool.cpp`
- **Features:**
  - Detects `defer(expression)` macro invocations in AST
  - Transforms to `ascii_defer_push()` runtime calls
  - Injects `ascii_defer_scope_t` at function entry
  - Injects `ascii_defer_execute_all()` at all return statements and function end
  - Built as `ascii-instr-defer` executable
  - Uses Clang LibTooling for source transformation

### 7. CMake Build Integration ✨ NEW
- **Location:** `cmake/tooling/Defer.cmake`
- **Features:**
  - `ascii_defer_prepare()` - configures transformation
  - `ascii_defer_finalize()` - applies transformed sources to targets
  - Similar to `Instrumentation.cmake` for source_print
  - Generates transformed sources in `build/defer_transformed/`
  - Parallel transformation with xargs for performance
  - Incremental builds (only transforms changed files)

### 8. Build System Support ✨ NEW
- **Updated files:**
  - `cmake/tooling/Targets.cmake` - adds `ascii-instr-defer` target
  - `cmake/tooling/run_defer.sh` - bash script for running transformation
  - `CMakeLists.txt` - integrates defer transformation into build
- **CMake variable:** `ASCIICHAT_BUILD_WITH_DEFER=ON` to enable

## ⏸️ Pending (Phase 3)

### 1. End-to-End Testing
- Transform example code with `-DASCIICHAT_BUILD_WITH_DEFER=ON`
- Verify deferred cleanup actually runs in transformed code
- Test all 7 example patterns from `example_defer_usage.c`
- Verify LIFO execution order
- Test with multiple exit paths

## How It Works

### Normal Build (Without Transformation)
```c
#include "defer.h"

void example() {
    FILE *f = fopen("test.txt", "r");
    defer(fclose(f));  // ❌ COMPILE ERROR

    // error: invalid application of 'sizeof' to an incomplete type
    // 'struct __ascii_defer_error__requires_ASCIICHAT_BUILD_WITH_DEFER_transformation'
}
```

### With Transformation (ASCIICHAT_BUILD_WITH_DEFER=ON)
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

### Without ASCIICHAT_BUILD_WITH_DEFER (Normal Builds)
```c
#define defer(expr) \
    do { \
        if (0) { (void)(expr); }  /* Type-check expr for editor */ \
        struct __ascii_defer_error__requires_ASCIICHAT_BUILD_WITH_DEFER_transformation; \
        (void)sizeof(struct __ascii_defer_error__requires_ASCIICHAT_BUILD_WITH_DEFER_transformation); \
    } while(0)
```

**How it works:**
1. `if (0) { (void)(expr); }` - Type-checks the expression but never executes (optimized away)
   - This makes text editors happy (sees valid syntax)
   - Validates that `expr` is a valid expression
2. Forward-declares an incomplete struct
3. Takes `sizeof()` of the incomplete struct → **COMPILE ERROR**

### With ASCIICHAT_BUILD_WITH_DEFER (Instrumented Builds)
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
   - Add `ASCIICHAT_BUILD_WITH_DEFER` option
   - Generate transformed sources before compilation

3. **Test Transformation**
   - Build example_defer_usage.c with transformation
   - Verify cleanup actually runs
   - Test all 7 example patterns

4. **Integration**
   - Update build scripts
   - Add to CI/CD
   - Document usage in CLAUDE.md

## How to Build With Defer Transformation

### Basic Usage

```bash
# Build with defer transformation enabled
cmake -B build -DASCIICHAT_BUILD_WITH_DEFER=ON
cmake --build build
```

### With Tooling Library (Recommended)

To avoid rebuilding the tooling when application code changes:

```bash
# Step 1: Build library first
cmake -B build
cmake --build build --target ascii-chat-static

# Step 2: Copy library to safe location
cp build/lib/libasciichat.a build/libasciichat_for_tooling.a

# Step 3: Reconfigure with defer transformation + library path
cmake -B build \
  -DASCIICHAT_BUILD_WITH_DEFER=ON \
  -DASCII_TOOLING_LIBRARY_PATH="build/libasciichat_for_tooling.a"

# Step 4: Build with transformation
cmake --build build
```

See `docs/tooling/BUILDING_TOOLING.md` for complete details.

### What Gets Transformed

The defer transformation tool processes all `.c`, `.m`, and `.mm` files except:
- `lib/tooling/` (tooling code itself)
- `lib/debug/` (debug infrastructure)
- `lib/platform/*/{system,mutex,thread}.c` (low-level platform code)
- SIMD intrinsics files
- `lib/tooling/defer/defer.c` (defer runtime)

Transformed sources are written to `build/defer_transformed/` and used automatically.

## References

- Runtime API: `lib/tooling/defer/defer.h`
- Examples: `tests/unit/tooling/defer/example_defer_usage.c`
- Unit Tests: `tests/unit/tooling/defer/test_defer.c`
- Design Doc: `docs/tooling/defer.md`
- Transformation Tool: `src/tooling/defer/tool.cpp`
- Build Integration: `cmake/tooling/Defer.cmake`
- Source Print (reference): `cmake/tooling/Instrumentation.cmake`
