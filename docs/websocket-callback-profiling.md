# WebSocket Callback Profiling Guide

## Overview

The WebSocket callback profiler instruments libwebsockets (LWS) callbacks to measure performance characteristics. It tracks callback frequency, execution time, and data throughput for both client and server modes, enabling detailed performance analysis and optimization.

## Profiled Callbacks

### Client-Side Callbacks (transport.c)

1. **LWS_CALLBACK_CLIENT_ESTABLISHED** - Connection established
   - Fires once per connection
   - Time: ~1-5ms (state mutex operations)
   - Data: 0 bytes

2. **LWS_CALLBACK_CLIENT_RECEIVE** - Data received from server
   - Fires for each message fragment
   - Time: ~0.1-1ms (buffer allocation and ringbuffer write)
   - Data: Payload size (variable, up to 921KB per frame)

3. **LWS_CALLBACK_CLIENT_CLOSED** - Connection closed
   - Fires once when connection terminates
   - Time: ~0.5-2ms (state cleanup)
   - Data: 0 bytes

4. **LWS_CALLBACK_CLIENT_CONNECTION_ERROR** - Connection failed
   - Fires when connection setup fails
   - Time: ~0.1-1ms (error logging)
   - Data: 0 bytes

5. **LWS_CALLBACK_CLIENT_WRITEABLE** - Socket ready for writing
   - Fires when socket buffer available
   - Time: ~0.05-0.5ms (typically no-op)
   - Data: 0 bytes

### Server-Side Callbacks (server.c)

1. **LWS_CALLBACK_ESTABLISHED** - Client connection established
   - Fires once per new client connection
   - Time: ~5-20ms (transport creation, thread spawn)
   - Data: 0 bytes
   - Critical section: Creates ACIP transport and spawns handler thread

2. **LWS_CALLBACK_CLOSED** - Client connection closed
   - Fires when client disconnects
   - Time: ~10-100ms (cleanup, transport destroy, thread join)
   - Data: 0 bytes (may include close code in payload)
   - Critical section: Joins handler thread, frees transport

3. **LWS_CALLBACK_SERVER_WRITEABLE** - Server can send data
   - Fires when socket ready for output
   - Time: ~1-10ms per fragment (lws_write call)
   - Data: Bytes written (256KB-512KB per call typically)
   - High-frequency: Fires many times during video transmission

4. **LWS_CALLBACK_RECEIVE** - Data received from client
   - Fires for each message fragment
   - Time: ~0.1-1ms (per-fragment processing)
   - Data: Payload size (variable)

5. **LWS_CALLBACK_FILTER_HTTP_CONNECTION** - WebSocket upgrade request
   - Fires during handshake
   - Time: <0.1ms
   - Data: 0 bytes

6. **LWS_CALLBACK_EVENT_WAIT_CANCELLED** - Service cancellation event
   - Fires when lws_cancel_service() called
   - Time: <0.1ms
   - Data: 0 bytes

## Implementation Details

### Architecture

The profiler uses:
- **Atomic operations** for thread-safe counters (no locks in hot path)
- **Per-callback records** in a hash table (keyed by callback reason)
- **Minimal overhead** design (single struct allocation per callback type)
- **Cross-thread safe** statistics collection and reporting

### Data Structures

```c
typedef struct {
  uint64_t invocation_count;      // Total invocations
  uint64_t total_time_ns;         // Total execution time
  uint64_t min_time_ns;           // Minimum execution time
  uint64_t max_time_ns;           // Maximum execution time
  uint64_t bytes_processed;       // Total data throughput
  uint64_t last_invocation_ns;    // Wall-clock timestamp of last call
} lws_callback_stats_t;
```

### Thread Safety

- All counters use C11 atomics (`_Atomic uint64_t`)
- No lock required for profiler_start/stop operations
- Hash table modifications protected by mutex (rare, only first callback invocation)
- Statistics reporting uses acquire semantics for consistency

## Usage

### Initialization

```c
// Must call once at application startup
if (!lws_profiler_init()) {
    log_error("Failed to initialize profiler");
    return;
}
```

### Automatic Instrumentation

The profiler is automatically integrated into:
- `websocket_callback()` in lib/network/websocket/transport.c
- `websocket_server_callback()` in lib/network/websocket/server.c

Each callback wraps its execution with:
```c
uint64_t prof_handle = lws_profiler_start((int)reason, 0);
// ... callback work ...
lws_profiler_stop(prof_handle, bytes_processed);
```

### Getting Statistics

```c
lws_callback_stats_t stats;
if (lws_profiler_get_stats(LWS_CALLBACK_SERVER_WRITEABLE, &stats)) {
    printf("Callback fired %llu times\n", stats.invocation_count);
    printf("Average time: %lld ns\n", stats.total_time_ns / stats.invocation_count);
    printf("Throughput: %lld bytes\n", stats.bytes_processed);
}
```

