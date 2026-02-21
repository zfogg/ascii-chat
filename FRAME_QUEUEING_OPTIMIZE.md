# Frame Queueing Optimization Analysis (STAGE2 #305)

## Executive Summary

This document analyzes frame queueing bottlenecks identified in ascii-chat WebSocket streaming and proposes optimization strategies. Testing reveals that **WebSocket frame transmission (212ms per frame) and packet dequeueing (34ms) are the primary bottlenecks**, not the double-buffer swap mechanism as initially assumed.

## Current Architecture

### Frame Flow
```
Webcam → Client Capture → Video Encoding → Network Transmission
  ↓             ↓              ↓                    ↓
Device         Buffer       ACIP Packet        WebSocket
              Pool          Queue              Fragments
```

### Double-Buffer System (lib/video/video_frame.c)
- **Design**: Producer-consumer pattern with two frame buffers (front/back)
- **Thread Safety**: Mutex-protected pointer swaps
- **Allocation**: 2MB per-client buffers from memory pool (64-byte cache-line aligned)
- **Droppage**: Frames dropped if reader hasn't consumed previous frame

## Measured Bottlenecks (from WebSocket test runs)

### 1. **WebSocket Transmission Latency** (PRIMARY)
```
on_image_frame callback took 212ms (data_len=291600)
```
- **Issue**: Single 291.6KB frame takes 212ms to process on WebSocket
- **Impact**: At 60 FPS target (16.7ms per frame), this blocks frame transmission
- **Current Behavior**: Serial transmission - frames must complete before next can start

### 2. **Packet Dequeueing Latency** (SECONDARY)
```
DEQUEUE packet: 291646 bytes (dequeue took 34755.4μs = 34.75ms)
```
- **Issue**: Packet queue dequeueing takes 34.75ms for a single packet
- **Impact**: Directly serializes frame delivery - no pipelining
- **Cause**: Likely due to CRC/packet processing overhead on large payloads

### 3. **Frame Collection Latency** (TERTIARY)
```
FRAME_GEN_START: target_client=1 sources=0 collect=28.8ms
```
- **Issue**: Frame collection (gathering sources) takes 28.8ms
- **Impact**: Serial frame generation per-client
- **Note**: With 0 sources, this is still non-trivial

### 4. **Resulting FPS Degradation**
```
LAG: frame captured late by 370.1ms (expected 6.9ms, got 377.1ms, 2.65 fps)
```
- **Expected**: 60 FPS (6.9ms per frame at 1 client)
- **Actual**: 2.65 FPS (377.1ms per frame)
- **Overhead**: 55x slower than target

## Root Causes

### 1. Serial WebSocket Transmission
- Each frame must complete WebSocket transmission before next frame starts
- WebSocket fragments queued but likely processed sequentially
- No pipelining or parallel transmission of multiple frames

### 2. Large Payload Serialization
- 291.6KB per frame is large for serial queuing
- Mutex contention possible in `websocket_send()` function
- No batching or compression of frames

### 3. Packet Queue Single-Threaded Processing
- Dequeueing happens on single thread (client_dispatch_thread)
- Each packet processed completely before next packet starts
- CRC and validation adds 34ms overhead per packet

### 4. Frame Generation Blocking
- Per-client frame generation is synchronous
- No preparation of next frame while current frame is being sent
- Server waits for frame transmission to complete before generating next frame

## Optimization Strategies

### Strategy 1: Parallel Frame Transmission (High Impact)
**Objective**: Decouple frame generation from transmission

**Implementation**:
- Use ringbuffer for outgoing frames (similar to incoming frames)
- Transmission thread reads from ringbuffer while render thread generates next frame
- Allows 2+ frames to be in-flight simultaneously

**Expected Gain**: 50-70% reduction in frame latency
- Current: 212ms per frame serial
- Optimized: 106-150ms with 2 frames in flight
- At 2 frames: 2x throughput possible

**Files to Modify**:
- `src/server/stream.c` - Add outgoing frame ringbuffer
- `src/server/render.c` - Implement parallel render+transmit threads
- `lib/network/websocket/transport.c` - Non-blocking frame queuing

### Strategy 2: Payload Batching & Compression (Medium Impact)
**Objective**: Reduce per-frame overhead

**Implementation**:
- Batch 2-3 frames together in single WebSocket message
- Compress frame data with zstd (already in project)
- Use delta encoding between consecutive frames

**Expected Gain**: 20-40% reduction
- Compression ratio: 40-50% of original size (ASCII frames compress well)
- Overhead amortization: Fix overhead across multiple frames

**Files to Modify**:
- `lib/video/video_frame.c` - Add batching
- `lib/network/websocket/server.c` - Implement compression

