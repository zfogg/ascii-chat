# macOS Homebrew LLVM Tooling Build Issue

## Problem

Building `ascii-instr-defer` and `ascii-instr-panic` tools on macOS with Homebrew LLVM 21+ fails with:
```
error: unknown type name 'size_t'
error: unknown type name 'ptrdiff_t'
error: unknown type name '__darwin_wint_t'
```

## Root Cause

Homebrew LLVM (21.1.6) bundles its own libc++ headers that use `#include_next` to find C standard library headers from the macOS SDK. The include chain is:

1. `clang/AST/ASTContext.h` (LLVM tooling header)
2. `llvm/ADT/BitmaskEnum.h`
3. `c++/v1/cassert` (Homebrew libc++)
4. `assert.h` (SDK via `#include_next`)
5. `stdlib.h` (SDK)
6. `arm/_types.h` (SDK)
7. `sys/_types/_ptrdiff_t.h` (SDK)
8. `stddef.h` (SDK - expects `size_t`/`ptrdiff_t` to already be defined)

The SDK's `arm/_types.h` expects `size_t` and `ptrdiff_t` to be defined by the compiler's `stddef.h` BEFORE it gets included. The problem is finding the wrong `stddef.h`.

## Attempts Log

### Attempt 1: `-isystem` paths with `-nostdinc++` (commit d21aced - FAILED)
**Date**: 2025-11-30
**Approach**: Add `-isystem` paths for clang resource dir and SDK headers, use `-nostdinc++` to disable automatic libc++ search
**Command produced**: `-nostdinc++ -isystem<clang_resource_dir> -isystem<sdk_include>`
**Result**: FAILED - Wrong include order, SDK headers found before clang builtins

### Attempt 2: Remove `-nostdinc++`, use default search (commit 242987d - FAILED)
**Date**: 2025-11-30
**Approach**: Let clang use its default header search order, only add LLVM include path
**Result**: FAILED - Same issue, `-stdlib=libc++` uses Homebrew's libc++ which does `#include_next` to SDK

### Attempt 3: Use SDK's libc++ instead of Homebrew's (commit 4fa70ae - FAILED)
**Date**: 2025-11-30
**Approach**: Use `-nostdinc++` and explicitly add SDK's libc++ path instead of Homebrew's
**Command**: `-nostdinc++ -isystem<sdk_libcxx> -isysroot <sdk>`
**Result**: FAILED - SDK's stddef.h still found instead of clang's builtin stddef.h

### Attempt 4: Prioritize clang resource dir with `-I` before `-isysroot` (commit 2c94d68 - FAILED)
**Date**: 2025-11-30
**Approach**: Use `-I` for clang resource dir (higher priority than `-isystem`) before `-isysroot`
**Command**: `-I<clang_resource_dir> -isysroot <sdk>`
**Result**: FAILED - `-isysroot` still makes SDK the primary search root

### Attempt 5: Force-include clang's stddef.h with `-include` flag (commit efb0b0b - FAILED)
**Date**: 2025-11-30
**Approach**: Use `-include <clang_resource_dir>/include/stddef.h` to force-include it before anything
**Result**: FAILED - Caused new errors: libc++'s cmath/cstddef complained about not finding their own headers. Message: "header search paths should contain C++ Standard Library headers before any C Standard Library"

### Attempt 6: Move LLVM include to `-isystem`, order: libc++, clang, SDK, LLVM (commit 17120d9 - FAILED)
**Date**: 2025-11-30
**Approach**: Remove `-I` for LLVM headers, use all `-isystem` in order: libc++, clang builtins, SDK, LLVM
**Command**: `-stdlib=libc++ -nostdinc++ -isystem<libc++> -isystem<clang_builtins> -isystem<sdk> -isystem<llvm>`
**Result**: FAILED - Same size_t errors, `-nostdinc++` removes compiler's implicit resource dir search

### Attempt 7: Remove `-nostdinc++`, use `-isysroot` with LLVM `-isystem` (commit c72d9b0 - FAILED)
**Date**: 2025-11-30
**Approach**: Let clang use normal header search, just add `-isysroot` for SDK and `-isystem` for LLVM
**Command**: `-stdlib=libc++ -isysroot <sdk> -isystem<llvm>`
**Result**: FAILED - `-isysroot` makes SDK stddef.h found before clang's builtin stddef.h

