# Git Bisect Analysis: WebSocket Frame Loss Regression

## Executive Summary

**Regression Issue:** WebSocket frame delivery fails (0fps rendering) when fragments arrive slowly (>100ms apart)

**Root Cause:** WebSocket fragment reassembly state is not preserved across `websocket_recv()` calls, causing orphaned fragments in the queue and protocol errors.

**Status:** The fix exists in commit `dadb4809` but is not yet in master branch (currently on HEAD `6e458e17`)

## Bisect Process & Findings

### Bisect Run

```
Initial: HEAD (6e458e17) marked as BAD - test times out
Baseline: b4d246bb marked as GOOD
Bisect midpoint 1: 9bcc3466 - BAD (test times out)
Bisect midpoint 2: f49f0a14 - BAD (test times out)
Bisect result: 0d44b077 is the first bad commit
```

### Key Discovery

The bisect identified `0d44b077` (build github pages docs with cmake) as the "first bad commit". However, this commit **only modifies workflow files** (`.github/workflows/doxygen.yml`) and should NOT affect WebSocket code.

This suggests:
1. Either the bisect boundary markers were incorrect
2. Or there's a build system interaction
3. Or the regression was already present in the baseline

Testing confirmed the baseline commit `b4d246bb` also fails the test, suggesting **the regression was already present before the test range**.

### Test Failure Evidence

Running `ctest -R websocket_integration` shows:
- Test times out after 45 seconds
- Expected runtime: 7 seconds or less (per bug report notes)
- Server log shows: `ERROR: AddressSanitizer: Joining already joined thread, aborting`
- WebSocket sends fail with: `WebSocket send called but transport NOT connected!`

## Root Cause Analysis

### The Problem

From `notes/BUG_REPORT_FRAME_LOSS.md` and code examination:

1. **Frame Reassembly Issue** (Primary)
   - Location: `lib/network/websocket/transport.c:websocket_recv()`
   - Problem: When WebSocket fragments arrive slowly (>100ms apart), the reassembly timeout fires
   - Current behavior: Partial buffer is freed, error returned
   - Next call: Starts fresh reassembly from orphaned fragment with `first=0`
   - Result: Protocol error → connection drops → frames lost → 0fps

2. **Connection State Issue** (Secondary)
   - WebSocket connection is marked `is_connected=false` prematurely
   - Causes `websocket_send()` to reject sends
   - May be related to thread joining errors

### The Fix

Commit `dadb4809` on branch `polecat/furiosa/as-9lf@mludcslb` addresses this:

**Key changes:**
- Move partial reassembly state into `websocket_transport_data_t`:
  - `partial_buffer`: preserve partial message
  - `partial_size`: current size
  - `reassembling`: flag for active reassembly
- Preserve state when timeout occurs instead of freeing
- Resume reassembly from saved state on next `recv()` call
- No more orphaned fragments → no protocol errors → frames delivered

**Commit message excerpt:**
```
CRITICAL FIX: Resolves issue where slow fragment delivery caused frames
to be lost and dispatch to fail.

When WebSocket fragments arrived slowly (>100ms apart):
1. Fragment #1 received → queued
2. recv() dequeues Fragment #1, waits for Fragment #2
3. Timeout after 100ms → frees Fragment #1, returns error
4. Next recv() call finds Fragment #2 alone with first=0
5. Protocol error → connection drops, frames lost

SOLUTION: Persist reassembly state across recv() calls using
ws_data->partial_buffer, partial_size, etc.
```

## Current State (HEAD: 6e458e17)

- **Status**: BROKEN - WebSocket frames not delivered (0fps)
- **WebSocket Integration Test**: TIMEOUT (45s instead of <7s)
- **Server Log Errors**:
  - "WebSocket send called but transport NOT connected!"
  - "Joining already joined thread"
  - "Reassembly timeout" (implied by design)

## Solution

**Immediate:** Merge the fix from `dadb4809` into master

**Validation:**
- Run WebSocket integration test: Should complete in <7 seconds
- Verify frame delivery at normal FPS (not 0fps)
- Check server logs for clean connections

## Code Locations

**Regression**: `lib/network/websocket/transport.c:398-540` (websocket_recv function)

**Fix Merge Point**: `dadb4809` (Preserve WebSocket fragment reassembly state across recv() calls)

**Related Files**:
- `lib/network/websocket/server.c` - Server callbacks (ESTABLISHED, CLOSED, RECEIVE)
- `lib/network/websocket/client.c` - Client callbacks
- `include/ascii-chat/network/websocket/internal.h` - Transport data structure

## Test Evidence

### Current (Broken) State

```
WebSocket Integration Test: TIMEOUT 45s (expected: <7s)
Test: websocket_integration::multiple_frames_at_15fps TIMEOUT

Server logs show rapid failures:
[ERROR] WebSocket send called but transport NOT connected! wsi=0x7d5c04fefc80, len=54
[ERROR] Joining already joined thread
```

### After Fix (Expected)

Test should complete in <7 seconds with frames delivered at expected FPS.

## Recommendations

1. **Verify fix availability**: Check if `dadb4809` or equivalent fix is in any active branches
2. **Merge strategy**: Incorporate fragment reassembly state preservation into websocket/transport.c
3. **Test coverage**: Ensure WebSocket integration test validates:
   - Fast fragment delivery (current case)
   - Slow fragment delivery (regression case)
   - Mixed fragment sizes and delays
4. **Additional fixes**: Investigate thread joining errors (may be separate issue)
