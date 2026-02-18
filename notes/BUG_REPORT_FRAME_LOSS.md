# Bug Report: Frame Loss in WebSocket Client Mode

**Date:** 2026-02-17
**Severity:** CRITICAL
**Status:** FIXED
**Component:** WebSocket server event loop

## User-Observed Behavior

1. Start server, navigate to `/client` in browser
2. **ONE frame renders**
3. Several more frames render over the next few seconds
4. **Frame rendering STOPS completely (0 FPS)**
5. After **~45 seconds**, the browser suddenly renders many frames at once (catching up to real-time)
6. **STOPS again (0 FPS)**
7. Pattern repeats: 45-second cycle of ~1-3 FPS followed by 0 FPS stalls

## Root Cause Found

**Location:** `lib/network/websocket/server.c:785`

```c
int result = lws_service(server->context, -1);  // BUG: -1 causes blocking
```

### The Problem

Passing `-1` to `lws_service()` was supposed to force non-blocking mode, but this is incorrect. The parameter `-1` actually causes `lws_service()` to:
- Use a very long internal timeout
- Block the service thread indefinitely or for extended periods
- Prevent RECEIVE callbacks from firing while blocked
- Prevent WRITEABLE callbacks from firing

### The Cascade

1. Service thread calls `lws_service(-1)` and blocks
2. No RECEIVE callbacks fire → fragments don't queue to recv_queue
3. No WRITEABLE callbacks fire → responses can't be sent
4. Handler thread blocks waiting for fragments in websocket_recv()
5. Queue remains empty
6. After ~45 seconds, an OS-level timeout (TCP or internal) wakes the service thread
7. Queued fragments suddenly process as a burst
8. Back to blocking state → cycle repeats

This explains the exact 45-second burst/stall pattern observed.

## The Fix

Changed line 785:

```c
// BEFORE (BLOCKING):
int result = lws_service(server->context, -1);

// AFTER (NON-BLOCKING):
int result = lws_service(server->context, 50); // 50ms timeout
```

Use a **50ms timeout** instead of -1. This keeps the event loop responsive:
- Poll every 50ms for new socket activity
- RECEIVE callbacks fire continuously as fragments arrive
- WRITEABLE callbacks fire to send responses
- No more 45-second stalls

This matches the correct pattern already used in `transport.c:87` for the client-side WebSocket service thread.

## Files Modified

- `lib/network/websocket/server.c` - Fixed lws_service() timeout parameter from -1 to 50ms
