# WebSocket E2E Pipeline Analysis - February 20, 2026

## Executive Summary

**Status**: ❌ CRITICAL ISSUES FOUND
- Inconsistent frame delivery (0-35 frames per run, 5-second test)
- Fragment reassembly timeouts causing data loss
- Heap-use-after-free crash in transport layer

## Validation Results

### Test Configuration
- **Runs**: 3 cycles
- **Duration**: 5 seconds per run
- **Transport**: WebSocket (ws://localhost:PORT)
- **Build**: Release with AddressSanitizer

### Test Outcomes

| Run | Frames | Crashes | Errors | Notes |
|-----|--------|---------|--------|-------|
| 1 | 35 | ✅ | 1× Reassembly timeout | Partial success |
| 2 | 0 | ✅ | ? | Parsing issue - needs investigation |
| 3 | - | ❌ HEAP-USE-AFTER-FREE | 1× Reassembly timeout | Crash detected |

### Frame Consistency: ⚠️ SEVERE VARIANCE
- Minimum: 0 frames
- Maximum: 35 frames
- Expected: ~150-200 frames (30fps × 5-8 seconds of actual transmission)
- **Variance**: 35 frames (234% deviation)

## Root Cause Analysis

### Issue #1: Fragment Reassembly Timeout

**Symptom**:
```
Fragment reassembly timeout - no data from network (code: 40)
```

**Location**: `lib/network/websocket/transport.c:444@websocket_recv()`

**Cause**: WebSocket connection drops mid-transmission, causing incomplete frame reassembly. The fragment reassembly logic times out waiting for the final fragment.

**Impact**: Partial frames are lost, causing data loss and incomplete ASCII art output.

---

### Issue #2: Heap-use-after-free (CRITICAL)

**Symptom**:
```
==696837==ERROR: AddressSanitizer: heap-use-after-free on address 0x7bdb0604df30
READ of size 8 at 0x7bdb0604df30 thread T0
```

**Location**: `src/client/capture.c:261` → `lib/network/acip/client.c:121@acip_send_image_frame()`

**Root Cause - Race Condition**:

1. **Capture thread** (line 258):
   ```c
   acip_transport_t *transport = server_connection_get_transport();
   ```
   Gets a pointer to the transport object.

2. **Receive thread** (async):
   Connection drops, `server_connection_lost()` is called, transport is destroyed.

3. **Capture thread** (line 261):
   ```c
   acip_send_image_frame(transport, ...);  // USE AFTER FREE!
   ```
   Tries to use the freed transport pointer.

**Stack Trace**:
- `acip_send_image_frame()` attempts to write to freed transport structure
- AddressSanitizer detects read of freed memory at address 0x7bdb0604df30

**Impact**:
- Immediate crash (SIGABRT via AddressSanitizer)
- Inconsistent frame delivery (sometimes crashes before all frames sent)
- Data loss

---

### Issue #3: Performance Degradation

**WebSocket Callback Timing**:
- Average: 5476-6137 µs (~5.5-6.1 ms per callback)
- Threshold: 200 µs (WARNINGS logged)
- **Status**: 🟡 SLOW - callback times exceed threshold by 25-30×

**Symptoms in logs**:
```
[WS_CALLBACK_DURATION] RECEIVE callback took 8863.0 µs (> 200µs threshold)
```

**Analysis**:
- Fragment reassembly and network I/O taking longer than expected
- Could indicate:
  - Large frame fragmentation (320×240 RGB = 230KB per frame)
  - System under load
  - Inefficient buffer management

---

## Performance Metrics

### WebSocket Callback Performance
```
Average callback time: 5.5-6.1 ms per message
Expected: < 0.2 ms (200 µs)
Status: ❌ 25-30× slower than expected
```

### Frame Transmission
```
Expected frames (30fps × 5sec): 150+ frames
Actual frames: 0-35 frames
Success rate: 0-23% (should be >95%)
```

### Potential Data Loss Indicators
```
Fragment reassembly timeouts: 1 per crash run
Partial connections: 2 of 3 runs
Crash rate: 33% (1 of 3 runs)
```

---

## Recommended Fixes

### Priority 1: Fix Race Condition (CRITICAL)

**File**: `src/client/capture.c` (lines 258-263)

**Problem**: Transport pointer can be freed while capture thread holds a reference.

**Solution**: Implement transport reference counting or add mutex protection:

Option A - Add transport validity check:
```c
acip_transport_t *transport = server_connection_get_transport();
if (!transport) {
    log_warn("Transport no longer available");
    image_destroy(processed_image);
    break;
}
```

Option B - Reference counting (preferred):
- Add `transport_ref()` / `transport_unref()` to acip_transport_t
- Increment ref count when capture thread gets transport
- Decrement when done
- Only free when ref count reaches zero

---

### Priority 2: Optimize WebSocket Fragment Reassembly

**File**: `lib/network/websocket/transport.c:440-450`

**Problem**: Fragment reassembly timeout at 5 seconds causes data loss.

**Solution**:
- Increase timeout for large frames (320×240 RGB frames fragment into 20+ pieces)
- Current: 5 second timeout (too short for slow networks)
- Proposed: 10-15 seconds or make configurable

---

### Priority 3: Optimize Callback Performance

**Files**:
- `lib/network/websocket/transport.c` (receive handling)
- `lib/network/acip/client.c` (frame parsing)

**Problem**: Callbacks taking 5-6ms instead of <200µs.

**Investigation needed**:
- Profile with `--log-level debug --grep "WS_TIMING|CALLBACK_DURATION"`
- Check buffer allocation/deallocation in receive path
- Consider pre-allocated buffer pools

---

## Testing Recommendations

### Short-term (Quick Validation)
```bash
# Run with extended timeout for WebSocket reassembly
./build/bin/ascii-chat server --websocket-port 27225
./build/bin/ascii-chat client ws://localhost:27225 --snapshot --snapshot-delay 10

# Run test script 5+ times and compare frame counts
for i in {1..5}; do
    bash scripts/test-websocket-server-client.sh
done
```

### Medium-term (Validation Suite)
- Create `scripts/validate-websocket-pipeline.sh` (✅ DONE)
- Add to CI/CD pipeline
- Set baseline: all runs should send 150+ frames, 0 crashes

### Long-term (Performance Benchmarking)
- Add performance telemetry to WebSocket layer
- Track callback timing statistics
- Alert on variance >10% from baseline

---

## Files Involved

| File | Issue | Impact |
|------|-------|--------|
| `src/client/capture.c` | Race condition on transport | CRITICAL |
| `lib/network/websocket/transport.c` | Fragment timeout, callback performance | HIGH |
| `lib/network/acip/client.c` | Use-after-free recipient | HIGH |
| `src/client/server.c` | Transport lifecycle mgmt | MEDIUM |

---

## Validation Script

A comprehensive validation script has been created:
- **Location**: `scripts/validate-websocket-pipeline.sh`
- **Features**:
  - Runs test repeatedly (configurable, default 5)
  - Checks for crashes (AddressSanitizer)
  - Tracks frame consistency
  - Detects reassembly timeouts
  - Reports performance metrics
- **Usage**: `bash scripts/validate-websocket-pipeline.sh 5`

---

## Next Steps

1. ✅ Created validation script
2. ⏳ Fix race condition in capture thread
3. ⏳ Increase fragment reassembly timeout
4. ⏳ Profile and optimize callback performance
5. ⏳ Re-run validation to confirm improvements
6. ⏳ Add pre-flight checks to CI pipeline

---

**Analysis Date**: February 20, 2026
**Analyst**: polecat valkyrie (asciichat rig)
**Status**: Ready for implementation