### Attempt 8: Replace `-isysroot` with explicit `-isystem` paths (commit 0b6807a - FAILED)
**Date**: 2025-11-30
**Approach**: Don't use `-isysroot` for compilation at all. Use explicit `-isystem` paths in order: clang builtins, LLVM, SDK
**Command**: `-stdlib=libc++ -isystem<clang_builtins> -isystem<llvm> -isystem<sdk_usr_include>`
**Result**: FAILED - The `-stdlib=libc++` flag implicitly adds Homebrew's libc++ path BEFORE any explicit `-isystem` paths. When libc++ does `#include_next`, it searches AFTER its own path, finding SDK headers before clang builtins.

### Attempt 9: Use `-idirafter` for SDK headers (commit e6aa60a - FAILED)
**Date**: 2025-11-30
**Approach**: Use `-idirafter` for SDK headers to place them LAST in search order. Let clang's default behavior (with `-stdlib=libc++`) handle libc++ and resource dir paths implicitly.
**Command**: `-stdlib=libc++ -isystem<llvm> -idirafter<sdk_usr_include>`
**Actual command**: `clang++ -O3 -DNDEBUG -std=gnu++20 -arch arm64 -O2 -fno-rtti -fexceptions -stdlib=libc++ -isystem/opt/homebrew/Cellar/llvm/21.1.6/include -idirafter/Applications/.../MacOSX.sdk/usr/include -c tool.cpp`
**Result**: FAILED - Same error. `-idirafter` does place SDK headers last, but `#include_next` from libc++ cassert still finds SDK's assert.h which includes SDK's arm/_types.h BEFORE clang's stddef.h gets a chance.

### Attempt 10: Use `-nostdinc` with explicit clang resource dir (commit 2284b16 - FAILED)
**Date**: 2025-11-30
**Approach**: Use `-nostdinc` to disable ALL implicit system headers, then explicitly add clang's resource dir first, then libc++, then SDK.
**Command**: `-nostdinc -isystem<clang_resource_dir> -isystem<llvm_libcxx> -isystem<llvm> -isystem<sdk_usr_include>`
**Actual command**: `clang++ -nostdinc -isystem/opt/homebrew/opt/llvm/lib/clang/21/include -isystem/opt/homebrew/Cellar/llvm/21.1.6/include/c++/v1 -isystem/opt/homebrew/Cellar/llvm/21.1.6/include -isystem/.../MacOSX.sdk/usr/include ...`
**Result**: FAILED - Same error. The problem is that libc++'s headers use `#include_next` to find SDK headers BEFORE stddef.h is included. Even though clang's resource dir is first, the SDK's arm/_types.h needs size_t/ptrdiff_t BEFORE it's processed.

### Attempt 11: Use Apple's system clang for compilation (SKIPPED)
**Date**: 2025-11-30
**Approach**: Use Apple's system clang (`/usr/bin/clang++`) for compiling the tooling tools, which is properly configured to work with the macOS SDK. Then link against Homebrew LLVM libraries.
**Command**: Set `CMAKE_CXX_COMPILER` to `/usr/bin/clang++` for the tooling ExternalProject
**Rationale**: Apple's clang is built and configured to work seamlessly with the macOS SDK. The header search order issues are solved at the toolchain level. We only need to add Homebrew's LLVM include paths for the clang tooling APIs.
**Risk**: Version mismatch between Apple's clang and Homebrew LLVM libraries. Apple clang may be older.
**Status**: SKIPPED - User prefers to use Homebrew clang

### Attempt 12: Use `-nostdinc++` with `-cxx-isystem` (commit b4ecff4 - FAILED)
**Date**: 2025-11-30
**Approach**: Use `-nostdinc++` to disable ONLY C++ standard library search (not all headers like `-nostdinc` does). Then use `-cxx-isystem` to add C++ includes explicitly. Use `-isysroot` for SDK C headers.
**Key insight**: `-cxx-isystem` is a Clang-specific flag for C++ system include paths, separate from `-isystem` for C paths.
**Command**: `-isysroot<SDK> -nostdinc++ -cxx-isystem<libc++> -cxx-isystem<llvm>`
**Actual command**: `clang++ -O3 -DNDEBUG -std=gnu++20 -arch arm64 -O2 -fno-rtti -fexceptions -isysroot/Applications/.../MacOSX.sdk -nostdinc++ -cxx-isystem/opt/homebrew/Cellar/llvm/21.1.6/include/c++/v1 -cxx-isystem/opt/homebrew/Cellar/llvm/21.1.6/include ...`
**Result**: FAILED - Same error. Even with `-nostdinc++` (not `-nostdinc`), when libc++ headers use `#include_next` to find SDK C headers, the SDK's `arm/_types.h` still can't find `size_t`/`ptrdiff_t`. The clang compiler's builtin `stddef.h` is apparently not being included BEFORE the SDK headers when `-isysroot` is used.

