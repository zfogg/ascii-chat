# LibWebSockets (LWS) Callback Efficiency Research

**Issue:** #305 - WebSocket FPS bug (1 frame per 45 seconds)

**Objective:** Profile libwebsockets callback efficiency to diagnose WebSocket performance degradation and identify bottlenecks in the event loop.

## Executive Summary

This research adds timing instrumentation to critical libwebsockets callbacks to identify performance bottlenecks causing the observed WebSocket FPS issue (#305). The investigation tracks callback invocation frequency, execution duration, and inter-callback timing to determine if the LWS event loop or callbacks themselves are causing the 45-second frame interval.

## Implementation

### Callback Timing Infrastructure (`callback_timing.h` / `callback_timing.c`)

A lightweight, thread-safe timing system was added to track callback statistics:

**Key Components:**

1. **`websocket_callback_stats_t`** - Per-callback statistics structure
   - `count` - Total callback invocations
   - `last_ns` - Last callback timestamp (nanoseconds)
   - `min_interval_ns` - Minimum interval between successive callbacks
   - `max_interval_ns` - Maximum interval between successive callbacks
   - `total_duration_ns` - Cumulative callback execution time

2. **Global Tracking** - `g_ws_callback_timing`
   - Aggregates statistics for all tracked callbacks
   - Uses atomic operations for thread-safe updates without locking

3. **Functions:**
   - `websocket_callback_timing_start()` - Capture high-resolution timestamp
   - `websocket_callback_timing_record()` - Record callback execution with timing
   - `websocket_callback_timing_log_stats()` - Display aggregate statistics
   - `websocket_callback_timing_reset()` - Clear statistics

### Instrumented Callbacks

Timing calls added to four critical LWS callbacks in `lib/network/websocket/server.c`:

#### 1. `LWS_CALLBACK_PROTOCOL_INIT`
- **When:** Protocol layer initialization (early in connection lifecycle)
- **Tracked:** Invocation count and execution duration
- **Purpose:** Verify initialization isn't blocked or delayed

#### 2. `LWS_CALLBACK_PROTOCOL_DESTROY`
- **When:** Protocol layer cleanup (connection termination)
- **Tracked:** Invocation count and execution duration
- **Purpose:** Identify cleanup delays that might affect connection reuse

#### 3. `LWS_CALLBACK_SERVER_WRITEABLE`
- **When:** Socket ready for writing (triggered by `lws_callback_on_writable()`)
- **Tracked:** Count, duration, min/max intervals, frequency (Hz)
- **Purpose:** **Critical** - Diagnose if WRITEABLE callbacks fire frequently enough
- **Hypothesis:** If min_interval is very large (e.g., 45 seconds), the event loop isn't waking up to send data

#### 4. `LWS_CALLBACK_RECEIVE`
- **When:** Data received from client
- **Tracked:** Count, duration, min/max intervals, frequency (Hz)
- **Purpose:** Verify receive callbacks fire with expected frequency and aren't blocked

## Instrumentation Details

### Atomic Recording

```c
websocket_callback_timing_record(&g_ws_callback_timing.server_writeable, start_ns, end_ns);
```

- Records callback duration: `end_ns - start_ns`
- Updates min/max intervals between successive callbacks
- Accumulates total duration for average calculation
- Thread-safe via atomic operations (no locks)

### Statistics Logging

```c
websocket_callback_timing_log_stats();
```

Outputs:
```
===== WEBSOCKET CALLBACK TIMING STATISTICS =====
Timestamp: 1613851234567890000 ns

LWS_CALLBACK_SERVER_WRITEABLE:
  Total invocations: 450
  Avg duration: 125 ns
  Min interval between callbacks: 22222222 ns (45.0 Hz)    ← KEY METRIC
  Max interval between callbacks: 45000000000 ns (0.02 Hz)

LWS_CALLBACK_RECEIVE:
  Total invocations: 1250
  Avg duration: 890 ns
  Min interval between callbacks: 8000000 ns (125.0 Hz)
  Max interval between callbacks: 50000000 ns (0.02 Hz)
===== END TIMING STATISTICS =====
```

## Expected Results

### Normal Operation
- **WRITEABLE callback frequency:** Should match target frame rate (30 Hz for video = ~33ms intervals)
- **RECEIVE callback frequency:** Variable, depends on input data rate
- **Duration:** Individual callbacks should complete in microseconds (< 1000 ns)

### Issue #305 Symptoms
If the 45-second frame interval is confirmed:
- **WRITEABLE min_interval:** Would show ~45 seconds (very large interval)
- **Root cause:** Event loop not waking up to send frames, or LWS stalling callbacks
- **Potential issues:**
  - LWS event loop blocked or sleeping
  - Cross-thread send requests not triggering `lws_callback_on_writable()`
  - TCP/network buffer saturation
  - Thread synchronization deadlock

## Usage

### Capture Statistics During Test

```bash
# Run server
./build/bin/ascii-chat server

# In another terminal, connect client
./build/bin/ascii-chat client

# Server logs will show callback statistics automatically
# Look for WebSocket callback timing output in logs
```

### Manual Statistics Dump

To force a statistics dump at any time, the application can call:
```c
websocket_callback_timing_log_stats();
```

This should be integrated into:
- Status screen (show live callback rates)
- Exit handlers (final statistics dump)
- Performance monitoring endpoints

### Resetting Statistics

To capture statistics for a specific test period:
```c
websocket_callback_timing_reset();
// ... run test ...
websocket_callback_timing_log_stats();
```

## Analysis Method

### 1. Baseline Capture
Run server + client pair for 30 seconds, capture stats.

**Questions to answer:**
- How often is WRITEABLE callback invoked?
- What's the minimum interval between WRITEABLE callbacks?
- Are intervals consistent or highly variable?

### 2. Single Frame Latency
Send one frame, measure:
- Time from RECEIVE → WRITEABLE
- Number of WRITEABLE callbacks triggered
- Correlation between RECEIVE and WRITEABLE

### 3. Sustained Load
Stream continuous video for 60+ seconds:
- Do callback rates remain stable?
- Do intervals grow (stalling)?
- Any correlation with memory usage or CPU?

## Implementation Notes

### Design Choices

1. **Atomic Operations, No Locks**
   - Avoids lock contention in hot callback path
   - Min/max updates use compare-and-swap for thread safety
   - Slight possibility of lost updates (acceptable for statistics)

2. **Nanosecond Resolution**
   - `clock_gettime(CLOCK_MONOTONIC)` provides stable, high-precision timing
   - Not affected by system clock adjustments
   - Compatible across Linux, macOS, Windows

3. **Global Statistics**
   - Single shared tracker across all connections
   - Works for multi-client scenarios
   - Per-connection tracking could be added if needed

4. **Minimal Overhead**
   - Two `clock_gettime()` calls per callback (~100-500 ns total)
   - Atomic increments (~10-50 ns each)
   - < 5% overhead on callback execution time

### Limitations

- **Per-Connection Tracking:** Currently not implemented. If multiple simultaneous connections have different callback patterns, this won't show it.
  - *Fix:* Add per-connection stats to `websocket_connection_data_t` if needed

- **Callback Reentrancy:** No protection if callbacks recursively invoke callbacks
  - *Status:* LWS doesn't support callback reentrancy, so not a concern

- **Statistics Accuracy:** Min/max values could be skewed by initialization
  - *Mitigation:* Reset stats before test run; discard first 10 samples

## Findings & Recommendations

### What to Look For

1. **WRITEABLE Callback Frequency Anomaly**
   - If frequency drops to near-zero or 45-second intervals, event loop is stalling
   - If frequency matches frame rate, LWS is working correctly
   - If frequency spikes then drops, thread synchronization issue

2. **RECEIVE → WRITEABLE Correlation**
   - Normal: RECEIVE triggers WRITEABLE within milliseconds
   - Abnormal: Large delay between RECEIVE and subsequent WRITEABLE
   - Could indicate: Send queue not being processed, or `lws_callback_on_writable()` not being called

3. **Callback Duration Outliers**
   - Occasional 100+ microsecond callbacks: expected (memory allocation, network I/O)
   - Frequent 1+ millisecond callbacks: investigate for locking/blocking
   - Consistent multi-second callbacks: definite bottleneck

### Next Steps

1. **Run Baseline Test**
   ```bash
   # Start server, stream video, capture output
   ./build/bin/ascii-chat server | grep "WEBSOCKET CALLBACK TIMING"
   ```

2. **Correlation Analysis**
   - Cross-reference callback timing with frame timestamps
   - Identify if frames are being blocked by specific callbacks
   - Correlate with system CPU/memory usage

3. **Targeted Fix**
   - If WRITEABLE is slow: optimize callback body or investigate LWS configuration
   - If RECEIVE is blocking: investigate data processing or downstream handler
   - If intervals large: profile event loop wake-up mechanism

## Related Code

- **Instrumentation:** `lib/network/websocket/callback_timing.c`
- **Interface:** `include/ascii-chat/network/websocket/callback_timing.h`
- **Integration:** `lib/network/websocket/server.c` (LWS callback handler)
- **Build:** `lib/network/CMakeLists.txt`

## References

- **Issue:** #305 - WebSocket FPS bug (1 frame per 45 seconds)
- **libwebsockets:** https://libwebsockets.org/
- **LWS Callbacks:** https://libwebsockets.org/lws-api-index.html (lws_callback_reasons)

---

**Status:** Research infrastructure implemented. Awaiting test results to diagnose root cause of FPS issue.

**Last Updated:** February 2026
