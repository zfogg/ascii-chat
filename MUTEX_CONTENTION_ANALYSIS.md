# Mutex Contention Analysis - ascii-chat

**Date**: 2026-02-20
**Scope**: Deep dive into mutex lock patterns, contention in hot paths, and TOCTOU race conditions
**Status**: Analysis Complete

---

## Executive Summary

The analysis identified several mutex contention issues in ascii-chat, with the primary bottleneck being the **node pool mutex in the packet queue**. While most of the codebase successfully uses lock-free patterns (buffer pools, ringbuffers, packet queues), the node pool acts as a synchronization point for every packet allocation/deallocation in high-frequency paths.

Additionally, several TOCTOU (Time-of-Check, Time-of-Use) race conditions were identified in the audio worker thread, and long-duration mutex lock holding was found during PortAudio stream initialization.

### Key Findings

| Issue | Location | Severity | Impact |
|-------|----------|----------|--------|
| Node pool mutex contention | `lib/network/packet_queue.c` | **HIGH** | Every packet operation locks |
| TOCTOU in audio worker | `lib/audio/audio.c` (line 231-275) | **MEDIUM** | Data may disappear between check and use |
| Long mutex hold during init | `lib/audio/audio.c` (line 1365-1699) | **MEDIUM** | Blocks other audio operations |
| PortAudio refcount mutex | `lib/audio/audio.c` (line 38-127) | **LOW** | Only contended during init/teardown |
| Error statistics mutex | `lib/asciichat_errno.c` | **LOW** | Infrequent contention |

---

## Detailed Findings

### 1. NODE POOL MUTEX CONTENTION (HIGH PRIORITY)

**Location**: `lib/network/packet_queue.c:65-121`

**Problem**: Every packet queue operation requires locking/unlocking the node pool mutex:

```c
// In packet_queue_enqueue() - line 229
packet_node_t *node = node_pool_get(queue->node_pool);  // LOCKS

// In packet_queue_dequeue() - various locations
node_pool_put(queue->node_pool, head);  // LOCKS
```

Both `node_pool_get()` and `node_pool_put()` acquire `pool->pool_mutex`:

```c
packet_node_t *node_pool_get(node_pool_t *pool) {
  ...
  mutex_lock(&pool->pool_mutex);        // CONTENTION POINT
  packet_node_t *node = pool->free_list;
  if (node) {
    pool->free_list = atomic_load_explicit(&node->next, ...);
    pool->used_count++;
    ...
  }
  mutex_unlock(&pool->pool_mutex);      // RELEASE
  ...
}
```

**Why It Matters**:

1. **High Frequency**: Packet queues are hit on every frame encode/decode cycle
2. **Multiple Producers/Consumers**:
   - Audio mixer enqueues packets (1 thread)
   - Video renderer enqueues packets (1 thread)
   - Per-client send threads dequeue packets (N threads, where N = number of clients)
3. **Lock Duration**: Simple LIFO pop/push, but mutex overhead dominates
4. **Shared Resource**: All clients may share the same node pool if configured globally

**Impact Estimation**:
- With 5 clients + audio/video producers = ~10 threads contending for single pool mutex
- Each packet operation = 1 lock + 1 unlock = 2 mutex syscalls
- At 30 FPS, 5 clients, 2 audio packets/frame = ~300 packets/sec
- With 20 mutex contention events/sec across 10 threads = potential for queue buildup

**Contention Pattern**:

```
Thread 1 (client send)    Thread 2 (client send)    Thread 3 (audio mixer)
  |                             |                           |
  |--lock(pool_mutex)-----------|--wait                  |--lock(pool_mutex)--wait
  |  get node                   |                           |
  |  unlock                     |                           |
  |--enqueue--                  |--lock(pool_mutex)--ok   |
  |                             |  get node                 |
  |                             |  unlock                   |
  |                             |--enqueue--                |
  |                             |                           |--lock(pool_mutex)--wait
  |--dequeue--                  |                           |
  |  lock(pool_mutex)--wait     |                           |
  |  put node (return to pool)  |                           |
  |  unlock                     |                           |
  |                             |                           |--lock--ok
  |                             |                           |  get node
  |                             |                           |  unlock
  v                             v                           v
```

