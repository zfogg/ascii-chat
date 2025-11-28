# Defer Statements for C - Design and Implementation

## Overview

This module implements Go/Zig-style `defer` statements for C using Clang libTooling transformation. Defer allows cleanup code to run automatically at scope exit, regardless of the exit path (return, goto, end of block).

## Status

**Phase 1: Runtime Library** ✅ **COMPLETE**
- [x] `lib/tooling/defer/defer.h` - API definitions
- [x] `lib/tooling/defer/defer.c` - Runtime implementation
- [x] `tests/unit/tooling/defer/test_defer.c` - Comprehensive unit tests
- [x] CMake integration in `cmake/targets/SourceFiles.cmake` and `cmake/targets/Libraries.cmake`
- [x] Module builds successfully as `ascii-chat-defer` library

**Phase 2: Clang Transformation Tool** ⏸️ **NOT STARTED**
- [ ] `src/tooling/defer/tool.cpp` - Clang libTooling AST rewriter
- [ ] Detect `defer <expression>;` statements
- [ ] Transform to runtime API calls
- [ ] Inject cleanup at all exit points
- [ ] Integration tests for transformation

**Phase 3: Build System Integration** ⏸️ **NOT STARTED**
- [ ] `cmake/tooling/Defer.cmake` - Build-time transformation integration
- [ ] Similar to `Instrumentation.cmake` but for defer
- [ ] Option: `ASCIICHAT_BUILD_WITH_DEFER` to enable transformation

## Architecture

### Runtime API (lib/tooling/defer/)

The runtime provides a stack-based defer scope that executes cleanup actions in LIFO order:

```c
// Initialize scope (auto-injected at function start)
ascii_defer_scope_t scope;
ascii_defer_scope_init(&scope);

// Register deferred action (transformed from `defer expr;`)
int value = 42;
ascii_defer_push(&scope, cleanup_fn, &value, sizeof(value));

// Execute all deferred actions (auto-injected at scope exit)
ascii_defer_execute_all(&scope);
```

**Key Features:**
- **LIFO execution**: Last deferred action runs first (like Go/Zig)
- **Context capture**: Copies context data at registration time
- **Double-execution protection**: Prevents re-running cleanup on multiple exits
- **Capacity limits**: `ASCII_DEFER_MAX_ACTIONS` (default 32) per scope
- **Memory safe**: Uses `SAFE_MALLOC` for context allocation

### Transformation Strategy (src/tooling/defer/)

The Clang tool will transform source code as follows:

**Original Code:**
```c
void process_file(const char *path) {
    FILE *f = fopen(path, "r");
    defer fclose(f);  // Cleanup happens automatically

    if (!f) return;   // fclose runs here

    // ... process file ...

    return;  // fclose runs here too
}
```

**Transformed Code:**
```c
void process_file(const char *path) {
    ascii_defer_scope_t __ascii_defer_scope_0;
    ascii_defer_scope_init(&__ascii_defer_scope_0);

    FILE *f = fopen(path, "r");
    ascii_defer_push(&__ascii_defer_scope_0, (ascii_defer_fn_t)fclose, &f, sizeof(f));

    if (!f) {
        ascii_defer_execute_all(&__ascii_defer_scope_0);
        return;
    }

    // ... process file ...

    ascii_defer_execute_all(&__ascii_defer_scope_0);
    return;
}
```

**Transformation Rules:**
1. **Scope initialization**: Inject `ascii_defer_scope_t` variable at function entry
2. **Defer detection**: Match `defer <expression>;` statements
3. **Action registration**: Replace defer with `ascii_defer_push()` call
4. **Exit point injection**: Insert `ascii_defer_execute_all()` before:
   - Every `return` statement
   - End of compound statements containing defers
   - Before `goto` targets outside the defer scope
5. **Context capture**: Extract variables from defer expression and capture them

### Build Integration (cmake/tooling/Defer.cmake)

Similar to source_print instrumentation, but for defer transformation:

```cmake
option(ASCIICHAT_BUILD_WITH_DEFER "Enable defer statement transformation" OFF)

function(ascii_defer_prepare)
    # Transform sources with defer statements
    # Generate transformed tree in build/defer/
    # Replace source lists with transformed versions
endfunction()

function(ascii_defer_finalize)
    # Link ascii-chat-defer runtime into targets
endfunction()
```

## Usage Examples

### Basic Cleanup
```c
void example() {
    char *buffer = malloc(1024);
    defer free(buffer);

    // ... use buffer ...
    // free(buffer) happens automatically
}
```

### Multiple Resources (LIFO order)
```c
void multi_resource() {
    FILE *f1 = fopen("file1.txt", "r");
    defer fclose(f1);  // Runs second (LIFO)

    FILE *f2 = fopen("file2.txt", "w");
    defer fclose(f2);  // Runs first (LIFO)

    // ... use files ...
    // Order: fclose(f2) then fclose(f1)
}
```

### Lock/Unlock Pattern
```c
void critical_section(mutex_t *mtx) {
    mutex_lock(mtx);
    defer mutex_unlock(mtx);

    // ... critical section code ...
    // mutex_unlock runs automatically on any exit path
}
```

