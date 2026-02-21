# Frame Queueing Implementation Roadmap

## Phase Status: Analysis Complete ✓ → Implementation Ready

This document provides a detailed roadmap for implementing frame queueing optimizations based on completed bottleneck analysis.

## Quick Start for Next Developer

1. **Review analysis document**: `FRAME_QUEUEING_OPTIMIZE.md`
2. **Understand current bottleneck**: WebSocket transmission takes 142-212ms per frame
3. **First implementation**: Parallel frame transmission (Strategy 1)
4. **Testing**: Use `./scripts/test-websocket-server-client.sh` for validation

## Implementation Strategy 1: Parallel Frame Transmission (PRIORITY 1)

### Objective
Decouple frame generation from transmission to allow multiple frames in-flight simultaneously, reducing end-to-end latency.

### Current Flow (Serial)
```
[Render Thread] → Get Frame (wait) → Send Frame (wait) → Next Frame
      ↓                                     ↓
    142ms                               Blocked until send complete
```

### Optimized Flow (Parallel)
```
[Render Thread] → Queue Frames → Signal Sender
[Send Thread]   → Transmit Queued Frames (non-blocking)
```

### Files to Modify

#### 1. `src/server/stream.c`
**Add outgoing frame ringbuffer per client:**
- In `stream_context_t` structure, add:
  ```c
  ringbuffer_t *outgoing_frames;  // Non-blocking queue of frames awaiting transmission
  ```
- In `create_stream_context()`:
  - Create ringbuffer with capacity for 3-4 frames
  - Initialize with mutex and condition variable

#### 2. `src/server/render.c`
**Modify frame generation:**
- Change from `create_mixed_ascii_frame_for_client()` writing directly
- Instead: Queue frame to `stream->outgoing_frames` ringbuffer
- Let separate transmit thread handle sending

#### 3. `src/server/client.c`
**Modify client send thread:**
- In `client_send_thread_func()`:
  - Instead of waiting for frame rendering
  - Poll `stream->outgoing_frames` ringbuffer
  - Dequeue and transmit frames asynchronously
  - If ringbuffer empty, sleep 1-2ms and retry

#### 4. `lib/network/websocket/transport.c`
**Optimize WebSocket transmission:**
- Change `websocket_send()` to be non-blocking when possible
- Add frame batching: queue up to 2 frames before sending
- Implement fragmenting strategy for large frames

### Implementation Steps

**Step 1: Add ringbuffer to stream context**
```c
// In struct stream_context_t
typedef struct {
  ringbuffer_t *outgoing_frames;
  mutex_t frame_queue_mutex;
} stream_context_t;
```

**Step 2: Enqueue frames instead of sending directly**
```c
// In create_mixed_ascii_frame_for_client()
// Instead of: send_frame_to_client()
// Do: ringbuffer_put(stream->outgoing_frames, frame)
//     condition_signal(&stream->has_frames)
```

**Step 3: Dequeue and transmit in separate loop**
```c
// In client_send_thread_func()
while (running) {
  frame_t *frame = ringbuffer_get(stream->outgoing_frames, timeout);
  if (frame) {
    transmit_frame(frame);  // Non-blocking
    ringbuffer_release(frame);
  }
}
```

### Testing This Implementation

**Baseline** (before changes):
```bash
./scripts/test-websocket-server-client.sh
# Expected: 1.95-2.65 FPS
# Frame latency: 377-514ms
```

**After Strategy 1**:
```bash
./scripts/test-websocket-server-client.sh
# Expected: 5-10 FPS (2-3x improvement)
# Frame latency: 150-250ms
```

**Success Criteria**:
- FPS improves to 5+ FPS (at least 2x)
- No frame drops or corruption
- No deadlocks or race conditions
- Memory usage stays <500MB for single client

### Risk Mitigation

**Potential Issues**:
1. **Memory bloat** - Ringbuffer holding multiple frames
   - Mitigation: Cap ringbuffer to 3-4 frames max

2. **Frame ordering** - Frames sent out of order
   - Mitigation: Use sequence numbers in frame header

3. **Stale frame drops** - Old frames dropped if new frames arrive
   - Mitigation: Implement LRU eviction with metrics

4. **Synchronization bugs** - Race conditions in queue
   - Mitigation: Use existing `ringbuffer_t` (already thread-safe)

### Fallback Plan
If parallelization causes issues:
1. Keep changes minimal - just ringbuffer, no threading changes
2. Test with single-frame ringbuffer first (no parallelism)
3. Revert to serial path if regression occurs
4. File issue for investigation

## Implementation Strategy 2: Payload Batching (PRIORITY 2)

### Objective
Reduce per-frame overhead by batching 2-3 frames or using compression.

### Implementation Approach

**Option A: Frame Batching**
- Collect 2-3 frames in ringbuffer
- Send as single multi-frame packet
- Reduces frame header overhead

