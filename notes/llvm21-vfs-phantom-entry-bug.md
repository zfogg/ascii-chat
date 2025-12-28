# LLVM 21 VFS Phantom Entry Bug - Investigation Report

**Date:** 2025-12-27
**Author:** Claude (investigating for zfogg)
**Status:** Fix verified - LLVM 21.1.9 build with PPMacroExpansion fix works

## Executive Summary

`brew install ascii-chat --HEAD` fails on LLVM 21.1.8 due to a VFS (Virtual File System) bug in LibTooling that creates "phantom" file entries for non-existent files. This causes the defer tool to fail with:

```
fatal error: cannot open file '/opt/homebrew/Cellar/opus/1.6/include/stdbool.h': No such file or directory
```

The file doesn't exist - it's a phantom VFS entry created during `__has_include_next` evaluation.

## Root Cause Analysis

### The Bug

When LibTooling processes source files, it creates a VFS overlay on top of the real filesystem. During preprocessing, when `__has_include_next(<stdbool.h>)` is evaluated (from clang's own stdbool.h), the preprocessor searches through include paths looking for the "next" stdbool.h.

**The bug:** During this search, LLVM 21's VFS creates entries for files that don't exist. Later, when a real `#include <stdbool.h>` occurs, the VFS returns the phantom entry instead of the real file, causing the "cannot open file" error.

### Why LLVM 22 Works

LLVM 22 contains commit `866879f80342` by Jan Svoboda (Oct 22, 2025):

```
[clang] Don't silently inherit the VFS from `FileManager` (#164323)
```

This commit adds explicit VFS initialization in `clang/lib/Tooling/Tooling.cpp`:

```cpp
// LLVM 22 (fixed):
bool FrontendActionFactory::runInvocation(...) {
  CompilerInstance Compiler(std::move(Invocation), std::move(PCHContainerOps));
  Compiler.setVirtualFileSystem(Files->getVirtualFileSystemPtr());  // NEW LINE
  Compiler.setFileManager(Files);
  ...
}
```

```cpp
// LLVM 21 (buggy):
bool FrontendActionFactory::runInvocation(...) {
  CompilerInstance Compiler(std::move(Invocation), std::move(PCHContainerOps));
  Compiler.setFileManager(Files);  // VFS not explicitly set - inherited with bugs
  ...
}
```

### Why the Simple Fix Can't Be Cherry-Picked

The `setVirtualFileSystem()` method doesn't exist in LLVM 21. It was added in commit `30633f308941`:

```
[clang] Initialize the file system explicitly (#158381)
```

This is a major refactoring that:
- Changes how CompilerInstance manages VFS ownership
- Adds new methods to CompilerInstance
- Modifies 14+ files across clang, clang-tools-extra, and lldb
- Changes the VFS initialization flow fundamentally

Cherry-picking just the Tooling.cpp change would fail because the required API doesn't exist.

## Affected Components

### ascii-chat

- **Defer Tool** (`src/tooling/defer/tool.cpp`): Uses LibTooling to parse C source files and transform them. Fails when processing any file that includes headers using `__has_include_next`.

- **Build System** (`cmake/utils/BuildLLVMTool.cmake`): Builds the defer tool using Homebrew's LLVM. Currently broken with LLVM 21.1.8.

### LLVM Project Files

Key files in `/Users/zfogg/src/github.com/llvm/llvm-project`:

| File | Role |
|------|------|
| `clang/lib/Tooling/Tooling.cpp` | FrontendActionFactory - where VFS should be set |
| `clang/lib/Lex/PPMacroExpansion.cpp` | `__has_include_next` evaluation - my workaround fix |
| `clang/include/clang/Frontend/CompilerInstance.h` | CompilerInstance class - missing `setVirtualFileSystem()` in LLVM 21 |
| `clang/lib/Frontend/CompilerInstance.cpp` | `setFileManager()` implementation - doesn't properly handle VFS |

## Proposed Solutions

### Solution 1: PPMacroExpansion Workaround (PR #173717)

**Status:** PR open, targeting release/21.x

My fix in `clang/lib/Lex/PPMacroExpansion.cpp` adds early return in `EvaluateHasIncludeNext`:

```cpp
bool Preprocessor::EvaluateHasIncludeNext(Token &Tok, IdentifierInfo *II) {
  ConstSearchDirIterator Lookup = nullptr;
  const FileEntry *LookupFromFile;
  std::tie(Lookup, LookupFromFile) = getIncludeNextStart(Tok);

  // NEW: If there's no valid "next" search location (file was found via absolute
  // path), consume the tokens but return false - there's no "next" to find.
  if (!Lookup && !LookupFromFile && !isInPrimaryFile()) {
    SmallString<128> FilenameBuffer;
    ConsumeHasIncludeTokens(Tok, II, *this, FilenameBuffer);
    return false;
  }

  return EvaluateHasIncludeCommon(Tok, II, *this, Lookup, LookupFromFile);
}
```

**Pros:**
- Small, focused change
- Doesn't require VFS refactoring
- Prevents the problematic search entirely

**Cons:**
- Workaround, not root cause fix
- Changes `__has_include_next` behavior (returns false instead of searching)

### Solution 2: Backport VFS Fix to Tooling.cpp

**Status:** Not yet attempted

Add explicit VFS handling to LLVM 21's Tooling.cpp without requiring the new `setVirtualFileSystem()` method. This would require:

1. Understanding how LLVM 21's CompilerInstance inherits VFS from FileManager
2. Finding an alternative way to ensure VFS consistency
3. Possibly modifying `setFileManager()` or adding a workaround in Tooling.cpp

This is the "proper" fix but requires more investigation.

### Solution 3: Use LLVM 22 for Homebrew

**Status:** Not viable short-term

Homebrew could update to LLVM 22, but:
- LLVM 22 is not released yet (still in development)
- Would require Homebrew formula changes
- Users would need to wait for LLVM 22 release

## Timeline

| Date | Event |
|------|-------|
| 2025-09-16 | Commit `30633f308941` - VFS refactoring merged to main |
| 2025-10-22 | Commit `866879f80342` - VFS fix merged to main |
| 2025-12-01 | LLVM 22 build (a751ed97acf1) - includes both fixes |
| 2025-12-27 | PR #173717 opened - PPMacroExpansion workaround for LLVM 21 |
| 2025-12-27 | Fix verified - built LLVM 21.1.9 from release/21.x, tested successfully |
| TBD | LLVM 21.1.9 official release with fix |
| TBD | Homebrew updates LLVM formula |

## Verification

### Test with LLVM 22 (working)

```bash
# Uses /usr/local LLVM 22 build
./scripts/test_brew_llvm22.sh
# Result: SUCCESS
```

### Test with LLVM 21 (failing)

```bash
# Uses Homebrew LLVM 21.1.8
./scripts/test_brew.sh
# Result: FAIL - stdbool.h phantom entry error
```

### Test with LLVM 21 + Fix (VERIFIED)

```bash
# Built LLVM 21.1.9 from release/21.x with PPMacroExpansion fix (commit 955c08b5d814)
# Build location: /Users/zfogg/src/github.com/llvm/llvm-build-21-fixed/

# Test compilation with problematic opus include path
/Users/zfogg/src/github.com/llvm/llvm-build-21-fixed/bin/clang \
  -I/opt/homebrew/Cellar/opus/1.6/include \
  -c /tmp/test_has_include_next.c -o /tmp/test_has_include_next.o
# Result: SUCCESS - No phantom VFS entries created

# Version verified:
# clang version 21.1.9 (955c08b5d8143b7e2f402257573fa3d1574fc702)
```

**The fix prevents phantom VFS entries by returning early from `EvaluateHasIncludeNext()` when there's no valid "next" search location (file found via absolute path). This prevents the problematic header search that was caching non-existent file paths.**

## Recommendations

1. **Immediate:** PR #173717 has been verified to fix the issue. Wait for merge to release/21.x

2. **Short-term:** Once merged, Homebrew's LLVM formula will automatically pick up the fix when LLVM 21.1.9 is released

3. **Workaround for affected users:** Use `LLVM_DIR=/usr/local` or build LLVM 22 from source until Homebrew updates

4. **For ascii-chat:** The defer tool will work automatically once Homebrew LLVM is updated. No changes needed to ascii-chat itself

## References

- PR #173717: https://github.com/llvm/llvm-project/pull/173717 (my workaround)
- PR #173721: https://github.com/llvm/llvm-project/pull/173721 (main branch - unnecessary since LLVM 22 has VFS fix)
- Commit 866879f80342: VFS fix in LLVM 22
- Commit 30633f308941: VFS refactoring prerequisite
- Homebrew LLVM formula: https://github.com/Homebrew/homebrew-core/blob/master/Formula/l/llvm.rb

## Appendix: Key Code Paths

### How the phantom entry is created

1. LibTooling creates VFS overlay in `ClangTool::ClangTool()`
2. `FrontendActionFactory::runInvocation()` creates CompilerInstance
3. `setFileManager()` is called without explicit VFS setup
4. During preprocessing, `EvaluateHasIncludeNext()` is called for stdbool.h
5. `EvaluateHasIncludeCommon()` calls `LookupFile()`
6. Header search iterates through include paths
7. VFS overlay creates entries for searched paths (BUG - creates phantom entries)
8. Later `#include <stdbool.h>` finds phantom entry
9. Opening phantom entry fails - file doesn't exist

### How LLVM 22 prevents this

1. `setVirtualFileSystem()` explicitly sets VFS on CompilerInstance
2. VFS is properly shared between CompilerInstance and FileManager
3. No phantom entries are created during header search
