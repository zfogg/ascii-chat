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

### Attempt 9: Use `-idirafter` for SDK headers (TESTING)
**Date**: 2025-11-30
**Approach**: Use `-idirafter` for SDK headers to place them LAST in search order. Let clang's default behavior (with `-stdlib=libc++`) handle libc++ and resource dir paths implicitly.
**Command**: `-stdlib=libc++ -isystem<llvm> -idirafter<sdk_usr_include>`
**Rationale**: `-idirafter` places paths at the very end of the search list, even after paths used by `#include_next`. This should allow clang's resource dir (implicitly added by the driver) to be found before SDK headers.
**Status**: Testing...

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