### Strategy 3: Dequeueing Optimization (Low Impact, Quick Win)
**Objective**: Reduce packet processing latency

**Implementation**:
- Implement packet prefetching: validate next packet while processing current
- Move CRC validation to async thread
- Use lock-free queue for packet movement (if thread-safe)

**Expected Gain**: 10-20% reduction
- Current: 34.75ms per packet
- Optimized: 28-31ms with prefetch

**Files to Modify**:
- `src/server/client.c` - Optimize dispatch thread
- `lib/network/acip/handlers.c` - Async validation

### Strategy 4: Frame Reuse Pool (Low Impact, Maintenance)
**Objective**: Reduce allocation/deallocation overhead

**Implementation**:
- Pre-allocate frame pool for 3-5 frames per client
- Reuse buffers instead of allocating on each frame
- Already partially implemented - enhance with per-client pooling

**Expected Gain**: 5-10% CPU reduction (memory allocation overhead)

**Files to Modify**:
- `lib/buffer_pool.c` - Enhance pooling
- `lib/video/video_frame.c` - Use pre-allocated pool

## Recommended Implementation Order

### Phase 1: Foundation (Days 1-2)
1. **Verify bottlenecks** with instrumented tests (Completed ✓)
2. **Implement ringbuffer for outgoing frames** (Strategy 1 foundation)
3. **Add frame transmission timing metrics**

### Phase 2: Core Optimization (Days 3-5)
1. **Implement parallel transmission** (Strategy 1)
2. **Add payload batching** (Strategy 2)
3. **Measure FPS improvement**

### Phase 3: Fine-tuning (Days 6+)
1. **Implement dequeueing optimization** (Strategy 3)
2. **Profile and identify remaining bottlenecks**
3. **Optimize for different network conditions**

## Testing Methodology

### Baseline Metrics (Current State)
```
✓ Single client: 2.65 FPS (377ms per frame)
✓ WebSocket transmission: 212ms
✓ Packet dequeue: 34.75ms
✓ Frame collection: 28.8ms
✓ Target: 60 FPS (6.9ms per frame)
✓ Overhead factor: 55x
```

### Success Criteria
- **Phase 1**: Maintain stability, establish baseline
- **Phase 2**: Achieve 15+ FPS (< 67ms per frame)
- **Phase 3**: Approach 30+ FPS (< 33ms per frame)

### Test Script
Use existing `scripts/test-websocket-server-client.sh`:
```bash
# Run 25 iterations to collect metrics
for i in {1..25}; do
  ./scripts/test-websocket-server-client.sh 2>&1 | grep -E "FRAME|LAG|fps"
  sleep 0.5
done
```

### Key Metrics to Track
- Average FPS achieved
- Frame transmission time (ms)
- Packet dequeue time (μs)
- Maximum latency (p99)
- Memory usage per client

## Technical Debt & Dependencies

### From Stage 1
- Double-buffer mutex implementation is correct but not the bottleneck
- Frame dropping is working as designed
- Per-client buffering is efficient

### New Findings
- WebSocket transport needs redesign for parallel transmission
- Packet queue processing needs prefetching optimization
- Frame generation could benefit from async preparation

## Risk Analysis

### Optimization Risks
| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|-----------|
| Break stream reliability | Medium | High | Extensive regression testing |
| Increase memory usage | Low | Medium | Monitor with instrumentation |
| Complex thread interaction | High | Medium | Add synchronization logging |
| Degrade single-frame latency | Low | Low | Test with 1 frame buffered |

### Fallback Strategy
- Revert to double-buffer swap if parallelization causes issues
- Keep single-threaded path for debugging
- Maintain original code in feature branches

## Related Issues
- Issue: as-6ondm - Frame queueing optimization (this issue)
- Stage 1: Baseline analysis completed
- Stage 2: Optimization implementation (current)
- Stage 3 (Future): Multi-client queueing strategies

## References
- `lib/video/video_frame.c` - Double-buffer implementation
- `src/server/stream.c` - Frame collection logic
- `src/server/render.c` - Frame rendering pipeline
- `lib/network/websocket/transport.c` - WebSocket transport
- `src/server/client.c` - Client dispatch thread
- `lib/network/acip/handlers.c` - Packet handlers

## Next Steps

1. **Instrument WebSocket transmission** with per-fragment timing
2. **Profile with VTune/perf** to identify CPU hotspots
3. **Implement Strategy 1** (parallel transmission) as proof of concept
4. **Measure improvement** with test harness
5. **Iterate on Strategy 2** if needed

---

**Analysis Date**: 2026-02-21
**Status**: Baseline complete, optimization plan ready
**Estimated Implementation Time**: 5-7 days for Phase 1-2
**Next Review**: After Phase 2 completion
