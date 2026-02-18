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

## Current Bug: Processing Stops After First Frame (2026-02-17 22:42)

**Observed Behavior:**
1. Browser connects to server via WebSocket ✓
2. Server receives FIRST frame ✓
3. Server processes it and sends ASCII art back to browser ✓
4. Browser displays ONE frame ✓
5. **Processing STOPS** - server no longer processes incoming frames
6. Connection persists - frames continue arriving from client
7. Server has frames in memory and COULD process them
8. But server is NOT processing them and NOT sending responses back

**The Bug:**
After processing the first frame, the processing/dispatch/send pipeline stops. The server is no longer:
- Reading from the received_packet_queue
- Processing queued frames
- Converting to ASCII art
- Sending responses back to the client

**Why the recv_queue fills up:**
Frames arrive faster than they're being processed (because processing stopped), so they queue up until the buffer overflows and fragments are dropped.

**What Works:**
- WebSocket connection ✓
- ACIP protocol negotiation ✓
- Fragment reception ✓
- First frame processing ✓
- Frame queuing ✓

**What's Broken:**
- **Processing loop never continues after first frame**
- Dispatch/send threads stop running or block
- No subsequent frames are converted to ASCII art
- No responses sent back to client

## Files Modified

- `lib/network/websocket/server.c` - Fixed lws_service() timeout parameter from -1 to 50ms
- `lib/network/websocket/transport.c` - Reduced recv_mutex lock contention (2026-02-17)