**Option B: Frame Compression**
- Already have zstd in project
- Compress ASCII frame data
- Expected compression: 40-50% ratio

**Recommendation**: Implement Option A first (simpler), then Option B if needed.

## Implementation Strategy 3: Dequeueing Optimization (PRIORITY 3)

### Objective
Reduce 34.75ms packet dequeueing overhead.

### Quick Wins
1. **Move CRC validation to async thread**
   - Don't block dispatch thread on CRC check
   - Validate in background, flag bad packets

2. **Implement packet prefetching**
   - While processing packet N, start reading packet N+1
   - Reduces memory bus stalls

### Files to Modify
- `src/server/client.c` - Add prefetch logic
- `lib/network/acip/handlers.c` - Async CRC validation

## Timeline Estimate

| Phase | Task | Estimate | Notes |
|-------|------|----------|-------|
| 1 | Ringbuffer infrastructure | 2-3 hours | Low risk, well-understood |
| 1 | Parallel transmission | 3-4 hours | Medium risk, needs testing |
| 1 | Testing & measurement | 2-3 hours | Validation with test suite |
| 2 | Frame batching | 2-3 hours | If Strategy 1 successful |
| 2 | Compression (optional) | 2-3 hours | If needed for further gains |
| 3 | Dequeueing optimization | 2-3 hours | Only if still needed |

**Total Estimate**: 11-19 hours for full implementation

## Testing Checklist

- [ ] Build succeeds without warnings
- [ ] Single client achieves 5+ FPS
- [ ] No frame corruption (compare hashes)
- [ ] No memory leaks (valgrind/asan)
- [ ] No deadlocks (run for 5+ minutes)
- [ ] Frame sequence numbers correct
- [ ] Regression testing with 2+ clients
- [ ] Performance profiling with perf/VTune
- [ ] Load testing (high frame rates)
- [ ] Network condition testing (jitter, loss)

## Known Gotchas

### 1. Double-Buffer Swap Contention
- The per-client double-buffer swap uses mutex
- Don't add unnecessary locking in parallel code
- Use existing `video_frame_get_latest()` function

### 2. WebSocket Fragment Ordering
- libwebsockets may reorder fragments
- Ensure frame header has sequence number
- Implement sequence validation on receive side

### 3. Audio/Video Sync
- Separating transmission may desync audio/video
- Test with multiple media streams
- Monitor A/V sync metrics

## Performance Monitoring

### Metrics to Track
```c
// Add to client_t structure
struct {
  uint64_t frames_queued;        // Frames put in ringbuffer
  uint64_t frames_sent;          // Frames successfully transmitted
  uint64_t frames_dropped;       // Frames dropped due to queue full
  uint64_t avg_queue_depth;      // Average ringbuffer occupancy
  uint64_t max_queue_wait_ms;    // Longest time frame waited in queue
  uint64_t send_thread_wake_count; // How often send thread woke up
} transmission_stats;
```

### Log Output
Modify logs to track:
- Frame queue depth at each step
- Time spent in transmission vs rendering
- Queue stalls and recovery
- Memory allocation for ringbuffers

## Related Files Reference

### Core Implementation Files
- `lib/video/video_frame.c` - Double-buffer (read-only, no changes needed)
- `lib/ringbuffer.c` - Use existing ringbuffer library
- `src/server/stream.c` - Add outgoing queue
- `src/server/render.c` - Queue frames instead of sending
- `src/server/client.c` - Dequeue in send thread

### Network Files
- `lib/network/websocket/transport.c` - WebSocket transmission
- `lib/network/acip/handlers.c` - Packet handling
- `src/server/protocol.c` - Protocol handlers

### Testing Files
- `scripts/test-websocket-server-client.sh` - Main test
- `tests/` directory - Add integration tests if needed

## Success Story Progression

1. **Current State**: 2.65 FPS, 140-212ms WebSocket transmission
2. **After Strategy 1**: 5+ FPS, 70-106ms with parallelization
3. **After Strategy 2**: 8+ FPS, 50-75ms with batching
4. **After Strategy 3**: 12+ FPS, 30-40ms optimal
5. **Final State**: Close to 30+ FPS, <33ms per frame

## Handoff Notes

This analysis was completed by analyzing:
- WebSocket server-client test runs (20+ iterations)
- Network packet traces showing 212ms transmission time
- Packet queue analysis showing 34.75ms dequeueing
- Frame generation timing showing 28.8ms collection

The bottleneck is clearly in the transmission pipeline, not in frame buffering. The double-buffer system is working correctly - the issue is that frames can't be generated fast enough because transmission blocks the render thread.

Implementing parallel transmission (Strategy 1) should yield 2-3x improvement with minimal risk.

---

**Analysis Completed**: 2026-02-21
**Ready for Implementation**: Yes
**Risk Level**: Medium (requires synchronization work)
**Estimated ROI**: 2-3x FPS improvement from Strategy 1 alone
