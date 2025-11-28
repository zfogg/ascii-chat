# Tooling Module Reorganization and Defer Implementation

**Date:** 2025-11-14
**Status:** ✅ Complete

## Summary

Successfully reorganized the instrumentation system from `debug/` to `tooling/` structure and implemented the foundation for defer statement support in C.

## Changes Made

### 1. Directory Reorganization ✅

**Removed (deleted from git):**
- `cmake/debug/` → Moved to `cmake/tooling/`
- `lib/debug/` → Moved to `lib/tooling/source_print/`
- `src/debug/` → Moved to `src/tooling/source_print/`
- `tests/unit/debug/` → Moved to `tests/unit/tooling/`

**Created:**
```
cmake/tooling/
├── Instrumentation.cmake    # Source print instrumentation (renamed)
├── Targets.cmake             # Source print targets (renamed)
└── run_instrumentation.sh    # Source print script (renamed)

lib/tooling/
├── source_print/
│   ├── instrument_log.c      # Runtime logging
│   ├── instrument_log.h      # Runtime API
│   └── instrument_cov.c      # Coverage logging
├── self-source-print/        # (existing)
└── defer/                    # NEW
    ├── defer.h               # Defer API definitions
    └── defer.c               # Defer runtime implementation

src/tooling/
├── source_print/
│   ├── tool.cpp              # Clang transformation tool
│   └── report.c              # Log analysis tool
├── self-source-print/        # (existing)
└── defer/                    # NEW (ready for tool.cpp)

tests/unit/tooling/
├── source_print/             # (future tests)
├── self-source-print/        # (existing)
└── defer/                    # NEW
    └── test_defer.c          # Runtime tests

docs/tooling/
├── NOTES.md
├── PLAN.md
├── STATUS.md
├── FINAL_STATUS.md
├── COMPLETE_VERIFICATION.md
├── REORGANIZATION_SUMMARY.md # This file
└── defer.md                  # NEW - Defer design doc
```

### 2. CMake Variable Renaming ✅

All CMake variables now explicitly reference `source_print`:

**Before:**
- `ASCII_BUILD_WITH_INSTRUMENTATION`
- `ASCII_INSTRUMENTATION_*` (generic)

**After:**
- `ASCIICHAT_BUILD_WITH_SOURCE_PRINT_INSTRUMENTATION`
- `ascii_instrumentation_prepare()` (kept for compatibility)
- `ascii_instrumentation_finalize()` (kept for compatibility)

**New Variables:**
- `TOOLING_SOURCE_PRINT_SRCS` - Source print runtime sources
- `TOOLING_SOURCE_PRINT_REPORT_SRCS` - Report tool sources
- `TOOLING_DEFER_SRCS` - Defer runtime sources

### 3. CMake Module Structure ✅

**Main CMakeLists.txt** (line 166):
```cmake
include(${CMAKE_SOURCE_DIR}/cmake/tooling/Instrumentation.cmake)
ascii_instrumentation_prepare()
# ... library definitions ...
ascii_instrumentation_finalize()
```

**Library Modules** (cmake/targets/Libraries.cmake):
- Module 9: `ascii-chat-debug` (source_print runtime)
- Module 9b: `ascii-chat-defer` (defer runtime) **NEW**
- Module 10: `ascii-chat-network` (existing)

### 4. Defer Implementation (Phase 1) ✅

**Runtime API** (`lib/tooling/defer/defer.h`):
```c
typedef void (*ascii_defer_fn_t)(void *context);

typedef struct {
    ascii_defer_fn_t fn;
    void *context;
    size_t context_size;
} ascii_defer_action_t;

typedef struct {
    ascii_defer_action_t actions[ASCII_DEFER_MAX_ACTIONS];
    size_t count;
    bool executed;
} ascii_defer_scope_t;

void ascii_defer_scope_init(ascii_defer_scope_t *scope);
bool ascii_defer_push(ascii_defer_scope_t *scope, ascii_defer_fn_t fn,
                      const void *context, size_t context_size);
void ascii_defer_execute_all(ascii_defer_scope_t *scope);
```

**Features:**
- LIFO execution order (last deferred = first executed)
- Context capture with memory copy
- Double-execution protection
- Max 32 actions per scope (configurable)
- Memory-safe using SAFE_MALLOC

**Unit Tests** (`tests/unit/tooling/defer/test_defer.c`):
- Scope initialization
- Single/multiple defer actions
- LIFO order verification
- NULL context handling
- Max actions limit
- Double-execution protection
- Realistic file handle cleanup

### 5. Documentation Updates ✅