### Attempt 13: Add clang resource dir BEFORE `-isysroot` (commit 4da27eb - FAILED)
**Date**: 2025-11-30
**Approach**: Add clang's resource directory (containing stddef.h with size_t/ptrdiff_t) FIRST with `-isystem` BEFORE `-isysroot`. Keep `-nostdinc++` and `-cxx-isystem` for C++ paths.
**Key insight**: `-isystem` paths are searched before `-isysroot` paths. By putting clang's builtin headers first, the compiler should find stddef.h before SDK headers need it.
**Command**: `-isystem<clang_resource_dir> -isysroot<SDK> -nostdinc++ -cxx-isystem<libc++> -cxx-isystem<llvm>`
**Actual command**: `clang++ -O2 -fno-rtti -fexceptions -isystem/opt/homebrew/opt/llvm/lib/clang/21/include -isysroot/Applications/.../MacOSX.sdk -nostdinc++ -cxx-isystem/opt/homebrew/Cellar/llvm/21.1.6/include/c++/v1 -cxx-isystem/opt/homebrew/Cellar/llvm/21.1.6/include ...`
**Result**: FAILED - Same error. The `-isystem` path before `-isysroot` doesn't help because `-isysroot` changes the "sysroot" which is a fundamental concept affecting all relative include paths. When libc++ uses `#include_next`, the SDK headers are still found through the sysroot mechanism, bypassing the explicit `-isystem` path for clang builtins. The SDK's arm/_types.h still expects size_t/ptrdiff_t to be defined before it's processed.

### Attempt 14: Use SDK libc++ and -nostdinc (no -isysroot) (FAILED)
**Date**: 2025-11-30
**Approach**: Don't use `-isysroot` for compilation at all. Use `-nostdinc` AND `-nostdinc++` to disable ALL implicit header search. Then add explicit `-isystem` paths in the exact order needed:
1. Clang resource dir (defines size_t, ptrdiff_t via builtin stddef.h)
2. SDK's libc++ (instead of Homebrew's - Apple's libc++ is designed to work with SDK)
3. LLVM tooling headers
4. SDK C headers (/usr/include)

**Key insight**: The SDK has its own libc++ at `/usr/include/c++/v1` which is designed to work correctly with SDK headers. Using it instead of Homebrew's bundled libc++ may avoid the `#include_next` search order issues.
**Command**: `-nostdinc -nostdinc++ -isystem<clang_builtins> -isystem<sdk_libcxx> -isystem<llvm> -isystem<sdk_include>`
**Result**: FAILED - SDK's libc++ explicitly checks that it finds its own `<stddef.h>` wrapper. Without it (because we're using clang builtins directly), SDK's `<cstddef>` errors with: "tried including <stddef.h> but didn't find libc++'s <stddef.h> header"

### Attempt 15: Force-include clang builtin headers (FAILED)
**Date**: 2025-11-30
**Approach**: Force-include clang's individual type definition headers (`__stddef_size_t.h`, `__stddef_ptrdiff_t.h`, `__stddef_nullptr_t.h`) using `-include` flags BEFORE any other headers, combined with Attempt 14's approach.
**Command**: `-include<clang_builtins>/__stddef_size_t.h -include.../__stddef_nullptr_t.h -nostdinc -nostdinc++ -isystem...`
**Result**: FAILED - Same error as Attempt 14. The `-include` flags define the types, but SDK's libc++ `<cstddef>` still complains about not finding libc++'s own `<stddef.h>` wrapper.

