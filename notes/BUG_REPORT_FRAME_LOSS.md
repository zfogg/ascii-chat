# Bug Report: Frame Loss in WebSocket Client Mode

**Date:** 2026-02-17
**Severity:** CRITICAL
**Status:** ACTIVE
**Component:** WebSocket server, async message handling

## User-Observed Behavior

1. Start server, navigate to `/client` in browser
2. **ONE frame renders**
3. Several more frames render over the next few seconds
4. **Frame rendering STOPS completely (0 FPS)**
5. After **1-2 minutes**, the browser suddenly renders many frames at once (catching up to real-time), then **STOPS again (0 FPS)**
6. Pattern repeats: ~45-second cycle of ~1-3 FPS rendering followed by 45+ seconds of 0 FPS stall

## The Bug

WebSocket data arrives from the browser and is received by libwebsockets. All fragments are queued to memory in `recv_queue`. We have the complete data buffered and available to process.

But processing doesn't happen continuously. Instead, data sits in the queue and is processed intermittently in 45-second bursts at approximately 0-3 FPS, followed by long stalls at 0 FPS.

## Root Issue: Architecture Mismatch With LWS

Our implementation deviates significantly from LWS design patterns, creating a bottleneck in the message pipeline:

### How LWS Examples Work (minimal-ws-server-echo)

```c
LWS_CALLBACK_RECEIVE:
  ├─ Receive fragment from browser
  ├─ Process immediately (memcpy, echo)
  └─ Send response back immediately
```

Simple, synchronous, no queueing.

### How Our Implementation Works

```c
LWS_CALLBACK_RECEIVE (LWS service thread):
  ├─ Receive fragment from browser
  ├─ Allocate buffer
  ├─ Copy to ringbuffer queue
  └─ Signal condition variable

Handler Thread (separate):
  ├─ Wait for queue_cond signal
  ├─ Call websocket_recv() to reassemble fragments into complete message
  ├─ Wait for missing fragments (50ms timeout × N times)
  ├─ Call handler to process complete message
  └─ Generate ASCII art response

Response path:
  ├─ Queue response for sending
  └─ Wait for LWS_CALLBACK_SERVER_WRITEABLE
```

### Where It Breaks Down

1. **Queuing Overhead**: LWS callbacks immediately queue fragments. The dispatch thread then waits for complete messages.

2. **Reassembly Bottleneck**: `websocket_recv()` waits for each fragment with a 10ms+ timeout loop. With 18 fragments per 921KB message, reassembly takes 100-200ms per message, even before TCP batching delays.

3. **Async Complexity**: Data arrives → queues → waits for handler → handler processes → queues response → waits for WRITEABLE callback. Each stage can have latency.

4. **No Continuous Draining**: Unlike the echo example which processes in-callback, our handler thread processes intermittently. When it's blocked waiting for fragments, the queue fills. When it catches up, a burst of processing happens.

5. **Threading Contention**: Multiple threads (LWS service thread, handler thread, status screen thread) compete for the queue_mutex. The dispatch thread can get starved.

## What Needs To Happen

The system needs to continuously process queued WebSocket data and send ASCII frames back at steady 30+ FPS, not in intermittent bursts.

Options:
1. Make the handler process frames continuously instead of waiting for complete messages
2. Make reassembly faster or non-blocking
3. Increase queue capacity to buffer more frames during processing stalls
4. Process multiple frames in parallel instead of one at a time
5. Simplify the architecture to match LWS examples more closely (less queueing, more direct processing)

## Configuration

`cond_timedwait()` timeout in `websocket_recv()`: 1ms

This is the timeout when waiting for the next fragment to arrive in the queue. This is unrelated to the 45-second stall pattern (10ms timeouts cannot cause 45-second stalls).

## Evidence

- Browser sends frames continuously (confirmed in RECEIVE callback logs)
- Fragments arrive and queue successfully (queue full after ~30-50 fragments)
- Handler thread exists and processes some messages (stall happens every 45 seconds)
- No errors in fragment reception or queueing
- The pattern is consistent: process, stall, burst catch-up, repeat

## What Was Investigated (Dead Ends)

- Flow control pause/resume - removing it didn't help
- recv_queue timeout duration - changing it didn't help
- Dispatch thread blocking - not the issue
- TCP window management - not the issue
- Fragment completion (final flag) - fragments do complete
- 10ms cond_timedwait blocking - removing it to enable non-blocking recv