### Reporting

```c
// Print human-readable report
lws_profiler_dump_report();

// Export as JSON for external analysis
char *json = lws_profiler_export_json();
if (json) {
    printf("Profiling data:\n%s\n", json);
    free(json);
}
```

### Cleanup

```c
// At application exit
lws_profiler_destroy();
```

## Performance Characteristics

### Overhead Per Callback

- **profiler_start()**: ~50-100 nanoseconds (atomic ops + hashtable lookup)
- **profiler_stop()**: ~200-300 nanoseconds (atomic updates + min/max compare-swap)
- **Total overhead per callback**: <1 microsecond (negligible for most callbacks)

### Memory Overhead

- ~64 bytes per callback type (hash table entry + stats struct)
- Fixed size after initialization (max ~20 callback types)
- Total: <2KB for entire profiler

## Metrics Interpretation

### Invocation Count

Indicates callback firing frequency:
- High CLIENT_RECEIVE count: Receiving many fragments (normal for video)
- High SERVER_WRITEABLE count: Sending many fragments (normal during streaming)
- Anomalies: Unexpected patterns may indicate errors or inefficient processing

### Timing

- **Min/Max timing**: Identifies consistent vs. variable performance
- **Average timing**: Baseline cost of callback processing
- **Total time**: Cumulative cost for this callback type

Example:
```
LWS_CALLBACK_SERVER_WRITEABLE
  Invocations: 1250
  Timing: min=0.5ms, max=15ms, avg=2ms
  Total data: 1250MB
```

Interpretation:
- 1250 write callbacks sent 1250MB total (~1MB per callback)
- Average write takes 2ms
- Wide variance (0.5-15ms) suggests buffer pressure fluctuations

### Throughput

Calculate from bytes_processed and timing:

```
Throughput = bytes_processed * 8 / (total_time_ns / 1e6) kbps
```

For above example:
```
1250MB * 8 / (1250*2ms / 1e6) = 10M kbps = 1250 MB/s
```

## Common Profiling Patterns

### Pattern 1: Client Receiving Video

**Expected profile:**
- CLIENT_RECEIVE invocations: 1000-2000 per second (fragment-based)
- Average time: 0.2-0.5ms per fragment
- Data: 128KB-256KB typical per fragment (for 921KB frames)

**Optimization opportunities:**
- If timing is high (>1ms): Buffer allocation stalling
- If fragmentation high: Consider larger TCP buffers

### Pattern 2: Server Sending Video

**Expected profile:**
- SERVER_WRITEABLE invocations: High frequency (100+ per second when busy)
- Average time: 1-5ms per write call
- Data: 256KB-512KB per write (matches buffer size)

**Optimization opportunities:**
- If timing variable: Potential memory pressure
- If data small: TCP window adjustments needed

### Pattern 3: Connection Lifecycle

**Expected profile:**
- ESTABLISHED: 1x per client (5-20ms)
- CLOSED: 1x per client disconnect (10-100ms)
- Timing during CLOSED: Thread join overhead

## Debugging High-Latency Callbacks

### Identify Bottleneck

1. Generate report: `lws_profiler_dump_report()`
2. Find callback with highest max time or high variance
3. Check correlation with other metrics

### Investigate

For CLIENT_RECEIVE with high timing:
```c
// Check buffer pool allocation latency
// May indicate memory fragmentation or allocation stalls
```

For SERVER_WRITEABLE with high variance:
```c
// Check for memory pressure or GC activity
// May indicate contention with other threads
```

For ESTABLISHED/CLOSED with high timing:
```c
// Check handler thread spawn/join overhead
// May indicate thread pool contention
```

## JSON Export Format

```json
{
  "callbacks": [
    {
      "reason": "SERVER_WRITEABLE",
      "invocations": 1250,
      "total_ns": 2500000000,
      "min_ns": 500000,
      "max_ns": 15000000,
      "avg_ns": 2000000,
      "bytes": 1250000000
    },
    ...
  ]
}
```

Use for:
- Performance trending over time
- Comparative analysis between deployments
- Integration with monitoring systems

## Limitations

1. **Pre-built profiler state**: Profiler must be initialized before callbacks fire
2. **No per-client tracking**: Statistics are global (aggregate for all clients)
3. **No breakdown by callback argument**: All invocations of same reason lumped together
4. **Wall-clock timestamp**: last_invocation_ns may jump backwards if system clock adjusted

## Future Enhancements

1. **Per-client tracking**: Identify slow clients
2. **Callback argument filtering**: Profile specific message types
3. **Time-windowed statistics**: Detect performance degradation over time
4. **Integration with logging**: Automatic alerts for anomalies

## References

- libwebsockets documentation: https://libwebsockets.org/
- ascii-chat util/time.h: High-precision timing utilities
- RFC 6455: WebSocket Protocol specification
