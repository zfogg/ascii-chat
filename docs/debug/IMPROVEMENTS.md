# Debug Instrumentation - Improvements Tracking

This document tracks proposed improvements and community feedback for the debug instrumentation system.

## High Priority Improvements

### 1. Fix Snippet Escaping Boundary Check
**Status:** 🔴 Open
**Priority:** High
**File:** `lib/debug/instrument_log.c:356-377`

**Issue:** Escape sequences write 2 characters but only check `pos < sizeof(buffer) - 2`. If loop is at boundary and hits escape sequence, could write beyond safe zone.

**Proposed Fix:**
```c
for (size_t i = 0; i < snippet_len; ++i) {
    const char ch = snippet[i];
    size_t chars_needed = 1;
    if (ch == '\n' || ch == '\r' || ch == '\t') {
        chars_needed = 2;
    }
    if (pos + chars_needed >= sizeof(buffer) - 1) {
        break;  // Not enough space
    }
    // ... existing switch statement ...
}
```

**Impact:** Low (unlikely in practice due to 4KB buffer) but simple to fix.

### 2. Add Missing Test Coverage
**Status:** 🔴 Open
**Priority:** High
**File:** `tests/unit/debug/instrument_log_test.c`

**Missing Tests:**
- [ ] Rate limiting (`ASCII_INSTR_RATE`) verification
- [ ] Regex filters (POSIX platforms)
- [ ] `ASCII_INSTR_ONLY` module selectors
- [ ] Concurrent logging from multiple threads
- [ ] Reentrant guard behavior
- [ ] Very long snippet handling

**Estimated Effort:** 2-3 hours

### 3. Create Quick Start Guide
**Status:** 🟡 In Progress
**Priority:** High
**File:** `docs/debug/QUICKSTART.md` (to be created)

**Content:**
- One-page crash debugging workflow
- Step-by-step from crash to root cause
- Common mistakes and solutions
- When to use alternatives

**Estimated Effort:** 1 hour

## Medium Priority Improvements

### 4. Add Performance Benchmarks
**Status:** 🔴 Open
**Priority:** Medium
**File:** `docs/debug/PERFORMANCE.md` (to be created)

**Benchmarks Needed:**
- Baseline (non-instrumented)
- Full instrumentation overhead
- Filtered instrumentation (single module)
- Rate-limited instrumentation
- Comparison with ASan overhead

**Estimated Effort:** 2-3 hours

### 5. Improve Error Messages
**Status:** 🔴 Open
**Priority:** Medium

**Improvements:**
- [ ] Warn on startup if output dir contains stale logs
- [ ] Log to stderr when file creation fails (not just silent fallback)
- [ ] Add debug mode to echo filter decisions

**Estimated Effort:** 1 hour

### 6. Stale Log Warning
**Status:** 🔴 Open
**Priority:** Medium
**File:** `lib/debug/instrument_log.c`

**Enhancement:** Detect and warn about existing `ascii-instr-*.log` files in output directory that might cause `O_EXCL` failures.

**Implementation:**
```c
// During initialization, scan output directory
// If stale logs found, fprintf(stderr, "Warning: ...")
```

## Low Priority Improvements

### 7. Windows Regex Support
**Status:** 🔴 Open
**Priority:** Low

**Options:**
1. Bundle PCRE2 or TRE library
2. Implement simple wildcard fallback (`*` and `?` only)
3. Document limitation prominently

**Recommendation:** Option 2 (simple wildcard) is easiest. Most users can work with glob patterns in `ASCII_INSTR_ONLY`.

### 8. Coverage PC Symbolizer Tool
**Status:** 🔴 Open
**Priority:** Low
**File:** `src/debug/ascii_symbolize_coverage.sh` (to be created)

**Implementation:**
```bash
#!/bin/bash
# Automatically resolve PCs to file:line for coverage logs
grep "pc=0x" "$1" | while read -r line; do
    pc=$(echo "$line" | sed -n 's/.*pc=\(0x[0-9a-f]*\).*/\1/p')
    addr2line -e "$2" -f -C "$pc"
done
```

### 9. Log Size Limits
**Status:** 🔴 Open
**Priority:** Low

**Enhancement:** Add `ASCII_INSTR_MAX_LOG_SIZE` environment variable to limit per-thread log file size (prevents disk space exhaustion).

**Default:** No limit (current behavior)

## Community Feedback

### Positive Feedback
_(Awaiting real-world usage)_

### Bug Reports
_(None yet)_

### Feature Requests
_(None yet)_

## Completed Improvements

### ✅ CLAUDE.md Documentation
**Status:** 🟢 Complete (2025-11-14)
**PR:** #XXX

Added comprehensive debug instrumentation section to CLAUDE.md with:
- Quick start guide
- Environment variable reference
- Workflow examples
- When to use vs alternatives

---

**Last Updated:** 2025-11-14
**Review Schedule:** Monthly