**Recommended Fix Approaches**:

1. **Per-Thread Pools** (Best): Each producer/consumer gets its own pool
   - Eliminates contention entirely
   - Requires allocating more nodes but scales linearly
   - Trade-off: Slightly more memory for zero contention

2. **Lock-Free Node Pool**: Replace mutex with atomic CAS-based LIFO
   - Similar to buffer pool implementation (already lock-free!)
   - Reduces contention from mutex syscall to atomic operations
   - More complex, but proven pattern in codebase

3. **Reduce Pool Frequency**: Use malloc/free fallback more often
   - Remove pool when contention exceeds threshold
   - Simpler but slower (malloc has its own global lock)

### 2. TOCTOU RACE CONDITION IN AUDIO WORKER (MEDIUM PRIORITY)

**Location**: `lib/audio/audio.c:231-275`

**Problem**: Classic Time-of-Check, Time-of-Use race condition:

```c
// Line 231-233: CHECK
size_t capture_available = audio_ring_buffer_available_read(ctx->raw_capture_rb);
size_t render_available = audio_ring_buffer_available_read(ctx->raw_render_rb);
size_t playback_available = audio_ring_buffer_available_read(ctx->playback_buffer);

// ... logging and other code ...

// Line 269-275: USE (44 lines later!)
if (capture_available >= MIN_PROCESS_SAMPLES) {
  size_t samples_to_process = (capture_available > WORKER_BATCH_SAMPLES)
    ? WORKER_BATCH_SAMPLES
    : capture_available;
  size_t capture_read = audio_ring_buffer_read(ctx->raw_capture_rb,
    ctx->worker_capture_batch, samples_to_process);  // Could be 0!
}
```

**What Can Go Wrong**:

1. Thread checks `capture_available = 2000` samples available
2. Between check and read, another thread consumes those samples
3. `audio_ring_buffer_read()` called with `samples_to_process = 2000` but only 0 samples available
4. Returns 0, worker skips processing, logs are misleading

**Actual Code Flow**:

```c
// Worker thread                          // PortAudio callback

capture_available = 2000 ✓                (checking state)

(logging, other work)                     (emptying buffer)

capture_read = ring_buffer_read(2000)
  // Returns 0! ring_buffer is now empty
if (capture_read > 0) {
  // SKIPPED!
}
```

**Why This Matters**:

- Ring buffers are lock-free (no synchronization between threads)
- `available_read()` returns snapshot; doesn't prevent concurrent consumption
- Leads to underutilized processing and unexpected log messages
- Can cause audio artifacts if this happens during high-latency network bursts

**Recommended Fix**:

```c
// SAFER: Check and read atomically within ringbuffer operations
size_t samples_to_process = MIN(capture_available, WORKER_BATCH_SAMPLES);
if (samples_to_process >= MIN_PROCESS_SAMPLES) {
  size_t capture_read = audio_ring_buffer_read(ctx->raw_capture_rb,
    ctx->worker_capture_batch, samples_to_process);

  if (capture_read > 0) {
    // Process what we actually got, not what we expected
    process_samples(ctx->worker_capture_batch, capture_read);
  } else {
    // Expected data but got 0 - another thread consumed it
    log_warn("Unexpected: buffer appeared empty on read (concurrent consumption?)");
  }
}
```

**Or**: Protect the entire sequence with a reader mutex if consistency is critical.

### 3. LONG MUTEX HOLD DURING AUDIO INIT (MEDIUM PRIORITY)

**Location**: `lib/audio/audio.c:1365-1699` (334 lines!)

**Problem**: `state_mutex` held while performing blocking PortAudio operations:

