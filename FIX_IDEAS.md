# FIX_IDEAS.md: Research Findings & Proposed Solutions

**Consolidated**: 2026-02-21
**Research Issues**: as-u9ue, as-nrqit, and related
**Status**: Ready for implementation

---

## Executive Summary

This document consolidates findings from comprehensive research into four critical issue areas affecting ascii-chat reliability:

1. **Shared Library Symbol Resolution** (Issue #311) - Debug symbols for `.so` functions not resolving
2. **WebSocket Client Disconnect Cascade** - Pool exhaustion, heap-use-after-free, race conditions
3. **WebSocket Fragment Crash** - SIGABRT during multi-fragment frame transmission
4. **WebSocket Connection Intermittency** - Port binding failures and initialization issues

All issues have proposed fixes with clear rationale and implementation approach.

---

## Issue #1: Shared Library Symbol Resolution (Issue #311)

### Problem

Backtrace symbols for shared library functions (`lib/` code compiled into `libasciichat.so`) fail to resolve. Instead of function names like `frame_encode()`, developers see raw hex addresses like `0x7f3a2b1c4d5e`.

**Root Cause**: `get_linux_file_offset()` filters out `.so` files, only accepting the main executable. This prevents symbol resolution from the project's shared library.

### Proposed Solution

Refactor symbol resolution to scan ALL executable segments in `/proc/self/maps`, not just the main executable.

#### Key Changes

1. **Create `binary_match_t` struct** to track multiple binary matches with metadata:
   ```c
   typedef struct {
     char path[PLATFORM_MAX_PATH_LENGTH];
     uintptr_t file_offset;
     bool is_project_lib;  // true if path contains "libasciichat"
   } binary_match_t;
   ```

2. **Refactor `get_linux_file_offset()` → `get_linux_file_offsets()`**:
   - Scan `/proc/self/maps` for ANY executable segment (not just "ascii-chat")
   - Remove name filter that rejects `.so` files
   - Set `is_project_lib = true` when path contains `"libasciichat"`
   - Return count of matches; populate `matches` array
   - Return type: `int` (number of matches found, 0-2 in practice)

3. **Refactor `get_macos_file_offset()` → `get_macos_file_offsets()`**:
   - Same approach: return count, populate array
   - Set `is_project_lib = true` for libasciichat images
   - Continue scanning all images (don't break on first match)

4. **Dual-pass symbolization in `run_llvm_symbolizer_batch()`**:
   - Build separate groups for exe (`is_project_lib == false`) and lib (`true`)
   - Run `llvm-symbolizer -e <exe_path>` for exe group
   - Run `llvm-symbolizer -e <lib_path>` for each lib in lib group
   - **Conflict detection**: If both exe and lib resolve, check for conflicts
   - Emit `[CONFLICT!]` in red with binary names if symbols differ

5. **Apply same pattern to `run_addr2line_batch()`** for fallback symbolization

#### Expected Outcome

```bash
# Before:
[3] 0x7f3a2b1c4d5e

# After:
[3] [libasciichat.so] frame_encode() (lib/video/frame.c:142)
```

#### Files Modified

- `lib/platform/symbols.c` — core implementation
- No CMakeLists.txt or paths.h changes needed

#### Rationale

- Fixes a common debugging pain point: unresolved symbols in crash backtraces
- Improves development velocity: no need to manually map hex offsets to source
- Maintains existing interfaces: `parse_llvm_symbolizer_result()`, `colored_string()` reused
- Conflict detection adds visibility for unexpected behavior

---

## Issue #2: WebSocket Client Disconnect Cascade

### Problem

WebSocket clients experience a cascade of failures during disconnect:
- **Pool exhaustion**: After 32 rapid reconnections, all pool slots filled, new connections rejected
- **Heap-use-after-free**: Race condition between cleanup and event loop callbacks
- **Data loss**: Dropped frames during disconnect
- **Thread cleanup delays**: Threads don't terminate cleanly, require force-kill after 5 retries

### Root Causes & Fixes

#### Root Cause #2A: Client Pool Slot Exhaustion

**Symptom**: "No available client slots (all 32 array slots are in use)" after ~32 reconnects

**Root Cause**: Disconnected WebSocket clients NOT removed from pool. Handler thread detected disconnect but never called `remove_client()`, leaving stale entries.

**Fix Applied** (Commit 4dce9818):
- Handler thread now calls `remove_client()` when detecting disconnection
- Properly frees the slot for next connection

**Status**: ✅ Fixed on master

---

#### Root Cause #2B: Heap-Use-After-Free Race Condition

**Symptom**: AddressSanitizer: heap-use-after-free during WebSocket client disconnect

**Root Cause**: Classic cleanup vs. event loop race:
1. Client initiates disconnect → LWS_CALLBACK_CLOSED queued
2. Handler thread calls `remove_client()` → destroys transport
3. Meantime, LWS event loop fires stale RECEIVE callback
4. RECEIVE callback uses freed transport → **CRASH**

**Fix Applied** (Commit 46ea736e):
- Disable permessage-deflate (libwebsockets 4.5.2 regression)
- Skip transport destruction for WebSocket clients (LWS_CALLBACK_CLOSED handles it)
- Add cleanup flag check in RECEIVE callback
- Lock-protected re-check after acquiring mutex (TOCTOU prevention)
- Proper lock ordering

**Status**: ✅ Fixed on master

---

#### Root Cause #2C: Fragment Reassembly Timeout Too Short

**Symptom**: Large frames (230KB RGB) split into 20+ WebSocket fragments. Under slow networks, reassembly timeout (100ms) expires before all fragments arrive, causing frame drop.

**Root Cause**: MAX_REASSEMBLY_TIME_NS set to 100ms — too short for large frame fragmentation.

**Fix Applied** (Commit 3a4707c3):
- Increased timeout from 100ms → 2 seconds
- Ample time for 20+ fragments to arrive
- Still short enough to detect genuinely stalled connections

**Status**: ✅ Fixed on master

---

#### Root Cause #2D: Client-Side Transport Null-Pointer Dereference

**Symptom**: Client crash when connection drops between capture thread status check and send attempt

**Root Cause**: Race condition in capture thread:
```
1. Check if transport exists (true)
2. Connection drops (network error)
3. Try to use transport (now freed/null)
4. CRASH
```

**Fix Applied** (Commit 3a4707c3):
```c
acip_transport_t *transport = server_connection_get_transport();
if (!transport) {
    log_warn("Transport became unavailable during capture, stopping transmission");
    image_destroy(processed_image);
    break;
}
```

**Status**: ✅ Fixed on master

---

### Proposed Enhanced Logging

For operational visibility of WebSocket client lifecycle, add debug logging:

**`lib/network/websocket/server.c`** (LWS_CALLBACK_CLOSED):
```c
log_debug("[WS_DISCONNECT] Client disconnecting (id=%u, cleanup_flag=%s, active=%s)",
          client->id,
          client->cleanup_in_progress ? "true" : "false",
          client->active ? "true" : "false");
```

**`lib/network/websocket/transport.c`** (fragment reassembly):
```c
log_debug_every(LOG_RATE_SLOW,
    "[WS_REASSEMBLY] fragments=%d, elapsed=%.2fs/2s, size=%zu",
    ws_data->fragment_count,
    (double)elapsed_ns / 1e9,
    ws_data->partial_size);
```

**`src/client/capture.c`** (transport availability):
```c
log_debug_every(LOG_RATE_SLOW,
    "[CAPTURE] Sending IMAGE_FRAME %ux%u via transport=%p",
    processed_image->w, processed_image->h, (void *)transport);
```

#### Rationale

- All four root causes address fundamental synchronization and lifecycle issues
- Fixes prevent both immediate crashes and gradual pool exhaustion
- Enhanced logging improves operational debugging without overhead

---

## Issue #3: WebSocket Fragment Crash (SIGABRT)

### Problem

During large frame transmission (230+ KB RGB frames), WebSocket fragmentation causes SIGABRT crash:

1. Frame split into 4096-byte chunks
2. Fragment 1 sends successfully via `lws_write()`
3. Fragment 2 triggers assertion failure in libwebsockets
4. Process crashes with SIGABRT signal

### Root Cause

State management between `lws_write()` fragment calls. When Fragment 2 is sent with `LWS_WRITE_CONTINUATION` flag, libwebsockets internal state is invalid—likely caused by:
- Improper buffer lifecycle between writes
- Missing synchronization on tx queue
- libwebsockets API misuse or version incompatibility

### Proposed Solution

#### Investigation Phase

1. Review `lib/network/websocket/transport.c:websocket_send()` fragmentation logic
2. Verify libwebsockets version compliance (4.5.x API)
3. Check buffer ownership and lifecycle
4. Test with smaller payloads to find threshold (< 4096 → OK, > 4096 → CRASH?)

#### Potential Fixes (after investigation)

**Option A: Batch writes within tx queue**
- Don't call `lws_write()` directly for each fragment
- Queue all fragments in libwebsockets tx queue
- Let libwebsockets manage fragmentation internally

**Option B: Proper tx queue synchronization**
- Add mutex protection around `lws_write()` sequence
- Ensure only one write operation in flight at a time
- Prevent concurrent fragment submissions

**Option C: Buffer ownership clarification**
- Document buffer lifetime: must remain valid until `lws_write()` completes
- Add validation: verify buffer not freed/reused between fragment writes
- Consider double-buffering for large frames

**Option D: libwebsockets version update**
- Check if 4.5.3+ has fix for this assertion
- Evaluate cost/benefit of version bump

#### Testing Approach

```bash
# Reproduce crash
./build/bin/ascii-chat client --file <230KB_video_file> --snapshot --snapshot-delay 1

# Monitor for SIGABRT
# If crash occurs, capture backtrace and state
ASCII_CHAT_MEMORY_REPORT_BACKTRACE=1 ./build/bin/ascii-chat client ... 2>&1 | grep -A 20 "SIGABRT\|backtrace"
```

#### Rationale

- SIGABRT is a hard assertion failure—indicates library contract violation
- Investigation required before fix (don't guess at state management issues)
- Multiple plausible causes depending on actual libwebsockets API contract

---

## Issue #4: WebSocket Connection Intermittency

### Problem

WebSocket server initialization inconsistent across test runs:

- **Run 1**: Server reports successful initialization on port 38617 ✓
- **Run 2**: Server fails with "Cannot bind to network port" (error code 41) ✗
- **Run 3**: Server reports initialization successful on port 32297, but client gets ECONNREFUSED 15 seconds later ✗

**Pattern**: IPv4 binding fails before WebSocket initialization (logs show "Failed to bind 0.0.0.0:PORT"), suggesting systemic issue with port allocation/deallocation.

### Root Causes

#### Root Cause #4A: Port Allocation Timing Issue

**Symptom**: Subsequent test runs fail with error code 41 (port bind failure)

**Likely Cause**: Previous test run doesn't properly release socket. libwebsockets context creation fails due to port still in TIME_WAIT or CLOSE_WAIT state.

**Proposed Fix**:
1. Verify `SO_REUSEADDR` socket option set on listening socket
2. Check system file descriptor limits (`ulimit -n`)
3. Add server-side retry logic for port binding (3-5 retries with backoff)

---

#### Root Cause #4B: IPv4 Binding Failure During WebSocket Initialization

**Symptom**: TCP binding fails on IPv4 (0.0.0.0:PORT) but succeeds on IPv6 ([::]:PORT). WebSocket initialization reports success but server doesn't actually listen on network interface.

**Likely Cause**:
- Libwebsockets context created but NOT bound to loopback/0.0.0.0
- Only listening on internal pipe, not network interface
- Missing `LWS_SERVER_OPTION_*` flags for network listening

**Proposed Fix**:
1. Check `info.options` flags in `websocket_server_init()`
2. Verify `LWS_SERVER_OPTION_VALIDATE_UTF8`, `LWS_SERVER_OPTION_DISABLE_IPV6` flags
3. Add explicit bind address (127.0.0.1 or 0.0.0.0) instead of port-only
4. Test: `netstat -tlnp | grep <port>` and `lsof -i :<port>` to verify listening state

---

#### Root Cause #4C: Client Connection Timeout During Server Event Loop Startup

**Symptom**: Server reports "WebSocket server starting event loop" but client immediately gets ECONNREFUSED (17 seconds before connection attempt)

**Likely Cause**:
- WebSocket context created but event loop not yet polling for new connections
- Race condition: context creation completes before `lws_service()` loop enters
- Client connection attempt arrives during initialization window

**Proposed Fix**:
1. Add synchronization: wait for event loop ready before considering initialization complete
2. Monitor `lws_service()` return value to detect if context actually processing events
3. Add debug logging: "Event loop started, ready for connections" AFTER first successful `lws_service()` call

### Proposed Verification

```bash
# Debug IPv4 binding
./build/bin/ascii-chat --log-level debug server --grep "/bind|TCP|IPv4/ig"

# Check actual listening state
# In another terminal while server running:
netstat -tlnp | grep ascii-chat
lsof -i :27224

# Test with explicit port
./build/bin/ascii-chat server --port 9999 --log-level debug

# Client connection attempt
nc -zv localhost 9999
```

#### Rationale

- Intermittency suggests timing/race condition, not consistent bug
- Multiple failures suggest different root causes (IPv4 vs initialization timing)
- Socket options and libwebsockets flags are common culprits
- Proper diagnostics required before fix

---

## Summary Table

| Issue | Type | Status | Priority | Implementation |
|-------|------|--------|----------|-----------------|
| Symbol Resolution (#311) | Feature | Ready | P2 | Refactor get_*_file_offset() → multi-binary version |
| Client Disconnect (A-D) | Fix | ✅ Merged | P1 | Commits 4dce9818, 46ea736e, 3a4707c3 present on master |
| Fragment Crash | Debug Required | 🔍 Investigation | P1 | Requires analysis of lws_write() state mgmt |
| Connection Intermittency | Debug Required | 🔍 Investigation | P2 | Requires socket options & event loop timing analysis |

---

## Implementation Priority

### Phase 1: No-Risk (Already Merged)
- ✅ Client Disconnect fixes (Commits present on master)
- Recommendation: Deploy with enhanced logging

### Phase 2: High-Value Feature
- 📋 Symbol Resolution (#311)
- Improves debugging experience
- No breaking changes

### Phase 3: Stability Investigation
- 🔍 Fragment Crash & Connection Intermittency
- Requires investigation & diagnostics
- After Phase 1 deployment

---

## Testing Checklist

### Pre-Implementation
- [ ] Verify all WebSocket client disconnect fixes present on master
- [ ] Confirm fragment crash reproducible with test-websocket-server-client.sh
- [ ] Document baseline intermittency frequency (X failures per 10 runs?)

### Symbol Resolution (#311)
- [ ] Compile with libasciichat.so present
- [ ] Trigger backtrace via `ASCII_CHAT_MEMORY_REPORT_BACKTRACE=1`
- [ ] Verify .so functions resolve to symbol names
- [ ] Check conflict detection works (if applicable)

### Fragment Crash Investigation
- [ ] Reproduce SIGABRT with 230KB+ frame
- [ ] Capture libwebsockets backtrace and state
- [ ] Identify if option A, B, C, or D applies

### Connection Intermittency
- [ ] Run test suite 10+ times, measure failure rate
- [ ] Check socket state with netstat/lsof during init
- [ ] Verify IPv4 vs IPv6 behavior
- [ ] Monitor event loop readiness logs

---

## References

- **Issue as-u9ue**: Client Disconnect Root Cause Analysis
- **Issue as-nrqit**: WebSocket Server-Client Test
- **Bead as-ni860**: CMake Duplicate WASM Targets (pre-existing)
- **Test Script**: `scripts/test-websocket-server-client.sh`
- **Build**: `cmake --preset default -B build && cmake --build build`
- **Docs**: `lib/platform/README.md`, `docs/crypto.md`

---

## Next Actions

1. ✅ Consolidate findings (this document)
2. 📋 Symbol Resolution: Implement Phase 2 feature
3. 🔍 Fragment Crash: Investigate libwebsockets state management
4. 🔍 Connection Intermittency: Add diagnostics & socket monitoring
5. 📊 Testing: Verify fixes don't introduce regressions