**Updated References:**
- `docs/debug/STATUS.md` - Changed `cmake/debug/` → `cmake/tooling/`
- `docs/debug/PLAN.md` - Updated all references
- `docs/debug/COMPLETE_VERIFICATION.md` - Updated file paths
- `docs/tooling-instrumentation.md` - Already referenced `cmake/tooling/`

**New Documentation:**
- `docs/tooling/defer.md` - Complete defer design document
  - Runtime API overview
  - Transformation strategy
  - Usage examples
  - Implementation notes
  - Testing strategy
  - Next steps

### 6. Build Verification ✅

**Build Output:**
```
[67/91] Building C object CMakeFiles/ascii-chat-debug.dir/lib/tooling/source_print/instrument_cov.c.obj
[68/91] Building C object CMakeFiles/ascii-chat-debug.dir/lib/tooling/source_print/instrument_log.c.obj
[2/3] Building C object CMakeFiles/ascii-chat-defer.dir/lib/tooling/defer/defer.c.obj
```

All modules build successfully with new structure.

## Module Organization

### Source Print (Existing - Renamed)

**Purpose:** Line-by-line execution logging using Clang transformation

**Components:**
- Runtime: `lib/tooling/source_print/`
- Tool: `src/tooling/source_print/tool.cpp`
- CMake: `cmake/tooling/Instrumentation.cmake`
- Target: `ascii-chat-debug` library

### Defer (New - Phase 1 Complete)

**Purpose:** Go/Zig-style defer statements for automatic cleanup

**Components:**
- Runtime: `lib/tooling/defer/` ✅
- Tool: `src/tooling/defer/tool.cpp` ⏸️ (not started)
- CMake: `cmake/tooling/Defer.cmake` ⏸️ (not started)
- Target: `ascii-chat-defer` library ✅

**Status:**
- ✅ Phase 1: Runtime library complete
- ⏸️ Phase 2: Clang transformation tool (not started)
- ⏸️ Phase 3: Build system integration (not started)

## Next Steps

### Immediate
1. Test the defer runtime:
   ```bash
   # Run defer tests (once Criterion is available)
   ./tests/scripts/run_tests.sh test_unit_defer
   ```

2. Commit the reorganization:
   ```bash
   git add cmake/tooling/ lib/tooling/ src/tooling/ tests/unit/tooling/ docs/tooling/
   git add cmake/targets/SourceFiles.cmake cmake/targets/Libraries.cmake
   git add CMakeLists.txt docs/debug/
   git commit -m "refactor: reorganize debug→tooling, add defer runtime

   - Move cmake/debug/ → cmake/tooling/
   - Move lib/debug/ → lib/tooling/source_print/
   - Move src/debug/ → src/tooling/source_print/
   - Rename all CMake variables to source_print-specific names
   - Add defer runtime library (Phase 1)
     - Runtime API with LIFO execution
     - Context capture and cleanup
     - Unit tests with Criterion
   - Update all documentation references
   - Verify build succeeds with new structure"
   ```

### Defer Implementation (Phase 2)

Create `src/tooling/defer/tool.cpp`:
- Use `src/tooling/source_print/tool.cpp` as reference
- Detect `defer <expression>;` syntax
- Transform to runtime API calls
- Inject cleanup at all exit points
- Start with single-argument functions
- Add multi-argument support later

### Defer Integration (Phase 3)

Create `cmake/tooling/Defer.cmake`:
- Add `ASCII_BUILD_WITH_DEFER` option
- Implement `ascii_defer_prepare()` function
- Implement `ascii_defer_finalize()` function
- Similar to Instrumentation.cmake approach

## Design Decisions

### Why Separate source_print and defer?

1. **Different purposes:**
   - source_print: Debug/tracing tool (temporary)
   - defer: Language enhancement (permanent)

2. **Different transformation complexity:**
   - source_print: Insert logging before every statement
   - defer: Complex control flow analysis and injection

3. **Independent evolution:**
   - source_print is mature and stable
   - defer is new and experimental

### Why Tooling Module?

The `tooling/` module is for **build-time transformations** using Clang libTooling:
- source_print: Statement logging instrumentation
- defer: Cleanup statement transformation
- Future: closure emulation, other language enhancements

This is distinct from `lib/debug/` which is for **runtime debugging** features.

## References

- Source Print Design: `docs/tooling-instrumentation.md`
- Defer Design: `docs/tooling/defer.md`
- CMake Integration: `cmake/tooling/Instrumentation.cmake`
- Clang libTooling: https://clang.llvm.org/docs/LibTooling.html