```c
mutex_lock(&ctx->state_mutex);        // Line 1365

// Lines 1379-1425: Get device info (PortAudio calls, potentially slow)
if (GET_OPTION(microphone_index) >= 0) {
  inputParams.device = GET_OPTION(microphone_index);
} else {
  inputParams.device = Pa_GetDefaultInputDevice();  // SLOW!
}
inputInfo = Pa_GetDeviceInfo(inputParams.device);   // SLOW!

// Lines 1451-1530: VERY SLOW - Open and start PortAudio streams
if (!try_separate) {
  err = Pa_OpenStream(&ctx->duplex_stream, &inputParams,
    &outputParams, AUDIO_SAMPLE_RATE, ...);         // BLOCKS!

  if (err == paNoError) {
    err = Pa_StartStream(ctx->duplex_stream);       // BLOCKS!
  }
}

// Lines 1470-1608: Fall back to separate streams (even slower!)
if (try_separate) {
  // ... more Pa_OpenStream calls ...
}

mutex_unlock(&ctx->state_mutex);      // Line 1699
```

**Why This Matters**:

1. **PortAudio Ops Block**: `Pa_GetDeviceInfo()`, `Pa_OpenStream()`, `Pa_StartStream()` can take hundreds of milliseconds
2. **State Queries Blocked**: While initializing, ANY query to audio state (is it running? what devices?) blocks
3. **Cascading Delays**: Other threads trying to query audio state pile up waiting for the mutex
4. **Initialization Jank**: User sees frozen UI during audio setup

**Estimated Lock Duration**:
- `Pa_GetDeviceInfo()`: ~10-50ms (ALSA/PulseAudio device enumeration)
- `Pa_OpenStream()`: ~50-200ms (driver initialization)
- `Pa_StartStream()`: ~50-100ms (actual stream startup)
- **Total**: ~300-500ms while lock is held!

**Recommended Fix**:

```c
// 1. Get device info WITHOUT lock
PaDeviceIndex inputDevice = GET_OPTION(microphone_index) >= 0
  ? GET_OPTION(microphone_index)
  : Pa_GetDefaultInputDevice();
const PaDeviceInfo *inputInfo = Pa_GetDeviceInfo(inputDevice);

// 2. Only lock for state update
mutex_lock(&ctx->state_mutex);
if (ctx->duplex_stream || ctx->input_stream || ctx->output_stream) {
  mutex_unlock(&ctx->state_mutex);
  return ASCIICHAT_OK;
}

// 3. Construct stream parameters (cheap operation)
PaStreamParameters inputParams = {...};
// ... setup params ...

// 4. Release lock, do blocking PortAudio ops
mutex_unlock(&ctx->state_mutex);

err = Pa_OpenStream(&ctx->duplex_stream, &inputParams, ...);  // NO LOCK!
if (err == paNoError) {
  err = Pa_StartStream(ctx->duplex_stream);  // NO LOCK!
}

// 5. Re-acquire lock only to update state
mutex_lock(&ctx->state_mutex);
if (err == paNoError) {
  // Successfully opened, save stream pointer
  // ... actual state update ...
} else {
  // Failed, maybe another thread succeeded meanwhile
  // ... error handling ...
}
mutex_unlock(&ctx->state_mutex);
```

### 4. PORTAUDIO REFCOUNT MUTEX (LOW PRIORITY)

**Location**: `lib/audio/audio.c:38, 54, 96, 115`

**Problem**: Global mutex protecting `Pa_Initialize()` / `Pa_Terminate()` reference count

**Why It's Low Priority**:
- Only contended during audio initialization and shutdown
- Not in audio processing hot path
- Holds lock for microseconds (just refcount ops), not milliseconds
- Simple and correct implementation

**Status**: No changes needed.

### 5. ERROR STATISTICS MUTEX (LOW PRIORITY)

**Location**: `lib/asciichat_errno.c`

**Problem**: Global mutex protecting error statistics counters

```c
static static_mutex_t g_error_stats_mutex = STATIC_MUTEX_INIT;

asciichat_error_t asciichat_get_error_stats(...) {
  static_mutex_lock(&g_error_stats_mutex);
  // Copy stats (fast operation)
  static_mutex_unlock(&g_error_stats_mutex);
}
```