### Attempt 16: Homebrew libc++ FIRST, clang builtins SECOND (SUCCESS!)
**Date**: 2025-11-30
**Approach**: Use Homebrew's libc++ (not SDK's) with correct `#include_next` order. The key insight is that Homebrew's libc++ has its own `<stddef.h>` wrapper that:
1. Does `#include_next <stddef.h>` to find the underlying C stddef.h
2. Then defines `nullptr_t` as `typedef decltype(nullptr) nullptr_t;`

For `#include_next` to work correctly, the search order must be:
1. Homebrew libc++ FIRST (so its `<stddef.h>` wrapper is found)
2. Clang resource dir SECOND (so `#include_next` finds clang's real `<stddef.h>`)
3. LLVM tooling headers
4. SDK C headers (for Darwin-specific headers like mach/*.h)

**Command**: `-nostdinc -nostdinc++ -isystem<homebrew_libcxx> -isystem<clang_builtins> -isystem<llvm> -isystem<sdk_include>`
**Actual command**: `clang++ -O2 -fno-rtti -fexceptions -nostdinc -nostdinc++ -isystem/opt/homebrew/Cellar/llvm/21.1.6/include/c++/v1 -isystem/opt/homebrew/Cellar/llvm/21.1.6/lib/clang/21/include -isystem/opt/homebrew/Cellar/llvm/21.1.6/include -isystem/.../MacOSX.sdk/usr/include ...`
**Result**: SUCCESS! The defer tool compiles and links correctly.

**Why this works**:
- When LLVM headers `#include <stddef.h>`, Homebrew libc++'s wrapper is found first (position 1)
- The wrapper does `#include_next <stddef.h>`, searching from position 2 onwards
- Clang's builtin `<stddef.h>` is found at position 2, which defines `size_t`, `ptrdiff_t`
- The wrapper then defines `nullptr_t` using `decltype(nullptr)`
- All types are properly defined before any code needs them

## Key Insights

1. The `-isysroot` flag makes the SDK the "sysroot", which changes how header search works at a fundamental level
2. The `-nostdinc++` flag removes not just libc++ but also compiler builtins from the implicit search path
3. The `-stdlib=libc++` flag with Homebrew clang automatically uses Homebrew's bundled libc++
4. Homebrew's libc++ uses `#include_next` which requires SDK headers to be in the search path
5. The SDK's low-level headers (`arm/_types.h`) expect `size_t`/`ptrdiff_t` to already be defined
6. **`#include_next` searches AFTER the current file's directory** - so explicit `-isystem` paths before libc++ don't help
7. The `-idirafter` flag places paths at the very END of the search list, even after `#include_next` paths

## Environment

- **macOS**: 15 (GitHub Actions runner macos-15)
- **Xcode SDK**: 16.4
- **Homebrew LLVM**: 21.1.6
- **Clang resource dir**: `/opt/homebrew/opt/llvm/lib/clang/21/include`
- **LLVM include dir**: `/opt/homebrew/Cellar/llvm/21.1.6/include`
- **Homebrew libc++**: `/opt/homebrew/Cellar/llvm/21.1.6/include/c++/v1/`
- **SDK path**: `/Applications/Xcode_16.4.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk`

## Secondary Issue: Sanitizer Link Flags Removed from Tests

### Problem
After fixing Attempt 16 (defer tool compilation), test executables failed to link with UBSan/ASan errors:
```
"___ubsan_handle_type_mismatch_v1", referenced from: ...
ld: symbol(s) not found for architecture arm64
```

### Root Cause
In `cmake/targets/Tests.cmake`, test targets on macOS were having their `LINK_OPTIONS` property **replaced** with just `-Wl,-pie`:
```cmake
# BAD: This replaced ALL link options, including sanitizer flags
set_target_properties(${test_exe_name} PROPERTIES
    LINK_OPTIONS "-Wl,-pie"
)
```

This removed the sanitizer link flags (`-fsanitize=address`, `-fsanitize=undefined`, etc.) that were added globally via `add_link_options()`.

### Fix
Changed from `set_target_properties` (which replaces) to `target_link_options` (which appends):
```cmake
# GOOD: This adds to existing link options, preserving sanitizer flags
target_link_options(${test_exe_name} PRIVATE
    -Wl,-all_load
)
```

The `-Wl,-all_load` flag serves the original purpose (preventing dead code elimination of Criterion test constructors) while preserving sanitizer link flags.
