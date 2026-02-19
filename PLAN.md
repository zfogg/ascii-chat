# Simplified Plan: Shared Library Symbol Resolution (Issue #311)

## Problem

Backtrace symbols for shared library functions (`lib/` code compiled into `libasciichat.so`) fail to resolve because `get_linux_file_offset()` filters out `.so` files. Result: raw hex like `0x7f3a2b1c4d5e` instead of function names.

## Solution

Remove the executable name filter in `get_linux_file_offset()` so it resolves symbols from ANY binary in `/proc/self/maps` (exe, our `.so`, system libs). `/proc/self/maps` already provides correct file offsets via ASLR-adjusted addresses.

---

## Files Modified

1. **`lib/platform/symbols.c`** — core implementation
2. No CMakeLists.txt or paths.h changes needed

---

## Implementation

### Step 1: Add `binary_match_t` struct (before existing functions)

```c
typedef struct {
  char path[PLATFORM_MAX_PATH_LENGTH];
  uintptr_t file_offset;
  bool is_project_lib;  // true if path contains "libasciichat"
} binary_match_t;
```

### Step 2: Refactor `get_linux_file_offset()` → `get_linux_file_offsets()`

**Current behavior:**
- Scans `/proc/self/maps`
- Filters to only "ascii-chat" executable (rejects `.so` files)
- Returns bool + sets single output via pointer

**New behavior:**
- Scan `/proc/self/maps` for ANY executable segment (`perms[2] == 'x'`)
- Remove the name filter: `strstr(path, "ascii-chat") != NULL && strstr(path, ".so") == NULL`
- Set `is_project_lib = true` when path contains `"libasciichat"`
- Return count of matches; populate `matches` array (0, 1, or 2 in practice)
- Preserve ASLR offset calculation: `(target_addr - start_addr) + file_offset`
- Keep existing `#ifndef NDEBUG log_debug(...)`, add log for `is_project_lib`

**Signature:**
```c
static int get_linux_file_offsets(const void *addr,
                                  binary_match_t *matches,
                                  int max_matches);
// Returns: number of matches found (0–max_matches)
```

### Step 3: Refactor `get_macos_file_offset()` → `get_macos_file_offsets()`

**Changes:**
- Return count instead of bool
- Populate `matches` array instead of single output pointer
- Set `is_project_lib = true` when image name contains `"libasciichat"`
- Continue scanning all images (don't break on first match)

**Signature:**
```c
static int get_macos_file_offsets(const void *addr,
                                  binary_match_t *matches,
                                  int max_matches);
// Returns: number of matches found
```

### Step 4: Update `run_llvm_symbolizer_batch()` to use new functions

Current flow (single pass):
1. Call old `get_linux_file_offset()` → single binary path
2. Build groups from exe and call `llvm-symbolizer -e <exe_path>`

New flow (dual-pass):
1. For each address, call new `get_linux_file_offsets()` → returns array of matches (exe + libs)
2. Build separate groups for `is_project_lib == false` (exe group) and `true` (lib group)
3. Run `llvm-symbolizer -e <exe_path>` for exe group
4. Run `llvm-symbolizer -e <lib_path>` for each lib in lib group
5. Merge results: per address, check both exe and lib results for conflicts
6. If both resolved and differ → emit `[CONFLICT!]` in red with binary names
7. Otherwise use whichever resolved (or raw hex if neither resolved)

**Conflict detection:**
```c
// After both exe and lib groups are symbolized:
// exe_result[i] vs lib_result[i] — if both exist and differ:
char conflict_buf[1024];
const char *exe_name = "ascii-chat";  // or basename of exe_path
const char *lib_name = strrchr(lib_path, '/');
lib_name = lib_name ? lib_name + 1 : lib_path;

safe_snprintf(conflict_buf, sizeof(conflict_buf),
              "[%s] %s | [%s] %s %s",
              exe_name, exe_result,
              lib_name, lib_result,
              colored_string(LOG_COLOR_ERROR, "[CONFLICT!]"));
result[i] = platform_strdup(conflict_buf);
```

### Step 5: Update `run_addr2line_batch()` similarly

Apply the same dual-pass approach:
1. Call new `get_linux_file_offsets()` / `get_macos_file_offsets()`
2. Build exe and lib groups separately
3. Run addr2line for each
4. Merge with conflict detection

### Step 6: Windows branch (optional best-effort)

Add a comment: `// TODO: implement multi-binary support for DLLs using EnumProcessModules()`

For now, keep exe-only behavior. No critical issue on Windows.

---

## Key Points

✅ **No new config variables** — everything stays local to `symbols.c`
✅ **No CMakeLists.txt changes**
✅ **No paths.h changes**
✅ **Reuses existing functions**: `parse_llvm_symbolizer_result()`, `colored_string()`, `platform_strdup()`
✅ **Handles system libs automatically** — llvm-symbolizer will try to resolve (shows `??` if stripped, correct behavior)
✅ **Catches edge cases** — conflict detection warns if something unexpected happens

---

## Verification

```bash
cmake --preset default -B build && cmake --build build

# Trigger backtrace
ASCII_CHAT_MEMORY_REPORT_BACKTRACE=1 ./build/bin/ascii-chat mirror --snapshot --snapshot-delay 0 2>&1 | grep -A 20 "Backtrace"

# Before: [3] 0x7f3a2b1c4d5e
# After:  [3] [libasciichat.so] frame_encode() (lib/video/frame.c:142)
```