**Why It's Low Priority**:
- Error reporting is not a high-frequency operation
- Lock held for nanoseconds (just copying counters)
- Uncontended in normal operation

**Status**: No changes needed.

---

## Lock Ordering and Potential Deadlocks

### Current Lock Hierarchy

Analysis of the codebase reveals the following lock acquisition patterns:

```
Audio Context Lifecycle:
  1. g_pa_refcount_mutex (PortAudio init/term)
  2. ctx->state_mutex (stream state management)
  3. ctx->worker_mutex + ctx->worker_cond (worker thread signaling)
  4. ctx->raw_capture_rb->mutex (ringbuffer protection)
  5. ctx->capture_buffer->mutex (ringbuffer protection)
  6. ctx->playback_buffer->mutex (ringbuffer protection)

Packet Queue:
  1. pool->pool_mutex (node pool allocation)
  2. queue->buffer_pool->shrink_mutex (only during shrinking)

Error Management:
  1. g_error_stats_mutex (error stats)
```

### Deadlock Risk Assessment

**Low Risk**: No observed circular wait patterns. The locking strategy follows a clear hierarchy:
- Init-time locks (PortAudio, state setup) →
- Runtime locks (worker coordination) →
- Data structure locks (ringbuffer, pool)

No situation where:
- Thread A holds lock X waiting for lock Y
- Thread B holds lock Y waiting for lock X

**Recommendation**: Document the lock hierarchy and enforce it during code review.

---

## Performance Impact Analysis

### Packet Queue Contention

**Scenario**: Video streaming at 30 FPS to 5 clients with audio mixing

**Operations per second**:
- Video frames: 30 fps × 5 clients = 150 packets/sec
- Audio frames: ~100 packets/sec (variable)
- **Total**: ~250 packets/sec

**With Node Pool (CURRENT)**:
- 250 packets × 2 operations (get + put) = 500 mutex ops/sec
- Lock contention events: ~50-100/sec (depends on thread count)

**Estimated Overhead**:
- Each mutex op on Linux: ~100-500 ns (uncontended), ~1-10 µs (contended)
- Uncontended: 250 packets × 1 µs = 250 µs/sec total = **negligible**
- Contended: 100 contention events × 10 µs = 1000 µs/sec = **0.1% CPU overhead**

**But**: With 10+ threads, contention probability increases. On high-load scenarios (multiprocessor with many clients), this becomes visible.

### Ringbuffer TOCTOU Impact

**Scenario**: High network jitter causing bursty audio arrivals

When data arrives in bursts:
1. Worker thread reads buffer status
2. During logging, network arrives with new data
3. Read operation gets inconsistent data count
4. Wastes one iteration doing no work

**Impact**: 1-5% of iterations skip processing = 1-5% audio gaps in extreme cases

---

## Lock-Free Patterns Already In Use

The codebase ALREADY uses sophisticated lock-free patterns:

1. **Buffer Pool** (`lib/buffer_pool.c`): Lock-free LIFO with atomic CAS
2. **Packet Queue** (`lib/network/packet_queue.c`): Lock-free Michael-Scott queue (atomic head/tail)
3. **Generic Ring Buffer** (`include/ascii-chat/ringbuffer.h`): Lock-free atomic operations

These prove the team knows how to write lock-free code correctly. The node pool mutex is an outlier and could be converted to use the same techniques.

---

## Recommendations

### Priority 1: NODE POOL MUTEX (HIGH IMPACT)

**Recommended Approach**: Convert node pool to lock-free implementation

```c
// Current (mutexed):
typedef struct node_pool {
  packet_node_t *free_list;
  mutex_t pool_mutex;           // ← PROBLEM
} node_pool_t;

// Proposed (lock-free):
typedef struct node_pool {
  _Atomic(packet_node_t *) free_list;  // Atomic instead of mutex-protected
  // ... rest unchanged ...
} node_pool_t;
```

**Effort**: Medium (similar to buffer pool implementation)
**Benefit**: Eliminate single biggest contention point in packet path
**Risk**: Low (proven pattern in codebase, atomic operations are well-tested)

