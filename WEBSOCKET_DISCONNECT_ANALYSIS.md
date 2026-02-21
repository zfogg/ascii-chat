# WebSocket Client Disconnect Issue - Analysis Report

## Executive Summary

The client crashes immediately upon attempting to send its first image frame (~230KB) to the server over WebSocket. The crash is caused by a **libwebsockets protocol violation**: the client sends a CONTINUATION frame after a frame marked with FIN (final) bit, violating RFC 6455.

## Disconnect Sequence Timeline

### Server Side

1. **Connection Established** (19:15:24.679s)
   - WebSocket connection accepted
   - Callback system initialized

2. **LWS_CALLBACK_CLOSED Triggered** (19:15:24.737s)
   - Connection closed **without WebSocket close handshake**
   - This is the smoking gun: the connection dropped before the crypto handshake could complete

3. **Handshake Failure** (19:15:24.811s)
   - `crypto_handshake_server_start()` attempts to send KEY_EXCHANGE_INIT
   - Transport reports: **"WebSocket send called but transport NOT connected!"**
   - Transport state: `is_connected=false`
   - Send fails with error code 40 (Network error)

4. **Client Removed** (19:15:24.895s)
   - Server removes the client after handshake failure

### Client Side

1. **Connection to Server** (19:15:24.222s)
   - Data reception thread spawned
   - Webcam capture thread spawned
   - Audio threads spawned
   - Keepalive/ping thread spawned

2. **Image Frame Send Attempt** (19:15:24.452s)
   - Capture thread sends IMAGE_FRAME (320x240, 230,400 bytes pixel data)
   - Total frame size: **230,424 bytes**
   - Method: `acip_send_image_frame()` → `packet_send_via_transport()` → `websocket_send()`

3. **Protocol Violation Detected** (19:15:24.595s)
   - libwebsockets logs: **"Sending CONTINUATION after previous frame that had FIN"**
   - Location: `rops_write_role_protocol_ws()` in `ops-ws.c:1860`
   - Assertion fails: `Assertion '0' failed`
   - Signal: SIGABRT (abort)

4. **Client Crash** (19:15:24.595s)
   - Client terminates with backtrace from libc abort()
   - Crash occurs in capture thread context

## Root Cause Analysis

### The Protocol Violation

RFC 6455 WebSocket framing defines:
- **FIN bit (0x80)**: When set, indicates this is the final fragment in a message
- **Opcode 0x00**: CONTINUATION frame (can only follow a non-final fragment)
- **Invalid sequence**: FIN=1 followed by opcode 0x00 (CONTINUATION)

The error message explicitly states the client violates this: sending CONTINUATION when the previous frame had FIN set.

### Where the Bug Lives

The issue is in the **WebSocket frame fragmentation logic**:

**Hypothesis 1: Frame State Management**
- The client's `websocket_send()` or packet fragmentation code doesn't properly track whether a frame has been marked as final
- It may be resetting the state after each `lws_write()` call but continuing to send more data
- This causes a new frame to be sent as CONTINUATION instead of starting a new frame

**Hypothesis 2: Packet Size vs Buffer Size Mismatch**
- The 230KB image frame is being split across multiple `lws_write()` calls
- The fragmentation logic incorrectly marks the first chunk as FIN=1
- Subsequent chunks are then sent as CONTINUATION, which is illegal

**Hypothesis 3: Race Condition**
- Multiple threads (webcam capture, audio sender) may be calling `websocket_send()` simultaneously
- Without proper synchronization, one thread marks a frame as FIN while another is still sending fragments

### Why Server Closes Connection

When libwebsockets detects the protocol violation:
1. It logs the error: `"Sending CONTINUATION after previous frame that had FIN"`
2. It triggers the assertion failure in the client
3. The client crashes (SIGABRT)
4. The TCP connection drops (no graceful close handshake)
5. Server receives `LWS_CALLBACK_CLOSED` with no close frame
6. Server marks transport as NOT connected

## Key Evidence from Logs

### Client Log - The Crash Point

```
[19:15:24.452714] Capture thread: sending IMAGE_FRAME 320x240 via transport
[19:15:24.454029] ACIP_SEND_IMAGE_FRAME: Called with 320x240
[19:15:24.455093] ACIP_SEND_IMAGE_FRAME: pixel_size=230400
[19:15:24.460836] ACIP_SEND_IMAGE_FRAME: total_size=230424
[19:15:24.471513] ACIP_SEND_IMAGE_FRAME: About to send packet

[19:15:24.595480] *** CRASH DETECTED ***
Signal: 6 (SIGABRT)
Backtrace: ... rops_write_role_protocol_ws() ops-ws.c:1860
Error: "Sending CONTINUATION after previous frame that had FIN"
```

### Server Log - Connection Drop

```
[19:15:24.737874] LWS_CALLBACK_CLOSED FIRED
[19:15:24.738702] Connection closed without WebSocket close handshake
[19:15:24.811254] websocket_send: WebSocket send called but transport NOT connected!
[19:15:24.865899] WebSocket transport closed
```

## Files Involved

The fragmentation logic is likely in one of these locations:

1. **`lib/network/websocket/transport.c`** - `websocket_send()`
   - Handles buffering and `lws_write()` calls
   - Manages frame state tracking

2. **`lib/network/acip/send.c`** - `packet_send_via_transport()`
   - High-level packet sending wrapper
   - May call multiple times for large packets

3. **`lib/network/websocket/server.c`** / `client.c`** (libwebsockets integration)
   - Callback handling
   - Frame fragmentation setup

4. **Thread synchronization in `src/client/capture.c`**
   - May be racing with other send threads

## Impact

- **No client can successfully connect and send video frames**
- Affects both TCP and WebSocket transports if same fragmentation logic is used
- Occurs consistently on any image frame send attempt

## Next Steps for Investigation

1. Add explicit frame state logging in `websocket_send()`
2. Add synchronization primitives around multi-threaded sends
3. Review libwebsockets `lws_write()` documentation for frame fragmentation requirements
4. Add unit tests for large frame sending (>64KB)
5. Validate frame opcode and FIN bit manually before calling `lws_write()`

## Test Execution Details

- **Test Script**: `./scripts/test-websocket-server-client.sh`
- **Server Port**: Random between 30000-40000 (37944 in this run)
- **Client Mode**: Snapshot mode (`-S -D 1`) - captures 1 frame then exits
- **Crash Time**: ~350ms after connection established
- **Consistent**: Yes - crash occurs every test run at the same point

## Observed Behavior Summary

```
Server: Listening for connection
Client: Connects successfully
        Starts webcam capture thread
        Starts audio capture thread
        Prepares 230KB image frame
        Calls websocket_send() with image data
libwebsockets: Detects protocol violation (CONTINUATION after FIN)
Client: Crashes (SIGABRT)
Server: Receives connection drop, removes client
Result: No frames transmitted, no handshake completed
```