### Error Handling
```c
asciichat_error_t process_data(void) {
    int fd = open("data.bin", O_RDONLY);
    if (fd < 0) {
        return SET_ERRNO_SYS(ERROR_CONFIG, "Failed to open file");
    }
    defer close(fd);  // Cleanup on any return path

    uint8_t *buffer = SAFE_MALLOC(4096, uint8_t*);
    defer free(buffer);

    if (read(fd, buffer, 4096) < 0) {
        return SET_ERRNO_SYS(ERROR_OPERATION_FAILED, "Read failed");
        // Both close(fd) and free(buffer) run here
    }

    return ASCIICHAT_OK;
    // Both close(fd) and free(buffer) run here too
}
```

## Implementation Notes

### Context Capture Complexity

The transformation tool needs to handle different expression types:

1. **Simple function calls**: `defer fclose(file);`
   - Extract argument `file`
   - Capture by value: `ascii_defer_push(&scope, (ascii_defer_fn_t)fclose, &file, sizeof(file))`

2. **Expressions with side effects**: `defer printf("cleanup\n");`
   - Wrap in lambda/closure (C doesn't have these natively)
   - May need to generate helper functions

3. **Multiple arguments**: `defer cleanup_resource(res, flags);`
   - Capture all arguments in a struct
   - Generate wrapper function that unpacks struct

**Simplification Strategy:**
- Phase 1: Support only single-argument function calls
- Phase 2: Add multi-argument support with struct packing
- Phase 3: Add expression wrapping for complex cases

### Scope Nesting

Defer scopes can be nested (e.g., defer in nested blocks):

```c
void nested_example() {
    ascii_defer_scope_t __defer_scope_0;  // Function level

    {
        ascii_defer_scope_t __defer_scope_1;  // Block level
        defer cleanup_inner();
        // __defer_scope_1 executes here
    }

    defer cleanup_outer();
    // __defer_scope_0 executes here
}
```

Each scope is independent and executes at its own exit points.

### Memory Overhead

Each deferred action costs:
- Function pointer: 8 bytes (x64)
- Context pointer: 8 bytes
- Context size: 8 bytes
- Context data: variable (copied)

Total per defer: ~24 bytes + context size

With `ASCII_DEFER_MAX_ACTIONS=32`: ~768 bytes + contexts per scope

## Testing Strategy

### Runtime Tests (tests/unit/tooling/defer/)

Current coverage:
- ✅ Scope initialization
- ✅ Single defer action
- ✅ Multiple actions (LIFO order verification)
- ✅ NULL context handling
- ✅ Max actions limit enforcement
- ✅ Double-execution protection
- ✅ Push-after-execution rejection
- ✅ Realistic file handle cleanup

### Transformation Tests (Future)

Will need:
- [ ] Simple function call transformation
- [ ] Multiple defer statements
- [ ] Nested scopes
- [ ] Early returns
- [ ] Goto statements
- [ ] Error handling paths

### Integration Tests (Future)

Real-world usage patterns:
- [ ] File I/O with multiple resources
- [ ] Lock/unlock with error handling
- [ ] Network socket cleanup
- [ ] Memory allocation tracking

## Comparison to Other Approaches

### vs Manual Cleanup
**Defer Advantages:**
- Cleanup code near allocation site (better readability)
- Automatic handling of all exit paths (no missed cleanup)
- LIFO order matches allocation order naturally

**Manual Disadvantages:**
- Easy to miss cleanup paths (early returns, errors)
- Cleanup far from allocation (hard to track)
- Error-prone with complex control flow

### vs Cleanup Attributes (GCC/Clang)
```c
void example() {
    __attribute__((cleanup(cleanup_fn))) int fd = open(...);
    // Cleanup runs at scope exit
}
```

**Defer Advantages:**
- More flexible (can defer any expression, not just declarations)
- Explicit control over cleanup order
- Works with any compiler after transformation

**Attribute Disadvantages:**
- Compiler-specific (non-portable without transformation)
- Tied to variable declarations only
- Less clear execution order

## Next Steps

1. **Implement Clang Transformation Tool**
   - Start with `src/tooling/defer/tool.cpp`
   - Use `src/tooling/source_print/tool.cpp` as reference
   - Focus on single-argument function calls first

2. **Create Transformation Tests**
   - Input: C code with `defer` statements
   - Expected output: Transformed code with runtime calls
   - Verify: Transformed code compiles and runs correctly

3. **Build System Integration**
   - Add `cmake/tooling/Defer.cmake`
   - Create `ASCIICHAT_BUILD_WITH_DEFER` option
   - Integration similar to source_print instrumentation

4. **Documentation**
   - User guide for writing defer statements
   - Transformation behavior documentation
   - Troubleshooting guide

5. **Performance Analysis**
   - Measure overhead vs manual cleanup
   - Optimize context capture for common cases
   - Consider inline optimizations

## References

- Go defer: https://go.dev/blog/defer-panic-and-recover
- Zig defer: https://ziglang.org/documentation/master/#defer
- Clang libTooling: https://clang.llvm.org/docs/LibTooling.html
- ascii-chat source_print instrumentation: `docs/tooling-instrumentation.md`