**Alternative Approaches** (in priority order):

1. **Per-thread pools**: Each thread gets own pool, eliminate contention
   - Effort: Low-medium (pool allocation logic)
   - Benefit: Same as lock-free, but uses more memory
   - Risk: Low

2. **Hybrid approach**: Keep pool but use atomic CAS for fast path, mutex only for pool exhaustion
   - Effort: Medium
   - Benefit: Reduces lock frequency for happy path
   - Risk: Medium (more complex logic)

### Priority 2: AUDIO WORKER TOCTOU (MEDIUM IMPACT)

**Recommended Approach**: Verify availability before processing, handle zero-length reads gracefully

```c
// Current vulnerable code:
size_t capture_available = audio_ring_buffer_available_read(...);
// ... 44 lines of other code ...
size_t capture_read = audio_ring_buffer_read(..., capture_available);
if (capture_read > 0) { /* process */ }

// Safer approach:
size_t capture_available = audio_ring_buffer_available_read(...);
if (capture_available >= MIN_PROCESS_SAMPLES) {
  size_t to_read = MIN(capture_available, WORKER_BATCH_SAMPLES);
  size_t capture_read = audio_ring_buffer_read(..., to_read);
  if (capture_read > 0) {
    // Process actual data received, not assumed data
    process_samples(..., capture_read);
  } else {
    log_debug("Available=%zu but read returned 0 (concurrent consumption)", to_read);
  }
}
```

**Effort**: Low (defensive coding pattern)
**Benefit**: Eliminates race condition, improves robustness
**Risk**: Very low

### Priority 3: AUDIO INITIALIZATION MUTEX HOLD (MEDIUM IMPACT)

**Recommended Approach**: Reduce lock-holding duration by doing PortAudio ops outside lock

**Effort**: Medium-high (refactoring state management)
**Benefit**: Eliminates UI jank during audio setup
**Risk**: Medium (need to carefully handle race conditions)

**Implementation Strategy**:
1. Check if already initialized (quick check inside lock)
2. Release lock
3. Do slow PortAudio ops
4. Re-acquire lock
5. Update state atomically (watch for races where another thread already initialized)

---

## Testing Recommendations

### Lock Contention Testing

```bash
# Run with high client load to stress mutex contention
./build/bin/ascii-chat server \
  --num-video-packets=100 \
  --num-audio-packets=50

# Monitor with debug locks enabled
DEBUG_LOCKS=1 ./build/bin/ascii-chat server
```

### TOCTOU Testing

```bash
# Simulate bursty network arrivals
# (needs custom test harness)
# - Measure audio gaps during network jitter
# - Count iterations with zero data reads
```

### Lock-Free Migration Testing

After converting node pool to lock-free:
```bash
# Run under ThreadSanitizer to detect data races
clang -fsanitize=thread -g build audio-test.c
./a.out
```

---

## Code Review Checklist

When reviewing changes to threading code:

- [ ] Lock hierarchy documented and respected
- [ ] No blocking operations while holding synchronization primitives
- [ ] No manual mutex code (use `mutex_lock`, `mutex_unlock`, `cond_wait` abstractions)
- [ ] No TOCTOU patterns (check state close to use, atomically if possible)
- [ ] Lock-free paths preferred over mutex paths
- [ ] Atomic operations used for lock-free code (not raw memory operations)
- [ ] Memory ordering specified explicitly in atomic operations

---

## Conclusion

ascii-chat's threading architecture is generally sound, with appropriate use of lock-free patterns for high-frequency operations. The main contention point (node pool mutex) can be eliminated by converting to the same lock-free techniques already proven in the buffer pool.

The TOCTOU race conditions are non-fatal but can be eliminated with defensive coding patterns that are already present elsewhere in the codebase.

No critical deadlock risks were identified.

**Next Steps**:
1. Implement lock-free node pool (Priority 1)
2. Add defensive checks in audio worker (Priority 2)
3. Refactor audio initialization to hold lock briefly (Priority 3)
4. Add ThreadSanitizer to CI to prevent future race conditions
